/*****************************************************************/ /**
 * \file   acomm_ex.h
 * \brief  xtended communication functionality (supports varmap data structure and compressed transmission)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_COMM_EX_H_
#define _ALEXIS_COMM_EX_H_

#include "acomm.h"
#include "avariant.h"

namespace alx {
    /**
     * \brief Connection base carrying whole varmap values instead of raw bytes
     *
     * Lifecycle, ownership and threading come from comm: the transport is owned, open() starts
     * the worker, mesg_prit reports what fails. data_send() frames a varmap into one
     * self-delimited pack -- LZ4 on request -- and on_bytes_recv() reassembles the incoming
     * stream, firing data_recv once per complete pack.
     */
    class ALXCOMM_API comm_ex
        : public comm {
    public:
        /**
         * \brief Wrap a transport and take ownership of it
         *
         * \param _trans Transport to own, deleted with the object; a null transport leaves the
         *               object invalid -- data_send() returns false
         */
        comm_ex(transmit* _trans)
            : comm(_trans) {}

    public:
        /**
         * \brief Frame a varmap into one self-delimited pack and hand it to the worker
         *
         * The pack carries its own size, and a CRC32C when the build and CPU can compute one --
         * the receiver finds frame boundaries on its own and rejects what fails that checksum.
         *
         * \param _data Value to send; an empty varmap sends nothing and still returns true
         * \param _loc Connection to send on; 0 = every connection of the transport
         * \param _compress true to LZ4 compress the payload
         * \return false when the transport is invalid, when the varmap cannot be serialized, or
         *         when bytes_send() fails -- not open, or closed while blocked on a full queue
         */
        bool data_send(const varmap& _data, uint_64 _loc, bool _compress);
        /// Same, on every connection of the transport (_loc 0)
        inline bool data_send(const varmap& _data, bool _compress) { return data_send(_data, 0, _compress); }

    protected:
        /**
         * \brief Reassemble the incoming byte stream and fire data_recv once per complete pack
         *
         * Called by the worker thread. A pack split over several receives is buffered until it is
         * complete, so a call need not produce a signal; bytes that never form a valid pack are
         * dropped, with a message on mesg_prit. An override must chain to this implementation, or
         * data_recv never fires.
         *
         * \param _data Received bytes, valid only for the duration of the call
         * \param _loc Connection the bytes came from, passed on to data_recv
         */
        virtual void on_bytes_recv(const bytes_view& _data, uint_64 _loc) override;

    public:
        /// One varmap per pack received, as (data, loc) -- the connection it came from
        signal<const varmap&, uint_64> data_recv;
    };
}

#endif