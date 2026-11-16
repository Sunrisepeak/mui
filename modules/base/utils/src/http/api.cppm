// mlibs.http.api — HTTP/1.1's framing for a program's own loopback server (a bridge a page reaches, a test page's
// server), once for the whole repository: a request's head read from what has arrived, and a response written.
// Only the framing: the connection is the caller's (mcpplibs.os's ipc or net), so this stays the standard library
// alone. Not a general web server — no chunked bodies, no keep-alive: each response closes its connection.
//
//     if (auto request = mlibs::http::parse(buffered)) { …request->body_length… }
//     connection.write(mlibs::http::response(200, body, {.type = "text/html"}));
export module mlibs.http.api;

import std;

export namespace mlibs::http {

struct Request {
    std::string method, path, query;                           // path without the query; both as sent (not decoded)
    std::vector<std::pair<std::string, std::string>> headers;  // names in lower case, in order
    std::size_t head_size = 0;                                 // the head's bytes, its blank line included
    std::size_t body_length = 0;                               // Content-Length, 0 without one

    // The first header of that (lower-case) name, or empty.
    [[nodiscard]] std::string_view header(std::string_view name) const;
};

// The request whose head `arrived` holds whole (the body may still be arriving: it starts at head_size); nothing
// while the head's blank line has not come; an error for a head that is not HTTP/1.x or larger than `most`.
[[nodiscard]] std::expected<std::optional<Request>, std::string> parse(std::string_view arrived, std::size_t most = 64 * 1024);

// A query's value for `key` (the first; not decoded), or empty.
[[nodiscard]] std::string query_value(std::string_view query, std::string_view key);

// %XX decoded (a path or a query value); '+' left as it is.
[[nodiscard]] std::string decoded(std::string_view text);

struct Options {
    std::string_view type = "application/json";                // empty: no Content-Type
    std::vector<std::pair<std::string, std::string>> headers;  // more, as given
    bool keep_alive = false;  // Connection: keep-alive rather than close
};

// A whole response: status line, Content-Type, Content-Length, Connection (close unless kept
// alive), the extra headers, the body.
[[nodiscard]] std::string response(int status, std::string_view body, const Options& options = {});

// A request's head and body: the request line, Host, the headers given, Content-Length when there
// is a body, the blank line. `target` is the path and query as sent.
[[nodiscard]] std::string
request(std::string_view method, std::string_view target, std::string_view host,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::string_view body = {});

struct Response {
    int status = 0;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> headers;  // names in lower case, in order
    std::size_t head_size = 0;  // the head's bytes, its blank line included
    std::optional<std::size_t>
        body_length;  // Content-Length; none: the body runs to the connection's end

    // The first header of that (lower-case) name, or empty.
    [[nodiscard]] std::string_view header(std::string_view name) const;
};

// The response whose head `arrived` holds whole (its body starts at head_size); nothing while the
// head's blank line has not come; an error for a head that is not HTTP/1.x or larger than `most`,
// or a chunked body.
[[nodiscard]] std::expected<std::optional<Response>, std::string>
parse_response(std::string_view arrived, std::size_t most = 64 * 1024);

// The status's reason phrase ("OK", "Not Found"…; "Error" for one not listed).
[[nodiscard]] std::string_view reason(int status);

// A file's media type by its extension (".html", ".js", ".wasm", ".json", ".png", ".webp", ".ttf", ".md", ".svg",
// ".css", ".data"…); application/octet-stream otherwise.
[[nodiscard]] std::string_view media_type(std::string_view extension);

}  // namespace mlibs::http
