// Live Design's designer (D-149, D-151): what a player in development mode
// is designed with — the design.* verbs (Live Design's catalog,
// design.ops.omui.ts), the history, previews and variants in stand-ins, the
// timeline. It works with the player's sources through the player's API, and
// says it is there as the program starts (provide_designer): a program that
// links omui.designer has it, a release (which does not) has none.
module omui.designer;

import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.semantic;
import omui.agent;
import omui.player;
import :views;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui::designer {

namespace {

constexpr Log log{"designer"};

using player::Language, player::Player, player::Twin;

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

std::optional<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

class Design final : public player::Designer {
public:
    explicit Design(Player& designed) : player(designed), rt(designed.runtime()), history(designed.manifest().base / ".omui" / "design") { verbs(); }
    ~Design() override {
        // Its verbs and its overlays go with it.
        for (const auto& name : added) (void)rt.verbs().remove(name);
        for (const std::string_view slot : {"design-preview", "design-variants", "design-timeline"}) rt.unmount(slot);
    }

private:
    Player&                  player;
    Runtime&                 rt;
    std::vector<std::string> added;  // the verbs it added
    // Live Design's history (D-151): states of the design on branches, beside the application.
    DesignHistory history;
    std::string   intent;  // what the design session is doing (design.begin): each step's commit says it
    // What the person expects of the design, in their words and as a query
    // that holds or does not (design.expect); design.check reads them against
    // the tree as it is — facts, not an account of them.
    // A motion expected (§2.12.4: "it opens in 300–400 ms and springs at most
    // 10%"): after an action, how a node's drawn box moves — measured, not
    // read off the tree, whose bounds are the target's (S-6).
    struct Motion {
        std::string                              act_node, action;
        Value                                    args;
        std::string                              node, measure = "height";  // height, width, x or y
        std::optional<std::pair<double, double>> within_ms;
        std::optional<double>                    overshoot;  // at most, of the distance moved

        static std::expected<Motion, std::string> parse(const Value& value) {
            Motion motion;
            const Value* act = value.get("act");
            if (!act || !act->get("node") || !act->get("action"))
                return std::unexpected(std::string("a motion says what starts it: act {node, action, args?}"));
            motion.act_node = std::string(act->get("node")->as_string());
            motion.action = std::string(act->get("action")->as_string());
            if (const Value* given = act->get("args")) motion.args = *given;
            motion.node = std::string(value.get("node") ? value.get("node")->as_string() : act->get("node")->as_string());
            if (const Value* measure = value.get("measure")) motion.measure = std::string(measure->as_string());
            if (motion.measure != "height" && motion.measure != "width" && motion.measure != "x" && motion.measure != "y")
                return std::unexpected(std::format("a motion measures height, width, x or y — not `{}`", motion.measure));
            if (const Value* within = value.get("within_ms")) {
                if (!within->list() || within->list()->size() != 2) return std::unexpected(std::string("within_ms is [the least, the most]"));
                motion.within_ms = std::pair{(*within->list())[0].as_double(), (*within->list())[1].as_double()};
            }
            if (const Value* overshoot = value.get("overshoot"); overshoot && overshoot->is_number()) motion.overshoot = overshoot->as_double();
            return motion;
        }
        [[nodiscard]] Value to_value() const {
            Value value = Value{}
                              .set("act", Value{}.set("node", act_node).set("action", action).set("args", args))
                              .set("node", node)
                              .set("measure", measure);
            if (within_ms) value.set("within_ms", Value(Value::List{Value(within_ms->first), Value(within_ms->second)}));
            if (overshoot) value.set("overshoot", *overshoot);
            return value;
        }
    };
    struct Expectation {
        std::string           say;
        semantic::Query       query;
        bool                  absent = false;
        std::optional<Motion> motion;
    };
    static Value expectation_value(const Expectation& expected) {
        Value value = Value{}.set("say", expected.say);
        if (expected.motion) return value.set("motion", expected.motion->to_value());
        return value.set("query", expected.query.to_value()).set("absent", expected.absent);
    }
    static std::expected<Expectation, std::string> expectation_of(const Value& value) {
        Expectation expected{.say = std::string(value.get("say") ? value.get("say")->as_string() : "")};
        if (const Value* motion = value.get("motion")) {
            auto parsed = Motion::parse(*motion);
            if (!parsed) return std::unexpected(parsed.error());
            expected.motion = std::move(*parsed);
            if (expected.say.empty()) expected.say = motion->json();
            return expected;
        }
        const Value* query = value.get("query");
        if (!query) return std::unexpected(std::string("an expectation is a `query` (nodes there or absent) or a `motion`"));
        auto parsed = semantic::Query::parse(*query);
        if (!parsed) return std::unexpected(std::format("query: {}", parsed.error()));
        expected.query = std::move(*parsed);
        expected.absent = value.get("absent") && value.get("absent")->as_bool();
        if (expected.say.empty()) expected.say = query->json();
        return expected;
    }
    std::vector<Expectation> expectations;
    // Where the person took the floor (design.handover): what they do after
    // it, done, is a scenario's steps (design.save_scenario).
    std::uint64_t handed_over_at = 0;

    // The expectations read against the tree as it is.
    Value check_expectations(std::span<const Expectation> list) {
        semantic::Tree tree(rt);
        Value::List results;
        std::size_t held = 0;
        for (const auto& x : list) {
            if (x.motion) {
                Value measured = motion_result(x);
                held += measured.get("holds")->as_bool();
                results.push_back(std::move(measured));
                continue;
            }
            const auto found = tree.find(x.query);
            const bool holds = x.absent ? found.empty() : !found.empty();
            held += holds;
            Value::List nodes;
            for (const auto& n : found) {
                const Value face = spec::to_value(n);
                nodes.push_back(Value{}.set("id", n.id).set("name", n.name).set("value", n.value).set("states", face.get("state") ? *face.get("state") : Value(Value::List{})));
            }
            Value one;
            one.set("say", x.say).set("holds", holds).set("found", Value(std::move(nodes)));
            // Not as expected: what the last press on the node came to (the recorder), or that it is not there.
            if (!holds && !x.query.id.empty()) {
                if (const NodeId node = rt.find(x.query.id); rt.valid(node)) {
                    if (const auto why = rt.explain(rt.screen_bounds(node).center())) one.set("last_press", why->summary);
                } else {
                    one.set("why", std::format("no node {}", x.query.id));
                }
            }
            results.push_back(std::move(one));
        }
        return Value{}.set("held", static_cast<double>(held)).set("of", static_cast<double>(list.size())).set("results", Value(std::move(results)));
    }
    // A design step that changed the design, kept as a commit (D-151: every
    // step is one): its intent, what it did, the semantic changes. Answered
    // with the commit's id; a step that changed nothing makes none.
    VerbResult step(VerbResult done, std::string what) {
        if (!done) return done;
        semantic::Tree tree(rt);
        const auto by = rt.current_invocation();
        const Value* changes = done->get("changes");
        auto commit = history.commit(design_texts(), intent.empty() ? std::move(what) : std::format("{} — {}", intent, what),
                                     by ? by->actor.text() : std::string("person"), agent::tree_hash(tree), std::nullopt,
                                     changes ? changes->json() : std::string());
        if (commit) done->set("commit", commit->id);
        shown_commit.clear();  // the branch's last commit, the step's
        refresh_timeline();
        return done;
    }

    // Every source's text as it runs now (edited, or its file's): a design's state.
    std::map<std::string, std::string> design_texts() {
        std::map<std::string, std::string> out;
        for (const Player::Source& source : player.sources())
            if (const auto text = player.text(source.path)) out.emplace(source.path, *text);
        return out;
    }

    // A state of the design put into the running application: each source
    // whose text differs edited as design.edit edits it (and presented so).
    std::expected<Value, std::string> apply_texts(const std::map<std::string, std::string>& texts) {
        Value::List changed;
        for (const auto& [path, text] : texts) {
            const auto now = player.text(path);
            if (now && *now == text) continue;
            auto done = player.edit(path, text);
            if (!done) return std::unexpected(std::format("{}: {}", path, done.error()));
            changed.push_back(done->set("path", path));
        }
        return Value{}.set("edits", Value(std::move(changed)));
    }

    static Value commit_value(const DesignCommit& commit) {
        Value::List parents;
        for (const auto& parent : commit.parents) parents.push_back(Value(parent));
        Value::List paths;
        for (const auto& [path, object] : commit.files) paths.push_back(Value(path));
        return Value{}
            .set("id", commit.id)
            .set("message", commit.message)
            .set("actor", commit.actor)
            .set("branch", commit.branch)
            .set("time", commit.time)
            .set("tree", commit.tree)
            .set("parents", Value(std::move(parents)))
            .set("files", Value(std::move(paths)));
    }

    // Previews and the variants board (D-149's 原地预演 and 替身方案板): what a
    // stand-in shows, above the application in slots of their own, until the
    // person (or the agent) takes one or lets it go.
    std::optional<std::map<std::string, std::string>> previewing;  // the texts a preview shows
    std::map<std::string, std::string>                  variants;    // a variant on the board → its branch

    // The texts as they would be after these edits: `edits` [{path, text}], or one `path` and `text`.
    std::expected<std::map<std::string, std::string>, std::string> with_edits(const Value& args) {
        std::map<std::string, std::string> texts = design_texts();
        Value::List one;
        const Value* edits = args.get("edits");
        if (!edits || !edits->list())
            one.push_back(Value{}.set("path", args.get("path") ? *args.get("path") : Value{}).set("text", args.get("text") ? *args.get("text") : Value{}));
        for (const Value& edit : edits && edits->list() ? *edits->list() : one) {
            const Value* path = edit.get("path");
            const Value* text = edit.get("text");
            if (!path || !text || text->type() != Value::Type::text)
                return std::unexpected(std::string("each edit is {path, text}: a source and its whole new text"));
            if (!texts.contains(std::string(path->as_string())))
                return std::unexpected(std::format("no script or module `{}` (design.sources lists them)", path->as_string()));
            texts[std::string(path->as_string())] = std::string(text->as_string());
        }
        return texts;
    }

    // What a stand-in shows of these texts, its picture kept in the history's store.
    struct Shown {
        std::filesystem::path picture;
        Value                 changes;  // against what the person sees now
    };
    // A twin of the application (the entry makes them), or why there is none.
    std::expected<std::unique_ptr<Twin>, std::string> twin(bool design) {
        if (!player.options().twin) return std::unexpected(std::string("this host makes no twin of the application to show it in (omui dev's player does)"));
        return player.options().twin(design);
    }

    std::expected<Shown, std::string> stand_in(const std::map<std::string, std::string>& texts) {
        auto made = twin(false);
        if (!made) return std::unexpected(made.error());
        Twin& shown = **made;
        if (auto ran = shown.player().show(texts); !ran) return std::unexpected(std::format("the stand-in: {}", ran.error()));
        shown.settle();
        auto png = shown.png();
        if (!png) return std::unexpected(std::format("the stand-in: {}", png.error()));
        auto picture = history.keep_picture(*png);
        if (!picture) return std::unexpected(picture.error());
        semantic::Tree here(rt), there(shown.runtime());
        return Shown{std::move(*picture), semantic::changes_between(here.snapshot(), there.snapshot())};
    }

    void show_preview(const std::filesystem::path& picture) {
        rt.mount("design-preview", preview_view(rt.size(), picture, [this](const Value&) { (void)accept_preview(); }, [this](const Value&) { reject_preview(); }));
    }
    VerbResult accept_preview() {
        if (!previewing) return std::unexpected(std::string("no preview is shown (design.preview shows one)"));
        auto texts = std::move(*previewing);
        previewing.reset();
        rt.unmount("design-preview");
        return step(apply_texts(texts), "the preview, accepted");
    }
    void reject_preview() {
        previewing.reset();
        rt.unmount("design-preview");
    }

    void show_board(const std::vector<Card>& cards, std::string_view brief) {
        rt.mount("design-variants", board_view(rt.size(), brief, cards, [this](const std::string& name) { (void)pick_variant(name); }));
    }
    VerbResult pick_variant(const std::string& name) {
        const auto branch = variants.find(name);
        if (branch == variants.end()) return std::unexpected(std::format("no variant `{}` on the board", name));
        auto texts = history.texts_of(branch->second);
        if (!texts) return std::unexpected(texts.error());
        variants.clear();
        rt.unmount("design-variants");
        return step(apply_texts(*texts), std::format("variant {} picked", name));
    }

    // The timeline (D-151 §2.14): shown or not, two commits being compared,
    // what the last press came to, and the commit the application shows.
    bool                     timeline_open = false, comparing = false;
    std::vector<std::string> compared;
    std::string              timeline_note, shown_commit;

    // Every commit the branches reach, oldest first.
    std::vector<DesignCommit> commits_in_time() const {
        std::map<std::string, DesignCommit> all;
        for (const auto& [branch, head] : history.branches())
            for (DesignCommit& commit : history.log(branch, 500)) all.try_emplace(commit.id, std::move(commit));
        std::vector<DesignCommit> out;
        for (auto& [id, commit] : all) out.push_back(std::move(commit));
        std::ranges::sort(out, [](const DesignCommit& left, const DesignCommit& right) {
            return left.time != right.time ? left.time < right.time : left.id < right.id;
        });
        return out;
    }

    std::string shown_now() const {
        if (!shown_commit.empty()) return shown_commit;
        const auto head = history.resolve("HEAD");
        return head ? *head : std::string();
    }

    Timeline timeline_model() const {
        Timeline model;
        model.note = timeline_note.empty() ? std::string("Press a commit to see it; ◀ ▶ go through time; Compare, then two commits") : timeline_note;
        model.comparing = comparing;
        const auto in_time = commits_in_time();
        model.columns = in_time.size();
        const std::string head = history.head_branch(), shown = shown_now();
        std::map<std::string, TimelineLane> lanes;
        for (std::size_t column = 0; column < in_time.size(); ++column) {
            const DesignCommit& commit = in_time[column];
            TimelineLane& lane = lanes[commit.branch];
            lane.branch = commit.branch;
            lane.head = commit.branch == head;
            lane.commits.push_back({.id = commit.id,
                                    .message = commit.message,
                                    .actor = commit.actor,
                                    .column = column,
                                    .picture = history.thumbnail(commit.id),
                                    .shown = commit.id == shown,
                                    .selected = std::ranges::contains(compared, commit.id)});
        }
        // The branch designed on first, then the rest by name.
        if (const auto designed = lanes.find(head); designed != lanes.end()) {
            model.lanes.push_back(std::move(designed->second));
            lanes.erase(designed);
        }
        for (auto& [branch, lane] : lanes) model.lanes.push_back(std::move(lane));
        return model;
    }

    void refresh_timeline() {
        if (!timeline_open) return;
        rt.mount("design-timeline",
                 timeline_view(rt.size(), timeline_model(),
                               {.open = [this](const std::string& id) { (void)see_commit(id); },
                                .select = [this](const std::string& id) { select_commit(id); },
                                .step = [this](int by) { (void)step_in_time(by); },
                                .compare =
                                    [this] {
                                        comparing = !comparing;
                                        compared.clear();
                                        timeline_note = comparing ? "Press two commits to compare them" : "";
                                        refresh_timeline();
                                    },
                                .close =
                                    [this] {
                                        timeline_open = false;
                                        rt.unmount("design-timeline");
                                    }}));
    }

    // A commit seen (the timeline's press, its steps): its texts run, the
    // branch where it was; refused while the design has changes not committed.
    std::expected<Value, std::string> see_commit(const std::string& id) {
        auto texts = history.texts_of(id);
        if (!texts) return std::unexpected(texts.error());
        const auto head = history.texts_of("HEAD");
        const std::string shown = shown_now();
        const auto now = shown.empty() ? head : history.texts_of(shown);
        if (now && *now != design_texts()) {
            timeline_note = "The design has changes not committed: commit them first (design.history.commit)";
            refresh_timeline();
            return std::unexpected(timeline_note);
        }
        auto applied = apply_texts(*texts);
        if (!applied) return std::unexpected(applied.error());
        shown_commit = id;
        if (const auto commit = history.commit_of(id)) timeline_note = std::format("{} — {}", id.substr(0, 8), commit->message);
        refresh_timeline();
        return applied->set("at", id);
    }

    std::expected<Value, std::string> step_in_time(int by) {
        const auto in_time = commits_in_time();
        if (in_time.empty()) return std::unexpected(std::string("the design has no commits yet"));
        const std::string shown = shown_now();
        const auto at = std::ranges::find(in_time, shown, &DesignCommit::id);
        const auto index = at == in_time.end() ? static_cast<std::ptrdiff_t>(in_time.size()) - 1 : at - in_time.begin();
        const auto next = std::clamp<std::ptrdiff_t>(index + by, 0, static_cast<std::ptrdiff_t>(in_time.size()) - 1);
        return see_commit(in_time[static_cast<std::size_t>(next)].id);
    }

    static std::string joined(const std::vector<std::string>& parts) {
        std::string out;
        for (const auto& part : parts) out += std::format("{}{}", out.empty() ? "" : "; ", part);
        return out;
    }

    void select_commit(const std::string& id) {
        if (std::ranges::contains(compared, id)) return;
        if (compared.size() == 2) compared.clear();
        compared.push_back(id);
        if (compared.size() == 2) {
            const auto before = history.texts_of(compared[0]), after = history.texts_of(compared[1]);
            std::vector<std::string> differ;
            if (before && after)
                for (const auto& [path, text] : *after)
                    if (!before->contains(path) || before->at(path) != text)
                        differ.push_back(std::format("{} from line {}", path, changed_lines(before->contains(path) ? before->at(path) : std::string(), text).get("line")->as_int()));
            timeline_note = differ.empty() ? std::format("{} and {}: the same design", compared[0].substr(0, 8), compared[1].substr(0, 8))
                                           : std::format("{} → {}: {}", compared[0].substr(0, 8), compared[1].substr(0, 8), joined(differ));
        } else {
            timeline_note = "And one more";
        }
        refresh_timeline();
    }

    // Pictures for the latest commits that have none, each in a stand-in (the timeline's thumbnails).
    std::size_t picture_commits(std::size_t at_most) {
        if (!player.options().twin) return 0;
        auto in_time = commits_in_time();
        std::size_t made = 0;
        for (auto commit = in_time.rbegin(); commit != in_time.rend() && made < at_most; ++commit) {
            if (history.thumbnail(commit->id)) continue;
            auto texts = history.texts_of(commit->id);
            if (!texts) continue;
            auto shown = stand_in(*texts);
            if (!shown) continue;
            (void)history.set_thumbnail(commit->id, shown->picture);
            ++made;
        }
        return made;
    }

    // The design's history as a film (D-151 §2.14, 设计回放成为作品): a
    // branch's commits from its first, each run in an unseen twin of the
    // application as a design edit runs (its presentation: written in,
    // unwritten, springing) under a caption saying what the step was, drawn
    // frame by frame; the frames (PNG) and a video description beside them,
    // which `omui video render` makes a WebM of (`omui design film`).
    std::expected<Value, std::string> film(const Value& args) {
        const std::string branch(args.get("branch") ? args.get("branch")->as_string() : history.head_branch());
        auto commits = history.log(branch, 1000);
        if (commits.empty()) return std::unexpected(std::format("`{}` has no commits to film (design.history.commit, or every design step, makes them)", branch));
        std::ranges::reverse(commits);
        const auto number = [&](std::string_view key, double fallback, double low, double high) {
            const Value* given = args.get(key);
            return std::clamp(given && given->is_number() ? given->as_double() : fallback, low, high);
        };
        const int fps = static_cast<int>(number("fps", 30, 1, 60));
        const double each = number("seconds", 1.5, 0.2, 10);
        const std::string out_text(args.get("out") ? args.get("out")->as_string() : std::string());
        const std::filesystem::path out = out_text.empty() ? history.directory() / "film" / branch
                                                           : std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(out_text.data()), out_text.size()));
        std::error_code error;
        std::filesystem::remove_all(out / "frames", error);
        std::filesystem::create_directories(out / "frames", error);
        if (error) return std::unexpected(std::format("{}: {}", generic_utf8(out), error.message()));

        // The twin: the application again, unseen, designing as the person's does.
        auto made = twin(true);
        if (!made) return std::unexpected(made.error());
        Twin& shown = **made;
        const Size window = shown.runtime().size();
        std::size_t frame = 0;
        double at = shown.time().count();
        const auto frames_each = static_cast<std::size_t>(std::lround(each * fps));
        for (std::size_t index = 0; index < commits.size(); ++index) {
            auto texts = history.texts_of(commits[index].id);
            if (!texts) return std::unexpected(texts.error());
            if (auto ran = shown.player().show(*texts); !ran) return std::unexpected(std::format("{}: {}", commits[index].id.substr(0, 8), ran.error()));
            shown.settle();
            shown.runtime().mount("design-caption", caption_view(window, std::format("{}/{} · {}", index + 1, commits.size(), commits[index].message)));
            for (std::size_t step = 0; step < frames_each; ++step, ++frame) {
                at += 1.0 / fps;
                shown.step(seconds(at));
                auto png = shown.png();
                if (!png) return std::unexpected(png.error());
                std::ofstream(out / "frames" / std::format("{:05}.png", frame), std::ios::binary)
                    .write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
            }
        }
        // The description `omui video render` reads (xengine.video): the frames, at their rate.
        const auto description = out / "design.video.omui.toml";
        std::ofstream(description, std::ios::binary)
            << std::format("doc = \"The design of {} on {}: {} steps, {} s each.\"\n\n[video]\nwidth = {}\nheight = {}\nfps = {}\nframes = {}\n\n[frames]\npng = "
                           "\"frames/{{:05}}.png\"\n",
                           player.manifest().id, branch, commits.size(), each, static_cast<int>(window.width), static_cast<int>(window.height), fps, frame);
        return Value{}
            .set("branch", branch)
            .set("steps", static_cast<double>(commits.size()))
            .set("frames", static_cast<double>(frame))
            .set("fps", static_cast<double>(fps))
            .set("description", generic_utf8(description));
    }

    // A motion measured (design.expect's `motion`, §2.12.4): the action done
    // in a twin of the design as it runs, then the node's drawn box sampled
    // every 1/120 s until it rests — where it went from and to, when it
    // settled (within 1% of where it ended, for good), how far it sprang past.
    struct Measured {
        double from = 0, to = 0, settle_ms = 0, overshoot = 0;
    };
    std::expected<Measured, std::string> measure(const Motion& motion) {
        auto made = twin(false);
        if (!made) return std::unexpected(made.error());
        Twin& shown = **made;
        if (auto ran = shown.player().show(design_texts()); !ran) return std::unexpected(ran.error());
        shown.settle();
        Runtime& there = shown.runtime();
        const auto value = [&]() -> std::optional<double> {
            const NodeId node = there.reveal(motion.node);
            if (!there.valid(node)) return std::nullopt;
            const Rect box = there.drawn_bounds(node);
            if (motion.measure == "width") return box.width;
            if (motion.measure == "x") return box.x;
            if (motion.measure == "y") return box.y;
            return box.height;
        };
        const auto first = value();
        if (!first) return std::unexpected(std::format("no node {} to measure", motion.node));
        Invocation as;
        as.actor = {"person", "check", {}};
        as.args = motion.args;
        semantic::Tree tree(there);
        if (const Envelope done = tree.act(motion.act_node, motion.action, as); done.exit_code().value_or(1) != 0)
            return std::unexpected(std::format("{} {}: {}", motion.act_node, motion.action, done.ndjson()));
        constexpr int rate = 120, longest = 3 * rate;
        const double start = shown.time().count();
        std::vector<std::pair<double, double>> samples;  // ms after the action, the value
        for (int frame = 1; frame <= longest; ++frame) {
            shown.step(seconds(start + static_cast<double>(frame) / rate));
            if (const auto now = value()) samples.emplace_back(frame * 1000.0 / rate, *now);
        }
        if (samples.empty()) return std::unexpected(std::format("{} went away", motion.node));
        Measured measured{.from = *first, .to = samples.back().second};
        const double span = std::abs(measured.to - measured.from);
        if (span < 0.5) return measured;  // it did not move
        const double band = std::max(0.5, span * 0.01), toward = measured.to > measured.from ? 1.0 : -1.0;
        for (const auto& [at, now] : samples) {
            if (std::abs(now - measured.to) > band) measured.settle_ms = at;
            measured.overshoot = std::max(measured.overshoot, (now - measured.to) * toward / span);
        }
        return measured;
    }

    // What a motion came to, said with what was expected of it.
    Value motion_result(const Expectation& expected) {
        Value one = Value{}.set("say", expected.say);
        auto measured = measure(*expected.motion);
        if (!measured) return one.set("holds", false).set("why", measured.error());
        const Motion& motion = *expected.motion;
        const bool in_time = !motion.within_ms || (measured->settle_ms >= motion.within_ms->first && measured->settle_ms <= motion.within_ms->second);
        const bool sprang = !motion.overshoot || measured->overshoot <= *motion.overshoot + 1e-9;
        std::string said = std::format("{} {:.0f} → {:.0f} px in {:.0f} ms", motion.measure, measured->from, measured->to, measured->settle_ms);
        if (motion.within_ms) said += std::format(" ({} {:.0f}–{:.0f} ms)", in_time ? "within" : "not within", motion.within_ms->first, motion.within_ms->second);
        said += std::format(", overshoot {:.0f}%", measured->overshoot * 100);
        if (motion.overshoot) said += std::format(" ({} {:.0f}%)", sprang ? "at most" : "more than", *motion.overshoot * 100);
        return one.set("holds", in_time && sprang)
            .set("measured", Value{}
                                 .set("from", measured->from)
                                 .set("to", measured->to)
                                 .set("settle_ms", measured->settle_ms)
                                 .set("overshoot", measured->overshoot))
            .set("why", said);
    }

    // Where a script built a node: its path as design.sources names it, line and column.
    struct Place {
        std::string path;
        std::uint32_t line = 0, column = 0;
    };
    std::expected<Place, std::string> place_of(std::string_view node) const {
        const NodeId id = rt.reveal(node);
        if (!rt.valid(id)) return std::unexpected(std::format("no node `{}`", node));
        // file:line:column, the file as the script ran it
        const std::string_view built = rt.built_at(id);
        const auto column_at = built.rfind(':'), line_at = column_at == std::string_view::npos ? column_at : built.rfind(':', column_at - 1);
        if (line_at == std::string_view::npos) return std::unexpected(std::format("`{}` was not built by a script (or not in development mode)", node));
        const std::string_view file = built.substr(0, line_at);
        Place place{player.source_name(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(file.data()), file.size())))};
        (void)std::from_chars(built.data() + line_at + 1, built.data() + column_at, place.line);
        (void)std::from_chars(built.data() + column_at + 1, built.data() + built.size(), place.column);
        return place;
    }

    // A structured operation on the builder call that built `node`: the new
    // text its language's editor makes, run as design.edit runs it, and
    // answered with the lines it changed.
    VerbResult structured(std::string_view node, const std::function<std::expected<std::string, std::string>(const Language&, std::string_view, const Place&)>& change) {
        auto place = place_of(node);
        if (!place) return std::unexpected(place.error());
        const Language* language = player.language_of(place->path);
        if (!language) return std::unexpected(std::format("`{}` was built by {}, which is not one of the application's sources", node, place->path));
        const auto text = player.text(place->path);
        if (!text) return std::unexpected(std::format("{} cannot be read", place->path));
        auto changed = change(*language, *text, *place);
        if (!changed) return std::unexpected(std::format("{}:{}: {}", place->path, place->line, changed.error()));
        auto done = player.edit(place->path, *changed);
        if (!done) return std::unexpected(done.error());
        return done->set("path", place->path).set("source", changed_lines(*text, *changed));
    }

    // Where two texts differ, by whole lines: the first line and the lines before and after.
    static Value changed_lines(std::string_view before, std::string_view after) {
        const auto lines = [](std::string_view text) {
            std::vector<std::string_view> out;
            for (const auto line : std::views::split(text, '\n')) out.emplace_back(line.begin(), line.end());
            return out;
        };
        const auto old_lines = lines(before), new_lines = lines(after);
        std::size_t head = 0, tail = 0;
        while (head < old_lines.size() && head < new_lines.size() && old_lines[head] == new_lines[head]) ++head;
        while (tail + head < old_lines.size() && tail + head < new_lines.size() &&
               old_lines[old_lines.size() - 1 - tail] == new_lines[new_lines.size() - 1 - tail])
            ++tail;
        const auto joined = [](std::span<const std::string_view> part) {
            std::string out;
            for (const auto line : part) out += std::format("{}{}", out.empty() ? "" : "\n", line);
            return out;
        };
        return Value{}
            .set("line", static_cast<double>(head + 1))
            .set("before", joined(std::span(old_lines).subspan(head, old_lines.size() - head - tail)))
            .set("after", joined(std::span(new_lines).subspan(head, new_lines.size() - head - tail)));
    }

    void verbs() {
        // Each verb as Live Design's catalog declares it (design.ops.omui.ts);
        // the timeline, when it is shown, follows what the history's verbs did.
        auto add = [&](std::string_view name, VerbFn fn) {
            const auto entry = std::ranges::find(agent::design::catalog, name, [](const auto& op) { return op.operation.name; });
            if (entry == agent::design::catalog.end()) return log.warn("design: {} is not in the catalog", name);
            if (name.starts_with("design.history.") || name.starts_with("design.variants"))
                fn = [this, inner = std::move(fn)](const Value& args) -> VerbResult {
                    auto done = inner(args);
                    refresh_timeline();
                    return done;
                };
            if (auto r = rt.verbs().add(agent::verb_of(*entry), std::move(fn)); !r) log.warn("design: {}", r.error());
            else added.emplace_back(name);
        };
        add("design.sources", [this](const Value&) -> VerbResult {
            Value::List scripts, modules;
            for (const Player::Source& source : player.sources()) {
                const auto text = player.text(source.path);
                Value one = Value{}.set("path", source.path).set("text", text ? *text : std::string()).set("edited", source.edited);
                if (source.imported_by.empty()) scripts.push_back(one.set("language", source.language));
                else modules.push_back(one.set("imported_by", source.imported_by));
            }
            return Value{}.set("scripts", Value(std::move(scripts))).set("modules", Value(std::move(modules)));
        });
        add("design.api", [this](const Value&) -> VerbResult {
            std::string catalog;
            (void)abi::omuiCatalog(player.handle(), collect, &catalog);
            Value languages(Value::Map{});
            for (const Player::Source& source : player.sources())
                if (const Language* language = player.language_of(source.path); language && language->describe && !languages.get(language->name))
                    languages.set(language->name, language->describe(catalog));
            if (languages.map()->empty()) return std::unexpected(std::string("no script language here describes its API"));
            return Value{}.set("languages", std::move(languages));
        });
        add("design.locate", [this](const Value& args) -> VerbResult {
            if (const Value* node = args.get("node")) {
                auto place = place_of(node->as_string());
                if (!place) return std::unexpected(place.error());
                Value answer = Value{}
                                   .set("node", rt.path(rt.reveal(node->as_string())))
                                   .set("path", place->path)
                                   .set("line", static_cast<double>(place->line))
                                   .set("column", static_cast<double>(place->column));
                if (const auto text = player.text(place->path)) {
                    std::string_view rest = *text;
                    for (std::uint32_t skipped = 1; skipped < place->line && rest.contains('\n'); ++skipped) rest.remove_prefix(rest.find('\n') + 1);
                    answer.set("text", std::string(rest.substr(0, rest.find('\n'))));
                }
                return answer;
            }
            const std::string path(args.get("path") ? args.get("path")->as_string() : "");
            const Value* line = args.get("line");
            if (path.empty() || !line || !line->is_number() || line->as_double() < 1)
                return std::unexpected(std::string("design.locate takes a `node`, or a `path` and a `line` (from 1)"));
            if (!player.language_of(path)) return std::unexpected(std::format("no script or module `{}` (design.sources lists them)", path));
            const auto wanted = static_cast<std::uint32_t>(line->as_double());
            std::vector<NodeId> built = rt.built_on(generic_utf8(player.source_file(path)), wanted);
            if (built.empty()) built = rt.built_on(generic_utf8(player.source_file(path).lexically_normal()), wanted);
            Value::List nodes;
            for (const NodeId id : built) nodes.push_back(Value{}.set("node", rt.path(id)).set("built_at", std::string(rt.built_at(id))));
            return Value{}.set("path", path).set("line", static_cast<double>(wanted)).set("nodes", Value(std::move(nodes)));
        });
        add("design.edit", [this](const Value& args) -> VerbResult {
            const Value* text = args.get("text");
            if (!text || text->type() != Value::Type::text) return std::unexpected(std::string("the new text is `text`, a string"));
            const std::string path(args.get("path") ? args.get("path")->as_string() : "");
            return step(player.edit(path, text->as_string()), std::format("edit {}", path));
        });
        add("design.set", [this](const Value& args) -> VerbResult {
            const Value* properties = args.get("props");
            if (!properties || !properties->map() || properties->map()->empty())
                return std::unexpected(std::string("design.set takes `node` and `props`: {name: value} (null takes one out)"));
            const std::string node(args.get("node") ? args.get("node")->as_string() : "");
            return step(structured(node,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.set) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  std::string now(text);
                                  for (const auto& [name, value] : *properties->map()) {
                                      const std::string code = value.json();
                                      auto next = language.edit.set(now, place.line, place.column, name,
                                                                    value.type() == Value::Type::null ? std::nullopt : std::optional<std::string_view>(code));
                                      if (!next) return next;
                                      now = std::move(*next);
                                  }
                                  return now;
                              }),
                        std::format("set {} of {}", properties->json(), node));
        });
        add("design.insert", [this](const Value& args) -> VerbResult {
            const Value* code = args.get("code");
            if (!code || code->type() != Value::Type::text || code->as_string().empty())
                return std::unexpected(std::string("design.insert takes `parent` and `code`: an element as the language writes it"));
            std::optional<std::size_t> index;
            if (const Value* at = args.get("index"); at && at->is_number() && at->as_double() >= 0) index = static_cast<std::size_t>(at->as_double());
            const std::string parent(args.get("parent") ? args.get("parent")->as_string() : "");
            return step(structured(parent,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.insert) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  return language.edit.insert(text, place.line, place.column, code->as_string(), index);
                              }),
                        std::format("insert into {}", parent));
        });
        add("design.remove", [this](const Value& args) -> VerbResult {
            const std::string node(args.get("node") ? args.get("node")->as_string() : "");
            return step(structured(node,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.remove) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  return language.edit.remove(text, place.line, place.column);
                              }),
                        std::format("remove {}", node));
        });
        add("design.begin", [this](const Value& args) -> VerbResult {
            intent = std::string(args.get("intent") ? args.get("intent")->as_string() : "");
            if (intent.empty()) return std::unexpected(std::string("design.begin says what the session is for: `intent`"));
            // Kept before the first step: the state the session began from (nothing, when it is the last commit already).
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto start = history.commit(design_texts(), std::format("{} — begun", intent), by ? by->actor.text() : std::string("person"), agent::tree_hash(tree));
            if (by) rt.note({.actor = by->actor.text(), .text = intent});
            return Value{}.set("intent", intent).set("branch", history.head_branch()).set("commit", start ? Value(start->id) : Value{});
        });
        add("design.film", [this](const Value& args) -> VerbResult { return film(args); });
        add("design.timeline", [this](const Value& args) -> VerbResult {
            std::size_t pictured = 0;
            if (args.get("pictures") && args.get("pictures")->as_bool()) pictured = picture_commits(12);
            if (const Value* at = args.get("at")) {
                if (auto seen = see_commit(std::string(at->as_string())); !seen) return std::unexpected(seen.error());
            } else if (const Value* by = args.get("step"); by && by->is_number()) {
                if (auto seen = step_in_time(static_cast<int>(by->as_double())); !seen) return std::unexpected(seen.error());
            }
            timeline_open = !args.get("show") || args.get("show")->as_bool();
            if (timeline_open) refresh_timeline();
            else rt.unmount("design-timeline");
            const Timeline model = timeline_model();
            Value::List lanes;
            for (const TimelineLane& lane : model.lanes) {
                Value::List commits;
                for (const TimelineCommit& commit : lane.commits)
                    commits.push_back(Value{}
                                          .set("id", commit.id)
                                          .set("message", commit.message)
                                          .set("actor", commit.actor)
                                          .set("column", static_cast<double>(commit.column))
                                          .set("picture", commit.picture ? Value(generic_utf8(*commit.picture)) : Value{})
                                          .set("shown", commit.shown));
                lanes.push_back(Value{}.set("branch", lane.branch).set("head", lane.head).set("commits", Value(std::move(commits))));
            }
            return Value{}
                .set("open", timeline_open)
                .set("shown", shown_now())
                .set("columns", static_cast<double>(model.columns))
                .set("lanes", Value(std::move(lanes)))
                .set("pictured", static_cast<double>(pictured));
        });
        // Live Design's history (D-151): states of the design as commits on branches, under .omui/design/.
        add("design.history", [this](const Value& args) -> VerbResult {
            const std::string branch(args.get("branch") ? args.get("branch")->as_string() : history.head_branch());
            const std::size_t limit = args.get("limit") && args.get("limit")->is_number() ? static_cast<std::size_t>(args.get("limit")->as_double()) : 50;
            Value::List log;
            for (const DesignCommit& commit : history.log(branch, limit)) log.push_back(commit_value(commit));
            Value branches(Value::Map{}), tags(Value::Map{});
            for (const auto& [name, id] : history.branches()) branches.set(name, id);
            for (const auto& [name, id] : history.tags()) tags.set(name, id);
            const auto head = history.texts_of("HEAD");
            return Value{}
                .set("head", history.head_branch())
                .set("branches", std::move(branches))
                .set("tags", std::move(tags))
                .set("log", Value(std::move(log)))
                .set("uncommitted", !head || *head != design_texts());
        });
        add("design.history.commit", [this](const Value& args) -> VerbResult {
            const std::string message(args.get("message") ? args.get("message")->as_string() : "");
            if (message.empty()) return std::unexpected(std::string("a commit says what it is: `message`"));
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), message, by ? by->actor.text() : std::string("person"), agent::tree_hash(tree));
            if (!commit) return std::unexpected(commit.error());
            return commit_value(*commit);
        });
        add("design.history.branch", [this](const Value& args) -> VerbResult {
            const std::string name(args.get("name") ? args.get("name")->as_string() : "");
            const Value* at = args.get("at");
            auto made = history.branch(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
            if (!made) return std::unexpected(made.error());
            return Value{}.set("branch", name).set("at", *made);
        });
        add("design.history.switch", [this](const Value& args) -> VerbResult {
            const std::string to(args.get("to") ? args.get("to")->as_string() : "");
            const auto head = history.texts_of("HEAD");
            if (head && *head != design_texts() && !(args.get("force") && args.get("force")->as_bool()))
                return std::unexpected(std::string("the design has changes not committed: design.history.commit them, or switch with force (they are dropped)"));
            const auto branches = history.branches();
            auto at = history.resolve(to);
            if (!at) return std::unexpected(std::format("{} (design.history lists the branches and tags)", at.error()));
            auto texts = history.texts_of(*at);
            if (!texts) return std::unexpected(texts.error());
            auto applied = apply_texts(*texts);
            if (!applied) return std::unexpected(applied.error());
            if (!branches.contains(to)) {  // a commit, seen: the branch stays; a commit keeps it there, design.history.branch makes it one
                shown_commit = *at;
                return applied->set("branch", history.head_branch()).set("at", *at).set("detached", true);
            }
            shown_commit.clear();
            (void)history.switch_to(to);
            return applied->set("branch", to).set("at", branches.at(to));
        });
        add("design.history.revert", [this](const Value& args) -> VerbResult {
            const std::string to(args.get("to") ? args.get("to")->as_string() : "");
            auto id = history.resolve(to);
            if (!id) return std::unexpected(id.error());
            auto texts = history.texts_of(*id);
            if (!texts) return std::unexpected(texts.error());
            auto applied = apply_texts(*texts);
            if (!applied) return std::unexpected(applied.error());
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), std::format("back to {}", id->substr(0, 8)), by ? by->actor.text() : std::string("person"),
                                         agent::tree_hash(tree));
            shown_commit.clear();
            if (commit) applied->set("commit", commit_value(*commit));
            return applied;
        });
        add("design.history.diff", [this](const Value& args) -> VerbResult {
            const std::string from(args.get("from") ? args.get("from")->as_string() : "HEAD");
            auto before = history.texts_of(from);
            if (!before) return std::unexpected(before.error());
            std::map<std::string, std::string> after;
            if (const Value* to = args.get("to")) {
                auto texts = history.texts_of(to->as_string());
                if (!texts) return std::unexpected(texts.error());
                after = std::move(*texts);
            } else {
                after = design_texts();  // what runs now
            }
            Value::List files;
            std::set<std::string> paths;
            for (const auto& [path, text] : *before) paths.insert(path);
            for (const auto& [path, text] : after) paths.insert(path);
            for (const auto& path : paths) {
                const std::string old_text = before->contains(path) ? before->at(path) : std::string();
                const std::string new_text = after.contains(path) ? after.at(path) : std::string();
                if (old_text != new_text) files.push_back(changed_lines(old_text, new_text).set("path", path));
            }
            return Value{}.set("from", from).set("to", args.get("to") ? *args.get("to") : Value("running")).set("files", Value(std::move(files)));
        });
        add("design.history.merge", [this](const Value& args) -> VerbResult {
            const std::string from(args.get("from") ? args.get("from")->as_string() : "");
            auto theirs_id = history.resolve(from);
            auto ours_id = history.resolve("HEAD");
            if (!theirs_id) return std::unexpected(theirs_id.error());
            if (!ours_id) return std::unexpected(std::string("nothing committed on this branch to merge into"));
            const auto base_id = history.common_ancestor("HEAD", *theirs_id);
            auto ours = history.texts_of(*ours_id), theirs = history.texts_of(*theirs_id);
            if (!ours || !theirs) return std::unexpected(!ours ? ours.error() : theirs.error());
            std::map<std::string, std::string> base;
            if (base_id)
                if (auto texts = history.texts_of(*base_id)) base = std::move(*texts);
            std::map<std::string, std::string> merged;
            Value::List conflicts;
            std::set<std::string> paths;
            for (const auto& [path, text] : *ours) paths.insert(path);
            for (const auto& [path, text] : *theirs) paths.insert(path);
            for (const auto& path : paths) {
                const std::string base_text = base.contains(path) ? base.at(path) : std::string();
                const std::string our_text = ours->contains(path) ? ours->at(path) : base_text;
                const std::string their_text = theirs->contains(path) ? theirs->at(path) : base_text;
                const TextMerge result = merge_text(base_text, our_text, their_text);
                if (result.conflict) conflicts.push_back(Value(path));
                merged.emplace(path, result.text);
            }
            if (!conflicts.empty())
                return std::unexpected(std::format("both branches changed the same lines of {}: nothing merged — design.edit one side to agree, commit, and merge again",
                                                   Value(conflicts).json()));
            auto applied = apply_texts(merged);
            if (!applied) return std::unexpected(applied.error());
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), std::format("merge {}", from), by ? by->actor.text() : std::string("person"), agent::tree_hash(tree),
                                         std::vector<std::string>{*ours_id, *theirs_id});
            shown_commit.clear();
            if (commit) applied->set("commit", commit_value(*commit));
            return applied;
        });
        add("design.history.tag", [this](const Value& args) -> VerbResult {
            const std::string name(args.get("name") ? args.get("name")->as_string() : "");
            const Value* at = args.get("at");
            auto tagged = history.tag(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
            if (!tagged) return std::unexpected(tagged.error());
            return Value{}.set("tag", name).set("at", *tagged);
        });
        // Previews and the variants board, in a stand-in (D-149 §2.12.3).
        add("design.preview", [this](const Value& args) -> VerbResult {
            auto texts = with_edits(args);
            if (!texts) return std::unexpected(texts.error());
            auto shown = stand_in(*texts);
            if (!shown) return std::unexpected(shown.error());
            previewing = std::move(*texts);
            show_preview(shown->picture);
            return Value{}.set("picture", generic_utf8(shown->picture)).set("changes", shown->changes);
        });
        add("design.preview.accept", [this](const Value&) -> VerbResult { return accept_preview(); });
        add("design.preview.reject", [this](const Value&) -> VerbResult {
            if (!previewing) return std::unexpected(std::string("no preview is shown"));
            reject_preview();
            return Value{}.set("rejected", true);
        });
        add("design.variants", [this](const Value& args) -> VerbResult {
            const std::string brief(args.get("brief") ? args.get("brief")->as_string() : "");
            const Value* list = args.get("variants");
            if (brief.empty() || !list || !list->list() || list->list()->size() < 2 || list->list()->size() > 6)
                return std::unexpected(std::string("design.variants takes a `brief` and 2 to 6 `variants`: [{name, edits: [{path, text}]}]"));
            const auto by = rt.current_invocation();
            std::vector<Card> cards;
            std::map<std::string, std::string> branches;
            Value::List answered;
            for (const Value& variant : *list->list()) {
                const std::string name(variant.get("name") ? variant.get("name")->as_string() : "");
                if (!valid_ref_name(name) || name.contains('/')) return std::unexpected(std::format("`{}` cannot name a variant (letters, digits, - _ .)", name));
                auto texts = with_edits(variant);
                if (!texts) return std::unexpected(std::format("{}: {}", name, texts.error()));
                auto shown = stand_in(*texts);
                if (!shown) return std::unexpected(std::format("{}: {}", name, shown.error()));
                // Each variant is a branch of its own (D-151), its picture the commit's.
                const std::string branch = std::format("variant/{}", name);
                auto commit = history.commit_on(branch, *texts, std::format("{}: {}", brief, name), by ? by->actor.text() : std::string("person"), std::string());
                if (commit) (void)history.set_thumbnail(commit->id, shown->picture);
                branches[name] = branch;
                cards.push_back({name, shown->picture});
                answered.push_back(Value{}
                                       .set("name", name)
                                       .set("branch", branch)
                                       .set("commit", commit ? Value(commit->id) : Value{})
                                       .set("picture", generic_utf8(shown->picture))
                                       .set("changes", shown->changes));
            }
            variants = std::move(branches);
            show_board(cards, brief);
            return Value{}.set("brief", brief).set("variants", Value(std::move(answered)));
        });
        add("design.variants.pick", [this](const Value& args) -> VerbResult {
            return pick_variant(std::string(args.get("name") ? args.get("name")->as_string() : ""));
        });
        add("design.variants.close", [this](const Value&) -> VerbResult {
            if (variants.empty()) return std::unexpected(std::string("no variants are shown"));
            variants.clear();
            rt.unmount("design-variants");
            return Value{}.set("closed", true);
        });
        add("design.expect", [this](const Value& args) -> VerbResult {
            if (args.get("query") || args.get("motion")) {
                auto expected = expectation_of(args);
                if (!expected) return std::unexpected(expected.error());
                expectations.push_back(std::move(*expected));
            }
            Value::List list;
            for (const auto& x : expectations) list.push_back(expectation_value(x));
            return Value{}.set("expected", Value(std::move(list)));
        });
        add("design.check",
            [this](const Value&) -> VerbResult { return check_expectations(expectations); });
        add("design.handover",
            [this](const Value&) -> VerbResult {
                const auto journal = rt.action_journal();
                handed_over_at = journal.empty() ? 0 : journal.back().id;
                if (const auto by = rt.current_invocation())
                    rt.note({.actor = by->actor.text(), .text = "Your turn: try it, and tell me when you are done", .hands_back = true});
                return Value{}.set("from", static_cast<double>(handed_over_at));
            });
        add("design.save_scenario",
            [this](const Value& args) -> VerbResult {
                const std::string name(args.get("name") ? args.get("name")->as_string() : "");
                if (name.empty() || name.find_first_of("/\\.") != std::string::npos) return std::unexpected(std::string("`name`: a scenario's name, no path"));
                Value::List steps;
                for (const auto& entry : rt.action_journal())
                    if (entry.id > handed_over_at && entry.actor.starts_with("person:") && entry.exit_code == 0 && entry.action != "input")
                        steps.push_back(Value{}.set("node", entry.path).set("action", entry.action).set("args", entry.args));
                Value::List expect;
                for (const auto& x : expectations) expect.push_back(expectation_value(x));
                const Value scenario = Value{}.set("omui", "0.1").set("scenario", name).set("steps", Value(steps)).set("expect", Value(std::move(expect)));
                const auto file = player.manifest().base / "tests" / (name + ".scenario.omui.json");
                std::error_code error;
                std::filesystem::create_directories(file.parent_path(), error);
                std::ofstream(file, std::ios::binary) << scenario.json() << '\n';
                return Value{}.set("file", generic_utf8(file)).set("steps", static_cast<double>(steps.size()));
            });
        add("design.replay",
            [this](const Value& args) -> VerbResult {
                const std::string name(args.get("name") ? args.get("name")->as_string() : "");
                const auto text = read_file(player.manifest().base / "tests" / (name + ".scenario.omui.json"));
                if (!text) return std::unexpected(std::format("no scenario `{}` (tests/{}.scenario.omui.json)", name, name));
                auto scenario = parse_json(*text);
                if (!scenario) return std::unexpected(std::format("{}: {}", name, scenario.error().text()));
                semantic::Tree tree(rt);
                Invocation as = rt.current_invocation().value_or(Invocation{});
                Value::List done;
                if (const Value* steps = scenario->get("steps"); steps && steps->list())
                    for (const Value& step : *steps->list()) {
                        as.args = step.get("args") ? *step.get("args") : Value{};
                        const Envelope e = tree.act(step.get("node") ? step.get("node")->as_string() : "", step.get("action") ? step.get("action")->as_string() : "", as);
                        FrameInput still;
                        still.time = rt.now();
                        (void)rt.frame(still);
                        done.push_back(Value{}.set("node", step.get("node") ? *step.get("node") : Value{}).set("exit", static_cast<double>(e.exit_code().value_or(1))));
                    }
                std::vector<Expectation> expected;
                if (const Value* list = scenario->get("expect"); list && list->list())
                    for (const Value& x : *list->list())
                        if (auto one = expectation_of(x)) expected.push_back(std::move(*one));
                return check_expectations(expected).set("steps", Value(std::move(done)));
            });
        add("design.discard", [this](const Value&) -> VerbResult { return player.discard_edits(); });
        add("design.commit", [this](const Value&) -> VerbResult { return player.commit_edits(); });
    }
};

std::unique_ptr<player::Designer> make(Player& designed) { return std::make_unique<Design>(designed); }

// Linked into a program, it says so before the program's main: a player in development mode then has it.
[[maybe_unused]] const bool provided = (player::provide_designer(&make), true);

}  // namespace

}  // namespace omui::designer
