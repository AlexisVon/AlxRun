/*****************************************************************/ /**
 * \file   afiber.h
 * \brief  Fiber support, lightweight coroutines
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_FIBER_H_
#define _ALEXIS_FIBER_H_

#include "abase.h"
#include "autility.h"
#include "avariant.h"
#ifndef _WIN32
#    include <sys/ucontext.h>
#endif

namespace alx {
    /**
     * \brief User-space coroutine with its own stack
     *
     * The body runs at the first switch into the fiber, and only on the thread that created it:
     * the main fiber, the current fiber and the stack are all thread-local, so a fiber must never
     * be resumed from another thread. Switching is explicit -- there is no scheduler. An exception
     * out of the body is caught and logged; the fiber still ends up FIBER_FINISHED. create()
     * returns an object the caller owns.
     */
    class ALXCORE_API fiber {
    public:
        /**
         * \brief State of a fiber; FIBER_READY is a created fiber not yet switched into
         *
         * The main fiber is FIBER_RUNNING from the start, a created one starts out FIBER_READY.
         */
        enum state { FIBER_READY,
                     /// Its body is executing on this thread
                     FIBER_RUNNING,
                     /// Yielded out; it resumes where it yielded when switched into again
                     FIBER_SUSPENDED,
                     /// Its body returned or threw; final -- switching to it just returns to main
                     FIBER_FINISHED };

    private:
        fiber();
        fiber(std::function<void()> _func, uint_64 _commit_size, uint_64 _reserve_size);

    public:
        /**
         * \brief Release what the fiber owns: the stack on POSIX, the context on Windows
         *
         * A fiber that never ran is destroyed as it is -- no part of its body is executed -- and
         * one suspended mid-body is torn down without unwinding its stack, so the locals it still
         * holds are never destructed. Delete it from the thread that created it, and never while
         * it is the running fiber.
         */
        ~fiber();
        /// No copy: a stack and a context belong to one fiber only
        fiber(const fiber&) = delete;
        /// No move, same reason -- a fiber does not leave the thread that created it
        fiber(fiber&&) = delete;
        /// No copy assignment, same reason
        fiber& operator=(const fiber&) = delete;
        /// No move assignment, same reason
        fiber& operator=(fiber&&) = delete;
        /// Current state; a created fiber reports FIBER_READY until the first switch into it
        inline state get_state() const { return state_; }
        /**
         * \brief Attach a value to the fiber
         *
         * The value is copied into a variant slot on the fiber object; the body reads it back
         * with get_data(), through fiber::current() from inside. It is a per-fiber slot, not a
         * channel: writing it neither wakes nor switches to anything.
         *
         * \param _data Value to copy into the slot
         */
        template <typename T>
        inline void set_data(T&& _data) { data_ = std::forward<T>(_data); }
        /// The attached value: a reference into the fiber, valid until the next set_data() or the
        /// end of the fiber object. A slot never written to holds a null variant
        inline const variant& get_data() const { return data_; }

    public:
        /**
         * \brief Make the calling thread fiber-capable and give it a main fiber
         *
         * The main fiber wraps the context the thread already had: it is what a plain yield()
         * switches back to and what main() returns. No-op when this thread already has one.
         * create() calls it implicitly; call it explicitly to use main() / current() / is_fiber()
         * before creating any fiber.
         */
        static void initialize();
        /**
         * \brief Drop the calling thread's main fiber
         *
         * Both thread-local pointers are cleared, so main() and current() are nullptr afterwards
         * and the next create() initializes the thread again. Call it from the thread that
         * initialized.
         */
        static void deinitialize();
        /// Create with the platform default stack: 1 MB on POSIX, CreateFiberEx(0, 0) on Windows
        static fiber* create(std::function<void()> _func) { return create(_func, 0, 0); }
        /**
         * \brief Create a fiber and its stack; its body runs only at the first switch into it
         *
         * The returned fiber is owned by the caller -- delete it. The calling thread gets a main
         * fiber first if it has none, so there is always something to return to. POSIX ignores
         * _reserve_size: the stack is one mmap'ed block of _commit_size with a PROT_NONE guard
         * page below it, so an overflow faults instead of eating the heap.
         *
         * \param _func Body, run on the calling thread
         * \param _commit_size Stack bytes; 0 = 1 MB on POSIX, 0 = the default on Windows
         * \param _reserve_size Stack to reserve, Windows only; must be >= _commit_size
         * \return nullptr when _commit_size > _reserve_size, with the reason in the log; else the
         *         new fiber, in state FIBER_READY
         */
        static fiber* create(std::function<void()> _func, uint_64 _commit_size, uint_64 _reserve_size);
        /**
         * \brief Suspend the calling fiber and return to the calling thread's main fiber
         *
         * The fiber resumes at the point of the call when something switches into it again. A
         * no-op when the caller is the main fiber itself, or when its thread has no main fiber.
         */
        static void yield();
        /**
         * \brief Switch into _fiber, suspending the caller
         *
         * Works from the main fiber too: this is how a suspended fiber is resumed. A no-op when
         * _fiber is null or is the caller itself. A FIBER_FINISHED _fiber cannot run again, so
         * the switch goes to the main fiber instead.
         *
         * \param _fiber Fiber to resume; must have been created by create() on this thread
         */
        static void yield(fiber* _fiber);
        /// This thread's main fiber, the one initialize() made; nullptr before that call
        static fiber* main();
        /// The fiber running now on this thread: the fiber itself inside a body, otherwise the
        /// main fiber; nullptr before initialize()
        static fiber* current();

    public:
        /**
         * \brief Whether the calling thread is fiber-capable
         *
         * POSIX: true once initialize() has run on this thread. Windows: the thread is a fiber at
         * all, so a thread converted elsewhere reports true here as well.
         */
        static bool is_fiber();

    private:
        static void fiber_entry();

    private:
#ifdef _WIN32
        void* context_;
#else
        ucontext_t ctx_;
        void* stack_ptr_;
        uint_64 stack_size_;
#endif
        state state_;
        std::function<void()> func_;
        variant data_;
    };
}

#endif
