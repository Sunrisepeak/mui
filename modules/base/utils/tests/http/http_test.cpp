// mlibs.http: a head read only once whole, its line and headers; the body's length; queries and decoding; a
// response's framing; media types.
import std;
import mcpplibs.mtest;
import mlibs.http;

using namespace mcpplibs::mtest;
namespace http = mlibs::http;

static UTest cases{"utils.http", [] {
    "a head is read once its blank line has come: method, path, query, headers in lower case, the body's length"_test = [] {
        const std::string head = "POST /omui/v1/next?session=ab%20c&x=1 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 5\r\nOrigin:  http://a \r\n\r\nhello";
        expect(http::parse(head.substr(0, 30)).value_or(std::nullopt) == std::nullopt);
        const auto parsed = http::parse(head);
        expect(parsed.has_value() && parsed->has_value());
        if (!parsed || !*parsed) return;
        const auto& request = **parsed;
        expect(eq(request.method, std::string("POST")) && eq(request.path, std::string("/omui/v1/next")));
        expect(eq(request.query, std::string("session=ab%20c&x=1")) && eq(request.body_length, std::size_t{5}));
        expect(eq(request.header("origin"), std::string_view("http://a")) && eq(request.header("host"), std::string_view("127.0.0.1")));
        expect(eq(std::string_view(head).substr(request.head_size), std::string_view("hello")));
        expect(eq(http::decoded(http::query_value(request.query, "session")), std::string("ab c")) && eq(http::query_value(request.query, "y"), std::string()));
    };

    "not HTTP, an oversized head, a length that is not a count: errors"_test = [] {
        expect(!http::parse("HELLO\r\n\r\n").has_value());
        expect(!http::parse(std::string(70 * 1024, 'x')).has_value());
        expect(!http::parse("GET / HTTP/1.1\r\nContent-Length: lots\r\n\r\n").has_value());
    };

    "a response's framing; reasons; decoding leaves a broken escape as it is; media types"_test = [] {
        expect(eq(http::response(404, "", {.type = "text/plain"}), std::string("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")));
        expect(http::response(200, "{}", {.headers = {{"Cache-Control", "no-store"}}}).ends_with("Cache-Control: no-store\r\n\r\n{}"));
        expect(eq(http::decoded("a%2Fb%zz%4"), std::string("a/b%zz%4")));
        expect(eq(http::media_type(".wasm"), std::string_view("application/wasm")) && eq(http::media_type(".xyz"), std::string_view("application/octet-stream")));
    };

    "a client's request: its line, Host, the headers given, the body's length"_test = [] {
        expect(eq(http::request("GET", "/json/version", "127.0.0.1:9222"),
                  std::string("GET /json/version HTTP/1.1\r\nHost: 127.0.0.1:9222\r\n\r\n")));
        expect(eq(http::request("POST", "/out", "localhost", {{"Origin", "null"}}, "hi"),
                  std::string("POST /out HTTP/1.1\r\nHost: localhost\r\nOrigin: "
                              "null\r\nContent-Length: 2\r\n\r\nhi")));
    };

    "a response's head is read once whole: status, reason, headers, the body's length or none"_test =
        [] {
            const std::string answer =
                "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: "
                "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n\x81\x02hi";
            expect(http::parse_response(answer.substr(0, 20)).value_or(std::nullopt) ==
                   std::nullopt);
            const auto parsed = http::parse_response(answer);
            expect(parsed.has_value() && parsed->has_value());
            if (!parsed || !*parsed)
                return;
            expect(eq((*parsed)->status, 101) &&
                   eq((*parsed)->reason, std::string("Switching Protocols")));
            expect(eq((*parsed)->header("sec-websocket-accept"),
                      std::string_view("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")) &&
                   !(*parsed)->body_length.has_value());
            expect(eq(std::string_view(answer).substr((*parsed)->head_size),
                      std::string_view("\x81\x02hi")));
            const auto sized =
                http::parse_response("HTTP/1.0 404 Not Found\r\nContent-Length: 12\r\n\r\n");
            expect(sized && *sized && (*sized)->status == 404 &&
                   (*sized)->body_length == std::optional<std::size_t>{12});
        };

    "not HTTP, a chunked body, a length that is not a count: errors; kept alive, no type"_test =
        [] {
            expect(!http::parse_response("SSH-2.0\r\n\r\n").has_value());
            expect(!http::parse_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
                        .has_value());
            expect(!http::parse_response("HTTP/1.1 200 OK\r\nContent-Length: some\r\n\r\n")
                        .has_value());
            expect(eq(http::response(404, "", {.type = "", .keep_alive = true}),
                      std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: "
                                  "keep-alive\r\n\r\n")));
        };
}};
