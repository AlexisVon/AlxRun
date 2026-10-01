/*****************************************************************/ /**
 * \file   ahttp.h
 * \brief  HTTP protocol client/server implementation
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_HTTP_H_
#define _ALEXIS_HTTP_H_

#include "acomm.h"
#include "adatetime.h"
#include "ajson.h"
#include "athreadpool.h"
#include "avariant.h"

namespace alx {
    namespace http {
        /// Protocol version of the start line; NONE is the unset sentinel
        enum class version : uint_8 { NONE = 0,
                                      /// HTTP/1.0
                                      V1_0,
                                      /// HTTP/1.1
                                      V1_1 };
#ifdef _MSC_VER
#    pragma push_macro("DELETE")
#    undef DELETE
#    pragma push_macro("TRACE")
#    undef TRACE
#    pragma push_macro("OPTIONS")
#    undef OPTIONS
#endif
        /// HTTP method; the enumerator name is the wire token of the start line
        enum class action : uint_8 { NONE = 0,
                                     /// GET
                                     GET,
                                     /// HEAD
                                     HEAD,
                                     /// POST
                                     POST,
                                     /// PUT
                                     PUT,
                                     /// DELETE
                                     DELETE,
                                     /// CONNECT
                                     CONNECT,
                                     /// OPTIONS
                                     OPTIONS,
                                     /// TRACE
                                     TRACE,
                                     /// PATCH
                                     PATCH };
#ifdef _MSC_VER
#    pragma pop_macro("OPTIONS")
#    pragma pop_macro("TRACE")
#    pragma pop_macro("DELETE")
#endif
        /// Status codes for a reply's status line; the enumerator value is the code sent
        namespace state {
            /// Not a status: what an unset reply carries, and what an aborted request delivers
            constexpr uint_16 NONE{0};
            /// 1xx: the request was received and the exchange continues
            enum info : uint_16 {
                /// 100 Continue
                CONTINUE = 100,
                /// 101 Switching Protocols
                SWITCHING_PROTOCOLS = 101,
                /// 102 Processing
                PROCESSING = 102,
                /// 103 Early Hints
                EARLY_HINT = 103
            };
            /// 2xx: the request was understood and accepted
            enum succ : uint_16 {
                /// 200 OK
                OK = 200,
                /// 201 Created
                CREATED = 201,
                /// 202 Accepted
                ACCEPTED = 202,
                /// 203 Non-Authoritative Information
                NO_AUTH_INFO = 203,
                /// 204 No Content
                NO_CONTENT = 204,
                /// 205 Reset Content
                RESET_CONTENT = 205,
                /// 206 Partial Content
                PARTIAL_CONTENT = 206,
                /// 207 Multi-Status
                MULTI_STATUS = 207,
                /// 208 Already Reported
                ALREADY_REPORTED = 208,
                /// 226 IM Used
                IM_USED = 226,
            };
            /// 3xx: the request needs to be taken further, elsewhere
            enum rurl : uint_16 {
                /// 301 Moved Permanently
                MOVED_PERMANENTLY = 301,
                /// 302 Found
                FOUND = 302,
                /// 303 See Other
                SEE_OTHER = 303,
                /// 304 Not Modified
                NOT_MODIFIED = 304,
                /// 305 Use Proxy
                USE_PROXY = 305,
                /// 307 Temporary Redirect
                TEMPORARY_REDIRECT = 307,
                /// 308 Permanent Redirect
                PERMANENT_REDIRECT = 308
            };
            /// 4xx: the request is at fault
            enum cerr : uint_16 {
                /// 400 Bad Request
                BAD_REQUEST = 400,
                /// 401 Unauthorized
                UNAUTHORIZED = 401,
                /// 403 Forbidden
                FORBIDDEN = 403,
                /// 404 Not Found
                NOT_FOUND = 404,
                /// 405 Method Not Allowed
                METHOD_NOT_ALLOWED = 405,
                /// 406 Not Acceptable
                NOT_ACCEPTABLE = 406,
                /// 407 Proxy Authentication Required
                PROXY_AUTH_REQUIRED = 407,
                /// 408 Request Timeout
                REQUEST_TIMEOUT = 408,
                /// 409 Conflict
                CONFLICT = 409,
                /// 410 Gone
                GONE = 410,
                /// 411 Length Required
                LENGTH_REQUIRED = 411,
                /// 412 Precondition Failed
                PRECONDITION_FAILED = 412,
                /// 413 Payload Too Large
                PAYLOAD_TOO_LARGE = 413,
                /// 414 URI Too Long
                URI_TOO_LONG = 414,
                /// 415 Unsupported Media Type
                UNSUPPORTED_MEDIA_TYPE = 415,
                /// 416 Range Not Satisfiable
                RANGE_NOT_SATISFIABLE = 416,
                /// 417 Expectation Failed
                EXPECTATION_FAILED = 417,
                /// 418 I'm a teapot
                IM_A_TEAPOT = 418,
                /// 421 Misdirected Request; the same code as MISDIRECTED_REQUEST
                METHOD_NOT_SUPPORTED = 421,
                /// 421 Misdirected Request
                MISDIRECTED_REQUEST = 421,
                /// 422 Unprocessable Entity
                UNPROCESSABLE_ENTITY = 422,
                /// 423 Locked
                LOCKED = 423,
                /// 424 Failed Dependency
                FAILED_DEPENDENCY = 424,
                /// 425 Too Early
                TOO_EARLY = 425,
                /// 426 Upgrade Required
                UPGRADE_REQUIRED = 426,
                /// 428 Precondition Required
                PRECONDITION_REQUIRED = 428,
                /// 429 Too Many Requests
                TOO_MANY_REQUESTS = 429,
                /// 431 Request Header Fields Too Large
                REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
                /// 451 Unavailable For Legal Reasons
                UNAVAILABLE_FOR_LEGAL_REASONS = 451
            };
            /// 5xx: the server is at fault
            enum serr : uint_16 {
                /// 500 Internal Server Error
                INTERNAL_SERVER_ERROR = 500,
                /// 501 Not Implemented
                NOT_IMPLEMENTED = 501,
                /// 502 Bad Gateway
                BAD_GATEWAY = 502,
                /// 503 Service Unavailable
                SERVICE_UNAVAILABLE = 503,
                /// 504 Gateway Timeout
                GATEWAY_TIMEOUT = 504,
                /// 505 HTTP Version Not Supported
                HTTP_VERSION_NOT_SUPPORTED = 505,
                /// 506 Variant Also Negotiates
                VARIANT_ALSO_NEGOTIATES = 506,
                /// 507 Insufficient Storage
                INSUFICIENT_STORAGE = 507,
                /// 508 Loop Detected
                LOOP_DETECTED = 508,
                /// 510 Not Extended
                NOT_EXTENDED = 510,
                /// 511 Network Authentication Required
                NETWORK_AUTH_REQUIRED = 511
            };
        }
        /// Media type codes the set_content() overloads take; each enumerator carries its string
        namespace mime {
            /// text/* types; plain is 0, which is text/plain and not an unset sentinel
            enum txt : uint_16 { plain = 0X0000U,
                                 /// text/html
                                 html,
                                 /// text/css
                                 css,
                                 /// text/csv
                                 csv };
            /// application/* types
            enum app : uint_16 { json = 0X0100U,
                                 /// application/xml
                                 xml,
                                 /// application/javascript
                                 js,
                                 /// application/pdf
                                 pdf,
                                 /// application/zip
                                 zip,
                                 /// application/gzip
                                 gzip,
                                 /// application/x-tar
                                 tar,
                                 /// application/octet-stream; the fallback for an unknown code
                                 oct };
            /// image/* types
            enum img : uint_16 { webp = 0X0200U,
                                 /// image/bmp
                                 bmp,
                                 /// image/png
                                 png,
                                 /// image/jpeg
                                 jpeg,
                                 /// image/gif
                                 gif,
                                 /// image/ico
                                 ico,
                                 /// image/svg+xml
                                 svg };
            /// audio/* types
            enum aud : uint_16 { mpeg = 0X0300U,
                                 /// audio/wav
                                 wav,
                                 /// audio/ogg
                                 ogg,
            };
            /// video/* types
            enum vid : uint_16 { webm = 0X0400U,
                                 /// video/mp4
                                 mp4,
                                 /// video/x-msvideo
                                 avi,
            };
            /// font/* types
            enum fnt : uint_16 { woff = 0X0500U,
                                 /// font/woff2
                                 woff2 };
        };

        /**
         * \brief One HTTP request: start line, header fields and an optional body
         *
         * A client fills one in by hand -- set_urlwords() builds the request target, set_content()
         * the body and its two header fields -- and serializes it with to_bytes(). A server
         * receives it already parsed, so get_urlwords() and get_param() hold the decoded path and
         * query.
         */
        class ALXCOMM_API request {
            friend class server;
            friend class client;
            friend class api;

        public:
            /// Default request: no action, no version, no url, no header field
            request() {}
            /**
             * \brief Build a request with its start line and a Connection field
             *
             * \param _action Method of the start line
             * \param _version Version of the start line
             * \param _alive true writes "Connection: keep-alive", false writes "Connection: close"
             */
            request(action _action, version _version, bool _alive);

        public:
            /**
             * \brief Serialize into "ACTION url HTTP/x.y", the fields, a blank line and the body
             *
             * An unset action or version writes an empty token, so a request that was not given
             * both has a malformed start line.
             */
            bytes to_bytes() const;

        public:
            /**
             * \brief Set the request target from path words and query parameters
             *
             * Words are percent-encoded (%XX stays uppercase) and joined with "/" behind a leading
             * slash; the parameters follow as "key=value" pairs joined with "&" behind a "?".
             *
             * \param _urlwords Path segments in order; pass bare segments, a "/" inside one is
             *                  encoded
             * \param _param Query parameters; an empty map leaves the "?" off
             */
            void set_urlwords(
                const std::list<std::string>& _urlwords,
                const std::map<std::string, std::string>& _param = std::map<std::string, std::string>());
            /// Serialize _val as compact JSON and set it as the body with application/json
            void set_content(const json_object& _val);
            /// Body with the Content-Type of a mime code; an unknown code becomes octet-stream
            void set_content(uint_16 _mime, const bytes_view& _val);
            /// Body with an explicit Content-Type; Content-Length is set from _val.size()
            void set_content(const std::string& _mime, const bytes_view& _val);
            /// Set or replace the header field _key
            inline void set_field(const std::string& _key, const std::string& _val) { m_field[_key] = _val; }
            /// Remove the header field _key; no-op when it is not there
            inline void del_field(const std::string& _key) { m_field.erase(_key); }

        public:
            /// Method of the start line
            inline action get_action() const { return m_action; }
            /// Version of the start line
            inline version get_version() const { return m_version; }
            /// true when the peer asked for a keep-alive connection
            inline bool is_alive() const { return m_alive; }
            /// Percent-encoded request target, as it goes on the wire
            inline std::string get_url() const { return m_urlorg; }
            /// Decoded path segments, a copy
            inline std::list<std::string> get_urlwords() const { return m_urlwords; }
            /// Header fields by name, a copy
            inline std::map<std::string, std::string> get_field() const { return m_field; }
            /// Decoded query parameters, name to value, a copy
            inline std::map<std::string, std::string> get_param() const { return m_param; }
            /// Body; empty when the request carries none
            inline const bytes& get_content() const { return m_content; }

        private:
            action m_action{action::NONE};
            version m_version{version::NONE};
            bool m_alive{true};
            std::string m_urlorg;
            std::list<std::string> m_urlwords;
            std::map<std::string, std::string> m_field;
            std::map<std::string, std::string> m_param;
            bytes m_content;
        };

        /**
         * \brief One HTTP reply: status line, header fields and an optional body
         *
         * A handler builds one -- set_content() sets the body and its two header fields -- and the
         * server serializes it with to_bytes(), adding its own Server field and gzipping a sizable,
         * compressible body when the request accepts gzip. A client receives it already parsed.
         *
         * A default reply is the no-answer value: version and state NONE, no field, no body. The
         * client delivers one when the send failed or the connection dropped.
         */
        class ALXCOMM_API reply {
            friend class server;
            friend class client;
            friend class api;

        public:
            /**
             * \brief Build a reply with its status line and a Connection field
             *
             * \param _v Version of the status line
             * \param _s Status code of the status line
             * \param _alive false makes the server close the connection after sending this reply
             */
            reply(version _v = version::NONE, uint_16 _s = state::NONE, bool _alive = true);

        public:
            /**
             * \brief Serialize into "HTTP/x.y code reason", the fields, a blank line and the body
             *
             * The reason phrase comes from the built-in table; a code with no entry there leaves
             * it empty.
             */
            bytes to_bytes() const;

        public:
            /// Serialize _val as compact JSON and set it as the body with application/json
            void set_content(const json_object& _val);
            /// Body with the Content-Type of a mime code; an unknown code becomes octet-stream
            void set_content(uint_16 _mime, const bytes_view& _val);
            /// Body with an explicit Content-Type; Content-Length is set from _val.size()
            void set_content(const std::string& _mime, const bytes_view& _val);
            /// Set or replace the header field _key
            inline void set_field(const std::string& _key, const std::string& _val) { m_field[_key] = _val; }
            /// Remove the header field _key; no-op when it is not there
            inline void del_field(const std::string& _key) { m_field.erase(_key); }

        public:
            /// true when the connection stays open after this reply
            inline bool is_alive() const { return m_alive; }
            /// Version of the status line
            inline version get_version() const { return m_version; }
            /// Status code; state::NONE (0) when the reply carries no answer
            inline uint_16 get_state() const { return m_state; }
            /// Header fields; a reference into the reply, valid while the reply lives
            inline const std::map<std::string, std::string>& get_field() const { return m_field; }
            /// Body; empty when the reply carries none
            inline const bytes& get_content() const { return m_content; }

        private:
            bool m_alive{true};
            version m_version{version::NONE};
            uint_16 m_state{state::NONE};
            std::map<std::string, std::string> m_field;
            bytes m_content;
        };

        /**
         * \brief URL tree that dispatches one request to one handler by path word
         *
         * A node holds a handler and one child per path word; exec() walks the words down the tree
         * and calls the handler of the node the path ends on. The destructor deletes every child
         * recursively, so a node handed to insert() belongs to that one word.
         */
        class ALXCOMM_API api : public noncopyable {
        public:
            /// Handler for one path word; it runs on a server pool thread and must be thread-safe
            typedef std::function<reply(const request& _req)> api_func;
            /// Build a node whose handler is _func; a null handler answers 403
            api(api_func _func) : m_func(std::move(_func)) {}
            /// Delete every child node below this one, recursively
            ~api() {
                for (const auto& it : m_api_map) delete it.second;
                m_api_map.clear();
            }
            /**
             * \brief Dispatch _req down the tree by the words still in _url
             *
             * The front word selects a child of this node -- or the child an alias of it names --
             * and the rest is dispatched there; this node's handler runs only when no word is
             * left. A node without a handler answers 403, an unmatched word answers 404.
             *
             * \param _url Path words without the leading slash; empty = this node's handler
             */
            reply exec(const request& _req, std::list<std::string> _url);
            /// Adopt _api as the child for _word, deleting the child that word had before
            void insert(api* _api, const std::string& _word);
            /**
             * \brief Register _alias as a second lookup word for the child named _word
             *
             * \return false when _alias is already a word or an alias here (a name never resolves
             *         two ways), or when _word has no child at this node
             */
            bool add_alias(const std::string& _alias, const std::string& _word);

        private:
            api_func m_func;
            std::unordered_map<std::string, api*> m_api_map;
            std::unordered_map<std::string, std::string> m_alias_map;
        };

        /**
         * \brief HTTP server: parses requests off a transport and answers them from an api tree
         *
         * Owns the transport and the tree; the destructor closes the connection, waits for the
         * handlers still running, then deletes both -- neither may have another owner.
         *
         * Requests are parsed on the transport worker thread and handed to the threadpool built
         * here, one api call each, and the reply goes back through bytes_send(). The Server field
         * is added here, and a body of 256 bytes or more whose Content-Type is compressible is
         * gzipped when the request carries "Accept-Encoding: gzip".
         */
        class ALXCOMM_API server
            : public comm {
        public:
            /**
             * \brief Build a server over an existing transport
             *
             * \param _trans Transport to own; deleted by the destructor
             * \param _api Handler tree to own; deleted by the destructor
             * \param _tsize Handler threads
             * \param _qsize Handler queue depth; a request waits there while every thread is busy
             */
            server(transmit* _trans, api* _api, uint_64 _tsize = 4, uint_64 _qsize = 1024);
            /// Same, over a "tcp_server" transport built from "host:port"
            server(const std::string _addr_port, api* _api, uint_64 _tsize = 4, uint_64 _qsize = 1024);
            /// Close the connection, let the handlers finish, then delete the pool and the tree
            ~server();

        protected:
            /**
             * \brief Parse the bytes and queue one handler call per complete request
             *
             * Called on the transport worker thread. A partial request is held in a per-thread
             * cache until the rest of it arrives; a head past 8K, a body past the 64M cap, or a
             * request line that cannot be found drops the cache with a log line.
             */
            virtual void on_bytes_recv(const bytes_view& _data, uint_64 _loc) override;

        private:
            void parse_bytes(const bytes_view& _data, std::list<request>& _pack);
            void process_request(const request& _request, uint_64 _loc);
            void reply_request(const reply& _reply, uint_64 _loc);

        private:
            api* m_api{nullptr};
            threadpool* m_pool{nullptr};
        };

        /**
         * \brief Server over a TLS transport
         *
         * Handler threads and queue depth are the ones server() takes.
         */
        class ALXCOMM_API server_tls
            : public server {
        public:
            /**
             * \brief Build a TLS server listening on "host:port"
             *
             * \param _addr_port "host:port" to listen on
             * \param _api Handler tree to own; deleted by the destructor
             * \param _ext_info Transport options; "http/1.1" is offered as ALPN unless it sets alpn
             */
            server_tls(const std::string _addr_port, api* _api, const varmap& _ext_info,
                       uint_64 _tsize = 4, uint_64 _qsize = 1024);
        };

        /**
         * \brief HTTP client: one request at a time, the reply arriving through a future
         *
         * Owns its transport. Only one request may be in flight -- exec() refuses while another is
         * pending -- and a reply completes its future on the transport worker thread. A connection
         * that drops before the reply arrives completes the future with an empty reply instead, so
         * a caller waiting on it never blocks forever.
         */
        class ALXCOMM_API client
            : public comm {
        public:
            /// Wrap a transport and take ownership of it
            client(transmit* _trans);
            /// Same, over a "tcp_client" transport built from "host:port"
            client(const std::string _addr_port);
            /// Close the connection, then complete any pending request with an empty reply
            ~client();

        public:
            /**
             * \brief Send _request; the reply arrives through _reply's future
             *
             * "Accept-Encoding: gzip" is added to the request, replacing a caller-set one, and a
             * gzipped reply is decompressed before the future is completed.
             *
             * \param _reply Assigned the future of this request
             * \return false when the connection is not open, when a request is still pending (the
             *         future is left untouched then), or when the send failed (the future is then
             *         already fulfilled with an empty reply)
             */
            bool exec(const request& _request, std::future<reply>& _reply);

        protected:
            /**
             * \brief Complete the pending future once a whole reply has arrived
             *
             * Called on the transport worker thread. Bytes that are not yet a whole reply are kept
             * for the next call; with no request pending they are logged and dropped.
             */
            virtual void on_bytes_recv(const bytes_view& _data, uint_64 _loc) override;
            /**
             * \brief Drop the queued sends and abort the pending request when the connection drops
             *
             * Called on the transport worker thread; a connect event is ignored.
             */
            virtual void on_trans_event(uint_64 _loc, bool _connect) override;

        private:

            bool abort_pending();

        private:
            std::promise<reply>* m_promise{nullptr};
            bytes m_buffer;
            reply* m_reply{nullptr};
        };

        /// Client over a TLS transport
        class ALXCOMM_API client_tls
            : public client {
        public:
            /**
             * \brief Build a TLS client connecting to "host:port"
             *
             * \param _addr_port "host:port" to connect to
             * \param _ext_info Transport options, passed through as given
             */
            client_tls(const std::string _addr_port, const varmap& _ext_info = varmap());
        };
    }
}

#endif
