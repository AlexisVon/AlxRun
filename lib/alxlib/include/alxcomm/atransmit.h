/*****************************************************************/ /**
 * \file   atransmit.h
 * \brief  Data transmission layer (TCP client/server, UDP, local Unix domain socket, TLS)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_TRANSMIT_H_
#define _ALEXIS_TRANSMIT_H_

#include "abytes.h"
#include "autility.h"
#include "avariant.h"

#include <list>

namespace alx {

    /**
     * \brief Abstract transport -- TCP, UDP, local socket or TLS -- built by create() from a name
     *
     * create() answers to tcp_server, tcp_client, udp_trans, local_server, local_client and,
     * with TLS compiled in, tls_server and tls_client; the local pair maps "x.x.x.x:port" onto
     * the Unix socket /tmp/alx_<ip>_<port>.sock, the TLS pair takes cert, key, ca, alpn and
     * hostname from _ext_info. A callback names a peer by its address key, the packed uint_64
     * that to_strip64() builds; local_server names it by a connection id instead.
     *
     * The caller owns the instance and deletes it. open() / close() -- the destructor included --
     * are serialized by convention on one instance, never from two threads at once and never from
     * inside a handler; is_open() and connect_num() are plain reads any thread may do; bytes_send()
     * calls never overlap one another, nor an open() / close(). The bytes_recv / trans_mesg
     * handlers run on the library's own threads -- the open and close messages on the caller's --
     * and must not call open(), close(), send or delete from inside.
     */
    class ALXCOMM_API transmit : public noncopyable {
    public:

        /**
         * \brief Fix the signature create() calls -- neither argument is kept
         *
         * Instances come from create(); each concrete type parses _addr_port and reads its own
         * _ext_info keys in its own constructor.
         */
        transmit(const std::string& _addr_port, const varmap& _ext_info = varmap()) {};
        /// Delete through the base; every concrete type closes itself first
        virtual ~transmit() = default;

    public:

        /**
         * \brief Build a transport of the named type
         *
         * \param _type Registered type name, one of list_all()
         * \param _addr_port "x.x.x.x:port", accepted the way from_strip() accepts it
         * \param _ext_info TLS settings, ignored by the other types: cert, key, ca, alpn, hostname
         * \return A new instance for the caller to own and delete; nullptr when _type is not
         *         registered
         */
        static transmit* create(const std::string& _type, const std::string& _addr_port, const varmap& _ext_info = varmap());

        /**
         * \brief Type names create() answers to
         *
         * Only the transports linked into the process are listed, in no particular order.
         */
        static std::list<std::string> list_all();

        /**
         * \brief Render an address key back as "a.b.c.d:port"
         *
         * The inverse of from_strip64(), which is how the keys handed to the callbacks are built.
         */
        static std::string to_strip(uint_64 _addr_port);

        /**
         * \brief Parse "a.b.c.d:port" into its two halves
         *
         * The pair is what to_strip() renders back and what the constructors turn into a socket
         * address. Accepts 9 to 21 characters of digits, dots and one colon: four octets up to 255
         * and a port up to 65535.
         *
         * \return {0, 0} when the string does not parse -- the same value as the legitimate
         *         "0.0.0.0:0", so the two cannot be told apart
         */
        static std::pair<uint_32, uint_16> from_strip(const std::string& _addr_port);

        /**
         * \brief Parse "a.b.c.d:port" into the packed key the callbacks use
         *
         * \return (ip << 32) | port; 0 when the string does not parse, as for "0.0.0.0:0"
         */
        static uint_64 from_strip64(const std::string& _addr_port);

        /**
         * \brief Resolve a host and service into address strings
         *
         * IPv4 and SOCK_STREAM only, whatever the transport does with them afterwards.
         *
         * \param _node Host name or literal address
         * \param _service Service name or decimal port, as getaddrinfo() takes them
         * \return One "a.b.c.d:port" per resolved address; empty when resolution fails
         */
        static std::list<std::string> get_addr_port(const std::string& _node, const std::string& _service);

    public:

        /**
         * \brief Start the transport: bind and listen, or connect
         *
         * True when it is already open, without touching the socket. On success the receiver
         * threads are running and bytes_recv may fire; a failure reports its reason on trans_mesg
         * before returning.
         *
         * \return false when the socket could not be created, binding or connecting failed, the
         *         TLS handshake failed, or -- tls_client -- the peer did not match hostname
         */
        virtual bool open() = 0;

        /**
         * \brief Close the transport, or one peer of it
         *
         * _loc 0 tears the transport down and blocks until the receiver threads have exited;
         * another _loc closes that one peer, named by the key the callbacks handed out (the address
         * key on the TCP and TLS servers, a connection id on local_server). The single-connection
         * types ignore _loc and always close their one connection, and the whole call is a no-op
         * when the transport is not open.
         */
        virtual void close(uint_64 _loc = 0) = 0;

        /// True between open() and close(0); a client also turns it false when its connection drops
        virtual bool is_open() = 0;

        /// Peers connected right now; 0 or 1 on the clients, udp_trans and local_server
        virtual uint_64 connect_num() = 0;

        /**
         * \brief Send bytes to every peer, or to one of them
         *
         * Synchronous, on the calling thread: the call returns once the bytes are handed to the
         * socket or the send failed. A null _data, a zero _size, or a transport with no peer all
         * send nothing and still return true. The TLS server skips a peer whose handshake is not
         * done yet, and calls a _loc that matches no peer a success where the TCP server answers
         * false for the same call.
         *
         * \param _loc Peer to send to; 0 = every peer of the transport, but on udp_trans _loc is
         *             the destination address key and 0 is refused
         * \return false when the transport is not open, when udp_trans was given no destination,
         *         or when every peer that matched refused the bytes
         */
        virtual bool bytes_send(const void* _data, const uint_64 _size, uint_64 _loc = 0) = 0;

    public:

        /// Send the bytes a view holds; the view may be dropped again once the call returns
        inline bool bytes_send(const bytes_view& _data, uint_64 _loc = 0) { return bytes_send(_data.data(), _data.size(), _loc); }

        /**
         * \brief Send a whole std::string, terminator excluded
         *
         * \note A literal with a count, bytes_send("abc", 3), binds to the (const void*, uint_64)
         *       overload instead -- the number is a byte count there, not a _loc
         */
        inline bool bytes_send(const std::string& _data, uint_64 _loc = 0) { return bytes_send(_data.data(), _data.size(), _loc); }

    public:

        /**
         * \brief Bytes arrived, as (data, loc)
         *
         * Fired from the library's receiver thread, with a view into the receive buffer that the
         * next read reuses -- so it is valid only for the duration of the handler; copy it to keep
         * it. loc is the sender's address key: 0 on the client types, a connection id on
         * local_server.
         */
        signal<const bytes_view&, uint_64> bytes_recv;

        /**
         * \brief Every transport event, as (loc, connect, text)
         *
         * Fired on open and close of the transport itself (loc 0), on a peer connecting and
         * disconnecting, and on a failure, which arrives with connect false and the reason in text.
         * The open and close messages come from the thread that called open() / close(), the rest
         * from the library's own threads; text is only valid for the duration of the handler.
         */
        signal<const uint_64, bool, const char*> trans_mesg;
    };
}

#endif
