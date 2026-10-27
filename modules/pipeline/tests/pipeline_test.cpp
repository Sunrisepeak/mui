// Pipelines build only when every read has a producer, types agree and
// nothing waits for itself; they run in a deterministic order, time and
// trace every stage, and take a replaced, added or removed stage between
// runs — or keep what they had.
import std;
import openmui.pipeline;
import openmui.trace;
import openmui.testing;

using namespace omui::testing;
namespace pl = omui::pipeline;

namespace {

struct Numbers {
    std::vector<int> values;
};
struct Text {
    std::string value;
};

}  // namespace

template <>
struct omui::pipeline::resource_type<Numbers> {
    static constexpr std::string_view name = "test.Numbers";
};
template <>
struct omui::pipeline::resource_type<Text> {
    static constexpr std::string_view name = "test.Text";
};

namespace {

// A stage made of three lists and a body.
struct Lambda final : pl::Stage {
    std::vector<pl::Port>                                     r, w;
    std::function<std::expected<void, std::string>(pl::Context&)> body;
    std::vector<pl::Port> reads() const override { return r; }
    std::vector<pl::Port> writes() const override { return w; }
    std::expected<void, std::string> run(pl::Context& c) override { return body(c); }
};

// Owns what the stages produce during a run.
struct World {
    Numbers numbers;
    Numbers doubled;
    Text    text;
    std::vector<std::string> log;
};

pl::Registry registry(World& w) {
    pl::Registry r;
    auto add = [&](std::string name, std::vector<pl::Port> reads, std::vector<pl::Port> writes,
                   std::function<std::expected<void, std::string>(pl::Context&, const pl::Config&)> body) {
        r.add({.name = name, .kind = "stage", .interface = "omui.pipeline.stage@1", .trace_category = "test"},
              [=, &w](const pl::Config& config) -> std::expected<std::unique_ptr<pl::Stage>, std::string> {
                  if (config.flag("refuse")) return std::unexpected("refused by its configuration");
                  auto s = std::make_unique<Lambda>();
                  s->r = reads;
                  s->w = writes;
                  s->body = [=, &w](pl::Context& c) {
                      w.log.push_back(name);
                      return body(c, config);
                  };
                  return s;
              });
    };
    add("count", {pl::port<Text>("seed")}, {pl::port<Numbers>("numbers")}, [&w](pl::Context& c, const pl::Config& config) {
        w.numbers.values.clear();
        const auto n = config.integer("n", 3);
        for (int i = 1; i <= n; ++i) w.numbers.values.push_back(i);
        c.resources.put("numbers", &w.numbers);
        return std::expected<void, std::string>{};
    });
    add("double", {pl::port<Numbers>("numbers")}, {pl::port<Numbers>("doubled")}, [&w](pl::Context& c, const pl::Config&) {
        w.doubled.values.clear();
        for (int v : c.resources.get<Numbers>("numbers")->values) w.doubled.values.push_back(2 * v);
        c.resources.put("doubled", &w.doubled);
        return std::expected<void, std::string>{};
    });
    add("bump", {pl::port<Numbers>("numbers")}, {pl::port<Numbers>("numbers")}, [](pl::Context& c, const pl::Config&) {
        for (int& v : c.resources.get<Numbers>("numbers")->values) v += 10;  // an updater
        return std::expected<void, std::string>{};
    });
    add("print", {pl::port<Numbers>("doubled")}, {pl::port<Text>("text")}, [&w](pl::Context& c, const pl::Config& config) {
        w.text.value = std::string(config.text("prefix", ""));
        for (int v : c.resources.get<Numbers>("doubled")->values) w.text.value += std::format("{} ", v);
        c.resources.put("text", &w.text);
        return std::expected<void, std::string>{};
    });
    add("wrongly-typed", {pl::port<Text>("numbers")}, {}, [](pl::Context&, const pl::Config&) { return std::expected<void, std::string>{}; });
    add("forgetful", {}, {pl::port<Text>("missing")}, [](pl::Context&, const pl::Config&) { return std::expected<void, std::string>{}; });
    add("failing", {}, {}, [](pl::Context&, const pl::Config&) { return std::expected<void, std::string>(std::unexpect, "it broke"); });
    return r;
}

const std::array<pl::Port, 1> inputs{pl::port<Text>("seed")};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"pipeline"};

    s.test("data decides the order; declaration breaks ties", [] {
        World w;
        auto r = registry(w);
        // Declared backwards: the build puts producers first.
        pl::Description d{"backwards", {{"print", "print", {}, {}}, {"double", "double", {}, {}}, {"count", "count", {}, {{"n", std::int64_t{4}}}}}};
        auto p = pl::Pipeline::build(d, r, inputs);
        check(p.has_value(), p ? "" : pl::to_text(p.error()));
        if (!p) return;
        check(p->order() == std::vector<std::string>{"count", "double", "print"});
        Text seed{"x"};
        pl::Resources res;
        res.put("seed", &seed);
        check(p->run(res).has_value());
        check_eq(w.text.value, std::string("2 4 6 8 "));
        check(p->timings().size() == 3 && p->timings()[0].name == "count");
        check(p->describe().contains("count → double → print") && p->describe().contains("writes doubled: test.Numbers"));
        check(p->produces("doubled") && p->produces("text") && !p->produces("seed"));
    });

    s.test("updaters: after the producer, readers before them see the old value", [] {
        World w;
        auto r = registry(w);
        // `double` is declared before `bump`, so it reads the numbers before they are bumped.
        pl::Description d{"update", {{"count", "count", {}, {}}, {"double", "double", {}, {}}, {"bump", "bump", {}, {}}, {"print", "print", {}, {}}}};
        auto p = pl::Pipeline::build(d, r, inputs);
        check(p.has_value(), p ? "" : pl::to_text(p.error()));
        if (!p) return;
        check(p->order() == std::vector<std::string>{"count", "double", "bump", "print"});
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        check(p->run(res).has_value());
        check_eq(w.text.value, std::string("2 4 6 "));
        check(w.numbers.values == std::vector<int>{11, 12, 13});
        // Declared the other way round, the doubling sees the bumped numbers.
        pl::Description e{"update2", {{"count", "count", {}, {}}, {"bump", "bump", {}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}};
        auto q = pl::Pipeline::build(e, r, inputs);
        check(q.has_value() && q->run(res).has_value());
        check_eq(w.text.value, std::string("22 24 26 "));
    });

    s.test("the build says what is wrong, and where", [] {
        World w;
        auto r = registry(w);
        auto problems = [&](pl::Description d) {
            auto p = pl::Pipeline::build(d, r, inputs);
            return p ? std::string() : pl::to_text(p.error());
        };
        check(problems({"a", {{"print", "print", {}, {}}}}).contains("reads `doubled` (test.Numbers), which no stage produces"));
        check(problems({"b", {{"count", "count", {}, {}}, {"t", "wrongly-typed", {}, {}}}}).contains("reads `numbers` as test.Text, but it is test.Numbers"));
        check(problems({"c", {{"count", "count", {}, {}}, {"again", "count", {}, {}}}}).contains("which stage `count` already produces"));
        check(problems({"d", {{"x", "nonesuch", {}, {}}}}).contains("no plugin `nonesuch`"));
        check(problems({"d", {{"x", "nonesuch", {}, {}}}}).contains("hint: stage plugins: count"));
        check(problems({"e", {{"x", "count", {}, {}}, {"x", "double", {}, {}}}}).contains("two stages have this name"));
        check(problems({"f", {{"count", "count", {}, {{"refuse", true}}}}}).contains("refused by its configuration"));
        check(problems({"g", {{"count", "count", {"print"}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}})
                  .contains("the stages wait for each other: "));
        check(problems({"h", {{"count", "count", {"nobody"}, {}}}}).contains("runs after `nobody`, which is not a stage"));
    });

    s.test("a run stops at the stage that fails, and checks what stages provide", [] {
        World w;
        auto r = registry(w);
        pl::Resources res;
        auto p = pl::Pipeline::build({"f", {{"fail", "failing", {}, {}}}}, r);
        check(p.has_value());
        auto out = p->run(res);
        check(!out && out.error() == "stage `fail` (failing): it broke");
        auto q = pl::Pipeline::build({"m", {{"forget", "forgetful", {}, {}}}}, r);
        auto out2 = q->run(res);
        check(!out2 && out2.error().contains("did not provide `missing`"));
    });

    s.test("replace, insert and remove between runs; a bad change keeps the pipeline", [] {
        World w;
        auto r = registry(w);
        auto p = pl::Pipeline::build({"live", {{"count", "count", {}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}}, r, inputs);
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        check(p->run(res).has_value());
        check(p->replace("print", "print", {{"prefix", std::string("= ")}}, r).has_value());
        check(p->run(res).has_value());
        check_eq(w.text.value, std::string("= 2 4 6 "));
        check(p->insert({"bump", "bump", {"count"}, {}}, r).has_value());
        check(p->run(res).has_value());
        check_eq(w.text.value, std::string("= 22 24 26 "));
        check(p->remove("bump", r).has_value());
        auto bad = p->replace("double", "wrongly-typed", {}, r);
        check(!bad, "an invalid replacement is refused");
        check(p->order() == std::vector<std::string>{"count", "double", "print"}, "and the pipeline is what it was");
        check(p->run(res).has_value());
        check_eq(w.text.value, std::string("= 2 4 6 "));
        check(!p->remove("nonesuch", r).has_value());
    });

    s.test("each stage is a span in its plugin's trace category", [] {
        World w;
        auto r = registry(w);
        auto p = pl::Pipeline::build({"traced", {{"count", "count", {}, {}}, {"double", "double", {}, {}}}}, r, inputs);
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        omui::trace::start();
        check(p->run(res).has_value());
        const auto t = omui::trace::stop();
        const auto stats = t.summary();
        check(std::ranges::any_of(stats, [](const auto& st) { return st.name == "count" && st.category == "test"; }));
        check(std::ranges::any_of(stats, [](const auto& st) { return st.name == "double" && st.category == "test"; }));
    });

    s.test("a description writes as <name>.pipeline.omui.toml", [] {
        pl::Description d{"desktop", {{"raster", "openmui.render.cpu", {"damage"}, {{"parallel", true}, {"rows_per_task", std::int64_t{64}}, {"gamma", 2.0}, {"label", std::string("a \"b\"")}}}}};
        const std::string toml = pl::to_toml(d);
        check_eq(toml, std::string("name = \"desktop\"\n\n[[stage]]\nname = \"raster\"\nplugin = \"openmui.render.cpu\"\nafter = [\"damage\"]\n"
                                   "config = { parallel = true, rows_per_task = 64, gamma = 2.0, label = \"a \\\"b\\\"\" }\n"));
    });

    s.test("resources are typed: a wrong type reads as nothing", [] {
        Numbers n;
        pl::Resources res;
        res.put("x", &n);
        check(res.get<Numbers>("x") == &n && res.get<Text>("x") == nullptr && res.get<Numbers>("y") == nullptr);
        check(res.has("x") && !res.has("y"));
    });

    return s.run(argc, argv);
}
