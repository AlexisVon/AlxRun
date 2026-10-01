# alxcomm — API Manual

`alxcomm` is the **communication layer** of AlxLib: transport (TCP/UDP/Unix socket/TLS), session encapsulation, varmap data frames, HTTP server/client, the RPC framework. It depends on `alxbase` + `alxcore`.

- Header directory: `include/alxcomm/`
- Library: `libalxcomm.so` (or the merged static library `alxlib.a`)
- Namespace: `alx` (HTTP in `alx::http`)

```bash
g++ -std=c++17 -I include -I include/alxbase -I include/alxcore -I include/alxcomm main.cpp \
    -L bin -lalxcomm -lalxcore -lalxbase -Wl,-rpath,$PWD/bin
```

Layering: `transmit` (sends and receives bytes) → `comm` (session: worker thread + send queue) → `comm_ex` (varmap packs) / `http` / `rpc`.

---

## 1. Transport layer `transmit` (`atransmit.h`)

### 1.1 Creation and introspection

```cpp
alx::transmit* srv = alx::transmit::create("tcp_server", "0.0.0.0:8080");
alx::transmit::list_all();                                    // registered transport type names

alx::transmit::to_strip(addr_key);                            // key → "a.b.c.d:port"
alx::transmit::from_strip("127.0.0.1:8080");                  // → (ip, port)
alx::transmit::from_strip64("127.0.0.1:8080");                // → packed into uint_64
alx::transmit::get_addr_port("example.com", "80");            // DNS resolve → list of address strings
```

**Address key**: `_loc` in every callback is the packed `uint_64` `(ip << 32) | port` -- `0` means "every peer", non-zero names one peer. `local_server` is the exception: it names a peer by a connection id instead.

### 1.2 Type table

| Type name | Transport | Address format | Extension parameters (`varmap`)|
|---|---|---|---|
| `tcp_server` | TCP server | `"x.x.x.x:port"` | — |
| `tcp_client` | TCP client | `"x.x.x.x:port"` | — |
| `udp_trans` | UDP | `"x.x.x.x:port"` | — |
| `local_server` | Unix domain socket (bind/listen) | `"x.x.x.x:port"` → `/tmp/alx_<ip>_<port>.sock` | — |
| `local_client` | Unix domain socket (connect) | same | — |
| `tls_server` | TLS server | `"x.x.x.x:port"` | `cert` / `key` / `ca` / `alpn` |
| `tls_client` | TLS client | `"x.x.x.x:port"` | `ca` / `hostname` / `cert` / `key` (mTLS)|

### 1.3 Use

```cpp
alx::transmit* srv = alx::transmit::create("tcp_server", "127.0.0.1:8080");
srv->bytes_recv.connect([](const alx::bytes_view& _data, uint_64 _from) {
    // _from is the source address key; the receive buffer is reused, call _data.to_bytes() to keep it
});
srv->trans_mesg.connect([](const uint_64 _loc, bool _connect, const char* _msg) {
    // connect/disconnect events (_connect true means connected)
});

srv->open();
srv->connect_num();
srv->bytes_send("hello", 5);          // send to every peer
srv->bytes_send("hi", 2, loc_key);    // send to one peer only
srv->close(loc_key);                  // close one peer (its disconnect event is reported); close(0) closes all
srv->is_open();
delete srv;
```

### 1.4 TLS client verification rules

- **With a `ca`**: it verifies the certificate chain and that the peer identity matches `hostname` (DNS name or IP literal). **A missing `hostname` refuses the connection outright**, with no fallback to "chain verification only".
- **Without a `ca`**: the peer identity is not verified (`SSL_VERIFY_NONE`).
- The certificate and key are for mTLS client authentication.

```cpp
alx::varmap ext;
ext["ca"] = "/path/ca.pem";
ext["hostname"] = "example.com";        // must match the certificate subject/SAN
auto* cli = alx::transmit::create("tls_client", "example.com:443", ext);
```

---

## 2. Session layer `comm` (`acomm.h`)

`comm` adds one layer over `transmit`: **one worker thread + one send queue**, which turns "open / send / close" into asynchronous hand-offs, so the caller never deals with the socket directly. `comm` is an abstract base class; `comm_ex` / `http::server` / `rpc` are the ones in use.

```cpp
bool open();                                  // starts the worker thread
void close(uint_64 _loc = 0);                 // 0 = close everything (joins the worker), non-zero = close one peer
bool bytes_send(const bytes_view&, uint64 loc = 0);   // hands it to the send queue
bool is_valid();  bool is_open();  bool is_connect();
void clear();                                 // drop the queued tasks
```

The second constructor argument is the **send queue depth cap** (default 128); a `bytes_send` blocks while the queue is full.

**Concurrency contract** (a convention, the library adds no gate): `open()` / `close()` are called serially by the host; `is_valid()` / `is_open()` / `is_connect()` are state reads any thread may do concurrently; `bytes_send()` does not overlap another `bytes_send()`, nor an `open()` / `close()`. The callbacks (`bytes_recv` / `trans_mesg`) are passive: they run on the library's own threads or on the calling thread, and must not `close` / `open` / `delete` / send from inside.

Signals:

| Signal | Parameter | Meaning |
|---|---|---|
| `mesg_prit` | `const std::string&` | text messages / errors / logs |
| `comm_flag` | `uint_64, bool` | peer connect/disconnect (key, state)|

Overridable hooks (empty by default except `on_bytes_recv`; semantics and threads in `design.md` §2.4):

| Virtual function | When |
|---|---|
| `on_bytes_recv(const bytes_view&, uint_64)` | a burst of bytes arrived (pure virtual, a derived class must implement it)|
| `on_trans_event(uint64 loc, bool connect)` | every connection event, `loc == 0` is the transport itself |

---

## 3. Data frames `comm_ex` (`acomm_ex.h`)

Sends and receives **`varmap`** values over `comm`, with optional LZ4 compression and CRC32C checksum:

```cpp
class my_peer : public alx::comm_ex {
public:
    my_peer(alx::transmit* t) : alx::comm_ex(t) {
        data_recv.connect([](const alx::varmap& _data, uint_64 _loc) {
            // one complete pack received
        });
    }
};

bool data_send(const varmap& _data, uint_64 _loc, bool _compress);
bool data_send(const varmap& _data, bool _compress);      // no _loc given (_loc = 0, i.e. every peer)
signal<const varmap&, uint64> data_recv;
```

A pack is **self-delimited** (8-byte head magic + sizes + control block + data + 8-byte tail magic), so TCP coalescing and splitting are handled inside the framework and `data_recv` always delivers one complete pack.

---

## 4. HTTP (`ahttp.h`, namespace `alx::http`)

### 4.1 Request and reply

```cpp
alx::http::request req(alx::http::action::GET, alx::http::version::V1_1, /*keep_alive=*/true);
req.set_urlwords({"api", "user"}, {{"id", "1"}});      // → /api/user?id=1
req.set_field("Host", "example.com");
req.set_content(alx::json_object(...));                // fills Content-Type/Content-Length in
req.set_content(alx::http::mime::app::json, bytes_view);
req.to_bytes();

alx::http::reply rep(alx::http::version::V1_1, alx::http::state::succ::OK, true);
rep.set_field("Server", "alx");
rep.set_content(json_obj);
rep.get_state();  rep.get_content();  rep.get_field();
```

Enumerations: `version` (`V1_0` / `V1_1`), `action` (`GET` / `HEAD` / `POST` / `PUT` / `DELETE` / `CONNECT` / `OPTIONS` / `TRACE` / `PATCH`), `state::*` (five status-code groups), `mime::*` (txt/app/img/aud/vid/fnt).

### 4.2 Server and routing

`api` is a **route tree that dispatches by URL word**: a node holds one handler and children are mounted by word.

```cpp
// leaf: /api/user
alx::http::api user_api([](const alx::http::request& req) {
    alx::http::reply rep(alx::http::version::V1_1, alx::http::state::succ::OK);
    rep.set_content(alx::json_object({{"id", 1}}));   // or rep.set_content(json)
    return rep;
});

// intermediate node: /api
alx::http::api api_root([](const alx::http::request&) { return alx::http::reply(); });
api_root.insert(&user_api, "user");                  // mounted at /api/user
api_root.add_alias("u", "user");                     // /api/u is equivalent to /api/user

alx::http::server srv("0.0.0.0:8080", &api_root,
                      /*threads=*/4, /*pool queue depth=*/1024);
srv.mesg_prit.connect([](const std::string& s) { printf("%s\n", s.c_str()); });
srv.open();
```

TLS server:

```cpp
alx::varmap ext;
ext["cert"] = "/path/server.crt";  ext["key"] = "/path/server.key";  ext["ca"] = "/path/ca.pem";
alx::http::server_tls tls_srv("0.0.0.0:8443", &api_root, ext, 4, 1024);
```

### 4.3 Client

```cpp
alx::http::client cli("example.com:80");
alx::http::request req(alx::http::action::GET, alx::http::version::V1_1, true);
req.set_urlwords({"index.html"});

std::future<alx::http::reply> fut;
cli.exec(req, fut);                       // submit the request
alx::http::reply rep = fut.get();         // wait for the reply (blocking)
rep.get_state();  rep.get_content();

// TLS client
alx::varmap ext;  ext["ca"] = "/path/ca.pem";  ext["hostname"] = "example.com";
alx::http::client_tls tls_cli("example.com:443", ext);
```

One future per request; both `Content-Length` and `Transfer-Encoding: chunked` response bodies are supported.

**Disconnect semantics**: when the connection drops, the in-flight future lands at once with an **empty `reply`** (`get_state() == 0`) and never blocks forever; the client state is reset with it, `comm` reconnects on its own, and `exec()` keeps working after the reconnect. A reply that drops halfway is discarded whole and cannot mix into the next answer; requests not yet sent are dropped as well (they are not re-sent on the new connection).

The disconnect event is reported by the transport (all three built-in client transports fire it both in `close()` and in the receive loop), and the client merely catches it. **A send failure is not one of them**: `exec()` returning true only means the request was queued, a send failure only reports on `mesg_prit` and the in-flight future does not land, and the timeout is the host's business (see the "Send and receive model" section of `notice.md`).

`exec()` must be called serially (one future `get` before the next send): it returns false when **not connected** or when **a request is already in flight**, and the client takes no lock for concurrent calls.

---

## 5. RPC (`arpc.h`)

RPC builds on `comm_ex`: **the serving side installs services, the calling side sends requests and consumes the results**, and both ends use the same classes.

### 5.1 Installing a service (the serving side)

```cpp
alx::rpc server("tcp_server", "0.0.0.0:9000", /*pool threads=*/2, /*queue=*/255);
server.mesg_prit.connect([](const std::string& s) { /* ... */ });

// version with an error out-parameter: the parameter types are deduced from the template, _error is filled in by the handler
auto pack = alx::rpc::service_pack::create<int, std::string>(
    [](int _id, const std::string& _name, std::string& _error) -> alx::variant {
        if (_id < 0) { _error = "bad id"; return 0; }
        return alx::variant(_name + "!");
    },
    "demo service", alx::rpc::cmps_option::NEVER, /*inline_exec=*/false);

uint_64 svid = server.install_service(pack);
server.list_service();          // svid → description
server.remove_service(svid);
server.open();
```

`create_noerr<...>` is the version without `_error`. A request whose argument count does not match comes back as `_error = "invalid params"` automatically.

### 5.2 Making a call (the calling side)

```cpp
auto call = alx::rpc::consume_pack::create(
    svid,                                             // target service id
    [](const alx::variant& _result, const std::string& _error) {
        if (_error.empty()) { /* use _result */ }
    },
    /*call_compress=*/false, alx::rpc::cmps_expect::AUTO,
    /* arguments */ 1, std::string("alx"));

client.consume_service(call);
```

Compression options: `cmps_option` (the sending side: `NEVER` / `CMPS_NON_FST` / `CMPS_FST` / `ALWAYS`) and `cmps_expect` (the receiving side: `AUTO` / `NEVER` / `ALWAYS`) -- the request's and the reply's compression policies are negotiated independently.

### 5.3 Overridable hooks

| Virtual function | When |
|---|---|
| `on_data_recv(const varmap&, uint_64)` | one frame received (interpreted by the RPC protocol by default)|
| `on_exec_service(rpcid, pack, param, cmps, loc)` | a service is about to run (usable for authentication/auditing)|
| `on_recv_result(rpcid, error, result)` | the calling side receives a reply |

A service runs on the threadpool `rpc` owns by default; `service_pack`'s `inline_exec = true` makes it run in place on the receive thread instead.
