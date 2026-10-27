// The tally counter, written twice: the C++ view and the view document make
// one semantic tree and the same pixels before and after every operation (CTS
// S level); the document passes `omui check` clean and is in the canonical
// format; saving the file shows at once and keeps the count.
import std;
import openmui;
import tally;
import openmui.testing;
import openmui.abi;
import openmui.lang;
import openmui.lang.lua;
import openmui.lang.js;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path document() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "ui" / "tally.view.omui.json";
}

// A version of the example: lua/tally.lua, ts/tally.ts.
std::filesystem::path script_file(std::string_view relative) {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / relative;
}

std::string text_of(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void collect(Headless& h, NodeId n, std::vector<spec::SemanticNode>& out) {
    out.push_back(h.runtime().semantic_node(n));
    for (NodeId c : h.runtime().children(n)) collect(h, c, out);
}

std::vector<spec::SemanticNode> tree(Headless& h) {
    std::vector<spec::SemanticNode> out;
    collect(h, h.runtime().find("main"), out);
    return out;
}

std::uint64_t pixels(Headless& h) {
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.value();
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.settle();
    return e.exit_code().value_or(-1);
}

struct Pair {
    Headless code{HeadlessOptions{.size = {360, 300}}};
    Headless doc{HeadlessOptions{.size = {360, 300}}};
    tally::State state;
    std::optional<tally::Document> document;

    Pair() {
        code.mount("main", tally::view(state));
        auto d = tally::Document::open(doc.runtime(), ::document());
        check(d.has_value(), d ? "" : d.error());
        document = std::move(*d);
        document->view().mount(doc.runtime(), "main");
        code.settle();
        doc.settle();
    }

    void same(std::string_view when, std::source_location where = std::source_location::current()) {
        const auto a = tree(code), b = tree(doc);
        if (a != b) {
            for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                if (!(a[i] == b[i])) {
                    fail(std::format("{}: {} differs ({} / {})", when, a[i].id, a[i].name, b[i].name), where);
                    return;
                }
            fail(std::format("{}: {} nodes against {}", when, a.size(), b.size()), where);
        }
        check(pixels(code) == pixels(doc), std::format("{}: the pixels", when), where);
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.counter"};
    register_base_components();

    s.test("the C++ view and the document are one tree, before and after each operation", [] {
        Pair p;
        p.same("at the start");
        for (Headless* h : {&p.code, &p.doc}) {
            check(h->click("main/controls/plus"));
            h->settle();
            check(h->click("main/controls/plus"));
            h->settle();
        }
        p.same("after two clicks");
        for (Headless* h : {&p.code, &p.doc}) check_eq(act(*h, "main/controls/minus", "activate"), std::int64_t{0});
        p.same("after an agent's −1");
        for (Headless* h : {&p.code, &p.doc}) check_eq(act(*h, "main/goal/value", "set_value", Value(Value::Map{{"value", Value(1)}})), std::int64_t{0});
        p.same("with the goal reached");
        check_eq(p.doc.tree().node("main/status")->name, std::string("Goal of 1 reached"));
        for (Headless* h : {&p.code, &p.doc}) check_eq(act(*h, "main/controls/reset", "activate"), std::int64_t{0});
        p.same("after a reset");
        check(p.doc.tree().node("main/controls/minus")->states.has(spec::State::disabled), "nothing to take away");
        check_eq(p.state.count.peek(), 0);
        check(p.document->view().signal("goal")->peek() == Value(1));
    });

    // A script version against the C++ one, through every operation.
    auto script_version = [](std::string_view language, auto make_script, std::string code, std::string name) {
        Headless code_host(HeadlessOptions{.size = {360, 300}});
        Headless script_host(HeadlessOptions{.size = {360, 300}});
        tally::State state;
        code_host.mount("main", tally::view(state));
        auto handle = abi::attach(script_host.runtime());
        {
            lang::Host host(handle);
            lang::Session session(host);
            auto script = make_script(session);
            std::vector<std::string> errors;
            script->on_error([&](std::string_view m) { errors.emplace_back(m); });
            auto ran = script->run(std::move(code), std::move(name));
            while (ran.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                script_host.step();
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            auto result = ran.get();
            check(result.has_value(), result ? "" : result.error().text());
            auto settle = [&] {
                for (int i = 0; i < 2000 && !host.idle(); ++i) {
                    script_host.step();
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                script_host.settle();
                code_host.settle();
            };
            settle();
            auto same = [&](std::string_view when) {
                const auto a = tree(code_host), b = tree(script_host);
                if (a != b) {
                    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                        if (!(a[i] == b[i])) {
                            fail(std::format("{} {}: {} differs ({} / {})", language, when, a[i].id, a[i].name, b[i].name));
                            return;
                        }
                    fail(std::format("{} {}: {} nodes against {}", language, when, a.size(), b.size()));
                }
                check(pixels(code_host) == pixels(script_host), std::format("{} {}: the pixels", language, when));
            };
            same("at the start");
            for (std::string_view op : {"plus", "plus", "minus"}) {
                for (Headless* h : {&code_host, &script_host})
                    check_eq(act(*h, std::format("main/controls/{}", op), "activate"), std::int64_t{0});
                settle();
            }
            same("after +1, +1, −1");
            for (Headless* h : {&code_host, &script_host})
                check_eq(act(*h, "main/goal/value", "set_value", Value(Value::Map{{"value", Value(1)}})), std::int64_t{0});
            settle();
            same("with the goal reached");
            check(errors.empty(), errors.empty() ? "" : errors[0]);
        }
        abi::detach(handle);
    };

    s.test("the Lua version is the same tree and pixels, before and after each operation", [&] {
        script_version("Lua", [](lang::Session& s) { return std::make_unique<lang::lua::Script>(s); }, text_of(script_file("lua/tally.lua")),
                       "@lua/tally.lua");
    });

    s.test("the TypeScript version is the same tree and pixels, before and after each operation", [&] {
        auto js = lang::js::compile_typescript(script_file("ts/tally.ts"));
        check(js.has_value(), js ? "" : js.error());
        if (!js) return;
        script_version("TypeScript", [](lang::Session& s) { return std::make_unique<lang::js::Script>(s); }, *js, "ts/tally.ts");
    });

    s.test("the document passes omui check clean and is in the canonical format", [] {
        const std::string text = text_of(document());
        const auto r = viewdoc::read(text, {.file = "tally.view.omui.json"});
        for (const auto& d : r.diagnostics) fail(d.text("tally.view.omui.json"));
        check(r.ok());
        auto formatted = viewdoc::format_document(text);
        check(formatted && *formatted == text, "omui fmt would change it");
    });

    s.test("saving the document shows at once and keeps the count", [] {
        const auto dir = scratch_dir("example-counter");
        const auto file = dir / "tally.view.omui.json";
        std::filesystem::copy_file(document(), file, std::filesystem::copy_options::overwrite_existing);
        Headless h(HeadlessOptions{.size = {360, 300}});
        auto d = tally::Document::open(h.runtime(), file);
        check(d.has_value());
        if (!d) return;
        d->start(h.runtime());
        h.settle(seconds(0.05));
        check(h.click("main/controls/plus"));
        h.settle(seconds(0.05));
        check(h.click("main/controls/plus"));
        h.settle(seconds(0.05));
        std::string text = text_of(file);
        text.replace(text.find("\"+1\""), 4, "\"Add one\"");
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << text;
        }
        // A later modification time than the copy's, whatever the file system's resolution.
        std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
        const auto start = std::chrono::steady_clock::now();
        h.advance(seconds(0.15));  // the watcher checks every 100 ms
        const auto took = std::chrono::steady_clock::now() - start;
        check_eq(h.tree().node("main/controls/plus")->name, std::string("Add one"));
        check_eq(h.tree().node("main/count")->name, std::string("2"));  // the count stays
        check(took < std::chrono::milliseconds(200), std::format("reloading took {}", std::chrono::duration_cast<std::chrono::milliseconds>(took)));
        // A broken save changes nothing and says why.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << R"({"omui": "0.1", "view": {"root": {"type": "colum"}}})";
        }
        std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(4));
        h.advance(seconds(0.15));
        check(d->status().find("did you mean `column`?") != std::string::npos, d->status());
        check_eq(h.tree().node("main/count")->name, std::string("2"));
    });

    s.test("pixels", [] {
        Headless h(HeadlessOptions{.size = {360, 300}});
        tally::State st;
        st.count.set(7);
        h.mount("main", tally::view(st));
        h.settle();
        const std::uint64_t got = pixels(h);
        std::println("example.counter pixels {:016x}", got);
        check(h.save_png(scratch_dir("example-counter-png") / "tally.png").has_value());
        check(got == 0x210b1831433cf1b2ULL, std::format("pixels {:016x}", got));
    });

    return s.run(argc, argv);
}
