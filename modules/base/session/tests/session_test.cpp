// Sessions: ordering, seeking, merging and slicing; NDJSON that reads back
// byte-identical; files; and errors that say where and what.
import std;
import mlibs.session;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace session = mlibs::session;

UTest cases{"session", [] {

    "entries stay ordered by time; equal times keep their order"_test = [] {
        session::Session r;
        expect(r.add(1.0, "frame", R"({"n":1})").has_value());
        expect(r.add(0.5, "act", R"({"node":"main/save"})").has_value());
        expect(r.add(1.0, "frame", R"({"n":2})").has_value());
        expect(r.add(0.0, "note", "").has_value());
        auto e = r.entries();
        expect(e.size() == 4);
        expect(e[0].kind == "note" && e[0].body == "{}");
        expect(e[1].kind == "act");
        expect(e[2].body == R"({"n":1})" && e[3].body == R"({"n":2})");
        expect(r.duration() == 1.0);
        expect(!r.add(std::numeric_limits<double>::quiet_NaN(), "x", "{}").has_value());
        expect(!r.add(1, "", "{}").has_value());
        expect(!r.add(1, "x", "[1]").has_value()) << "a body is an object";
        expect(!r.add(1, "x", "{oops").has_value());
    };

    "seek and spans by binary search"_test = [] {
        session::Session r;
        for (int i = 0; i < 100; ++i) (void)r.add(i * 0.1, i % 2 ? "act" : "frame", std::format(R"({{"i":{}}})", i));
        expect(r.seek(0) == 0);
        expect(r.seek(5.0) == 50 || r.seek(5.0) == 51);  // 50 × 0.1 is a hair under or over 5
        expect(r.seek(100) == 100);
        auto mid = r.between(2.05, 3.05);
        expect(mid.size() == 10);
        expect(mid.front().body == R"({"i":21})");
        expect(r.of("act").size() == 50);
        auto part = r.slice(1.0, 2.0);
        expect(part.size() == 10 && part.entries().front().time < 1e-9);
    };

    "merge interleaves by time"_test = [] {
        session::Session a, b;
        (void)a.add(0, "a", "{}");
        (void)a.add(2, "a", "{}");
        (void)b.add(0, "b", "{}");
        (void)b.add(0.5, "b", "{}");
        a.merge(b, 1.0);
        std::string kinds;
        for (const auto& e : a.entries()) kinds += e.kind;
        expect(kinds == "abba");
        expect(a.entries()[2].time == 1.5);
    };

    "NDJSON reads back and writes the same bytes"_test = [] {
        session::Session r;
        r.header.app = "org.example.notes";
        r.header.spec = "0.1";
        r.header.width = 800;
        r.header.height = 600;
        r.header.scale = 2;
        r.header.seed = 42;
        r.header.meta = {{"bug", "1234"}, {"by", "agent:qa"}};
        (void)r.add(0.1, "frame", R"({"events":[{"type":"pointer","kind":"down","x":10.5,"y":3}]})");
        (void)r.add(1.0 / 3.0, "act", R"({"node":"main/删除","action":"activate","answer":"yes"})");
        const std::string text = r.ndjson();
        expect(text.starts_with(R"({"format":"openmui.session@0.1","app":"org.example.notes")"));
        auto back = session::Session::parse(text);
        expect(back.has_value()) << (back ? "" : back.error());
        expect(back->ndjson() == text);
        expect(back->entries()[1].time == 1.0 / 3.0) << "times round-trip exactly";
        expect(back->header.meta.size() == 2 && back->header.meta[1].second == "agent:qa");
        expect(back->header.seed == 42 && back->header.scale == 2);
    };

    "errors say where and what"_test = [] {
        auto none = session::Session::parse("");
        expect(!none && none.error().find("header") != std::string::npos);
        auto noheader = session::Session::parse(R"({"t":0,"kind":"x"})");
        expect(!noheader && noheader.error().find("line 1") != std::string::npos);
        auto unordered = session::Session::parse("{\"format\":\"openmui.session@0.1\"}\n{\"t\":1,\"kind\":\"a\"}\n{\"t\":0.5,\"kind\":\"b\"}\n");
        expect(!unordered && unordered.error().find("line 3") != std::string::npos) << (unordered ? "" : unordered.error());
        auto bad = session::Session::parse("{\"format\":\"openmui.session@0.1\"}\n\n{\"t\":\"soon\",\"kind\":\"a\"}\r\n");
        expect(!bad && bad.error().find("line 3") != std::string::npos);
        auto blank = session::Session::parse("{\"format\":\"openmui.session@0.1\"}\r\n\r\n");
        expect(blank.has_value() && blank->empty());
    };

    "files"_test = [] {
        auto dir = scratch_dir("record");
        session::Session r;
        (void)r.add(0.25, "note", R"({"text":"hello"})");
        auto file = dir / "a.session.omui.jsonl";
        expect(r.save(file).has_value());
        auto back = session::Session::load(file);
        expect(back.has_value() && back->ndjson() == r.ndjson());
        auto missing = session::Session::load(dir / "missing.session.omui.jsonl");
        expect(!missing.has_value());
    };

    "a session saves and loads under a name that is not ASCII"_test = [] {
        session::Session r;
        expect(r.add(0.25, "act", R"({"node":"main/保存"})").has_value());
        const auto file = scratch_dir("record") / std::filesystem::path(u8"会话 录制.session.omui.jsonl");
        expect(r.save(file).has_value());
        auto back = session::Session::load(file);
        expect(back.has_value() && back->size() == 1) << (back ? "" : back.error());
        auto missing = session::Session::load(scratch_dir("record") / std::filesystem::path(u8"没有.session.omui.jsonl"));
        expect(!missing && missing.error().contains("没有")) << (missing ? "" : missing.error());
    };
}};
