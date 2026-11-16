# mlibs.http

HTTP/1.1's framing for a program's own loopback server and client, once for the whole repository: `omui connect`'s bridge that a page reaches, the test page servers of `omui-devtools` (a wasm test program in a real browser, the site served as Pages serves it), and its client of a browser's DevTools endpoint. A request's head read from what has arrived, a query's values, a path decoded, a response written (closing its connection, or kept alive), a file's media type; a request written and a response's head read. Only the framing: the connection is the caller's (mcpplibs.os's `ipc` or `net`), so this stays the standard library alone (D-55).

```cpp
import mlibs.http;

auto request = mlibs::http::parse(arrived);          // nothing yet while the head is incomplete; an error for a bad one
if (request && *request) { /* the body starts at (*request)->head_size, (*request)->body_length long */ }
connection.write(mlibs::http::response(200, page, {.type = mlibs::http::media_type(".html")}));
connection.write(mlibs::http::request("GET", "/json/version", "127.0.0.1:9222"));
auto answer = mlibs::http::parse_response(arrived);  // status, headers, the body's length (none: to the connection's end)
```

Not a general web server: no chunked bodies (a chunked response is refused), no TLS; keep-alive only when the response asks for it. A server that a person's browser reaches beyond the loopback is out of scope.
