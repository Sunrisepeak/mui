// Mui on a page (the site plan §9, D1): it sits at the page's bottom; when a
// visitor's agent acts it goes there — walking, hopping up onto what it may
// stand on — its eyes lit, and points; while the agent waits for the
// person it holds up a card; asked, it points, says a line, sleeps and
// wakes; less motion asked, it changes frames in place and never walks.
// Sampled over time and saved (a strip of frames).
import std;
import omui;
import omui.site;
import mlibs.image;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

struct Page {
    Headless h{HeadlessOptions{.size = {640, 360}, .scale = 1}};
    int deleted = 0, asked = 0;
    explicit Page(bool reduced = false, std::string arrive = {}) {
        register_base_components();
        register_type(component_type<site::Mascot>());
        (void)h.runtime().verbs().add({"doc.delete", "Delete", Effect::irreversible, "delete", {}}, [this](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        if (reduced) h.runtime().set_reduce_motion(true);
        h.mount("main", stack({.id = "page", .width = 640.0f, .height = 360.0f, .background = Fill(Color::hex(0xfbfbfa))},
                              {column({.id = "body", .padding = 32, .gap = 40},
                                      {row({.id = "top", .gap = 16},
                                           {button({.id = "save", .text = "Save", .variant = ButtonVariant::primary}),
                                            button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger, .verb = "doc.delete"})}),
                                       box({.id = "card", .width = 220.0f, .height = 80.0f, .background = Fill(Color::hex(0xffffff)), .radius = 12, .shadow = "md"})}),
                               site::mascot({.id = "mui", .arrive = arrive, .arrive_after = 0.2, .surfaces = Value(Value::List{Value("main/body/card")}),
                                             .on_activated = [this] { ++asked; }})}));
        if (arrive.empty()) h.settle();
        else h.step();  // an arrival is watched from its start
    }
    std::int64_t agent(std::string_view node, std::string_view action, std::string answer = {}) {
        Invocation call;
        call.actor = {"agent", "visitor", {}};
        call.answer = std::move(answer);
        return h.tree().act(node, action, call).exit_code().value_or(-1);
    }
    std::int64_t person(std::string_view node, std::string_view action, Value args = {}) {
        Invocation call;
        call.actor = {"person", "user", {}};
        call.args = std::move(args);
        return h.tree().act(node, action, call).exit_code().value_or(-1);
    }
    Value state() { return h.tree().node("main/mui")->value; }
    std::string text(std::string_view key) {
        const Value now = state();
        return std::string(now.get(key)->as_string());
    }
    Point feet() {
        const Value now = state();  // held: the list is borrowed from it
        const auto& list = *now.get("feet")->list();
        // A whole number comes back an integer.
        const auto number = [](const Value& v) { return static_cast<float>(v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double()); };
        return {number(list[0]), number(list[1])};
    }
    void run(double seconds_) {
        for (int i = 0; i < static_cast<int>(seconds_ * 60); ++i) h.advance(seconds(1.0 / 60));
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site.mui"};

    s.test("it sits at the page's bottom, quiet; it takes the pointer only on itself, and a click on it asks what it offers", [] {
        Page p;
        p.run(0.5);
        check(p.h.tree().node("main/mui")->role == Role::image && p.h.tree().node("main/mui")->name == "Mui");
        check_eq(p.text("state"), std::string("quiet"));
        check(p.text("palette") == "indigo" && (p.text("frame") == "sit" || p.text("frame") == "blink"), p.text("frame"));
        check(std::abs(p.feet().y - 356) < 3, std::format("on the page's bottom: {}", p.feet().y));
        const Rect save = p.h.runtime().screen_bounds(p.h.runtime().find("main/body/top/save"));
        check(p.h.runtime().hit_test(save.center()) == p.h.runtime().find("main/body/top/save"), "the page beneath it takes the page's clicks");
        const Point on = {p.feet().x, p.feet().y - 20};
        check(p.h.runtime().hit_test(on) == p.h.runtime().find("main/mui"), "on itself, it takes the pointer");
        p.h.click(on);
        p.h.step();
        check_eq(p.asked, 1);
        check(p.person("main/mui", "activate") == 0 && p.asked == 2, "activate: the same, for a reader or an agent");
    });

    s.test("over a control it yields: a click on Mui where a button lies beneath is the button's (content first)", [] {
        Headless h{HeadlessOptions{.size = {640, 360}, .scale = 1}};
        register_base_components();
        register_type(component_type<site::Mascot>());
        int pressed = 0, asked = 0;
        h.mount("main", stack({.id = "page", .width = 640.0f, .height = 360.0f},
                              {column({.id = "body", .width = 640.0f, .height = 360.0f, .justify = "end"},
                                      {button({.id = "under", .text = "A button along the bottom", .on_activated = [&] { ++pressed; }, .width = Dim(640.0f),
                                               .height = Dim(70.0f)})}),
                               site::mascot({.id = "mui", .on_activated = [&] { ++asked; }})}));
        h.settle();
        const Value now = h.tree().node("main/mui")->value;
        const auto& feet = *now.get("feet")->list();
        const auto number = [](const Value& v) { return static_cast<float>(v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double()); };
        const Point on{number(feet[0]), number(feet[1]) - 20};
        check(h.runtime().hit_test(on) == h.runtime().find("main/body/under"), "the button's, not Mui's");
        h.click(on);
        h.step();
        check(pressed == 1 && asked == 0, std::format("pressed {} asked {}", pressed, asked));
    });

    s.test("the agent acts: Mui walks there, hops up onto the card it goes to, its eyes lit, and points; the strip saved", [] {
        Page p;
        const auto dir = scratch_dir("mui");
        const Point start = p.feet();
        // The agent works on the card's neighbour first: Save, up on the page's top row.
        check(p.agent("main/body/top/save", "activate") == 0);
        p.h.step();
        check_eq(p.text("target"), std::string("main/body/top/save"));
        check(p.text("palette") == "agent", "its eyes lit: the agent's");
        std::vector<std::string> frames;
        std::vector<float> xs;
        for (int k = 0; k < 8; ++k) {
            p.run(0.25);
            frames.push_back(p.text("frame"));
            xs.push_back(p.feet().x);
            (void)p.h.save_png(dir / std::format("walk-{}.png", k));
        }
        check(std::ranges::any_of(frames, [](const std::string& f) { return f.starts_with("walk"); }), "it strode");
        check(xs.back() < start.x - 200, std::format("it went left, toward Save: {} → {}", start.x, xs.back()));
        p.run(3);
        const Rect save = p.h.runtime().screen_bounds(p.h.runtime().find("main/body/top/save"));
        check(std::abs(p.feet().x - (save.x + save.width / 2)) < 60, std::format("by Save: {} (Save at {})", p.feet().x, save.x + save.width / 2));
        check(p.feet().y > save.bottom(), "under it, not on it: a control is pointed at, not climbed");
        check_eq(p.text("frame"), std::string("point"));
        check_eq(p.text("state"), std::string("pointing"));
        (void)p.h.save_png(dir / "there.png");
    });

    s.test("the agent asks the person: Mui holds up the card; the answer is the person's own, Mui answers nothing", [] {
        Page p;
        check(p.agent("main/body/top/delete", "activate") == spec::exit_code::needs_answer);
        p.run(6);
        check(p.text("state") == "asking" && p.text("frame") == "hold", p.text("state") + " " + p.text("frame"));
        check(p.deleted == 0, "nothing answered for the person");
        (void)p.h.save_png(scratch_dir("mui-asking") / "asking.png");
    });

    s.test("asked: it points at a node, says one line, sleeps and wakes — through the page's own actions", [] {
        Page p;
        check(p.person("main/mui", "point_at", Value{}.set("node", "main/body/card")) == 0);
        const auto dir = scratch_dir("mui-card");
        for (int k = 0; k < 20; ++k) {
            p.run(0.25);
            (void)p.h.save_png(dir / std::format("{:02}.png", k));
        }
        check_eq(p.text("target"), std::string("main/body/card"));
        const Rect card = p.h.runtime().screen_bounds(p.h.runtime().find("main/body/card"));
        check(std::abs(p.feet().y - card.y) < 4, std::format("up on the card: feet {} card top {}", p.feet().y, card.y));
        check(p.person("main/mui", "say", Value{}.set("text", "This one.\nAnd more")) == 0);
        p.h.step();
        check_eq(p.text("said"), std::string("This one."));
        check(p.person("main/mui", "point_at", Value{}.set("node", "main/nowhere")) != 0, "a node that is not there: refused");
        check(p.person("main/mui", "sleep") == 0);
        p.run(1);
        check(p.text("state") == "asleep" && p.text("frame") == "sleep");
        check(p.person("main/mui", "wake") == 0);
        p.run(0.5);
        check(p.text("state") != "asleep");
    });

    s.test("it comes alive from where it is told: its pixels gather there, then it drops to the page's bottom; with less motion it is simply there", [] {
        Page p(false, "main/body/card");
        check_eq(p.text("state"), std::string("arriving"));
        const auto dir = scratch_dir("mascot-arrival");
        for (int k = 0; k < 8; ++k) {
            p.run(0.15);
            (void)p.h.save_png(dir / std::format("{:02}.png", k));
        }
        p.run(3);
        check(p.text("state") != "arriving", p.text("state"));
        const Rect card = p.h.runtime().screen_bounds(p.h.runtime().find("main/body/card"));
        check(std::abs(p.feet().x - (card.x + card.width / 2)) < 20, std::format("it dropped from the card's middle: feet at {} (card centre {})", p.feet().x, card.x + card.width / 2));
        check(std::abs(p.feet().y - 356) < 3, std::format("and landed on the page's bottom: {}", p.feet().y));
        Page still(true, "main/body/card");
        check(still.text("state") != "arriving" && std::abs(still.feet().y - 356) < 3, "less motion: no gathering, already standing");
    });

    s.test("while a link waits for your agent it listens, looking up; paired, it is quiet again", [] {
        Page p;
        p.h.runtime().agent_link_changed({"waiting", "ABCD-1234", {}, {}, 47110, "agent.md"});
        p.run(0.5);
        check(p.text("state") == "listening" && p.text("frame") == "look", p.text("state") + " " + p.text("frame"));
        p.h.runtime().agent_link_changed({"paired", "ABCD-1234", "claude", {}, 47110, "agent.md"});
        p.run(0.5);
        check(p.text("state") != "listening", p.text("state"));
    });

    s.test("less motion asked: frames change in place; it never walks", [] {
        Page p(true);
        const Point start = p.feet();
        check(p.agent("main/body/top/save", "activate") == 0);
        p.run(2);
        check(std::abs(p.feet().x - start.x) < 1, "it stays where it was");
        check(!p.text("frame").starts_with("walk"), p.text("frame"));
        check(p.text("palette") == "agent", "but its eyes still say the agent is here");
    });

    return s.run(argc, argv);
}
