/*****************************************************************/ /**
 * \file   athread_safe.h
 * \brief  Thread-safe containers and utilities
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_THREADSAFE_H_
#define _ALEXIS_THREADSAFE_H_

#include "autility.h"

#include <atomic>

namespace alx {
    /**
     * \brief Scoped exclusive lock over a std::mutex
     *
     * Locks in the constructor and unlocks in the destructor, so the lock covers exactly the
     * enclosing block. The mutex arrives as a const reference whose constness is cast away, so a
     * const member function can lock the object it guards. The mutex is not owned: it must
     * outlive the lock object, and it is never reentrant.
     */
    class thread_safe_lock {
    public:
        /// Acquire _mutex, blocking until it is free
        thread_safe_lock(const std::mutex& _mutex) : mutx(const_cast<std::mutex&>(_mutex)) { mutx.lock(); }
        /// Release it
        ~thread_safe_lock() { mutx.unlock(); }

    private:
        std::mutex& mutx;
    };
#if CPP_VERSION >= CPP_17_ID || defined(_MSC_VER)
    /**
     * \brief Scoped writer lock over a std::shared_mutex
     *
     * The exclusive side of the pair: blocks while any reader or any other writer holds the
     * mutex. Locks in the constructor, unlocks in the destructor, owns nothing.
     */
    class thread_safe_wlock {
    public:
        /// Acquire _mutex exclusively, blocking until it is free
        thread_safe_wlock(const std::shared_mutex& _mutex) : mutx(const_cast<std::shared_mutex&>(_mutex)) { mutx.lock(); }
        /// Release it
        ~thread_safe_wlock() { mutx.unlock(); }

    private:
        std::shared_mutex& mutx;
    };
    /**
     * \brief Scoped reader lock over a std::shared_mutex
     *
     * The shared side of the pair: any number of readers hold it at once, and only a writer
     * blocks them. Locks in the constructor, unlocks in the destructor, owns nothing.
     */
    class thread_safe_rlock {
    public:
        /// Acquire _mutex shared, blocking only while a writer holds it
        thread_safe_rlock(const std::shared_mutex& _mutex) : mutx(const_cast<std::shared_mutex&>(_mutex)) { mutx.lock_shared(); }
        /// Release it
        ~thread_safe_rlock() { mutx.unlock_shared(); }

    private:
        std::shared_mutex& mutx;
    };
#else
    /// Before C++17 there is no shared mutex, so a writer lock is a plain exclusive lock
    using thread_safe_wlock = thread_safe_lock;
    /// Before C++17 a reader lock is exclusive too: readers do not run together
    using thread_safe_rlock = thread_safe_lock;
#endif

    /**
     * \brief A container carrying its own mutex
     *
     * T comes in as a protected base and std::mutex as a public one, so one object is both the
     * guarded container and the lock that guards it: data() hands out the container, and the
     * object itself is what THREAD_SAFE() or thread_safe_lock() takes. T's constructors are
     * inherited.
     */
    template <typename T>
    class thread_safe_mutex
        : protected T,
          public std::mutex {
    public:
        using T::T;
        /// The guarded container; touch it only while holding the lock
        inline T& data() { return *this; }
        /// Read-only view of the guarded container, likewise under the lock
        inline const T& data() const { return *this; }
    };
    /**
     * \brief A container carrying its own reader/writer lock
     *
     * As thread_safe_mutex, on a std::shared_mutex rather than a std::mutex: R_THREAD_SAFE()
     * lets readers in together while W_THREAD_SAFE() keeps a writer alone. Before C++17 the base
     * is a plain std::mutex and both macros lock exclusively.
     */
    template <typename T>
    class thread_safe_readwrite
        : protected T
#if CPP_VERSION >= CPP_17_ID || defined(_MSC_VER)
        ,
          public std::shared_mutex
#else
        ,
          public std::mutex
#endif
    {
    public:
        using T::T;
        /// The guarded container; touch it only while holding the lock
        inline T& data() { return *this; }
        /// Read-only view of the guarded container, likewise under the lock
        inline const T& data() const { return *this; }
    };
    /// Specialization with nothing to guard: the bare mutex, for a container the caller cannot
    /// derive from -- hold it beside the container and take the same macros
    template <> class thread_safe_mutex<void> : public std::mutex {};
#if CPP_VERSION >= CPP_17_ID || defined(_MSC_VER)
    /// Specialization with nothing to guard: the bare reader/writer lock, for a container the
    /// caller cannot derive from
    template <> class thread_safe_readwrite<void> : public std::shared_mutex {};
#else
    /// Specialization with nothing to guard: the bare lock, for a container the caller cannot
    /// derive from; before C++17 it is a plain std::mutex
    template <> class thread_safe_readwrite<void> : public std::mutex {};
#endif

/**
 * \brief Read-lock _obj for the rest of the enclosing block
 *
 * _obj must be a std::shared_mutex -- a thread_safe_readwrite is one. Declares a local lock
 * object of a fixed name, so the macro fits one scope only, and it needs the trailing semicolon
 * to end the declaration.
 */
#define R_THREAD_SAFE(OBJ)             \
    thread_safe_rlock __r_locker(OBJ); \
    (void) __r_locker;
/**
 * \brief Write-lock _obj for the rest of the enclosing block
 *
 * _obj must be a std::shared_mutex -- a thread_safe_readwrite is one. Declares a local lock
 * object of a fixed name, so the macro fits one scope only, and it needs the trailing semicolon
 * to end the declaration.
 */
#define W_THREAD_SAFE(OBJ)             \
    thread_safe_wlock __w_locker(OBJ); \
    (void) __w_locker;
/**
 * \brief Lock _obj exclusively for the rest of the enclosing block
 *
 * _obj must be a std::mutex -- a thread_safe_mutex is one. Declares a local lock object of a
 * fixed name, so the macro fits one scope only, and it needs the trailing semicolon to end the
 * declaration.
 */
#define THREAD_SAFE(OBJ)            \
    thread_safe_lock __locker(OBJ); \
    (void) __locker;

    /**
     * \brief FIFO queue of ELEM objects that several threads may share
     *
     * Every member locks the queue's own mutex, so any thread may call any of them, and elements
     * are copied in and out. destroy() shuts the queue down for good, reset() re-arms it and
     * clear() only empties it -- destroy() carries the exact state machine.
     */
    template <typename ELEM>
    class safe_queue
        : public thread_safe_mutex<std::queue<ELEM>> {
    public:
        /// The guarded std::queue, with the mutex mixed in
        using base = thread_safe_mutex<std::queue<ELEM>>;
        /// Destroy: elements still queued are dropped and blocked callers are woken
        ~safe_queue() { destroy(); }
        /// Build an empty queue, ready to use
        safe_queue() = default;
        /// Deleted: the queue holds a mutex and condition variables, and is shared by reference
        safe_queue(const safe_queue&) = delete;
        /// Deleted, on the same grounds as the copy constructor
        safe_queue& operator=(const safe_queue&) = delete;
        /// Deleted: callers may be blocked on this very object, moving it would strand them
        safe_queue(safe_queue&&) = delete;
        /// Deleted, on the same grounds as the move constructor
        safe_queue& operator=(safe_queue&&) = delete;

    public:
        /**
         * \brief Drop every element, wake every blocked caller and shut the queue down
         *
         * The queue drains under the lock and then both condition variables are notified, so
         * callers blocked in block_push()/block_pop() come back instead of sleeping on. From
         * then on push() discards the element without a word, and pop() and both block_*()
         * return false at once; reset() is the only way back.
         */
        inline void destroy() {
            {

                THREAD_SAFE(*this);
                b_destroy = true;
                while (!base::empty()) base::pop();
            }
            cond_in.notify_all();
            cond_out.notify_all();
        }
        /**
         * \brief Empty the queue and make it usable again
         *
         * Undoes destroy(): the elements go and the destroyed state is lifted, so push() and the
         * blocking calls work from then on. Nobody is woken -- call it on a queue no thread is
         * blocked on, or a waiter in block_push() may sleep until a pop that never comes.
         */
        inline void reset() {
            THREAD_SAFE(*this);
            while (!base::empty()) base::pop();
            b_destroy = false;
        }
        /// Drop every element, leaving the destroyed state exactly as it was
        inline void clear() {
            THREAD_SAFE(*this);
            while (!base::empty()) base::pop();
        }
        /// Elements queued right now, read under the lock
        inline size_t size() const {
            THREAD_SAFE(*this);
            return base::size();
        }
        /// True when nothing is queued, read under the lock
        inline bool empty() const {
            THREAD_SAFE(*this);
            return base::empty();
        }
        /// True from destroy() until the next reset(); read without taking the lock
        inline bool is_destroy() const { return b_destroy.load(); }

    public:
        /**
         * \brief Append an element
         *
         * Never blocks and never refuses: the queue has no limit of its own, only block_push()
         * honours one. On a destroyed queue the element is dropped in silence.
         */
        inline void push(const ELEM& _data) {
            if (b_destroy.load()) return;
            THREAD_SAFE(*this);
            push_impl(_data);
        }
        /**
         * \brief Take the oldest element out
         *
         * \param _data Receives the element; left untouched when the call returns false
         * \return false when the queue is empty, or when it has been destroyed
         */
        inline bool pop(ELEM& _data) {
            if (b_destroy.load()) return false;
            THREAD_SAFE(*this);
            return !base::empty() ? pop_impl(_data), true : false;
        }
        /**
         * \brief Append an element, waiting while the queue is at its limit
         *
         * Blocks until the queue holds fewer than _limit elements or is destroyed; a pop()
         * elsewhere is what lets it through, and the wait has no deadline.
         *
         * \param _limit Depth to stay below
         * \return false once the queue is destroyed, whether before the call or while it waited;
         *         the element is not queued then
         */
        inline bool block_push(const ELEM& _data, size_t _limit) {
            std::unique_lock<std::mutex> lock(*this);
            return cond_out.wait(lock,
                                 [&]() { return base::size() < _limit || b_destroy.load(); }),
                   !b_destroy.load() ? push_impl(_data), true : false;
        }
        /**
         * \brief Append an element, giving up after a timeout
         *
         * As above, with the wait bounded.
         *
         * \param _wait_ms Milliseconds to wait at most
         * \return false when the queue is destroyed or the wait ran out first; the element is
         *         not queued then
         */
        inline bool block_push(const ELEM& _data, size_t _limit, uint_64 _wait_ms) {
            std::unique_lock<std::mutex> lock(*this);
            return cond_out.wait_for(lock, std::chrono::milliseconds(_wait_ms),
                                     [&]() { return base::size() < _limit || b_destroy.load(); }) &&
                       !b_destroy.load()
                   ? push_impl(_data),
                   true : false;
        }
        /**
         * \brief Take the oldest element out, waiting for one to arrive
         *
         * Blocks until an element is queued or the queue is destroyed; a push() elsewhere is
         * what lets it through, and the wait has no deadline.
         *
         * \param _data Receives the element; left untouched when the call returns false
         * \return false once the queue is destroyed, whether before the call or while it waited
         */
        inline bool block_pop(ELEM& _data) {
            std::unique_lock<std::mutex> lock(*this);
            return cond_in.wait(lock,
                                [&]() { return !base::empty() || b_destroy.load(); }),
                   !b_destroy.load() ? pop_impl(_data), true : false;
        }
        /**
         * \brief Take the oldest element out, giving up after a timeout
         *
         * As above, with the wait bounded: finding nothing to pop ends the wait.
         *
         * \param _data Receives the element; left untouched when the call returns false
         * \param _wait_ms Milliseconds to wait at most
         * \return false when the queue is destroyed or the wait ran out first
         */
        inline bool block_pop(ELEM& _data, uint_64 _wait_ms) {
            std::unique_lock<std::mutex> lock(*this);
            return cond_in.wait_for(lock, std::chrono::milliseconds(_wait_ms),
                                    [&]() { return !base::empty() || b_destroy.load(); }) &&
                       !b_destroy.load()
                   ? pop_impl(_data),
                   true : false;
        }

    private:
        inline void pop_impl(ELEM& _data) {
            _data = base::front();
            base::pop();
            cond_out.notify_one();
        }
        inline void push_impl(const ELEM& _data) {
            base::push(_data);
            cond_in.notify_one();
        }

    private:
        std::atomic<bool> b_destroy{false};
        std::condition_variable cond_in;
        std::condition_variable cond_out;
    };

    /**
     * \brief Thread-safe map from a key to an owned heap object of T
     *
     * A std::unordered_map<s_key, T*> behind a reader/writer lock: contain(), keys() and call()
     * take it shared and may run concurrently, insert() and remove() take it exclusively. The
     * base map's constructors are inherited.
     *
     * The map owns every value stored in it: insert() deletes the value it replaces, remove()
     * deletes the one it drops, and the destructor deletes all that are left. A value must come
     * from new and must not be deleted or shared anywhere else.
     */
    template <typename s_key, typename T>
    class safe_map
        : public thread_safe_readwrite<std::unordered_map<s_key, T*>> {
    public:
        /// The guarded std::unordered_map, with the reader/writer lock mixed in
        using base = thread_safe_readwrite<std::unordered_map<s_key, T*>>;
        using base::base;
        /// Delete every value still stored
        ~safe_map() {
            for (auto& i : base::data()) delete i.second;
        }

    public:
        /**
         * \brief Store a value under a key, taking ownership of it
         *
         * The value already stored under that key, if any, is deleted first.
         *
         * \param _entity Value to store; deleted by the map from then on
         */
        inline void insert(const s_key& _key, T* _entity) {
            W_THREAD_SAFE(*this);
            auto iter = base::data().find(_key);
            if (iter == base::data().end()) base::data().insert({_key, _entity});
            else {
                delete iter->second;
                iter->second = _entity;
            }
        }
        /// Drop one key and delete its value; no-op when the key is absent
        inline void remove(const s_key& _key) {
            W_THREAD_SAFE(*this);
            auto iter = base::data().find(_key);
            if (iter == base::data().end()) return;
            else {
                delete iter->second;
                base::data().erase(iter);
            }
        }
        /// True when a value is stored under that key
        inline bool contain(const s_key& _key) const {
            R_THREAD_SAFE(*this);
            return base::data().find(_key) != base::data().end();
        }
        /**
         * \brief Every key stored, in unspecified order
         *
         * They come back as std::string whatever s_key is, so s_key must convert to one.
         */
        inline std::list<std::string> keys() const {
            R_THREAD_SAFE(*this);
            std::list<std::string> result;
            for (const auto& it : base::data()) result.push_back(it.first);
            return result;
        }
        /**
         * \brief Call a member function of the value stored under a key
         *
         * The map's read lock is held for the whole call, so _fun must not call back into the
         * map. A miss returns Return(), which _success is there to tell apart from a hit -- so
         * Return must be default-constructible.
         *
         * \param _success Set to true when the key was found, false when it was not
         * \param _fun Member function of T to call on the stored value
         * \param _args Arguments passed on to _fun
         * \return What _fun returned, or Return() when the key is absent
         */
        template <typename Return, typename... Args>
        inline Return call(const s_key& _key, bool& _success, Return (T::*_fun)(Args...), Args... _args) {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (_success = true, (it->second->*_fun)(_args...)) : (_success = false, Return());
        }
        /// As the overload above, on a const map and calling a const member function
        template <typename Return, typename... Args>
        inline Return call(const s_key& _key, bool& _success, Return (T::*_fun)(Args...) const, Args... _args) const {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (_success = true, (it->second->*_fun)(_args...)) : (_success = false, Return());
        }
        /**
         * \brief Call a member function without reporting whether the key was found
         *
         * \return What _fun returned, or Return() when the key is absent -- a miss and a call
         *         that happened to return Return() look the same to the caller
         */
        template <typename Return, typename... Args>
        inline Return call(const s_key& _key, Return (T::*_fun)(Args...), Args... _args) {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (it->second->*_fun)(_args...) : Return();
        }
        /// As the overload above, on a const map and calling a const member function
        template <typename Return, typename... Args>
        inline Return call(const s_key& _key, Return (T::*_fun)(Args...) const, Args... _args) const {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (it->second->*_fun)(_args...) : Return();
        }
        /// As above, for a member function that takes no argument
        template <typename Return>
        inline Return call(const s_key& _key, Return (T::*_fun)()) {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (it->second->*_fun)() : Return();
        }
        /// As above, on a const map, no argument either
        template <typename Return>
        inline Return call(const s_key& _key, Return (T::*_fun)() const) const {
            R_THREAD_SAFE(*this);
            auto it = base::data().find(_key);
            return it != base::data().end() ? (it->second->*_fun)() : Return();
        }
    };
}

#endif
