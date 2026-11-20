// Glade at the runtime (M6.5 T5.8b): the valley as a game the agent kernel serves, poked by a hand. The game says
// its controls and its tasks, and its tasks do what they say; an agent that reads only the view — what each thing
// is and the goals, no picture — meets the goals by trying the hand on branches of the future and doing the first
// that works, and each answer says why, back to the hand.
import std;
import mlibs.agent;
import mlibs.json;
import openas;
import omui.showcase.glade.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or(""), out.text = record;
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed)), out.text = record;
    });
    return out;
}
Answered told(agent::SubjectSession& session, std::string_view line, std::source_location at = std::source_location::current()) {
    Answered out = ask(session, line);
    expect(fatal(out.data.has_value() && out.error.empty()), at) << line << " → " << out.text.substr(0, 400);
    return out;
}

// An agent in the seat, the game stepped by it, shown everything (it may ask why).
meng::runtime::Options assist() {
    return {.tier = {.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true},
            .stepping = meng::runtime::Stepping::agent,
            .seat = meng::runtime::Seat::agent,
            .keep = 600};
}

// What the agent reads of a view.
struct Thing {
    std::string name, material;
    double      x = 0, y = 0, z = 0;
};
struct Goal {
    std::string              name, goal;
    std::vector<std::string> about, verbs;
    std::optional<double>    x_below;
    bool                     done = false;
};
struct View {
    std::vector<Thing> things;
    std::vector<Goal>  goals;
};
View read(const mlibs::json::Node& view) {
    View out;
    for (const auto& one : view["things"].items()) {
        const auto at = one["at"].items();
        out.things.push_back({std::string(one["name"].text().value_or("")), std::string(one["material"].text().value_or("")), at[0].number().value_or(0),
                              at[1].number().value_or(0), at[2].number().value_or(0)});
    }
    for (const auto& one : view["goals"].items()) {
        Goal goal{std::string(one["name"].text().value_or("")), std::string(one["goal"].text().value_or(""))};
        for (const auto& name : one["about"].items()) goal.about.emplace_back(name.text().value_or(""));
        for (const auto& verb : one["verbs"].items()) goal.verbs.emplace_back(verb.text().value_or(""));
        goal.x_below = one["x_below"].number();
        goal.done = one["done"].boolean() == true;
        out.goals.push_back(std::move(goal));
    }
    return out;
}
const Goal& goal_named(const View& view, std::string_view name) {
    static const Goal none;
    const auto found = std::ranges::find(view.goals, name, &Goal::name);
    return found == view.goals.end() ? none : *found;
}

// A plan: the hand's first action, what follows it (each so many steps in), and how long to watch.
struct Plan {
    std::string                              said;
    std::string                              first;  // the members of the first world.act
    std::vector<std::pair<int, std::string>> then;
    int                                      steps = 0;
};
std::string pointer(double x, double z) {
    const auto at = glade::pointer_at({static_cast<float>(x), 0, static_cast<float>(z)});
    return std::format(R"("pointer":[{:.5f},{:.5f}])", at[0], at[1]);
}

// The plans a goal's verbs allow, from what is seen: a torch held to each thing that burns, nearest the goal's
// things first, for longer and longer; and, where pushing is allowed, a torch to a thing that burns and then the
// goal's thing shoved toward it. Only where the hand may act.
std::vector<Plan> plans(const View& view, const Goal& goal) {
    const auto allowed = [&](const Thing& thing) { return !goal.x_below || thing.x < *goal.x_below; };
    const auto burns = [](const Thing& thing) { return thing.material == "wood" || thing.material == "grass" || thing.material == "leaves" || thing.material == "bark"; };
    const auto distance = [&](const Thing& thing) {
        double best = 1e9;
        for (const std::string& name : goal.about)
            if (const auto it = std::ranges::find(view.things, name, &Thing::name); it != view.things.end())
                best = std::min(best, std::hypot(thing.x - it->x, thing.z - it->z));
        return best;
    };
    std::vector<Thing> near;
    for (const Thing& thing : view.things)
        if (allowed(thing) && (burns(thing) || std::ranges::contains(goal.about, thing.name))) near.push_back(thing);
    std::ranges::stable_sort(near, {}, distance);
    if (near.size() > 4) near.resize(4);
    const bool torch = std::ranges::contains(goal.verbs, "torch"), push = std::ranges::contains(goal.verbs, "push");
    std::vector<Plan> out;
    // Nearest first, and on each a torch held longer before the next is tried.
    for (const Thing& thing : near)
        for (const int seconds : {1, 3, 6})
            if (torch)
                out.push_back({std::format("a torch to {} for {} s", thing.name, seconds), std::format(R"("buttons":[0],{})", pointer(thing.x, thing.z)),
                               {{seconds * 60, "{}"}}, seconds * 60 + 600});
    if (torch && push)
        for (const std::string& name : goal.about) {
            const auto mover = std::ranges::find(view.things, name, &Thing::name);
            if (mover == view.things.end()) continue;
            for (const Thing& fire : near) {
                if (!burns(fire)) continue;
                const double dx = fire.x - mover->x, dz = fire.z - mover->z, length = std::max(std::hypot(dx, dz), 1e-6);
                out.push_back({std::format("a torch to {} for 2 s, then {} pushed toward it", fire.name, name), std::format(R"("buttons":[0],{})", pointer(fire.x, fire.z)),
                               {{120, "{}"}, {121, std::format(R"({{"buttons":[2],"axis":[{:.3f},{:.3f}],{}}})", dx / length, dz / length, pointer(mover->x, mover->z))}, {122, "{}"}},
                               1800});
            }
        }
    return out;
}

// A plan tried on a branch: whether the goal would be met, and the future's hash.
struct Tried {
    bool        met = false;
    std::string hash;
};
// The goal is watched along the branch (every half second), not only at its end: a fire that catches and burns out
// met it.
Tried tried(agent::SubjectSession& session, const Plan& plan, std::string_view goal) {
    const View now = read(told(session, R"({"op":"world.observe"})")["view"]);
    const auto index = std::ranges::find(now.goals, goal, &Goal::name) - now.goals.begin();
    std::string then;
    for (const auto& [after, act] : plan.then) {
        std::string members = act == "{}" ? std::string() : act.substr(1, act.size() - 2);
        then += std::format(R"({}{{"after":{}{}{}}})", then.empty() ? "" : ",", after, members.empty() ? "" : ",", members);
    }
    const auto began = std::chrono::steady_clock::now();
    const auto future = told(session, std::format(R"({{"op":"world.branch","steps":{},{},"then":[{}],"watch":"/goals/{}/done","every":30}})", plan.steps, plan.first, then, index));
    std::println("  tried {} ({} steps): {:.1f} s", plan.said, plan.steps, std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
    const View view = read(future["view"]);
    bool met = goal_named(view, goal).done;
    for (const auto& seen : future["watched"].items()) met = met || seen["value"].boolean() == true;
    return {met, std::string(future["hash"].text().value_or(""))};
}

// A plan done for real: the same actions at the same steps, then steps until the goal is met (at most the plan's).
void done(agent::SubjectSession& session, const Plan& plan, std::string_view goal) {
    told(session, std::format(R"({{"op":"world.act",{}}})", plan.first));
    int played = 0;
    for (const auto& [after, act] : plan.then) {
        told(session, std::format(R"({{"op":"world.step","steps":{}}})", after - played));
        played = after;
        told(session, std::format(R"({{"op":"world.act"{}{}}})", act == "{}" ? "" : ",", act == "{}" ? "" : act.substr(1, act.size() - 2)));
    }
    while (played < plan.steps && !goal_named(read(told(session, R"({"op":"world.observe"})")["view"]), goal).done) {
        const int more = std::min(60, plan.steps - played);
        told(session, std::format(R"({{"op":"world.step","steps":{}}})", more));
        played += more;
    }
}

// A goal met: already, or by the first plan whose branch meets it, done; what was done, said.
std::optional<Plan> meet(agent::SubjectSession& session, std::string_view goal, int& branches) {
    const View view = read(told(session, R"({"op":"world.observe"})")["view"]);
    if (goal_named(view, goal).done) return Plan{"already met"};
    for (const Plan& plan : plans(view, goal_named(view, goal))) {
        ++branches;
        if (!tried(session, plan, goal).met) continue;
        done(session, plan, goal);
        return plan;
    }
    return std::nullopt;
}

}  // namespace

UTest cases{"glade.headless", [] {
    "the game says what its controls mean and what to begin with; its tasks, sent as written, do what they say (D-186)"_test = [] {
        glade::Standalone game(glade::begin(), assist());
        agent::SubjectSession session({&game});
        const auto state = told(session, R"({"op":"world.state"})");
        expect(eq(state["controls"].size(), std::size_t{15}) && state["controls"][1].dump().contains(R"("name":"torch")")) << state["controls"].dump().substr(0, 300);
        const auto described = told(session, R"({"op":"describe"})");
        expect(described.text.contains("light-the-campfire") && described.text.contains("pocket valley")) << "the game's own words and tasks";
        // The task as written: a torch held to the campfire for three seconds.
        told(session, R"({"op":"world.act","buttons":[0],"pointer":[0.32727,0.125]})");
        told(session, R"({"op":"world.step","steps":180})");
        told(session, R"({"op":"world.act"})");
        told(session, R"({"op":"world.step","steps":120})");
        const View view = read(told(session, R"({"op":"world.observe"})")["view"]);
        expect(goal_named(view, "campfire").done) << "the campfire burns";
    };

    "the same hand is the same valley: a branch's future is the game's when it is done for real"_test = [] {
        glade::Standalone game(glade::begin(), assist());
        agent::SubjectSession session({&game});
        const Plan plan{"a bucket over the ice, then a push", R"("buttons":[1],"pointer":[0.40909,0.125])", {{30, "{}"}, {31, R"({"buttons":[2],"axis":[-1,0],"pointer":[0.40909,0.125]})"}, {32, "{}"}}, 240};
        const Tried ahead = tried(session, plan, "ice");
        done(session, plan, "none");  // no goal of that name: the whole plan is played
        expect(eq(told(session, R"({"op":"world.state"})")["hash"].text().value_or(""), std::string_view(ahead.hash))) << "the branch and the game agree, bit for bit";
    };

    "an agent meets the goals from the view alone, trying the hand on branches; each answer says why, back to the hand"_test = [] {
        int branches = 0;
        // Each goal from the valley as it begins (the campfire's after ten seconds of rain), by an agent shown everything.
        const auto solve = [&](std::string_view goal, std::initializer_list<std::string_view> things, bool rain_first) {
            glade::Standalone game(glade::begin(), assist());
            agent::SubjectSession session({&game}, {.actor = {openas::Kind::agent, "the one who plays", {}}});
            if (rain_first) {
                for (const char* line : {R"({"op":"world.act","buttons":[3]})", R"({"op":"world.step","steps":1})", R"({"op":"world.act"})", R"({"op":"world.step","steps":600})",
                                         R"({"op":"world.act","buttons":[3]})", R"({"op":"world.step","steps":1})", R"({"op":"world.act"})"})
                    told(session, line);
            }
            const auto plan = meet(session, goal, branches);
            expect(plan.has_value() && plan->said != "already met") << goal << ": met by the hand";
            const Answered observed = told(session, R"({"op":"world.observe"})");
            const auto why = observed["view"]["why"];
            std::string thing, said;
            for (const std::string_view one : things)
                if (const auto text = why[one].text(); text && !text->empty()) {
                    thing = one, said = *text;
                    break;
                }
            std::println("{}: {} — why {}:\n{}", goal, plan ? plan->said : std::string("not met"), thing, said.substr(0, 700));
            expect(said.contains("done from outside")) << goal << ": the answer runs back to the hand";
        };
        solve("across", {"crate"}, false);
        solve("campfire", {"log 1", "log 2", "log 3"}, true);
        solve("ice", {"ice"}, false);
        std::println("{} branches tried for three goals", branches);
    };
}};
