/*****************************************************************/ /**
 * \file   astream.h
 * \brief  Abstract stream interfaces with in-memory buffer implementations
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_STREAM_H_
#define _ALEXIS_STREAM_H_

#include "abase.h"
#include "abytes.h"
#include "autility.h"

namespace alx {

    /**
     * \brief Abstract output stream: bytes reach an implementation only through append()
     *
     * The append() template, append_ordinary() and every operator<< overload funnel into the one
     * virtual append(). A stream that refuses a block reports it on that call, and the operator<<
     * overloads drop the answer. Derived streams are not serialized internally, so one thread
     * drives a stream at a time. name_ is a diagnostic label a derived constructor may fill.
     */
    class ALXBASE_API ostream : public noncopyable {
    public:
        /// Default-construct with an empty name_; a derived stream sets the label
        ostream() = default;
        /// Virtual, so a derived stream is destroyed through ostream*
        virtual ~ostream() = default;

        /**
         * \brief Write _size raw bytes
         *
         * The only write path of the interface. False when the sink refuses the bytes; a zero-size
         * append is never a failure.
         *
         * \param _ptr Bytes to write
         * \param _size Byte count
         * \return false when the bytes did not reach the sink
         */
        virtual bool append(const void* _ptr, uint_64 _size) = 0;

        /// Write out whatever append() left buffered; true when nothing was pending
        virtual bool flush() = 0;

        /// Drop the accumulated data and start over; buffered bytes are discarded, not written
        virtual void reset() = 0;

        /**
         * \brief Bytes accumulated in the sink
         *
         * Not a count of what append() wrote: a stream wrapping an existing buffer reports that
         * buffer's size, content it held beforehand included, and one that transforms bytes on the
         * way out may report a length the sink does not hold. A caller wanting a delta reads
         * total() before writing and subtracts.
         */
        virtual uint_64 total() const = 0;

        /// Append the bytes of _data -- data() and size() -- with no element conversion
        template <typename T>
        inline bool append(const T& _data) { return append(_data.data(), _data.size()); }
        /// Append the object representation of _data, sizeof(T) bytes; unlike operator<< it is not
        /// restricted to trivially copyable types
        template <typename T>
        inline bool append_ordinary(const T& _data) { return append(&_data, sizeof(T)); }

        /**
         * \brief Append the object representation of _data, sizeof(T) bytes
         *
         * Host layout and byte order, no length prefix. The result of append() is dropped, so a
         * refusing sink stays silent through operator<<.
         *
         * \return *this, for chaining
         */
        template <typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
        inline ostream& operator<<(const T& _data) {
            append_ordinary(_data);
            return *this;
        }
        /// Append a C string: strlen bytes, terminator excluded
        inline ostream& operator<<(const char* _data) {
            append(_data, strlen(_data));
            return *this;
        }
        /// Append the buffer's bytes
        inline ostream& operator<<(const bytes& _data) {
            append(_data.data(), _data.size());
            return *this;
        }
        /// Append the window's bytes
        inline ostream& operator<<(const bytes_view& _data) {
            append(_data.data(), _data.size());
            return *this;
        }
        /// Append the string's bytes, terminator excluded
        inline ostream& operator<<(const std::string& _data) {
            append(_data.data(), _data.size());
            return *this;
        }
        /// The stream's label: empty on the base, filled by the derived stream that owns one. The
        /// reference points into the stream and stays valid until the stream dies
        inline const std::string& name() const { return name_; }

    protected:
        /// Label handed out by name()
        std::string name_;
    };

    /**
     * \brief Abstract input stream: positional reads of a byte range
     *
     * get() is the whole interface -- an offset and a length, no cursor and no state change, so the
     * same range can be read again at any time; the view it returns keeps its own bytes alive.
     * total() and the range a read is clamped against are fixed at construction, so bytes added to
     * the source afterwards are not seen. Reads are not serialized: one thread at a time.
     */
    class ALXBASE_API istream : public noncopyable {
    public:
        /// Default-construct with an empty name_; a derived stream sets the label
        istream() = default;
        /// Virtual, so a derived stream is destroyed through istream*
        virtual ~istream() = default;

        /**
         * \brief View of _size bytes starting at _ofst
         *
         * A read that runs past the end is short, not an error: the view is clamped to what is
         * there, and comes back empty when _ofst is at or past the end, so a caller detects a short
         * read by comparing size() against what it asked for.
         *
         * \param _ofst Offset from the stream's first byte
         * \param _size Bytes wanted; uint_64_npos reads to the end
         */
        virtual bytes_view get(uint_64 _ofst, uint_64 _size) const = 0;

        /// Bytes the stream covers, as fixed when it was opened; 0 on a stream that never opened
        virtual uint_64 total() const = 0;

        /// The stream's label: empty on the base, filled by the derived stream that owns one. The
        /// reference points into the stream and stays valid until the stream dies
        inline const std::string& name() const { return name_; }

    protected:
        /// Label handed out by name()
        std::string name_;
    };

    /**
     * \brief ostream appending straight into a caller-owned bytes buffer
     *
     * The stream holds a reference, not a copy: _buff must outlive it, and every append() lands in
     * it immediately -- nothing is buffered, so there is nothing to flush and append() always
     * reports success. Content the buffer already holds is kept, and the stream writes after it.
     */
    class ALXBASE_API ostream_buff
        : public ostream {
    public:
        /// Wrap _buff: no copy is taken and nothing is cleared
        ostream_buff(bytes& _buff)
            : ostream(), buff_(_buff) {}
        /// Nothing is written out and nothing is freed: the buffer belongs to the caller
        ~ostream_buff() {}

    public:
        /// Keep the ostream append() overloads visible under the override below
        using ostream::append;
        /// Append to the wrapped buffer; always true -- this sink cannot refuse a block
        virtual bool append(const void* _ptr, uint_64 _size) override {
            buff_.append(_ptr, _size);
            return true;
        }
        /// No-op, the bytes are already in the buffer; always true
        inline virtual bool flush() override { return true; }
        /// Truncate the wrapped buffer, whatever it held; total() then reads 0
        inline virtual void reset() override { buff_.resize(0); }
        /// Size of the wrapped buffer, content it held before the stream was attached included
        inline virtual uint_64 total() const override { return buff_.size(); }
        /// Set the label reported by name()
        inline void set_name(const std::string& _name) { name_ = _name; }

    private:
        bytes& buff_;
    };

    /**
     * \brief istream reading a byte window without copying it
     *
     * The view is taken by value and holds its block alive, so the bytes it was made from may die
     * first. Offsets are relative to the window's first byte, and the window's own size -- not the
     * whole block's -- is the limit a read is clamped against.
     */
    class ALXBASE_API istream_buff
        : public istream {
    public:
        /// Wrap the window _buff; no byte is copied, and the view keeps its block alive
        istream_buff(const bytes_view& _buff)
            : istream(), buff_(_buff) {}
        /// Release the view's hold on the block; the bytes belong to whoever made the window
        ~istream_buff() {}

    public:
        /// Window into the wrapped view, _ofst counted from its first byte and the read clamped to
        /// the view's size
        virtual bytes_view get(uint_64 _ofst, uint_64 _size) const override {
            return buff_.mid_view(_ofst, _size);
        }
        /// Size of the wrapped window
        virtual uint_64 total() const override { return buff_.size(); }
        /// Set the label reported by name()
        inline void set_name(const std::string& _name) { name_ = _name; }

    private:
        const bytes_view buff_;
    };
}

#endif