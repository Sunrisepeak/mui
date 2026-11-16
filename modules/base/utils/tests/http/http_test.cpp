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
}};
