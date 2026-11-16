// omui-devtools cdp: WebSocket frames written as a client writes them (masked) and read back whole,
// in pieces, at each length's encoding (7, 16 and 64 bits: a screenshot's several MB), fragments
// joined, control frames passed through; the handshake's accept key (RFC 6455's own example); gzip
// chosen as Pages chooses it and round-tripped; a request path resolved without leaving the root; a
// page script's reading of values; a check's error emulated; the server answering over the
// loopback, kept alive, gzip when asked.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.cdp;

using namespace mcpplibs::mtest;
namespace cdp = omui::devtools::cdp;
namespace ws = omui::devtools::cdp::websocket;

namespace {

// A server's frame (unmasked) of `payload`.
std::string server_frame(ws::Opcode opcode, std::string_view payload, bool fin = true) {
    std::string out;
    out += static_cast<char>((fin ? 0x80 : 0) | static_cast<int>(opcode));
    if (payload.size() < 126) {
        out += static_cast<char>(payload.size());
    } else if (payload.size() <= 0xffff) {
        out += static_cast<char>(126);
        out += static_cast<char>(payload.size() >> 8);
        out += static_cast<char>(payload.size() & 0xff);
    } else {
        out += static_cast<char>(127);
        for (int shift = 56; shift >= 0; shift -= 8)
            out += static_cast<char>((static_cast<std::uint64_t>(payload.size()) >> shift) & 0xff);
    }
    return out + std::string(payload);
}

}  // namespace

UTest cases{
    "devtools.cdp", [] {
        "a client's frame is masked and reads back as it was written, at every length's encoding"_test =
            [] {
                for (const std::size_t size :
                     {std::size_t{0}, std::size_t{5}, std::size_t{125}, std::size_t{126},
                      std::size_t{65535}, std::size_t{65536}, std::size_t{3} << 20}) {
                    std::string payload(size, '\0');
                    for (std::size_t index = 0; index < size; ++index)
                        payload[index] = static_cast<char>(index * 31 + 7);
                    const std::string written =
                        ws::frame(ws::Opcode::text, payload, {0x12, 0x34, 0x56, 0x78});
                    expect((static_cast<std::uint8_t>(written[1]) & 0x80) != 0) << "masked";
                    if (size > 0)
                        expect(written.substr(written.size() - size) != payload)
                            << "the payload is masked on the wire";
                    const auto read = ws::parse(written);
                    expect(read && *read && (*read)->fin && (*read)->opcode == ws::Opcode::text &&
                           (*read)->size == written.size());
                    if (read && *read)
                        expect((*read)->payload == payload) << size;
                    // In pieces: nothing until the last byte has come.
                    expect(ws::parse(std::string_view(written).substr(0, written.size() - 1))
                               .value_or(std::nullopt) == std::nullopt);
                }
            };

        "a server's frames: one after another in what arrived; fragments joined; control frames between them"_test =
            [] {
                const std::string arrived = server_frame(ws::Opcode::text, "{\"id\":", false) +
                                            server_frame(ws::Opcode::ping, "beat") +
                                            server_frame(ws::Opcode::continuation, "1}") +
                                            server_frame(ws::Opcode::close, "");
                ws::Assembler messages;
                std::vector<std::pair<ws::Opcode, std::string>> got;
                for (std::string_view rest = arrived; !rest.empty();) {
                    const auto frame = ws::parse(rest);
                    if (!frame || !*frame)
                        break;
                    rest.remove_prefix((*frame)->size);
                    const auto message = messages.add(std::move(**frame));
                    if (message && *message)
                        got.push_back(**message);
                }
                expect(eq(got.size(), std::size_t{3}));
                if (got.size() == 3) {
                    expect(got[0].first == ws::Opcode::ping &&
                           eq(got[0].second, std::string("beat")));
                    expect(got[1].first == ws::Opcode::text &&
                           eq(got[1].second, std::string("{\"id\":1}")));
                    expect(got[2].first == ws::Opcode::close);
                }
                ws::Assembler stray;
                expect(!stray.add({.fin = true, .opcode = ws::Opcode::continuation, .payload = "x"})
                            .has_value())
                    << "a continuation with nothing started";
                expect(!ws::parse(std::string("\x83\x00", 2)).has_value()) << "a reserved opcode";
                expect(!ws::parse(server_frame(ws::Opcode::binary, std::string(300, 'x')), 100)
                            .has_value())
                    << "larger than allowed";
            };

        "the handshake's accept key: RFC 6455's example"_test = [] {
            expect(eq(ws::accept("dGhlIHNhbXBsZSBub25jZQ=="),
                      std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")));
        };

        "gzip as Pages serves it: the kinds it compresses, the word in Accept-Encoding, a member that inflates back"_test =
            [] {
                for (const std::string_view kind :
                     {".html", ".js", ".wasm", ".ttf", ".json", ".md"})
                    expect(cdp::compressed_kind(kind)) << kind;
                for (const std::string_view kind : {".png", ".webp", ".txt", ""})
                    expect(!cdp::compressed_kind(kind)) << kind;
                expect(cdp::accepts_gzip("gzip, deflate, br") &&
                       cdp::accepts_gzip("br;q=1, gzip;q=0.8") &&
                       !cdp::accepts_gzip("br, x-gzipped") && !cdp::accepts_gzip(""));
                std::string text;
                for (int line = 0; line < 2000; ++line)
                    text += std::format("<p>line {} of a page</p>\n", line % 37);
                const std::string packed = cdp::gzip(text);
                expect(packed.size() > 18 && static_cast<std::uint8_t>(packed[0]) == 0x1f &&
                       static_cast<std::uint8_t>(packed[1]) == 0x8b);
                expect(packed.size() < text.size() / 5);
                const auto back = cdp::gunzip(packed);
                expect(back.has_value() && *back == text);
                expect(!cdp::gunzip("not gzip").has_value());
                expect(eq(cdp::pages_type(".wasm"), std::string_view("application/wasm")) &&
                       eq(cdp::pages_type(".svg"), std::string_view("application/octet-stream")));
            };

        "a request path: decoded, its query and fragment dropped, never above the root"_test = [] {
            expect(eq(cdp::resolved_path("/"), std::string("")));
            expect(eq(cdp::resolved_path("/index.html?x=1#/docs"), std::string("index.html")));
            expect(eq(cdp::resolved_path("/a%20b/./c/../d.js"), std::string("a b/d.js")));
            expect(eq(cdp::resolved_path("/../../etc/passwd"), std::string("etc/passwd")));
        };

        "values read as a page script reads them; numbers as a template literal writes them"_test =
            [] {
                const auto value = nlohmann::json::parse(
                    R"({"data":{"nodes":[{"id":"a","bounds":[1,2,3,4]},{"id":"b"}]},"n":0,"s":""})");
                expect(eq(cdp::text(cdp::dig(value, "data", "nodes", 0, "id")), std::string("a")));
                expect(cdp::dig(value, "data", "nodes", 5, "id").is_null() &&
                       cdp::dig(value, "n", "x").is_null());
                expect(!cdp::truthy(value["n"]) && !cdp::truthy(value["s"]) &&
                       cdp::truthy(value["data"]) && !cdp::truthy(cdp::at(value, "missing")));
                expect(cdp::box(cdp::dig(cdp::find_id(value["data"]["nodes"], "a"), "bounds"))
                           .has_value() &&
                       !cdp::box(cdp::find_id(value["data"]["nodes"], "b")).has_value());
                expect(eq(cdp::js_number(0.25), std::string("0.25")) &&
                       eq(cdp::js_number(3), std::string("3")) &&
                       eq(cdp::js_number(-21), std::string("-21")));
                expect(eq(cdp::q("a\"b"), std::string("\"a\\\"b\"")));
            };

        "a check's error: what follows is skipped, the guard records it, a finally runs anyway"_test =
            [] {
                cdp::Script script;
                script.check(true, "first");
                script.guard("the scenario completes", [&] {
                    script.check(true, "inside");
                    script.raise("it broke");
                    script.check(true, "after the error: not recorded");
                });
                bool ran = false;
                script.guard("unused", [&] { script.finally([&] { ran = true; }); });
                const auto& checks = script.report["checks"];
                expect(eq(checks.size(), std::size_t{3}) && ran);
                if (checks.size() == 3) {
                    expect(eq(checks[2]["what"].get<std::string>(),
                              std::string("the scenario completes")) &&
                           !checks[2]["ok"].get<bool>());
                    expect(
                        eq(checks[2]["detail"].get<std::string>(), std::string("Error: it broke")));
                }
                expect(!script.all_ok() && !script.thrown());
            };

        "the server over the loopback: a file, kept alive for the next request, gzip when asked, 404 for what is not there"_test =
            [] {
                const auto directory = std::filesystem::temp_directory_path() /
                                       std::format("omui-cdp-test-{}", std::random_device{}());
                std::filesystem::create_directories(directory / "docs");
                std::ofstream(directory / "index.html", std::ios::binary) << std::string(4000, 'a');
                std::ofstream(directory / "docs" / "index.html", std::ios::binary) << "docs";
                auto server = cdp::Server::start(cdp::pages(directory));
                expect(server.has_value());
                if (server) {
                    const auto page = cdp::get((*server)->port(), "/");
                    expect(page.has_value() && eq(page->size(), std::size_t{4000}));
                    expect(cdp::get((*server)->url() + "docs/").value_or("") == "docs");
                    expect(!cdp::get((*server)->port(), "/missing.js").has_value());
                    // Two requests on one connection, the second asking for gzip.
                    auto connection = mcpplibs::os::net::connect_loopback((*server)->port());
                    expect(connection.has_value());
                    if (connection) {
                        static_cast<void>(connection->write(
                            "GET /docs/ HTTP/1.1\r\nHost: x\r\n\r\nGET /index.html "
                            "HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n"));
                        std::string arrived;
                        for (int attempt = 0;
                             attempt < 50 && !arrived.contains("content-encoding: gzip");
                             ++attempt) {
                            auto chunk = connection->read_for(std::chrono::milliseconds(100));
                            if (chunk && *chunk)
                                arrived += **chunk;
                        }
                        for (int attempt = 0; attempt < 10; ++attempt) {
                            auto chunk = connection->read_for(std::chrono::milliseconds(50));
                            if (chunk && *chunk)
                                arrived += **chunk;
                        }
                        expect(arrived.starts_with("HTTP/1.1 200 OK") &&
                               arrived.contains("Connection: keep-alive") &&
                               arrived.contains("content-encoding: gzip"))
                            << arrived.substr(0, 300);
                        const std::size_t second =
                            arrived.find("\r\n\r\n", arrived.find("HTTP/1.1 200", 10));
                        if (second != std::string::npos)
                            expect(cdp::gunzip(std::string_view(arrived).substr(second + 4))
                                       .value_or("") == std::string(4000, 'a'));
                    }
                    (*server)->close();
                }
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            };
    }};
