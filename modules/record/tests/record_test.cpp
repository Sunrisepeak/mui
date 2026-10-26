// Sessions: ordering, seeking, merging and slicing; NDJSON that reads back
// byte-identical; files; and errors that say where and what.
import std;
import openmui.record;
import openmui.testing;

using namespace omui::testing;
namespace record = omui::record;

int main(int argc, char** argv) {
    Suite s{"record"};

    s.test("entries stay ordered by time; equal times keep their order", [] {
        record::Session r;
        check(r.add(1.0, "frame", R"({"n":1})").has_value());
        check(r.add(0.5, "act", R"({"node":"main/save"})").has_value());
        check(r.add(1.0, "frame", R"({"n":2})").has_value());
        check(r.add(0.0, "note", "").has_value());
        auto e = r.entries();
        check(e.size() == 4);
        check(e[0].kind == "note" && e[0].body == "{}");
        check(e[1].kind == "act");
        check(e[2].body == R"({"n":1})" && e[3].body == R"({"n":2})");
        check(r.duration() == 1.0);
        check(!r.add(std::numeric_limits<double>::quiet_NaN(), "x", "{}").has_value());
        check(!r.add(1, "", "{}").has_value());
        check(!r.add(1, "x", "[1]").has_value(), "a body is an object");
        check(!r.add(1, "x", "{oops").has_value());
    });

    s.test("seek and spans by binary search", [] {
        record::Session r;
        for (int i = 0; i < 100; ++i) (void)r.add(i * 0.1, i % 2 ? "act" : "frame", std::format(R"({{"i":{}}})", i));
        check(r.seek(0) == 0);
        check(r.seek(5.0) == 50 || r.seek(5.0) == 51);  // 50 × 0.1 is a hair under or over 5
        check(r.seek(100) == 100);
        auto mid = r.between(2.05, 3.05);
        check(mid.size() == 10);
        check(mid.front().body == R"({"i":21})");
        check(r.of("act").size() == 50);
        auto part = r.slice(1.0, 2.0);
        check(part.size() == 10 && part.entries().front().time < 1e-9);
    });

    s.test("merge interleaves by time", [] {
        record::Session a, b;
        (void)a.add(0, "a", "{}");
        (void)a.add(2, "a", "{}");
        (void)b.add(0, "b", "{}");
        (void)b.add(0.5, "b", "{}");
        a.merge(b, 1.0);
        std::string kinds;
        for (const auto& e : a.entries()) kinds += e.kind;
        check(kinds == "abba");
        check(a.entries()[2].time == 1.5);
    });

    s.test("NDJSON reads back and writes the same bytes", [] {
        record::Session r;
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
        check(text.starts_with(R"({"format":"openmui.session@0.1","app":"org.example.notes")"));
        auto back = record::Session::parse(text);
        check(back.has_value(), back ? "" : back.error());
        check(back->ndjson() == text);
        check(back->entries()[1].time == 1.0 / 3.0, "times round-trip exactly");
        check(back->header.meta.size() == 2 && back->header.meta[1].second == "agent:qa");
        check(back->header.seed == 42 && back->header.scale == 2);
    });

    s.test("errors say where and what", [] {
        auto none = record::Session::parse("");
        check(!none && none.error().find("header") != std::string::npos);
        auto noheader = record::Session::parse(R"({"t":0,"kind":"x"})");
        check(!noheader && noheader.error().find("line 1") != std::string::npos);
        auto unordered = record::Session::parse("{\"format\":\"openmui.session@0.1\"}\n{\"t\":1,\"kind\":\"a\"}\n{\"t\":0.5,\"kind\":\"b\"}\n");
        check(!unordered && unordered.error().find("line 3") != std::string::npos, unordered ? "" : unordered.error());
        auto bad = record::Session::parse("{\"format\":\"openmui.session@0.1\"}\n\n{\"t\":\"soon\",\"kind\":\"a\"}\r\n");
        check(!bad && bad.error().find("line 3") != std::string::npos);
        auto blank = record::Session::parse("{\"format\":\"openmui.session@0.1\"}\r\n\r\n");
        check(blank.has_value() && blank->empty());
    });

    s.test("files", [] {
        auto dir = scratch_dir("record");
        record::Session r;
        (void)r.add(0.25, "note", R"({"text":"hello"})");
        auto file = dir / "a.session.omui.jsonl";
        check(r.save(file).has_value());
        auto back = record::Session::load(file);
        check(back.has_value() && back->ndjson() == r.ndjson());
        auto missing = record::Session::load(dir / "missing.session.omui.jsonl");
        check(!missing.has_value());
    });

    s.test("a session saves and loads under a name that is not ASCII", [] {
        record::Session r;
        check(r.add(0.25, "act", R"({"node":"main/保存"})").has_value());
        const auto file = scratch_dir("record") / std::filesystem::path(u8"会话 录制.session.omui.jsonl");
        check(r.save(file).has_value());
        auto back = record::Session::load(file);
        check(back.has_value() && back->size() == 1, back ? "" : back.error());
        auto missing = record::Session::load(scratch_dir("record") / std::filesystem::path(u8"没有.session.omui.jsonl"));
        check(!missing && missing.error().contains("没有"), missing ? "" : missing.error());
    });

    return s.run(argc, argv);
}
