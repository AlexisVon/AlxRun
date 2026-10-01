/*****************************************************************/ /**
 * \file   autility.h
 * \brief  Utility types and helpers (traits, bit ops, noncopyable, signal, singleton)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_UTILITY_H_
#define _ALEXIS_UTILITY_H_

#include "abase.h"

#include <cstring>
#include <list>
#include <map>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <atomic>
#include <mutex>
#if CPP_VERSION >= CPP_17_ID || defined(_MSC_VER)
#    include <shared_mutex>
#endif

#include <functional>
#include <future>
#include <utility>

#if CPP_VERSION < CPP_17_ID
#    ifdef __clang__
#        pragma clang diagnostic ignored "-Wc++17-extensions"
#        if CPP_VERSION < CPP_14_ID
#            pragma clang diagnostic ignored "-Wc++14-extensions"
#        endif
#    elif defined(__GNUC__)
#        pragma GCC diagnostic ignored "-Wc++17-extensions"
#        if CPP_VERSION < CPP_14_ID
#            pragma GCC diagnostic ignored "-Wc++14-extensions"
#        endif
#    endif
#endif

#if CPP_VERSION < CPP_17_ID && !defined(_WIN32)
namespace std {
    template <typename T> constexpr bool is_pointer_v = is_pointer<T>::value;
    template <typename T> constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;
    template <typename T, typename U> constexpr bool is_same_v = is_same<T, U>::value;
    template <typename T, typename U> constexpr bool is_base_of_v = is_base_of<T, U>::value;
    template <typename T> constexpr bool is_null_pointer_v = is_null_pointer<T>::value;
    template <typename T> constexpr bool is_copy_constructible_v = is_copy_constructible<T>::value;

#    if CPP_VERSION < CPP_14_ID
    template <typename T> using decay_t = typename decay<T>::type;
    template <bool COND, typename T = void> using enable_if_t = typename enable_if<COND, T>::type;
    template <bool COND, typename T, typename F> using conditional_t = typename conditional<COND, T, F>::type;
#    endif
}
#endif

namespace alx {

    /// True when T is int_8, int_16, int_32 or int_64; char matches, signed char and long do not
    template <typename T>
    constexpr bool is_integer =
        std::is_same_v<T, int_8> || std::is_same_v<T, int_16> || std::is_same_v<T, int_32> || std::is_same_v<T, int_64>;
    /// True when T is uint_8, uint_16, uint_32 or uint_64
    template <typename T>
    constexpr bool is_uinteger =
        std::is_same_v<T, uint_8> || std::is_same_v<T, uint_16> || std::is_same_v<T, uint_32> || std::is_same_v<T, uint_64>;
    /// True when T is real_32 or real_64; long double does not match
    template <typename T>
    constexpr bool is_realnum =
        std::is_same_v<T, real_32> || std::is_same_v<T, real_64>;
    /// True when any of the three traits above matches; false for bool, pointers and classes
    template <typename T>
    constexpr bool is_number = is_integer<T> || is_uinteger<T> || is_realnum<T>;

    /**
     * \brief Whether every value of From survives the conversion to To
     *
     * Widening qualifies: a signed source needs a signed target at least as wide, or a real target
     * strictly wider; an unsigned source needs an unsigned target at least as wide, or a signed or
     * real target strictly wider; a real source needs a real target at least as wide. Everything
     * else is false -- pointers and classes, any narrowing, a sign that cannot be carried, and
     * real-to-integer, which never qualifies.
     */
    template <typename From, typename To>
    constexpr bool is_lossless =
        std::is_same_v<From, To> ? true : is_integer<From> ? (is_integer<To> && sizeof(To) >= sizeof(From)) || (is_realnum<To> && sizeof(To) > sizeof(From))
                                      : is_uinteger<From>  ? (is_integer<To> && sizeof(To) > sizeof(From)) || (is_uinteger<To> && sizeof(To) >= sizeof(From)) || (is_realnum<To> && sizeof(To) > sizeof(From))
                                      : is_realnum<From>   ? is_realnum<To> && sizeof(To) >= sizeof(From)
                                                           : false;

    /// The signed or unsigned integer of exactly Size bytes; any other Size is void
    template <uint_8 Size, bool Sign>
    using size_int = std::conditional_t<Sign,
                                        std::conditional_t<Size == 1, int_8,
                                                           std::conditional_t<Size == 2, int_16,
                                                                              std::conditional_t<Size == 4, int_32,
                                                                                                 std::conditional_t<Size == 8, int_64, void>>>>,
                                        std::conditional_t<Size == 1, uint_8,
                                                           std::conditional_t<Size == 2, uint_16,
                                                                              std::conditional_t<Size == 4, uint_32,
                                                                                                 std::conditional_t<Size == 8, uint_64, void>>>>>;

    /// Reverse the byte order of a 16-bit value -- an endianness swap: 0X1234 becomes 0X3412
    inline uint_16 byte_reverse(uint_16 _value) {
        return static_cast<uint_16>((_value & 0xFF00) >> 8 | (_value & 0x00FF) << 8);
    }
    /// The 32-bit overload: the four bytes are reversed
    inline uint_32 byte_reverse(uint_32 _value) {
        _value = (_value & 0xFFFF0000) >> 16 | (_value & 0x0000FFFF) << 16;
        return (_value & 0xFF00FF00) >> 8 | (_value & 0x00FF00FF) << 8;
    }
    /// The 64-bit overload: the eight bytes are reversed
    inline uint_64 byte_reverse(uint_64 _value) {
        _value = (_value & 0xFFFFFFFF00000000) >> 32 | (_value & 0x00000000FFFFFFFF) << 32;
        _value = (_value & 0xFFFF0000FFFF0000) >> 16 | (_value & 0x0000FFFF0000FFFF) << 16;
        return (_value & 0xFF00FF00FF00FF00) >> 8 | (_value & 0x00FF00FF00FF00FF) << 8;
    }

    /// _value rounded up to a multiple of 2^_align: _align is the shift, not the alignment
    template <uint_64 _value, uint_8 _align>
    constexpr uint_64 bit_align_v = ((_value + (uint_64(1) << _align) - uint_64(1))) & ~((uint_64(1) << _align) - uint_64(1));
    /// The runtime form of bit_align_v; bit_align(1, 12) is 4096
    inline uint_64 bit_align(uint_64 _value, uint_8 _align) { return ((_value + ((uint_64) 1 << _align) - 1)) & ~(((uint_64) 1 << _align) - 1); }

    /// Reverse the bit order of an 8-bit value: 0X01 becomes 0X80
    inline uint_8 bit_reverse(uint_8 _value) {
        _value = static_cast<uint_8>((_value & 0xF0) >> 4 | (_value & 0x0F) << 4);
        _value = static_cast<uint_8>((_value & 0xCC) >> 2 | (_value & 0x33) << 2);
        _value = static_cast<uint_8>((_value & 0xAA) >> 1 | (_value & 0x55) << 1);
        return _value;
    }
    /// The 16-bit overload: the sixteen bits are reversed
    inline uint_16 bit_reverse(uint_16 _value) {
        _value = static_cast<uint_16>((_value & 0xFF00) >> 8 | (_value & 0x00FF) << 8);
        _value = static_cast<uint_16>((_value & 0xF0F0) >> 4 | (_value & 0x0F0F) << 4);
        _value = static_cast<uint_16>((_value & 0xCCCC) >> 2 | (_value & 0x3333) << 2);
        _value = static_cast<uint_16>((_value & 0xAAAA) >> 1 | (_value & 0x5555) << 1);
        return _value;
    }
    /// The 32-bit overload: the thirty-two bits are reversed
    inline uint_32 bit_reverse(uint_32 _value) {
        _value = (_value & 0xFFFF0000) >> 16 | (_value & 0x0000FFFF) << 16;
        _value = (_value & 0xFF00FF00) >> 8 | (_value & 0x00FF00FF) << 8;
        _value = (_value & 0xF0F0F0F0) >> 4 | (_value & 0x0F0F0F0F) << 4;
        _value = (_value & 0xCCCCCCCC) >> 2 | (_value & 0x33333333) << 2;
        _value = (_value & 0xAAAAAAAA) >> 1 | (_value & 0x55555555) << 1;
        return _value;
    }
    /// The 64-bit overload: the sixty-four bits are reversed
    inline uint_64 bit_reverse(uint_64 _value) {
        _value = (_value & 0xFFFFFFFF00000000) >> 32 | (_value & 0x00000000FFFFFFFF) << 32;
        _value = (_value & 0xFFFF0000FFFF0000) >> 16 | (_value & 0x0000FFFF0000FFFF) << 16;
        _value = (_value & 0xFF00FF00FF00FF00) >> 8 | (_value & 0x00FF00FF00FF00FF) << 8;
        _value = (_value & 0xF0F0F0F0F0F0F0F0) >> 4 | (_value & 0x0F0F0F0F0F0F0F0F) << 4;
        _value = (_value & 0xCCCCCCCCCCCCCCCC) >> 2 | (_value & 0x3333333333333333) << 2;
        _value = (_value & 0xAAAAAAAAAAAAAAAA) >> 1 | (_value & 0x5555555555555555) << 1;
        return _value;
    }

    /// Read bit _offset of the block, counted from its start, low bit of each byte first
    inline bool bit_get(const void* _obj, uint_64 _offset) {
        return (((const uint_8*) (_obj))[_offset >> 3] & (0X01U << (_offset & 0X07U))) != 0;
    }
    /// Write bit _offset of the block as bit_get() reads it; _state false clears the bit
    inline void bit_set(void* _obj, uint_64 _offset, bool _state) {
        _state ? (((uint_8*) (_obj))[_offset >> 3]) |= (0X01U << (_offset & 0X07U)) : (((uint_8*) (_obj))[_offset >> 3]) &= (~(0X01U << (_offset & 0X07U)));
    }

    /**
     * \brief Delete what a pointer holds, then set the pointer to null
     *
     * A null pointer is a no-op. is_arr picks delete[] over delete, and the choice has to match
     * how the pointer was allocated.
     */
    template <typename T>
    inline void safe_delete(T*& _ptr, bool is_arr = false) {
        if (is_arr) delete[] _ptr;
        else delete _ptr;
        _ptr = nullptr;
    }

    /**
     * \brief View raw memory as a T object, without copying it
     *
     * Nothing is checked: _ptr has to be aligned for T and hold a T's bytes. The reference aliases
     * the buffer, so it is valid only while the buffer is; writing through it writes the buffer.
     */
    template <typename T>
    inline T& r_interpret(void* _ptr) { return *reinterpret_cast<T*>(_ptr); }
    /// The const overload: the same view, read-only
    template <typename T>
    inline const T& r_interpret(const void* _ptr) { return *reinterpret_cast<const T*>(_ptr); }

    /// Copy sizeof(T) bytes of _val into _ptr; _ptr may be unaligned but must have room for a T
    template <typename T>
    inline void m_interpret(void* _ptr, T _val) { memcpy(_ptr, &_val, sizeof(T)); }

    /**
     * \brief Read sizeof(T) bytes from _ptr and answer them as a T
     *
     * The bytes are copied and the T is built from that copy, so _ptr may be unaligned; T has to
     * be a trivially copyable type.
     */
    template <typename T>
    inline T m_interpret(const void* _ptr) {
        alignas(T) uint_8 buf[sizeof(T)];
        memcpy(buf, _ptr, sizeof(T));
        return *reinterpret_cast<const T*>(buf);
    }

    /// _true when _cond is set, _false otherwise; both arguments are evaluated by the caller
    template <typename T>
    constexpr T cond_value(bool _cond, T _true, T _false) { return _cond ? _true : _false; }
    /// The larger of two values of the same type; a tie answers the left one
    template <typename T>
    constexpr T max_value(T _left, T _right) { return _left >= _right ? _left : _right; }
    /// The smaller of two values of the same type; a tie answers the left one
    template <typename T>
    constexpr T min_value(T _left, T _right) { return _left <= _right ? _left : _right; }

    /// Clamp _val into [_min, _max]; the two bounds are not compared with each other
    template <typename T>
    inline T border(T _val, T _min, T _max) { return _val > _max ? _max : _val < _min ? _min
                                                                                      : _val; }

    /**
     * \brief Look a key up and answer the value that goes with it
     *
     * \param _key Key to look for; the map itself is not modified
     * \param _def Value to answer when the key is absent
     * \return A reference to the value in the map, or to _def when the key is absent. The default
     *         _def is a temporary that dies with the call, so a caller relying on it has to copy
     *         the result before the call expression ends
     */
    template <typename K, typename V>
    inline const V& map_value(const std::map<K, V>& _map, const K _key, const V& _def = V()) {
        typename std::map<K, V>::const_iterator iter = _map.find(_key);
        if (iter == _map.cend()) return _def;
        else return iter->second;
    }

    /// The unordered_map overload: the same lookup and the same _def lifetime
    template <typename K, typename V>
    inline const V& map_value(const std::unordered_map<K, V>& _map, const K _key, const V& _def = V()) {
        typename std::unordered_map<K, V>::const_iterator iter = _map.find(_key);
        if (iter == _map.cend()) return _def;
        else return iter->second;
    }

    /// The unsigned integer that has the same size as std::thread::id: uint_64 or uint_32
    typedef size_int<sizeof(std::thread::id), false> tid_t;
    /**
     * \brief The calling thread's id, as the bits of std::thread::id read as an integer
     *
     * Two threads alive at the same time answer different values; a value may be handed out again
     * once its thread has exited, and means nothing outside the process that produced it.
     */
    inline tid_t this_tid() {
        std::thread::id tid = std::this_thread::get_id();
        return r_interpret<tid_t>(&tid);
    }

    /**
     * \brief FNV-1a hash of a NUL-terminated string, usable at compile time
     *
     * The terminator is not hashed, and _h is the running hash, so a caller may hand a previous
     * result back in to continue over the next string as if the two had been concatenated.
     */
    constexpr uint_32 fnv1a_32(const char* _s, uint_32 _h = 2166136261u) {
        return *_s ? fnv1a_32(_s + 1, (_h ^ static_cast<uint_32>(*_s)) * 16777619u) : _h;
    }

    /// The 64-bit FNV-1a: the same contract over a wider hash
    constexpr uint_64 fnv1a_64(const char* _s, uint_64 _h = 14695981039346656037ULL) {
        return *_s ? fnv1a_64(_s + 1, (_h ^ static_cast<uint_64>(*_s)) * 1099511628211ULL) : _h;
    }

    /**
     * \brief Base class that deletes copy construction and assignment and keeps move
     *
     * It holds no data and no virtual function, and has no virtual destructor -- never delete a
     * derived object through a noncopyable*. A class deriving from it is non-copyable in turn,
     * because its own implicit copy operations have to call the deleted ones.
     */
    class noncopyable {
    public:
        /// Constructible; a derived class constructs it implicitly
        noncopyable() = default;
        /// Non-virtual and empty: nothing to destroy
        ~noncopyable() = default;
        /// Deleted, so a derived class cannot be copied either
        noncopyable(const noncopyable&) = delete;
        /// Deleted; see the copy constructor
        noncopyable& operator=(const noncopyable&) = delete;
        /// Defaulted: the base moves, and so may a derived class
        noncopyable(noncopyable&&) = default;
        /// Defaulted; see the move constructor
        noncopyable& operator=(noncopyable&&) = default;
    };
    /// True when T derives from noncopyable
    template <typename T>
    constexpr bool is_noncopyable = std::is_base_of_v<noncopyable, T>;

    /**
     * \brief Signal/slot: the connected callables run on every emission, in connection order
     *
     * connect() appends one slot, clear() drops them all, exec() -- operator() is the same call --
     * runs them on the calling thread. A signal may be emitted from several threads at once: a lock
     * guards the list, taken for writing by connect() and clear() and for reading by an emission,
     * which holds it while the slots run. A slot must not connect, clear or re-emit its own signal.
     */
    template <typename... Args>
    class signal : public noncopyable {
    protected:
#if CPP_VERSION >= CPP_17_ID || defined(_MSC_VER)
        /// Mutex that guards the slot list: shared where the standard has it, plain below C++17
        using SHARED_MUTEX = std::shared_mutex;
        /// Read-side lock; below C++17 it is a lock_guard, so every call is exclusive there
        using SHARED_LOCK = std::shared_lock<std::shared_mutex>;
        /// Write-side lock
        using UNIQUE_LOCK = std::unique_lock<std::shared_mutex>;
#else
        using SHARED_MUTEX = std::mutex;
        using SHARED_LOCK = std::lock_guard<std::mutex>;
        using UNIQUE_LOCK = std::lock_guard<std::mutex>;
#endif
    public:
        /// Callable connected to the signal: it takes the arguments of the emission
        using slot = std::function<void(Args...)>;
        /// Append a slot: it runs on every emission until clear(), duplicates included
        void connect(const slot& _fun) {
            UNIQUE_LOCK w_lock(m_mutex);
            (void) w_lock;
            m_slots.push_back(_fun);
        }
        /**
         * \brief Forward every emission of this signal to another one
         *
         * The other signal is referenced, not copied, so it must outlive this one.
         *
         * \param _sig Signal to emit whenever this one is emitted
         */
        void connect(const signal& _sig) {
            UNIQUE_LOCK w_lock(m_mutex);
            (void) w_lock;
            m_slots.push_back([&_sig](Args... _args) { _sig.exec(_args...); });
        }
        /// Drop every connected slot
        void clear() {
            UNIQUE_LOCK w_lock(m_mutex);
            (void) w_lock;
            m_slots.clear();
            m_slots.shrink_to_fit();
        }
        /// Run every connected slot, in connection order, on the calling thread
        void exec(Args... _args) const {
            SHARED_LOCK r_lock(m_mutex);
            (void) r_lock;
            for (const slot& iter : m_slots) iter(_args...);
        }
        /// exec(), as a call
        void operator()(Args... _args) const {
            SHARED_LOCK r_lock(m_mutex);
            (void) r_lock;
            for (const slot& iter : m_slots) iter(_args...);
        }

    private:
        mutable SHARED_MUTEX m_mutex;
        std::vector<slot> m_slots;
    };

    /**
     * \brief Thread-safe lazy singleton: the one T, built on first use
     *
     * instance() answers the same pointer every time, the T is made on the heap and never
     * destroyed, so a static object can still reach it while its own destructor runs. Usually used
     * through CRTP: "class x : public single<x>" makes instance() answer an x*.
     */
    template <typename T>
    class single : public noncopyable {
    public:
        /// The one instance, default-constructed the first time it is asked for
        static T* instance() {
            static T* obj = new T();
            return obj;
        }
    };

    /**
     * \brief RAII holder for a pointer from new: the destructor deletes what is still held
     *
     * Deletion is a plain delete, never delete[], so the pointer must come from new -- and T needs
     * a virtual destructor if it is used as a base class. The holder is non-copyable; take() is how
     * the pointer is handed on to somebody else.
     */
    template <typename T>
    class auto_delete : public noncopyable {
    public:
        /// Take ownership of _ptr, which may be null
        inline auto_delete(T* _ptr) : m_ptr(_ptr) {}
        /// Delete what is still held; a no-op once take() or drop() has let it go
        inline ~auto_delete() { delete m_ptr; }
        /**
         * \brief Hand the pointer to the caller and stop owning it
         *
         * \return The pointer that was held, null included; the destructor will not delete it
         */
        inline T* take() {
            T* ptr = m_ptr;
            m_ptr = nullptr;
            return ptr;
        }
        /// The pointer still held; null once take() or drop() has run
        inline T* get() { return m_ptr; }
        /// Abandon the pointer: neither deleted nor handed out, so the holder stops owning it
        inline void drop() { m_ptr = nullptr; }

    private:
        T* m_ptr;
    };
}

#endif
