/*****************************************************************/ /**
 * \file   arpc.h
 * \brief  RPC remote procedure call framework
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_RPC_H_
#define _ALEXIS_RPC_H_

#include "abase.h"
#include "acomm_ex.h"
#include "autility.h"
#include <codecvt>
#include <functional>

namespace alx {
    /// Forward declaration of the pool rpc owns and runs its service handlers on
    class threadpool;
    /**
     * \brief RPC endpoint: serves the services installed on it and calls the peer's
     *
     * Both ends of a connection are rpc objects. install_service() publishes a handler the peer
     * reaches by id; consume_service() sends a request and names the callback that takes the
     * reply. The transport is owned and deleted with the object, and open() must have succeeded
     * before anything flows.
     *
     * The receive thread only dispatches -- handlers and reply callbacks run on a pool of their
     * own threads, so they may block without stalling the connection.
     */
    class ALXCOMM_API rpc
        : public comm_ex {
    public:
        /**
         * \brief Compression a service asks for on its own replies
         *
         * The serving side combines this with the caller's cmps_expect: a NEVER on either side is
         * absolute and the reply goes out plain, an ALWAYS on either side otherwise compresses
         * it. With neither absolute, CMPS_FST compresses and CMPS_NON_FST does not -- which
         * leaves CMPS_FST equivalent to ALWAYS, and CMPS_NON_FST yielding to a caller that asks
         * for compression.
         *
         * NEVER (the default) keeps every reply plain, whatever the caller expects.
         */
        enum class cmps_option : uint_8 { NEVER = 0X00U,
                                          /// Plain reply unless the caller asks to compress it
                                          CMPS_NON_FST = 0X0FU,
                                          /// Compressed reply unless the caller asks for plain data
                                          CMPS_FST = 0XF0U,
                                          /// The same effect as CMPS_FST, stated outright
                                          ALWAYS = 0XFFU };
        /**
         * \brief Compression the caller expects for the reply, sent with the request
         *
         * Combined on the serving side with the service's cmps_option as described there: an
         * absolute on either side wins. AUTO (the default) states no preference at all and leaves
         * the choice to the service.
         */
        enum class cmps_expect : uint_8 { AUTO = 0X00U,
                                          /// Ask for plain replies; not overridable by the service
                                          NEVER = 0X0FU,
                                          /// Ask for compressed replies unless the service refuses
                                          ALWAYS = 0XF0U };
        /**
         * \brief One installed service: its handler, its description and its reply policy
         *
         * Build it with create() or create_noerr(), which bind the request's parameter types and
         * reject a request carrying a different number of them.
         */
        struct service_pack {
            /// Handler shape: request parameters, reply payload, error text (empty on success)
            typedef std::function<void(const varvec& _in, variant& _out, std::string& _error)> service_func;
            /**
             * \brief Fill a pack by hand; prefer create()/create_noerr()
             *
             * \param _func Handler; an empty one leaves valid() false
             * \param _desc Description list_service() reports for this service
             * \param _cmps Compression the service asks for on its replies
             * \param _isinli true to run the handler on the receive thread instead of the pool
             */
            service_pack(service_func _func = nullptr, const std::string& _desc = std::string(),
                         const cmps_option _cmps = cmps_option::NEVER, bool _isinli = false)
                : __function(_func), __describe(_desc), __compress(_cmps), __inline_exec(_isinli) {
            }
            service_func __function;
            std::string __describe;
            cmps_option __compress;
            bool __inline_exec{false};

            /// True when a handler is bound; a pack without one answers "service not found"
            inline bool valid() const { return nullptr != __function; }

            /**
             * \brief Bind a handler whose last parameter is the error string it reports
             *
             * The callable runs as _func(args..., _error): what it returns becomes the reply
             * payload, and _error goes back empty on success. A request carrying another number
             * of arguments is answered with "invalid params" without calling it, and each
             * argument is read with variant::to<>() -- one whose kind does not fit arrives
             * default-constructed rather than raising.
             *
             * \tparam Args Parameter types of the handler, in the order the request sends them
             * \param _desc Description list_service() reports for this service
             */
            template <typename... Args, typename FUNC>
            inline static service_pack create(FUNC _func, const std::string& _desc, const cmps_option _cmps = cmps_option::NEVER, bool _isinli = false) {
                auto wrap_func = [_func, _pcount = sizeof...(Args)](const varvec& _in, variant& _out, std::string& _error) -> void {
                    if (_in.size() != _pcount) {
                        _error = "invalid params";
                        _out = 0;
                        return;
                    }
#if defined(__clang__)
                    int_64 index = 0;
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunsequenced"
                    _out = _func((_in[index++].to<std::decay_t<Args>>())..., _error);
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
                    int_64 index = int_64(_pcount - 1);
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wsequence-point"
                    _out = _func((_in[index--].to<std::decay_t<Args>>())..., _error);
#    pragma GCC diagnostic pop
#else
                    int_64 index = int_64(_pcount - 1);
                    _out = _func((_in[index--].to<std::decay_t<Args>>())..., _error);
#endif
                    (void) index;
                };
                return {wrap_func, _desc, _cmps, _isinli};
            }
            /// As create(), for a handler that takes no error string
            template <typename... Args, typename FUNC>
            inline static service_pack create_noerr(FUNC _func, const std::string& _desc, const cmps_option _cmps = cmps_option::NEVER, bool _ininli = false) {
                auto wrap_func = [_func, _pcount = sizeof...(Args)](const varvec& _in, variant& _out, std::string& _error) -> void {
                    if (_in.size() != _pcount) {
                        _error = "invalid params";
                        _out = 0;
                        return;
                    }
#if defined(__clang__)
                    int_64 index = 0;
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunsequenced"
                    _out = _func((_in[index++].to<std::decay_t<Args>>())...);
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
                    int_64 index = int_64(_pcount - 1);
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wsequence-point"
                    _out = _func((_in[index--].to<std::decay_t<Args>>())...);
#    pragma GCC diagnostic pop
#else
                    int_64 index = int_64(_pcount - 1);
                    _out = _func((_in[index--].to<std::decay_t<Args>>())...);
#endif
                    (void) index;
                };
                return {wrap_func, _desc, _cmps, _ininli};
            }
        };
        /**
         * \brief One outgoing request: its callback, its arguments and the reply's compression
         *
         * Build it with create() and hand it to consume_service(); the callback runs once, when
         * the reply comes back.
         */
        struct consume_pack {
            /// Reply shape: payload and error text (empty on success)
            typedef std::function<void(const variant& _result, const std::string& _error)> consume_func;
            /**
             * \brief Fill a pack by hand; prefer create()
             *
             * \param _func Callback for the reply; an empty one leaves valid() false
             * \param _param Request arguments, in the order the service declares them
             * \param _svid Service id the request names
             * \param _call_cmps true to compress the request itself
             * \param _rst_cmps Compression the caller expects for the reply
             */
            consume_pack(consume_func _func = nullptr, const varvec& _param = varvec(),
                         uint_64 _svid = 0, bool _call_cmps = false, cmps_expect _rst_cmps = cmps_expect::AUTO)
                : __function(_func), __param(_param), __svid(_svid), __call_compress(_call_cmps), __rst_compress(_rst_cmps) {
            }
            consume_func __function;
            varvec __param;
            uint_64 __svid;
            bool __call_compress;
            cmps_expect __rst_compress;

            /// True when a callback is bound; without one the reply is dropped
            inline bool valid() const { return nullptr != __function; }

            /**
             * \brief Build a request around one call
             *
             * The arguments become the request payload in the order given; the service reads them
             * with the types it declared, and a count it disagrees with comes back as "invalid
             * params".
             *
             * \param _svid Service id to call; 0 = the built-in list_service()
             * \param _call_cmps true to compress the request itself, whatever the reply does
             * \param _rst_cmps Compression the caller expects for the reply
             */
            template <typename... Args>
            inline static consume_pack create(
                const uint_64 _svid, std::function<void(const variant& _result, const std::string& _error)> _func,
                const bool _call_cmps, const cmps_expect _rst_cmps, const Args&... _param) {
                return consume_pack{_func, varvec{
                                               _param...,
                                           },
                                    _svid, _call_cmps, _rst_cmps};
            }
        };

        /// Service id -> its pack, the table the receive thread reads
        typedef std::map<uint_64, service_pack> service_map;
        /// Call id -> the callback waiting for that reply
        typedef std::map<uint_64, consume_pack> consume_map;

    public:
        /// No default constructor: an endpoint is always bound to a transport
        rpc() = delete;
        /**
         * \brief Wrap a transport and start the pool that runs the handlers
         *
         * \param _trans Transport to own; deleted by the destructor. A null one leaves the object
         *               invalid -- open() fails
         * \param _pool_size Threads that run service handlers and reply callbacks; 0 means 1
         * \param _queue_limit Tasks the pool holds before the receive thread blocks on it
         */
        rpc(transmit* _trans, uint_64 _pool_size = 2, uint_64 _queue_limit = 0X00FF);
        /**
         * \brief Wrap a transport built from its type name
         *
         * \param _type Name transmit::create() answers to, e.g. "tcp_client"; an unknown one
         *              leaves the object invalid, as a null transport does
         * \param _addr_port Address the transport parses, "host:port" for the socket types
         * \param _ext_info Settings the transport reads; only the tls types use any
         */
        inline rpc(const std::string& _type, const std::string& _addr_port,
                   uint_64 _pool_size = 2, uint_64 _queue_limit = 0X00FF,
                   const varmap& _ext_info = varmap())
            : rpc(transmit::create(_type, _addr_port, _ext_info), _pool_size, _queue_limit) {}
        /// Stop the pool -- it runs what it already holds, then joins -- and let comm delete the
        /// transport
        ~rpc();

    public:
        /**
         * \brief Describe the services installed on this object
         *
         * This is what the peer gets when it calls service id 0; safe from any thread, including
         * from inside a handler.
         *
         * \return Service id as text -> the description given at install time; id 0 itself is
         *         left out
         */
        varmap list_service() const;
        /**
         * \brief Publish a handler for the peer to call
         *
         * \param _pack Handler and its description, copied into the table
         * \return Id to name in a request; it comes from a counter shared by the whole process,
         *         so it is never 0 (the built-in list handler) and never reused
         */
        uint_64 install_service(const service_pack& _pack);
        /// Uninstall a service; an unknown id is ignored, and a request already dispatched still
        /// gets its reply -- the handler was copied out before it ran
        void remove_service(const uint_64 _svid);

    public:
        /**
         * \brief Send a request; the reply reaches the pack's callback
         *
         * Returns once the request is queued, not when the reply arrives. The callback runs on a
         * pool thread, once: its entry is dropped before the call, so a duplicate reply is
         * ignored and an exception it throws is logged. A request handed over while the transport
         * is not open is dropped silently -- the callback never runs.
         */
        void consume_service(const consume_pack& _pack);

    protected:
        /**
         * \brief Take one received frame: dispatch a request or a reply
         *
         * Called on the receive thread. A frame is a varmap carrying the fields rpcid, state,
         * service, expcmps, error and data; an override must chain to this implementation, which
         * does the dispatch.
         *
         * \param _loc Connection the frame came from, and the one a reply goes back on
         */
        virtual void on_data_recv(const varmap& _data, uint_64 _loc);
        /**
         * \brief Run one service handler and send its reply
         *
         * Called on a pool thread, or on the receive thread for a pack installed inline. The pack
         * was copied out of the table before this call, so the handler may install, remove or call
         * services itself. A handler that throws comes back to the caller as an error reply --
         * "exception: ..." or "unknown exception" -- and never escapes.
         *
         * \param _cmps Compression the caller expects for the reply
         * \param _loc Connection to send the reply on
         */
        virtual void on_exec_service(uint_64 _rpcid, service_pack _svc, const variant _param, cmps_expect _cmps, uint64_t _loc);
        /// Hand one reply to its callback and drop the entry; called on a pool thread, and a
        /// reply whose id is no longer there is dropped
        virtual void on_recv_result(uint_64 _rpcid, const std::string _error, const variant _result);

    private:
        threadpool* m_threadpool{nullptr};
        thread_safe_readwrite<service_map> m_service_map;
        thread_safe_readwrite<consume_map> m_consume_map;
    };
}

#endif
