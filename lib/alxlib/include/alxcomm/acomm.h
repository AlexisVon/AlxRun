/*****************************************************************/ /**
 * \file   acomm.h
 * \brief  Communication module encapsulation (advanced encapsulation based on transmit, with heartbeat and message queue)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_COMM_H_
#define _ALEXIS_COMM_H_

#include "athread_safe.h"
#include "atransmit.h"

namespace alx {
    /**
     * \brief Connection base: owns a transport, a worker thread and a send queue
     *
     * The constructor takes ownership of the transport and the destructor deletes it. After
     * open(), the worker thread keeps the transport connected -- reopening it when it drops --
     * and drains the send queue, so bytes_send() only hands the buffer over; a send the
     * transport refuses is reported through mesg_prit rather than through the return value.
     *
     * open() and close() are serialized by convention: the caller must not run them
     * concurrently with each other. is_valid(), is_open() and is_connect() may be read from any
     * thread at any time.
     */
    class ALXCOMM_API comm : public noncopyable {
    public:
        /**
         * \brief Wrap a transport and take ownership of it
         *
         * \param _trans Transport to own; deleted by the destructor. A null transport leaves the
         *               object invalid -- open() fails and bytes_send() returns false
         * \param _queue_limit Queue depth; a sender blocks while the queue is full
         */
        comm(transmit* _trans, const uint_64 _queue_limit = 0X80U);
        /// Close the connection, then delete the owned transport
        virtual ~comm();

    public:
        /**
         * \brief Start the worker thread and reset the send queue
         *
         * \return true when running, including the case of an already open connection; false
         *         only when the transport is invalid
         */
        bool open();
        /**
         * \brief Close the connection
         *
         * _loc 0 stops the worker, joins it, destroys the queue and releases callers blocked in
         * bytes_send(); any other _loc is queued for the worker to close that one connection.
         * No-op when not open.
         *
         * \param _loc Connection to close; 0 = the transport itself
         */
        void close(uint_64 _loc = 0);
        /// Drop every task still waiting in the send queue, without running it
        void clear();
        /**
         * \brief Hand a buffer to the worker for sending
         *
         * Blocks while the queue is full. The buffer is copied into the queued task, so _data
         * may be reused as soon as the call returns.
         *
         * \param _data Bytes to send
         * \param _loc Connection to send on; 0 = every connection of the transport
         * \return false when the connection is not open, or when it was closed while the call
         *         was blocked on a full queue
         */
        bool bytes_send(const bytes_view& _data, uint_64 _loc = 0);
        /// True when a transport is held; false only when the constructor was given null
        inline bool is_valid() const { return nullptr != m_trans; }
        /// True between open() and close(0)
        inline bool is_open() const { return m_opened.load(); }
        /// True when the transport reports at least one live connection
        inline bool is_connect() const { return is_valid() && m_trans->connect_num() != 0; }
        /// Transport messages as text: connection events, and send failures
        signal<const std::string&> mesg_prit;
        /// Connection flag changes, fired as (loc, connect)
        signal<uint_64, bool> comm_flag;

    protected:
        /**
         * \brief Consume bytes received on any connection
         *
         * Called by the worker thread. The buffer is only valid for the duration of the call.
         *
         * \param _data Received bytes
         * \param _loc Connection the bytes came from
         */
        virtual void on_bytes_recv(const bytes_view& _data, uint_64 _loc) = 0;

        /**
         * \brief Observe every connection event of the transport, open and close alike
         *
         * Called by the worker thread, after mesg_prit and comm_flag.
         *
         * \param _loc Connection that changed; 0 = the transport itself
         * \param _connect true on connect, false on disconnect
         */
        virtual void on_trans_event(uint_64 _loc, bool _connect) {}

    private:
        void worker_loop();
        void pump_queue();

    private:
        void close_comm_impl(uint_64 _loc);
        void bytes_send_impl(const bytes_view _data, uint_64 _loc);

    private:
        transmit* m_trans{nullptr};
        std::thread* m_thread{nullptr};
        std::atomic<bool> m_opened{false};
        uint_64 m_queue_limit{0X80U};
        safe_queue<std::function<void()>> m_queue;
    };
}

#endif