# mlibs.http

HTTP/1.1's framing for a program's own loopback server, once for the whole repository: `omui connect`'s bridge that a page reaches, and the test page servers of `omui-devtools` (a wasm test program in a real browser). A request's head read from what has arrived, a query's values, a path decoded, a response written, a file's media type. Only the framing: the connection is the caller's (mcpplibs.os's `ipc` or `net`), so this stays the standard library alone (D-55).

```cpp
import mlibs.http;

auto request = mlibs::http::parse(arrived);          // nothing yet while the head is incomplete; an error for a bad one
if (request && *request) { /* the body starts at (*request)->head_size, (*request)->body_length long */ }
connection.write(mlibs::http::response(200, page, {.type = mlibs::http::media_type(".html")}));
```

Not a general web server: no chunked bodies, no keep-alive (each response closes its connection), no TLS. A server that a person's browser reaches beyond the loopback is out of scope.
