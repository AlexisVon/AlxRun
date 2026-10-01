/*****************************************************************/ /**
 * \file   abytes.h
 * \brief  Byte array with COW (copy-on-write) and zero-copy view
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_BYTES_H_
#define _ALEXIS_BYTES_H_

#include "aalgo.h"
#include "abase.h"
#include "arefcount.h"
#include "autility.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace alx {

    /**
     * \brief Storage block behind bytes: a header, with the payload allocated right behind it
     *
     * One allocation carries both. Payload sizes are rounded up to a multiple of 2^_align bytes,
     * _align 4 (the default) meaning 16-byte alignment.
     */
    struct ALXBASE_API bytes_block {
        /// References on the block; more than one means a writer has to detach first
        ref_count ref;
        /// Payload bytes in use, never above alloc
        uint_64 size;
        /// Payload bytes allocated, rounded up to the alignment
        uint_64 alloc;
        /// Empty block, capacity 0, counted as one reference
        inline bytes_block() noexcept : size(0), alloc(0) { ref.init_owned(); }
        /// Payload pointer
        inline uint_8* data() noexcept { return reinterpret_cast<uint_8*>(this) + sizeof(bytes_block); }
        /// Payload pointer at byte offset _oft; no bounds check
        inline uint_8* data(uint_64 _oft) noexcept { return reinterpret_cast<uint_8*>(this) + sizeof(bytes_block) + _oft; }
        /// Payload pointer, read-only
        inline const uint_8* data() const noexcept { return reinterpret_cast<const uint_8*>(this) + sizeof(bytes_block); }
        /// Payload pointer at byte offset _oft, read-only; no bounds check
        inline const uint_8* data(uint_64 _oft) const noexcept { return reinterpret_cast<const uint_8*>(this) + sizeof(bytes_block) + _oft; }

        /// Alignment of allocate() and reallocate(), as an exponent: 4 means 16-byte aligned
        static const uint_8 default_align = 4;
        /**
         * \brief Allocate a block whose payload can hold _size bytes
         *
         * The payload is uninitialized and the block counts as one reference. Throws
         * std::length_error when _size cannot be represented, std::bad_alloc when malloc fails.
         *
         * \param _size Payload bytes to reserve
         * \param _align Alignment exponent
         * \return New block, never null
         */
        static bytes_block* allocate(uint_64 _size, uint_8 _align = default_align);
        /**
         * \brief Resize the payload of a block
         *
         * A null _block allocates a fresh one. Otherwise the payload may move, the first
         * min(_size, previous capacity) bytes survive, size is left alone (the caller owns it) and
         * the block comes back with a single reference, so it must be solely owned here.
         *
         * \param _block Block to resize, possibly null
         * \param _size Payload bytes wanted
         * \param _align Alignment exponent
         * \return Resized block, the very same one when the capacity already matches
         */
        static bytes_block* reallocate(bytes_block* _block, uint_64 _size, uint_8 _align = default_align);
        /// Free a block; the caller must hold the last reference
        inline static void deallocate(bytes_block* _block) { free(_block); }
    };
    /// Zero-copy read-only window over a bytes buffer
    class bytes_view;

    /**
     * \brief Copy-on-write byte buffer
     *
     * Copies share the payload through an atomic reference count and behave exactly like deep
     * copies: while the block is shared, any mutating call detaches first, so no other copy and no
     * view ever observes the write. A raw pointer is tied to the object that produced it, and any
     * later call on that object which detaches or grows the block invalidates it.
     *
     * The count is atomic, so copies may be released from other threads; a single object is not
     * internally locked, so concurrent access to one bytes needs the caller's own synchronization.
     * A call that allocates from a size throws std::length_error when that size is not
     * representable, and std::bad_alloc on failure -- the object is left unchanged either way.
     */
    class ALXBASE_API bytes {
    public:
        /// Null buffer: no block at all
        inline bytes() noexcept {}
        /// Release this object's reference; the payload dies with the last one
        inline ~bytes() noexcept {
            if (!null() && !m_data->ref.deref()) bytes_block::deallocate(m_data);
        }
        /// Share the payload of _other, no bytes copied
        bytes(const bytes& _other) noexcept;
        /// Take over the payload of _other, leaving it null
        bytes(bytes&& _other) noexcept;
        /// Release this object's payload and share the one of _other
        bytes& operator=(const bytes& _other) noexcept;
        /// Swap payloads with _other: it is left holding the buffer this object dropped
        bytes& operator=(bytes&& _other) noexcept;

    public:
        /**
         * \brief Copy _size bytes from _src
         *
         * \param _src Bytes to copy; must be valid for _size bytes
         * \param _size Bytes to copy; 0 gives an empty but non-null buffer
         */
        explicit bytes(const void* _src, uint_64 _size);
        /// Allocate _size bytes, uninitialized
        explicit bytes(uint_64 _size);
        /// Allocate _size bytes, every one set to _value
        explicit bytes(uint_64 _size, uint_8 _value);
        /// Copy the C string _src up to its terminator, which is not part of the buffer
        explicit inline bytes(const char* _src) : bytes(_src, strlen(_src)) {}
        /// Copy _src in full, embedded zeros included
        explicit inline bytes(const std::string& _src) : bytes(_src.data(), _src.length()) {}

    public:
        /// True when no block is attached at all
        inline bool null() const noexcept { return nullptr == m_data; }
        /// True when null, or when the payload holds no byte
        inline bool empty() const noexcept { return null() || 0 == m_data->size; }
        /// Payload bytes in use
        inline uint_64 size() const noexcept { return null() ? 0 : m_data->size; }
        /// Payload bytes allocated, never below size()
        inline uint_64 capacity() const noexcept { return null() ? 0 : m_data->alloc; }

    public:
        /**
         * \brief Release the payload
         *
         * A buffer holding bytes is left null; an already empty one keeps its block and changes
         * nothing.
         */
        inline void clear() noexcept {
            if (!empty()) *this = bytes();
        }

        /// Deep-copy the payload while a copy or a view shares it; a no-op otherwise
        inline void detach() {
            if (!null() && m_data->ref.is_shared()) reallocate(m_data->size);
        }

        /**
         * \brief Set the payload length to _size
         *
         * Growing keeps the first min(_size, size()) bytes and leaves the new tail uninitialized;
         * shrinking keeps the first _size. Both detach first while the block is shared, so aliases
         * keep their own bytes and their own length. resize(0) drops the payload: the block goes
         * away when it is shared, and stays with size 0 when it is not.
         */
        inline void resize(uint_64 _size) {
            if (_size == size()) return;
            if (0 == _size) {
                if (m_data->ref.is_shared()) clear();
                else m_data->size = 0;
                return;
            }
            if (_size > capacity() || m_data->ref.is_shared()) reallocate(_size);
            m_data->size = _size;
        }
        /**
         * \brief Grow the allocation to at least _size bytes
         *
         * A no-op when the capacity already covers _size. Nothing of the payload changes, but a
         * shared block is detached, so the allocation becomes this object's own.
         */
        inline void reserve(uint_64 _size) {
            if (_size <= capacity()) return;
            reallocate(_size);
        }

        /// Shrink the allocation down to the payload length; a no-op when it already fits
        inline void fitsize() {
            if (capacity() > size()) reallocate(size());
        }

    public:

        /// Mutable payload pointer, detaching first while the block is shared; null when null()
        inline uint_8* data() { return null() ? nullptr : (detach(), reinterpret_cast<uint_8*>(m_data->data())); }
        /// Payload pointer, read-only; null when null()
        inline const uint_8* data() const noexcept { return null() ? nullptr : reinterpret_cast<const uint_8*>(m_data->data()); }
        /// data(), as a read-only pointer
        inline const uint_8* cdata() const noexcept { return data(); }

    public:
        /// Iterator to the first byte; detaches like data()
        inline uint_8* begin() { return data(); }
        /// Iterator one past the last byte
        inline uint_8* end() { return data() + size(); }
        /// Read-only iterator to the first byte
        inline const uint_8* begin() const noexcept { return data(); }
        /// Read-only iterator one past the last byte
        inline const uint_8* end() const noexcept { return data() + size(); }
        /// cdata(), as an iterator
        inline const uint_8* cbegin() const noexcept { return cdata(); }
        /// cbegin() + size()
        inline const uint_8* cend() const noexcept { return cdata() + size(); }

    public:
        /// Mutable byte at _index; detaches like data(), no bounds check
        inline uint_8& operator[](uint_64 _index) { return data()[_index]; }
        /// Byte at _index, read-only, no bounds check
        inline const uint_8& operator[](uint_64 _index) const noexcept { return data()[_index]; }
        /// True when both buffers hold the same bytes
        inline bool operator==(const bytes& _other) const noexcept { return size() != _other.size() ? false : equa(_other.data(), _other.size(), 0); }
        /// True when the view covers exactly these bytes
        inline bool operator==(const bytes_view& _other) const noexcept;
        /// True when the buffers differ in length or content
        inline bool operator!=(const bytes& _other) const noexcept { return !operator==(_other); }
        /// True when the view does not cover exactly these bytes
        inline bool operator!=(const bytes_view& _other) const noexcept { return !operator==(_other); }

    public:

        /// Reverse _len bytes in place; a no-op when _ptr is null or _len is 0
        static void revers(void* _ptr, uint_64 _len);

        /// Reverse this buffer in place and return *this; detaches first while shared
        inline bytes& revers() noexcept { return revers(data(), size()), *this; }

    public:

        /// Lowercase hex of the [_ptr, _ptr + _size) range; empty when _size is 0
        static std::string to_hex(const uint_8* _ptr, const uint_64 _size);
        /// Base64 of the [_ptr, _ptr + _size) range, '=' padded; empty when _size is 0
        static std::string to_base64(const uint_8* _ptr, const uint_64 _size);
        /// Copy the [_ptr, _ptr + _size) range into a string, embedded zeros included
        static std::string to_string(const uint_8* _ptr, const uint_64 _size);
        /// Copy the [_ptr, _ptr + _size) range into a byte vector
        static std::vector<uint_8> to_vector(const uint_8* _ptr, const uint_64 _size);
        /// Lowercase hex of this buffer
        inline std::string to_hex() const { return empty() ? std::string() : to_hex(data(), size()); }
        /// Base64 of this buffer, '=' padded
        inline std::string to_base64() const { return empty() ? std::string() : to_base64(data(), size()); }
        /// This buffer as a string
        inline std::string to_string() const { return empty() ? std::string() : to_string(data(), size()); }
        /// This buffer as a byte vector
        inline std::vector<uint_8> to_vector() const { return empty() ? std::vector<uint_8>() : to_vector(data(), size()); }

        /**
         * \brief Reinterpret the bytes at _ofst as T&
         *
         * Detaches first, so the reference is writable in place without disturbing a copy. No
         * length and no alignment check: the caller must know a T lies at _ofst.
         *
         * \tparam T Type to overlay
         * \param _ofst Byte offset from data()
         * \return Reference into the payload
         */
        template <typename T> T& to(const uint_64 _ofst = 0) { return r_interpret<T>(data() + _ofst); }

        /**
         * \brief Reinterpret the bytes at _ofst as const T&
         *
         * No length and no alignment check: the caller must know a T lies at _ofst. The reference
         * points into the shared block and never triggers a detach.
         *
         * \tparam T Type to overlay
         * \param _ofst Byte offset from data()
         * \return Read-only reference into the payload
         */
        template <typename T> const T& to(const uint_64 _ofst = 0) const noexcept { return r_interpret<const T>(data() + _ofst); }

    public:

        /**
         * \brief Decode a hex string
         *
         * Either case is accepted. A null bytes comes back when the length is odd, or when any
         * character is not a hex digit.
         *
         * \param _v Hex digit pairs
         * \return Decoded bytes
         */
        static bytes from_hex(const std::string& _v);

        /**
         * \brief Decode a base64 string
         *
         * The length must be a multiple of 4, and the trailing '=' padding is honoured. Characters
         * outside the alphabet are not rejected: they decode as if they were 'A'.
         *
         * \param _v Base64 text
         * \return Decoded bytes; null when the length is not a multiple of 4
         */
        static bytes from_base64(const std::string& _v);

        /**
         * \brief Copy the object representation of a value into a buffer
         *
         * \tparam T Value type
         * \param _v Value to copy
         * \return bytes holding its sizeof(T) bytes
         */
        template <typename T>
        inline static bytes from_ordinary(const T& _v) { return bytes(&_v, sizeof(T)); }

    public:
        /**
         * \brief Append _len bytes from _src
         *
         * The buffer grows, and a view taken before still holds the bytes it lent. Throws as the
         * class describes, and then appends nothing.
         *
         * \param _src Bytes to append; must be valid for _len bytes
         * \param _len Bytes to append; 0 appends nothing
         * \return *this
         */
        bytes& append(const void* _src, const uint_64 _len);
        /// Append another buffer; appending this buffer to itself is safe
        inline bytes& append(const bytes& _v) {
            bytes t(_v);
            return append(t.data(), t.size());
        }
        /// Append a string in full, embedded zeros included
        inline bytes& append(const std::string& _v) { return append(_v.data(), _v.length()); }
        /// Append the C string _v, its terminator excluded
        inline bytes& append(const char* _v) { return append(_v, strlen(_v)); }
        /// Append the bytes the view covers
        inline bytes& append(const bytes_view& _v);
        /// Append the bytes the (pointer, length) pair points at
        inline bytes& operator<<(std::pair<const void*, const uint_64> _v) { return append(_v.first, _v.second); }
        /// Append another buffer
        inline bytes& operator<<(const bytes& _v) { return append(_v); }
        /// Append a string in full
        inline bytes& operator<<(const std::string& _v) { return append(_v); }
        /// Append the C string _v, its terminator excluded
        inline bytes& operator<<(const char* _v) { return append(_v); }
        /// Append the bytes a view covers
        inline bytes& operator<<(const bytes_view& _v) { return append(_v); }

        /// Append the object representation of _v, sizeof(T) bytes; returns *this
        template <typename T>
        inline bytes& append_ordinary(const T& _v) { return append(&_v, sizeof(T)); }
        /// Append any trivially copyable value by copying its sizeof(T) bytes
        template <typename T, typename = std::enable_if<std::is_trivially_copyable_v<T>>>
        inline bytes& operator<<(const T& _v) { return append(&_v, sizeof(T)); }

    public:

        /**
         * \brief Copy a byte range out of the buffer
         *
         * _len is clamped to what is left after _pos, and the default max_uint_64 means "to the
         * end". A null buffer, or _pos at or past size(), gives a null bytes.
         *
         * \param _pos First byte to copy
         * \param _len Bytes wanted
         * \return Copied range
         */
        bytes mid(uint_64 _pos, uint_64 _len = max_uint_64) const;

        /**
         * \brief Window over a byte range, without copying
         *
         * _len is clamped to what is left after _pos, and the default max_uint_64 means "to the
         * end". A null buffer, or _pos at or past size(), gives an empty view. The view keeps the
         * block alive on its own, so it outlives this object and this call.
         *
         * \param _pos First byte of the window
         * \param _len Bytes wanted
         * \return View over the same block
         */
        bytes_view mid_view(uint_64 _pos, uint_64 _len = max_uint_64) const noexcept;

        /// Copy the first _len bytes, clamped to size(); the whole buffer when _len is larger
        bytes left(uint_64 _len) const;

        /// Window over the first _len bytes, clamped to size(); empty when null()
        bytes_view left_view(uint_64 _len) const noexcept;

        /// Copy the last _len bytes, clamped to size(); the whole buffer when _len is larger
        bytes right(uint_64 _len) const;

        /// Window over the last _len bytes, clamped to size(); empty when null()
        bytes_view right_view(uint_64 _len) const noexcept;

    public:

        /**
         * \brief Find a byte sequence
         *
         * The match has to lie inside [_from, min(size(), _to)); a null tag, an empty span or a tag
         * longer than the span gives uint_64_npos. A tag of length 0 matches at _from.
         *
         * \param _tag Bytes to look for
         * \param _len Length of _tag
         * \param _from Position to start the search at
         * \param _to End of the search span, exclusive; max_uint_64 = size()
         * \return First match, or uint_64_npos
         */
        uint_64 find(const void* _tag, const uint_64 _len, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            return alx::find(data(), size(), _tag, _len, _from, _to);
        }

        /// Find the bytes of _tag, under the span rules of the search above
        inline uint_64 find(const bytes& _tag, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            return find(_tag.data(), _tag.size(), _from, _to);
        }

        /// Find the sizeof(T) bytes of _tag's object representation, same span rules
        template <typename T>
        inline uint_64 find_ordinary(const T& _tag, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            return find(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _from, _to);
        }

        /**
         * \brief Find the last occurrence of a byte sequence
         *
         * The match has to lie inside [_rto, min(size() - 1, _rfrom)], so it never runs past the
         * end. A null tag, or an empty span, gives uint_64_npos; a tag of length 0 matches at
         * min(size() - 1, _rfrom).
         *
         * \param _tag Bytes to look for
         * \param _len Length of _tag
         * \param _rfrom Position to start the backward search at; max_uint_64 = size() - 1
         * \param _rto Lower bound of the search span
         * \return Last match, or uint_64_npos
         */
        uint_64 rfind(const void* _tag, const uint_64 _len, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            return alx::rfind(data(), size(), _tag, _len, _rfrom, _rto);
        }

        /// Last occurrence of the bytes of _tag, under the span rules of the search above
        inline uint_64 rfind(const bytes& _tag, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            return rfind(_tag.data(), _tag.size(), _rfrom, _rto);
        }

        /// Last occurrence of _tag's object representation, same span rules
        template <typename T>
        inline uint_64 rfind_ordinary(const T& _tag, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            return rfind(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _rfrom, _rto);
        }

    public:

        /**
         * \brief Compare a byte sequence with the bytes at _from
         *
         * A comparison of length 0 is true whatever _from is; a null _tag, or a range running past
         * size(), compares false.
         *
         * \param _tag Bytes to compare with
         * \param _len Length of _tag
         * \param _from Offset in this buffer to start at
         * \return True when both ranges hold the same bytes
         */
        bool equa(const void* _tag, const uint_64 _len, const uint_64 _from) const noexcept {
            if (_len == 0) return true;
            if (_from + _len > size() || nullptr == _tag)
                return false;
            return memcmp(_tag, m_data->data() + _from, _len) == 0;
        }

        /// Compare the bytes of _tag with the bytes at _from
        inline bool equa(const bytes& _tag, const uint_64 _from) const noexcept {
            return equa(_tag.data(), _tag.size(), _from);
        }

        /// Compare _tag's object representation with the bytes at _from
        template <typename T>
        inline bool equa_ordinary(const T& _tag, const uint_64 _from) const noexcept {
            return equa(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _from);
        }

    private:
        void reallocate(uint_64 _size);
        void ensure_capacity(uint_64 _need);

    private:
        bytes_block* m_data{nullptr};
    };

    /**
     * \brief Zero-copy read-only window over a bytes buffer
     *
     * The view holds a reference on the block, so its bytes are frozen for the view's whole life:
     * the buffer it came from may be written, resized or destroyed without disturbing the window
     * -- a write detaches instead of touching the shared block -- and a view may therefore outlive
     * the bytes it was taken from. Nothing here ever modifies the payload.
     *
     * Every position-relative operation works on the window, not on the whole block: find() and
     * rfind() return view-relative offsets, mid/left/right count from the window's first byte.
     */
    class bytes_view {
    public:
        /// Empty window, over nothing
        inline bytes_view() noexcept {}
        /// Window over the whole of _orig
        inline bytes_view(const bytes& _orig) noexcept : m_orig(_orig), m_size(_orig.size()) {}
        /// Window over _orig, a temporary that the view takes over and keeps alive
        inline bytes_view(bytes&& _orig) noexcept : m_orig(std::move(_orig)) { m_size = m_orig.size(); }
        /**
         * \brief Window over [_ofst, _ofst + _size) of _orig
         *
         * _size is clamped to what is left of _orig after _ofst; _ofst at or past _orig.size()
         * gives an empty window.
         *
         * \param _orig Buffer to look into; not copied
         * \param _ofst First byte of the window
         * \param _size Bytes wanted
         */
        inline bytes_view(const bytes& _orig, uint_64 _ofst, uint_64 _size) noexcept : m_orig(_orig) {
            if (_ofst >= _orig.size())
                return;
            m_ofst = _ofst;
            m_size = alx::min_value(_orig.size() - _ofst, _size);
        }
        /// Drop this view's reference; the block dies with the last one
        inline ~bytes_view() noexcept {}
        /// Window over the same bytes; no bytes are copied
        inline bytes_view(const bytes_view& _other) noexcept : m_orig(_other.m_orig), m_ofst(_other.m_ofst), m_size(_other.m_size) {}
        /// Take over the window of _other, which is left empty
        inline bytes_view(bytes_view&& _other) noexcept {
            m_orig = std::move(_other.m_orig);
            m_ofst = _other.m_ofst;
            m_size = _other.m_size;
            _other.m_ofst = 0;
            _other.m_size = 0;
        }
        /// Re-point this view at the window of _other, dropping the reference it held
        inline bytes_view& operator=(const bytes_view& _other) noexcept {
            if (this != &_other) {
                m_orig = _other.m_orig;
                m_ofst = _other.m_ofst;
                m_size = _other.m_size;
            }
            return *this;
        }
        /**
         * \brief Take over the window of _other
         *
         * _other is left empty (size 0), holding the reference this view dropped: the block it
         * points at lives until _other is reassigned or destroyed.
         */
        inline bytes_view& operator=(bytes_view&& _other) noexcept {
            if (this != &_other) {
                m_orig = std::move(_other.m_orig);
                m_ofst = _other.m_ofst;
                m_size = _other.m_size;
                _other.m_ofst = 0;
                _other.m_size = 0;
            }
            return *this;
        }

    public:
        /// empty(): a zero-length window reports null even while it holds a block
        inline bool null() const noexcept { return empty(); }
        /// True when the window covers no byte
        inline bool empty() const noexcept { return 0 == m_size; }
        /// Window length in bytes
        inline uint_64 size() const noexcept { return m_size; }
        /// First byte of the window; null only when no block is held at all
        inline const uint_8* data() const noexcept { return m_orig.null() ? nullptr : m_orig.data() + m_ofst; }
        /// data(), as a read-only pointer
        inline const uint_8* cdata() const noexcept { return data(); }
        /// Iterator to the window's first byte
        inline const uint_8* begin() const noexcept { return data(); }
        /// Iterator one past the window's last byte
        inline const uint_8* end() const noexcept { return data() + size(); }
        /// cdata(), as an iterator
        inline const uint_8* cbegin() const noexcept { return cdata(); }
        /// cbegin() + size()
        inline const uint_8* cend() const noexcept { return cdata() + size(); }

        /// Drop the reference and reset the view to empty; a pointer taken from it before stops
        /// being valid
        inline void destroy() noexcept {
            m_orig = bytes();
            m_ofst = 0;
            m_size = 0;
        }

    public:
        /// Byte at _index in the window; no bounds check
        inline const uint_8& operator[](uint_64 _index) const noexcept { return data()[_index]; }
        /// True when the window covers exactly the bytes of _other
        inline bool operator==(const bytes& _other) const noexcept { return size() != _other.size() ? false : equa(_other.data(), _other.size(), 0); }
        /// True when both windows cover the same bytes
        inline bool operator==(const bytes_view& _other) const noexcept { return size() != _other.size() ? false : equa(_other.data(), _other.size(), 0); }
        /// True when the window and the buffer differ in length or content
        inline bool operator!=(const bytes& _other) const noexcept { return !operator==(_other); }
        /// True when the windows differ in length or content
        inline bool operator!=(const bytes_view& _other) const noexcept { return !operator==(_other); }

    public:

        /// The window as an owned bytes: a window over the whole block hands its own handle over,
        /// a partial one copies the bytes
        inline bytes to_bytes() const { return m_ofst == 0 && m_size == m_orig.size() ? m_orig : bytes(m_orig.data() + m_ofst, m_size); }
        /// Lowercase hex of the window
        inline std::string to_hex() const { return empty() ? std::string() : bytes::to_hex(data(), size()); }
        /// Base64 of the window, '=' padded
        inline std::string to_base64() const { return empty() ? std::string() : bytes::to_base64(data(), size()); }
        /// The window as a string
        inline std::string to_string() const { return empty() ? std::string() : bytes::to_string(data(), size()); }
        /// The window as a byte vector
        inline std::vector<uint_8> to_vector() const { return empty() ? std::vector<uint_8>() : bytes::to_vector(data(), size()); }

        /**
         * \brief Reinterpret the window bytes at _ofst as const T&
         *
         * No length and no alignment check: the caller must know a T lies at _ofst.
         *
         * \tparam T Type to overlay
         * \param _ofst Byte offset from data()
         * \return Read-only reference into the block
         */
        template <typename T> const T& to(const uint_64 _ofst = 0) const noexcept { return r_interpret<const T>(data() + _ofst); }

    public:

        /**
         * \brief Copy a byte range out of the window
         *
         * _len is clamped to what is left after _pos, max_uint_64 meaning "to the end of the
         * window". An empty window, or _pos at or past size(), gives a null bytes.
         *
         * \param _pos Offset from the window's first byte
         * \param _len Bytes wanted
         * \return Copied range
         */
        inline bytes mid(uint_64 _pos, uint_64 _len = max_uint_64) const {
            return empty() || _pos >= m_size ? bytes() : m_orig.mid(_pos + m_ofst, alx::min_value(_len, m_size - _pos));
        }

        /**
         * \brief Window over a byte range of the window
         *
         * _len is clamped to what is left after _pos. The result points into the same block and
         * keeps it alive on its own.
         *
         * \param _pos Offset from the window's first byte
         * \param _len Bytes wanted
         * \return Sub-window, empty when out of range
         */
        inline bytes_view mid_view(uint_64 _pos, uint_64 _len = max_uint_64) const noexcept {
            return empty() || _pos >= m_size ? bytes_view() : m_orig.mid_view(_pos + m_ofst, alx::min_value(_len, m_size - _pos));
        }

        /// Copy the first _len bytes of the window, clamped to size()
        inline bytes left(uint_64 _len) const {
            return m_orig.mid(m_ofst, alx::min_value(_len, m_size));
        }

        /// Window over the first _len bytes of the window, clamped to size()
        inline bytes_view left_view(uint_64 _len) const noexcept {
            return m_orig.mid_view(m_ofst, alx::min_value(_len, m_size));
        }

        /// Copy the last _len bytes of the window, clamped to size()
        inline bytes right(uint_64 _len) const {
            const uint_64 len = alx::min_value(_len, m_size);
            return m_orig.mid(m_ofst + m_size - len, len);
        }

        /// Window over the last _len bytes of the window, clamped to size()
        inline bytes_view right_view(uint_64 _len) const noexcept {
            const uint_64 len = alx::min_value(_len, m_size);
            return m_orig.mid_view(m_ofst + m_size - len, len);
        }

    public:

        /**
         * \brief Find a byte sequence inside the window
         *
         * The match has to lie inside the window, in [_from, min(size(), _to)) counted from its
         * first byte, and it never runs past the window's end.
         *
         * \param _tag Bytes to look for
         * \param _len Length of _tag
         * \param _from Offset to start the search at
         * \param _to End of the search span, exclusive; max_uint_64 = size()
         * \return View-relative match offset, or uint_64_npos
         */
        uint_64 find(const void* _tag, const uint_64 _len, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            uint_64 result = m_orig.find(_tag, _len, m_ofst + _from, m_ofst + alx::min_value(_to, m_size));
            return result == uint_64_npos ? uint_64_npos : result - m_ofst;
        }

        /// Find the bytes of _tag inside the window
        inline uint_64 find(const bytes& _tag, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            return find(_tag.data(), _tag.size(), _from, _to);
        }

        /// Find _tag's object representation inside the window, same span rules
        template <typename T>
        inline uint_64 find_ordinary(const T& _tag, const uint_64 _from = 0, const uint_64 _to = max_uint_64) const noexcept {
            return find(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _from, _to);
        }

        /**
         * \brief Find the last occurrence of a byte sequence inside the window
         *
         * The search spans [_rto, min(size() - 1, _rfrom)] counted from the window's first byte,
         * and a match never runs past the window's end; a tag of length 0 matches at
         * min(size() - 1, _rfrom).
         *
         * \param _tag Bytes to look for
         * \param _len Length of _tag
         * \param _rfrom Offset to start the backward search at; max_uint_64 = size() - 1
         * \param _rto Lower bound of the search span
         * \return View-relative match offset, or uint_64_npos
         */
        uint_64 rfind(const void* _tag, const uint_64 _len, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            if (empty()) return uint_64_npos;
            const uint_64 result = m_orig.rfind(_tag, _len, m_ofst + alx::min_value(size() - 1, _rfrom), m_ofst + _rto);
            return result == uint_64_npos ? uint_64_npos : result - m_ofst;
        }

        /// Last occurrence of the bytes of _tag inside the window
        inline uint_64 rfind(const bytes& _tag, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            return rfind(_tag.data(), _tag.size(), _rfrom, _rto);
        }

        /// Last occurrence of _tag's object representation inside the window
        template <typename T>
        inline uint_64 rfind_ordinary(const T& _tag, const uint_64 _rfrom = max_uint_64, const uint_64 _rto = 0) const noexcept {
            return rfind(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _rfrom, _rto);
        }

    public:

        /**
         * \brief Compare a byte sequence with the window bytes at _from
         *
         * As in bytes: a comparison of length 0 is true whatever _from is, a null _tag or a range
         * running past size() compares false.
         *
         * \param _tag Bytes to compare with
         * \param _len Length of _tag
         * \param _from Offset in the window to start at
         * \return True when both ranges hold the same bytes
         */
        inline bool equa(const void* _tag, const uint_64 _len, const uint_64 _from) const noexcept {
            if (_len == 0) return true;
            if (_from + _len > size() || nullptr == _tag)
                return false;
            return memcmp(_tag, data() + _from, _len) == 0;
        }

        /// Compare the bytes of _tag with the window bytes at _from
        inline bool equa(const bytes& _tag, const uint_64 _from) const noexcept {
            return equa(_tag.data(), _tag.size(), _from);
        }

        /// Compare _tag's object representation with the window bytes at _from
        template <typename T>
        inline bool equa_ordinary(const T& _tag, const uint_64 _from) const noexcept {
            return equa(reinterpret_cast<const uint_8*>(&_tag), sizeof(T), _from);
        }

    private:
        bytes m_orig;
        uint_64 m_ofst{0};
        uint_64 m_size{0};
    };
    /// True when the view covers exactly these bytes
    inline bool bytes::operator==(const bytes_view& _other) const noexcept {
        return size() != _other.size() ? false : equa(_other.data(), _other.size(), 0);
    }
    /// Append the bytes the view covers
    inline bytes& bytes::append(const bytes_view& _v) {
        return append(_v.data(), _v.size());
    }
}

#endif