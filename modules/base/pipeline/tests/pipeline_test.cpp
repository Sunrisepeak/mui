// Pipelines build only when every read has a producer, types agree and
// nothing waits for itself; they run in a deterministic order, time and
// trace every stage, and take a replaced, added or removed stage between
// runs — or keep what they had.
import std;
import mlibs.pipeline;
import mlibs.trace;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace pl = mlibs::pipeline;

namespace {

struct Numbers {
    std::vector<int> values;
};
struct Text {
    std::string value;
};

}  // namespace

template <>
struct mlibs::pipeline::resource_type<Numbers> {
    static constexpr std::string_view name = "test.Numbers";
};
template <>
struct mlibs::pipeline::resource_type<Text> {
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
        (void)r.add({.name = name, .kind = "stage", .interface = "omui.pipeline.stage@1", .trace_category = "test", .cost = pl::Cost{.cpu_us = 10}},
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

UTest cases{"pipeline", [] {

    "data decides the order; declaration breaks ties"_test = [] {
        World w;
        auto r = registry(w);
        // Declared backwards: the build puts producers first.
        pl::Description d{"backwards", {{"print", "print", {}, {}}, {"double", "double", {}, {}}, {"count", "count", {}, {{"n", std::int64_t{4}}}}}};
        auto p = pl::Pipeline::build(d, r, inputs);
        expect(p.has_value()) << (p ? "" : pl::to_text(p.error()));
        if (!p) return;
        expect(p->order() == std::vector<std::string>{"count", "double", "print"});
        Text seed{"x"};
        pl::Resources res;
        res.put("seed", &seed);
        expect(p->run(res).has_value());
        expect(eq(w.text.value, std::string("2 4 6 8 ")));
        expect(p->timings().size() == 3 && p->timings()[0].name == "count");
        expect(p->describe().contains("count → double → print") && p->describe().contains("writes doubled: test.Numbers"));
        expect(p->produces("doubled") && p->produces("text") && !p->produces("seed"));
    };

    "updaters: after the producer, readers before them see the old value"_test = [] {
        World w;
        auto r = registry(w);
        // `double` is declared before `bump`, so it reads the numbers before they are bumped.
        pl::Description d{"update", {{"count", "count", {}, {}}, {"double", "double", {}, {}}, {"bump", "bump", {}, {}}, {"print", "print", {}, {}}}};
        auto p = pl::Pipeline::build(d, r, inputs);
        expect(p.has_value()) << (p ? "" : pl::to_text(p.error()));
        if (!p) return;
        expect(p->order() == std::vector<std::string>{"count", "double", "bump", "print"});
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        expect(p->run(res).has_value());
        expect(eq(w.text.value, std::string("2 4 6 ")));
        expect(w.numbers.values == std::vector<int>{11, 12, 13});
        // Declared the other way round, the doubling sees the bumped numbers.
        pl::Description e{"update2", {{"count", "count", {}, {}}, {"bump", "bump", {}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}};
        auto q = pl::Pipeline::build(e, r, inputs);
        expect(q.has_value() && q->run(res).has_value());
        expect(eq(w.text.value, std::string("22 24 26 ")));
    };

    "the build says what is wrong, and where"_test = [] {
        World w;
        auto r = registry(w);
        auto problems = [&](pl::Description d) {
            auto p = pl::Pipeline::build(d, r, inputs);
            return p ? std::string() : pl::to_text(p.error());
        };
        expect(problems({"a", {{"print", "print", {}, {}}}}).contains("reads `doubled` (test.Numbers), which no stage produces"));
        expect(problems({"b", {{"count", "count", {}, {}}, {"t", "wrongly-typed", {}, {}}}}).contains("reads `numbers` as test.Text, but it is test.Numbers"));
        expect(problems({"c", {{"count", "count", {}, {}}, {"again", "count", {}, {}}}}).contains("which stage `count` already produces"));
        expect(problems({"d", {{"x", "nonesuch", {}, {}}}}).contains("no plugin `nonesuch`"));
        expect(problems({"d", {{"x", "nonesuch", {}, {}}}}).contains("hint: stage plugins: count"));
        expect(problems({"e", {{"x", "count", {}, {}}, {"x", "double", {}, {}}}}).contains("two stages have this name"));
        expect(problems({"f", {{"count", "count", {}, {{"refuse", true}}}}}).contains("refused by its configuration"));
        expect(problems({"g", {{"count", "count", {"print"}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}})
                  .contains("the stages wait for each other: "));
        expect(problems({"h", {{"count", "count", {"nobody"}, {}}}}).contains("runs after `nobody`, which is not a stage"));
    };

    "a run stops at the stage that fails, and checks what stages provide"_test = [] {
        World w;
        auto r = registry(w);
        pl::Resources res;
        auto p = pl::Pipeline::build({"f", {{"fail", "failing", {}, {}}}}, r);
        expect(p.has_value());
        auto out = p->run(res);
        expect(!out && out.error() == "stage `fail` (failing): it broke");
        auto q = pl::Pipeline::build({"m", {{"forget", "forgetful", {}, {}}}}, r);
        auto out2 = q->run(res);
        expect(!out2 && out2.error().contains("did not provide `missing`"));
    };

    "replace, insert and remove between runs; a bad change keeps the pipeline"_test = [] {
        World w;
        auto r = registry(w);
        auto p = pl::Pipeline::build({"live", {{"count", "count", {}, {}}, {"double", "double", {}, {}}, {"print", "print", {}, {}}}}, r, inputs);
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        expect(p->run(res).has_value());
        expect(p->replace("print", "print", {{"prefix", std::string("= ")}}, r).has_value());
        expect(p->run(res).has_value());
        expect(eq(w.text.value, std::string("= 2 4 6 ")));
        expect(p->insert({"bump", "bump", {"count"}, {}}, r).has_value());
        expect(p->run(res).has_value());
        expect(eq(w.text.value, std::string("= 22 24 26 ")));
        expect(p->remove("bump", r).has_value());
        auto bad = p->replace("double", "wrongly-typed", {}, r);
        expect(!bad) << "an invalid replacement is refused";
        expect(p->order() == std::vector<std::string>{"count", "double", "print"}) << "and the pipeline is what it was";
        expect(p->run(res).has_value());
        expect(eq(w.text.value, std::string("= 2 4 6 ")));
        expect(!p->remove("nonesuch", r).has_value());
    };

    "each stage is a span in its plugin's trace category"_test = [] {
        World w;
        auto r = registry(w);
        auto p = pl::Pipeline::build({"traced", {{"count", "count", {}, {}}, {"double", "double", {}, {}}}}, r, inputs);
        Text seed;
        pl::Resources res;
        res.put("seed", &seed);
        mlibs::trace::start();
        expect(p->run(res).has_value());
        const auto t = mlibs::trace::stop();
        const auto stats = t.summary();
        expect(std::ranges::any_of(stats, [](const auto& st) { return st.name == "count" && st.category == "test"; }));
        expect(std::ranges::any_of(stats, [](const auto& st) { return st.name == "double" && st.category == "test"; }));
    };

    "a description writes as <name>.pipeline.omui.toml"_test = [] {
        pl::Description d{"desktop", {{"raster", "openmui.render.cpu", {"damage"}, {{"parallel", true}, {"rows_per_task", std::int64_t{64}}, {"gamma", 2.0}, {"label", std::string("a \"b\"")}}}}};
        const std::string toml = pl::to_toml(d);
        expect(eq(toml, std::string("name = \"desktop\"\n\n[[stage]]\nname = \"raster\"\nplugin = \"openmui.render.cpu\"\nafter = [\"damage\"]\n"
                                   "config = { parallel = true, rows_per_task = 64, gamma = 2.0, label = \"a \\\"b\\\"\" }\n")));
    };

    "resources are typed: a wrong type reads as nothing"_test = [] {
        Numbers n;
        pl::Resources res;
        res.put("x", &n);
        expect(res.get<Numbers>("x") == &n && res.get<Text>("x") == nullptr && res.get<Numbers>("y") == nullptr);
        expect(res.has("x") && !res.has("y"));
    };

    "a stage declares its cost and its tiers, or is not registered (performance tiers §3); the tiers as bits"_test = [] {
        pl::Registry r;
        auto none = r.add({.name = "acme.free", .kind = "stage"}, [](const pl::Config&) -> std::expected<std::unique_ptr<pl::Stage>, std::string> { return nullptr; });
        expect(!none.has_value() && none.error().contains("declares its cost")) << "refused without a cost";
        expect(r.info("acme.free") == nullptr) << "not there";
        expect(r.add({.name = "acme.tool", .kind = "tool"}, [](const pl::Config&) -> std::expected<std::unique_ptr<pl::Stage>, std::string> { return nullptr; })
                  .has_value()) << "a kind that is not costed needs none";
        const pl::Cost glass{.unit = "a megapixel", .gpu_us = 400, .tiers = pl::tiers_from(pl::Tier::t2), .degrade = {"frost", ""}};
        expect(r.add({.name = "acme.glass", .kind = "stage", .cost = glass}, [](const pl::Config&) -> std::expected<std::unique_ptr<pl::Stage>, std::string> {
                   return nullptr;
               }).has_value());
        expect(r.info("acme.glass")->cost->runs_on(pl::Tier::t3) && !glass.runs_on(pl::Tier::t1)) << "from T2 up";
        expect(eq(pl::tiers_from(pl::Tier::t0), pl::all_tiers));
        expect(eq(pl::tiers_from(pl::Tier::t3), std::uint8_t{0x18}));
        expect(glass.within(700, 2) && !glass.within(900, 2)) << "a megapixel and a half: within; more than it said: not";
        expect(eq(pl::name(pl::Tier::t4), std::string_view("T4")));
    };
}};
