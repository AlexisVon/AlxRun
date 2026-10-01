/*****************************************************************/ /**
 * \file   athreadpool.h
 * \brief  Thread pool implementation
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_THREADPOOL_H_
#define _ALEXIS_THREADPOOL_H_

#include <autility.h>

namespace alx {
    /**
     * \brief Pool of worker threads that run queued callables
     *
     * Tasks handed to enqueue() wait in a queue and are dispatched in arrival order, but several
     * workers run them at once, so they may finish out of order; what a task throws surfaces in
     * the future enqueue() returned. The workers start in the constructor and stop in the
     * destructor, which lets every task still queued at that moment run to completion -- a task
     * that reached the queue is never dropped.
     *
     * enqueue(), wait_until_empty() and wait_until_nothing_in_flight() may be called from any
     * thread; each blocks as described below. The pool must not be called once its destructor has
     * started.
     */
    class ALXCORE_API threadpool : public noncopyable {
    public:
        /**
         * \brief Build the pool and start its workers
         *
         * \param _threads Worker count; 0 is taken as 1, so a pool always has at least one
         * \param _queue_limit Queue depth at which enqueue() starts to block; 0 is taken as 1
         */
        explicit threadpool(uint_64 _threads = (std::max) (0X02U, std::thread::hardware_concurrency()), uint_64 _queue_limit = 0X7FFFU);
        /**
         * \brief Stop the pool after the queued tasks have run
         *
         * Every task that is queued or running at this point still runs to completion, and the
         * destructor blocks until the last one has returned; a producer waiting in enqueue() is
         * released and throws.
         */
        ~threadpool();

    public:

        /**
         * \brief Hand a task to the pool
         *
         * Blocks the calling thread while the queue already holds _queue_limit tasks; a worker
         * taking one off releases it. The callable and its arguments are copied or moved into the
         * task, so the caller may release them as soon as the call returns.
         *
         * \tparam F Callable type; invoked as _f(_args...) on a worker thread
         * \tparam Args Argument types; each is decayed and stored in the task
         * \param _f Callable to run
         * \param _args Arguments for _f
         * \return Future carrying _f's result, or the exception _f throws
         * \throws std::runtime_error when the pool is stopped -- including a pool stopped while
         *         the call was blocked on a full queue; the task is not run
         */
        template <class F, class... Args>
#if CPP_VERSION >= CPP_17_ID
        std::future<typename std::invoke_result<F&&, Args&&...>::type>
#else
        std::future<typename std::result_of<F && (Args && ...)>::type>
#endif
        enqueue(F&& _f, Args&&... _args) {
#if CPP_VERSION >= CPP_17_ID
            using return_type = typename std::invoke_result<F&&, Args&&...>::type;
#else
            using return_type = typename std::result_of<F && (Args && ...)>::type;
#endif
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(_f), std::forward<Args>(_args)...));
            std::future<return_type> res = task->get_future();
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (tasks.size() >= max_queue_size) condition_producers.wait(lock, [this] { return tasks.size() < max_queue_size || stop; });
            if (stop) throw std::runtime_error("enqueue on stopped threadpool");
            tasks.emplace([task]() { (*task)(); });
            in_flight.fetch_add(uint_64(1), std::memory_order_relaxed);
            condition_consumers.notify_one();
            return res;
        }

    public:

        /**
         * \brief Block until nothing is queued
         *
         * A task a worker has already taken may still be running when this returns --
         * wait_until_nothing_in_flight() covers those too. Returns at once when the queue is
         * empty.
         */
        void wait_until_empty();

        /**
         * \brief Block until every enqueued task has finished
         *
         * Queued and running tasks count alike, so this waits for the queue to drain and for the
         * last task to return. A task enqueued after the call is not covered. Returns at once
         * when nothing is in flight.
         */
        void wait_until_nothing_in_flight();

        /**
         * \brief Change how many worker threads the pool runs
         *
         * Growing starts the new workers at once. Shrinking lets the surplus workers exit once
         * the task each one is running has returned; nothing queued is dropped, and 0 is taken as
         * 1, so the pool always keeps at least one worker. No-op once the pool is stopped.
         *
         * \param _limit New worker count
         */
        void set_pool_size(uint_64 _limit);

        /**
         * \brief Change the queue depth at which enqueue() starts to block
         *
         * Raising the limit releases the producers waiting in enqueue(). Lowering it drops
         * nothing that is already queued; the next enqueue() simply waits until the queue has
         * drained below the new limit. 0 is taken as 1. No-op once the pool is stopped.
         *
         * \param _limit New queue depth, in tasks
         */
        void set_queue_size_limit(uint_64 _limit);

    private:

        void start_worker(uint_64 _worker_number, std::unique_lock<std::mutex> const& _lock);

        static void worker_func(threadpool* _this, const uint_64 _worker_number);

    private:
        std::vector<std::thread> workers;
        uint_64 pool_size;
        std::queue<std::function<void()>> tasks;
        uint_64 max_queue_size = 0X7FFFU;
        bool stop = false;

        std::mutex queue_mutex;
        std::condition_variable condition_producers;
        std::condition_variable condition_consumers;

        std::mutex in_flight_mutex;
        std::condition_variable in_flight_condition;
        std::atomic<uint_64> in_flight;

        struct handle_in_flight_decrement {
            /// Pool whose in-flight counter this guard decrements
            threadpool& tp;
            /// Arm the guard; the destructor does the decrement
            handle_in_flight_decrement(threadpool& _tp) : tp(_tp) {}
            /// Decrement the pool's in-flight count and release waiters if it reaches zero
            ~handle_in_flight_decrement() {
                if (tp.in_flight.fetch_sub(uint_64(1), std::memory_order_acq_rel) == 1) {
                    std::unique_lock<std::mutex> guard(tp.in_flight_mutex);
                    tp.in_flight_condition.notify_all();
                }
            }
        };
    };
}

#endif