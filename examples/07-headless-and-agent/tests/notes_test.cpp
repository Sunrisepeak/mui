// Example 07: the notes app at the headless levels, driven by an agent
// script, with the irreversible action stopped until answered, and a
// recorded session replayed to the same tree.
import std;
import omui;
import mlibs.testing;
import mcpplibs.mtest;
import notes;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

struct App {
    notes::Model model;
    std::unique_ptr<Headless> host;

    explicit App(HeadlessLevel level) {
        register_base_components();
        AppSpec spec = notes::app(model);
        HeadlessOptions o;
        o.level = level;
        o.size = spec.size;
        host = std::make_unique<Headless>(o);
        for (VerbSpec& v : spec.verbs) (void)host->runtime().verbs().add(std::move(v.desc), std::move(v.fn));
        host->mount("main", spec.view());
        host->settle();
    }
};

std::string essence(semantic::Tree& t) {
    std::string out;
    for (const auto& n : t.snapshot().nodes) {
        out += std::format("{} {} '{}' {}", n.id, spec::name(n.role), n.name, n.value.json());
        for (const auto& a : n.actions) out += std::format(" {}:{}", a.name, spec::name(a.effect));
        out += '\n';
    }
    return out;
}

}  // namespace

UTest cases{"example.headless-and-agent", [] {

    "H0: the semantic tree alone, operated by id"_test = [] {
        App a(HeadlessLevel::semantics);
        semantic::Tree& t = a.host->tree();
        expect(t.find({.role = Role::checkbox}).size() == 2);
        expect(a.host->click("main/list/n1/star"));
        expect(a.model.notes.peek()[0].starred) << "starred by activating it";
        expect(!t.node("main/compose/add")->bounds) << "no layout at H0";
        expect(t.violations().empty()) << std::format("{} violations", t.violations().size());
    };

    "H1: places and hit testing, the same tree"_test = [] {
        App h0(HeadlessLevel::semantics), h1(HeadlessLevel::layout);
        expect(essence(h0.host->tree()) == essence(h1.host->tree()));
        const Rect add = h1.host->runtime().bounds(h1.host->runtime().find("main/compose/add"));
        expect(!add.empty() && h1.host->runtime().hit_test(add.center()) == h1.host->runtime().find("main/compose/add"));
        // Starred only hides the unstarred note.
        expect(h1.host->click("main/starred-only"));
        h1.host->settle();
        expect(h1.host->tree().find({.role = Role::checkbox}).size() == 1);
        expect(!h1.host->runtime().valid(h1.host->runtime().find("main/list/n1")));
    };

    "H2: the picture"_test = [] {
        App a(HeadlessLevel::pixels);
        expect(a.host->save_png(scratch_dir("notes") / "notes.png").has_value());
        const Rect del = a.host->runtime().bounds(a.host->runtime().find("main/delete-all"));
        const auto& px = a.host->pixels();
        const auto c = px.at(static_cast<int>(del.x + 6), static_cast<int>(del.center().y));
        expect(c.r > 180 && c.g < 80) << "the danger colour";
    };

    "an agent script, with the irreversible action stopped until answered (S-1, S-2)"_test = [] {
        App a(HeadlessLevel::pixels);
        agent::Protocol p(a.host->runtime(), {.app = "org.openmui.examples.notes"});
        std::ifstream in(here() / "data" / "delete-all.agent.omui.jsonl");
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            ++n;
            auto req = parse_json(line).value();
            const Value wanted = *req.get("expect");
            std::erase_if(*req.map(), [](const auto& kv) { return kv.first == "expect"; });
            std::vector<Value> records;
            p.request(req.json(), [&](std::string_view l) { records.push_back(parse_json(l).value()); });
            for (int i = 0; i < 300 && records.empty(); ++i) {  // a wait: frames until it is answered
                a.host->step();
                p.poll();
            }
            expect(!records.empty()) << std::format("line {}: no answer", n);
            if (records.empty()) continue;
            const std::int64_t exit = records.back().get("exitCode")->as_int(-1);
            expect(exit == wanted.get("exit")->as_int()) << std::format("line {}: exit {} for {}", n, exit, line);
            if (const Value* code = wanted.get("code"))
                expect(records.front().get("code") && records.front().get("code")->as_string() == code->as_string()) << std::format("line {}: {}", n, records.front().json());
            if (const Value* count = wanted.get("nodes")) {
                const Value* nodes = records.front().get("data") ? records.front().get("data")->get("nodes") : nullptr;
                expect(nodes && nodes->list() && static_cast<std::int64_t>(nodes->list()->size()) == count->as_int()) << std::format("line {}: {}", n, records.front().json());
            }
            a.host->step();
        }
        expect(n == 11) << std::format("{} lines", n);
        expect(a.model.notes.peek().empty()) << "deleted, once answered";
        const auto journal = a.host->runtime().action_journal();
        const auto stopped = std::ranges::count_if(journal, [](const ActionEntry& e) { return e.exit_code == spec::exit_code::needs_answer; });
        expect(stopped == 1) << "the unanswered attempt is in the journal";
    };

    "a recorded session replays exactly"_test = [] {
        mlibs::session::Session session;
        std::uint64_t end = 0;
        {
            App a(HeadlessLevel::pixels);
            semantic::Recorder rec(a.host->tree(), session);
            Invocation agent;
            agent.actor = {"agent", "recorder", {}};
            rec.act("main/compose/draft", "set_value", Invocation{Value{}.set("value", "Recorded note"), {}, agent.actor});
            rec.act("main/compose/add", "activate", agent);
            rec.frame({seconds(1), {}});
            rec.act("main/list/n3/star", "toggle", agent);
            rec.frame({seconds(1.5), {}});
            rec.checkpoint("after starring");
            end = a.host->tree().digest();
        }
        App b(HeadlessLevel::pixels);
        auto r = semantic::replay(b.host->tree(), session);
        expect(r.ok()) << std::format("{}", r.mismatches);
        expect(r.digest == end && b.model.notes.peek().size() == 3 && b.model.notes.peek()[2].starred);
    };
}};
