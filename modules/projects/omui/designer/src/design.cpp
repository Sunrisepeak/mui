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
import mlibs.agent;
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

class Design final : public player::Designer, private DesignSources {
public:
    explicit Design(Player& designed) : player(designed), rt(designed.runtime()), history(designed.manifest().base / ".omui" / "design") { verbs(); }
    ~Design() override {
        // Its verbs and its overlays go with it.
        if (card_timer) rt.cancel(card_timer);
        for (const auto& name : added) (void)rt.verbs().remove(name);
        for (const std::string_view slot : {"design-preview", "design-variants", "design-timeline", "design-code"}) rt.unmount(slot);
    }

private:
    Player&                  player;
    Runtime&                 rt;
    std::vector<std::string> added;  // the verbs it added
    // Live Design's history (D-151): states of the design on branches, beside the application.
    mlibs::agent::History history;
    HistoryVerbs  held{rt, history, *this};  // the history's verbs, over the player's sources (DesignSources below)
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
        held.see();  // the branch's last commit, the step's
        refresh_timeline();
        return done;
    }

    // DesignSources: the player's sources, an edit run at once.
    std::map<std::string, std::string> texts() override { return design_texts(); }
    std::expected<Value, std::string> apply(const std::map<std::string, std::string>& texts, Runtime::DesignPresentation::Kind kind) override {
        return apply_texts(texts, kind);
    }
    std::string tree_hash() override {
        semantic::Tree tree(rt);
        return agent::tree_hash(tree);
    }

    // Every source's text as it runs now (edited, or its file's): a design's state.
    std::map<std::string, std::string> design_texts() {
        std::map<std::string, std::string> out;
        for (const Player::Source& source : player.sources())
            if (const auto text = player.text(source.path)) out.emplace(source.path, *text);
        return out;
    }

    // A state of the design put into the running application: each source
    // whose text differs edited as design.edit edits it, presented as `kind`
    // says (an edit grows in; an undo plays back; a switch carries what both
    // share), at the design's pace.
    std::expected<Value, std::string> apply_texts(const std::map<std::string, std::string>& texts,
                                                  Runtime::DesignPresentation::Kind kind = Runtime::DesignPresentation::Kind::edit) {
        rt.set_design_presentation({kind, pace});
        struct Back {
            Runtime& rt;
            double   pace;
            ~Back() { rt.set_design_presentation({Runtime::DesignPresentation::Kind::edit, pace}); }
        } back{rt, pace};
        Value::List changed;
        for (const auto& [path, text] : texts) {
            const auto now = player.text(path);
            if (now && *now == text) continue;
            auto done = player.edit(path, text);
            if (!done) return std::unexpected(std::format("{}: {}", path, done.error()));
            changed.push_back(done->set("path", path));
        }
        return Value{}.set("edits", Value(std::move(changed)))
            .set("presented", kind == Runtime::DesignPresentation::Kind::undo        ? "undo"
                              : kind == Runtime::DesignPresentation::Kind::switch_to ? "switch"
                                                                                      : "edit");
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

    void show_preview(const std::filesystem::path& picture, const std::filesystem::path& now = {}) {
        rt.mount("design-preview",
                 preview_view(rt.size(), picture, [this](const Value&) { (void)accept_preview(); }, [this](const Value&) { reject_preview(); }, now));
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

    // The board as it is shown (its cards and brief), to fly a picked card back from.
    std::vector<Card> board_cards;
    std::string       board_brief;
    std::uint64_t     board_leaving = 0;  // the timer that takes the board away once a card has flown

    void show_board(const std::vector<Card>& cards, std::string_view brief, std::string_view flying = {}) {
        board_cards = cards;
        board_brief = std::string(brief);
        rt.mount("design-variants", board_view(rt.size(), brief, cards, [this](const std::string& name) { (void)pick_variant(name); }, flying));
    }
    // The picked card flies from the board to fill the window (the same
    // nodes, springing to their new places) while the application becomes it,
    // a switch carrying what the two share; then the board goes.
    VerbResult pick_variant(const std::string& name) {
        const auto branch = variants.find(name);
        if (branch == variants.end()) return std::unexpected(std::format("no variant `{}` on the board", name));
        auto texts = history.texts_of(branch->second);
        if (!texts) return std::unexpected(texts.error());
        variants.clear();
        show_board(board_cards, board_brief, name);
        board_leaving = rt.after(seconds(0.7 / pace), [this] {
            board_leaving = 0;
            if (variants.empty()) rt.unmount("design-variants");
        });
        return step(apply_texts(*texts, Runtime::DesignPresentation::Kind::switch_to), std::format("variant {} picked", name))
            .transform([&](Value done) { return done.set("flying", name); });
    }

    // The timeline (D-151 §2.14): shown or not, two commits being compared,
    // what the last press came to, and the commit the application shows.
    bool                     timeline_open = false, comparing = false;
    std::vector<std::string> compared;
    std::string              timeline_note;
    double                   pace = 1;  // the design steps' presentation: its time scaled (design.pace)

    // Every commit the branches reach, oldest first.
    std::vector<mlibs::agent::Commit> commits_in_time() const {
        std::map<std::string, mlibs::agent::Commit> all;
        for (const auto& [branch, head] : history.branches())
            for (mlibs::agent::Commit& commit : history.log(branch, 500)) all.try_emplace(commit.id, std::move(commit));
        std::vector<mlibs::agent::Commit> out;
        for (auto& [id, commit] : all) out.push_back(std::move(commit));
        std::ranges::sort(out, [](const mlibs::agent::Commit& left, const mlibs::agent::Commit& right) {
            return left.time != right.time ? left.time < right.time : left.id < right.id;
        });
        return out;
    }

    std::string shown_now() const { return held.shown_now(); }

    Timeline timeline_model() const {
        Timeline model;
        model.note = timeline_note.empty() ? std::string("Press a commit to see it; ◀ ▶ go through time; Compare, then two commits") : timeline_note;
        model.comparing = comparing;
        const auto in_time = commits_in_time();
        model.columns = in_time.size();
        const std::string head = history.head_branch(), shown = shown_now();
        std::map<std::string, TimelineLane> lanes;
        for (std::size_t column = 0; column < in_time.size(); ++column) {
            const mlibs::agent::Commit& commit = in_time[column];
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
                                .scrub = [this](Scrub kind, std::size_t column) { scrub(kind, column); },
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

    // The scrubber dragged (D-151 §2.14): every commit the pointer passes is
    // shown as it passes — the history played back, as fast as the hand goes.
    bool scrubbing = false;
    void scrub(Scrub kind, std::size_t column) {
        if (kind == Scrub::press) scrubbing = true;
        if (!scrubbing) return;  // the pointer only passing over it
        if (kind == Scrub::release) scrubbing = false;
        const auto in_time = commits_in_time();
        if (in_time.empty()) return;
        const mlibs::agent::Commit& reached = in_time[std::min(column, in_time.size() - 1)];
        if (reached.id != shown_now()) (void)see_commit(reached.id);
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
        auto applied = apply_texts(*texts, Runtime::DesignPresentation::Kind::switch_to);
        if (!applied) return std::unexpected(applied.error());
        held.see(id);
        if (const auto commit = history.commit_of(id)) timeline_note = std::format("{} — {}", id.substr(0, 8), commit->message);
        refresh_timeline();
        return applied->set("at", id);
    }

    std::expected<Value, std::string> step_in_time(int by) {
        const auto in_time = commits_in_time();
        if (in_time.empty()) return std::unexpected(std::string("the design has no commits yet"));
        const std::string shown = shown_now();
        const auto at = std::ranges::find(in_time, shown, &mlibs::agent::Commit::id);
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
        const auto number = [&](const Value& from, std::string_view key, double fallback, double low, double high) {
            const Value* given = from.get(key);
            return std::clamp(given && given->is_number() ? given->as_double() : fallback, low, high);
        };
        const auto path_of = [](std::string_view text) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); };
        const std::string out_text(args.get("out") ? args.get("out")->as_string() : std::string());
        const std::filesystem::path out = out_text.empty() ? history.directory() / "film" / branch : path_of(out_text);

        // The film's script (D-151: a replay is edited — its pace, its shots, its words): what the
        // person or an agent changed in it is followed, a step it does not name yet is added as it
        // would be filmed, and the merged script written back, for the next edit.
        const std::string script_text(args.get("script") ? args.get("script")->as_string() : std::string());
        const std::filesystem::path script_file = script_text.empty() ? history.directory() / "film" / std::format("{}.film.omui.json", branch) : path_of(script_text);
        Value script(Value::Map{});
        if (std::ifstream in{script_file, std::ios::binary}) {
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto read = parse_json(text);
            if (!read) return std::unexpected(std::format("{}:{}", generic_utf8(script_file), read.error().text()));
            if (const Value* film = read->get("film")) script = *film;
        }
        const int fps = static_cast<int>(number(script.get("fps") ? script : args, "fps", 30, 1, 60));
        const double each = number(args, "seconds", 1.5, 0.2, 10);
        struct Shot {
            std::string                commit;
            std::string                caption;
            double                     seconds = 1.5;
            bool                       keep = true;
            std::optional<std::string> node;  // the camera on what was built there, framed
            std::optional<Rect>        rect;  // or on a region of the window
        };
        std::vector<Shot> shots;
        const Value::List* written = script.get("steps") ? script.get("steps")->list() : nullptr;
        for (std::size_t index = 0; index < commits.size(); ++index) {
            // What the step did (the session's intent, which every step's message begins with, said once by the first).
            std::string_view said = commits[index].message;
            if (const auto dash = said.find(" — "); dash != std::string_view::npos && index > 0) said.remove_prefix(dash + std::string_view(" — ").size());
            Shot shot{.commit = commits[index].id, .caption = std::format("{}/{} · {}", index + 1, commits.size(), said), .seconds = each};
            if (written)
                for (const Value& step : *written) {
                    const std::string_view named = step.get("commit") ? step.get("commit")->as_string() : std::string_view{};
                    if (named.size() < 7 || !commits[index].id.starts_with(named)) continue;
                    if (const Value* caption = step.get("caption"); caption && caption->type() == Value::Type::text) shot.caption = caption->as_string();
                    shot.seconds = number(step, "seconds", each, 0.1, 30);
                    if (const Value* keep = step.get("keep")) shot.keep = keep->as_bool(true);
                    if (const Value* camera = step.get("camera"); camera && camera->map()) {
                        if (const Value* node = camera->get("node")) shot.node = std::string(node->as_string());
                        if (const Value* rect = camera->get("rect"); rect && rect->list() && rect->list()->size() == 4) {
                            const auto& r = *rect->list();
                            shot.rect = Rect{static_cast<float>(r[0].as_double()), static_cast<float>(r[1].as_double()), static_cast<float>(r[2].as_double()),
                                             static_cast<float>(r[3].as_double())};
                        }
                    }
                    break;
                }
            shots.push_back(std::move(shot));
        }
        {
            Value::List steps;
            for (const Shot& shot : shots) {
                Value camera;  // null: the whole window
                if (shot.node) camera = Value{}.set("node", *shot.node);
                else if (shot.rect) camera = Value{}.set("rect", Value(Value::List{Value(shot.rect->x), Value(shot.rect->y), Value(shot.rect->width), Value(shot.rect->height)}));
                steps.push_back(Value{}.set("commit", shot.commit.substr(0, 12)).set("caption", shot.caption).set("seconds", shot.seconds).set("keep", shot.keep).set("camera", camera));
            }
            const Value written_back = Value(Value::Map{})
                                           .set("doc", "The film of a design branch, one step a commit: its caption, its time on screen, whether it is kept, and a camera "
                                                       "(null: the whole window; {node: a path}: what was built there, framed; {rect: [x, y, width, height]}). "
                                                       "Edit it and film again (design.film).")
                                           .set("branch", branch)
                                           .set("fps", fps)
                                           .set("steps", Value(std::move(steps)));
            std::error_code made;
            std::filesystem::create_directories(script_file.parent_path(), made);
            std::ofstream(script_file, std::ios::binary) << indented_json(Value(Value::Map{}).set("film", written_back));
        }

        std::error_code error;
        std::filesystem::remove_all(out / "frames", error);
        std::filesystem::create_directories(out / "frames", error);
        if (error) return std::unexpected(std::format("{}: {}", generic_utf8(out), error.message()));

        // The twin: the application again, unseen, designing as the person's does.
        auto made = twin(true);
        if (!made) return std::unexpected(made.error());
        Twin& shown = **made;
        const Size window = shown.runtime().size();
        const Rect whole{0, 0, window.width, window.height};
        Rect camera = whole;  // where the camera was when the last shot ended
        std::size_t frame = 0, steps_filmed = 0;
        double at = shown.time().count();
        const auto lerp = [](float from, float to, float amount) { return from + (to - from) * amount; };
        for (std::size_t index = 0; index < shots.size(); ++index) {
            const Shot& shot = shots[index];
            auto texts = history.texts_of(commits[index].id);
            if (!texts) return std::unexpected(texts.error());
            // A step left out is still run (the next builds on it), not filmed.
            if (auto ran = shown.player().show(*texts); !ran) return std::unexpected(std::format("{}: {}", commits[index].id.substr(0, 8), ran.error()));
            shown.settle();
            if (!shot.keep) continue;
            ++steps_filmed;
            shown.runtime().mount("design-caption", caption_view(window, shot.caption));
            // Where the camera goes: the node framed with a margin, a region, or the whole window.
            Rect target = whole;
            if (shot.rect) target = *shot.rect;
            else if (shot.node) {
                const NodeId framed = shown.runtime().reveal(*shot.node);
                if (!shown.runtime().valid(framed)) return std::unexpected(std::format("the camera's node {} is not in step {}", *shot.node, index + 1));
                const Rect box = shown.runtime().drawn_bounds(framed);
                constexpr float margin = 16;
                target = {box.x - margin, box.y - margin, box.width + 2 * margin, box.height + 2 * margin};
            }
            const auto frames_each = static_cast<std::size_t>(std::lround(shot.seconds * fps));
            const double move_frames = std::max(1.0, 0.6 * fps);  // the camera's move: the shot's first 0.6 s, eased in and out
            for (std::size_t step = 0; step < frames_each; ++step, ++frame) {
                at += 1.0 / fps;
                shown.step(seconds(at));
                const float progress = static_cast<float>(std::min(1.0, static_cast<double>(step + 1) / move_frames));
                const float eased = progress * progress * (3 - 2 * progress);
                const Rect now{lerp(camera.x, target.x, eased), lerp(camera.y, target.y, eased), lerp(camera.width, target.width, eased),
                               lerp(camera.height, target.height, eased)};
                const bool all = std::abs(now.x) < 0.25f && std::abs(now.y) < 0.25f && std::abs(now.width - whole.width) < 0.25f && std::abs(now.height - whole.height) < 0.25f;
                auto png = all ? shown.png() : shown.framed_png(now, "design-caption");
                if (!png) return std::unexpected(png.error());
                std::ofstream(out / "frames" / std::format("{:05}.png", frame), std::ios::binary)
                    .write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
            }
            camera = target;
        }
        // The description `omui video render` reads (xengine.video): the frames, at their rate.
        const auto description = out / "design.video.omui.json";
        const Value video = Value(Value::Map{})
                                .set("doc", std::format("The design of {} on {}: {} steps.", player.manifest().id, branch, steps_filmed))
                                .set("width", static_cast<int>(window.width))
                                .set("height", static_cast<int>(window.height))
                                .set("fps", fps)
                                .set("frames", static_cast<std::int64_t>(frame))
                                .set("from", Value(Value::Map{}).set("png", "frames/{:05}.png"));
        std::ofstream(description, std::ios::binary) << indented_json(Value(Value::Map{}).set("video", video));
        return Value{}
            .set("branch", branch)
            .set("steps", static_cast<double>(steps_filmed))
            .set("frames", static_cast<double>(frame))
            .set("fps", static_cast<double>(fps))
            .set("description", generic_utf8(description))
            .set("script", generic_utf8(script_file));
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

    // The code an edit wrote, beside the first node it changed, for a moment
    // (§2.12.2: the TS and the picture tied together); the next edit's replaces it.
    std::uint64_t card_timer = 0;
    void show_code(const std::string& path, const Value& answer, std::string_view before, std::string_view after) {
        const Value lines = answer.get("source") ? *answer.get("source") : changed_lines(before, after);
        if (!lines.get("line") || (lines.get("before")->as_string().empty() && lines.get("after")->as_string().empty())) return;
        std::string node;
        if (const Value* changes = answer.get("changes"))
            for (const std::string_view kind : {"changed", "added"})
                if (const Value* list = changes->get(kind); node.empty() && list && list->list() && !list->list()->empty())
                    node = std::string(list->list()->front().get("id")->as_string());
        const NodeId id = node.empty() ? NodeId{} : rt.reveal(node);
        const Rect near = rt.valid(id) ? rt.screen_bounds(id) : Rect{16, 16, 0, 0};
        rt.mount("design-code", code_card_view(rt.size(), near, std::format("{}:{}", path, lines.get("line")->as_int()), lines.get("before")->as_string(),
                                               lines.get("after")->as_string()));
        if (card_timer) rt.cancel(card_timer);
        card_timer = rt.after(seconds(3.0), [this] {
            card_timer = 0;
            rt.unmount("design-code");
        });
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
        done->set("path", place->path).set("source", changed_lines(*text, *changed));
        show_code(place->path, *done, *text, *changed);
        return done;
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
            const std::string before = player.text(path).value_or(std::string());
            auto done = step(player.edit(path, text->as_string()), std::format("edit {}", path));
            if (done) show_code(path, *done, before, text->as_string());
            return done;
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
                        std::format("{}: {}", node, [&] {
                            std::string said;
                            for (const auto& [name, value] : *properties->map()) said += std::format("{}{} = {}", said.empty() ? "" : ", ", name, value.json());
                            return said;
                        }()));
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
        // How fast the steps are seen (W14 step 4): every presentation's time, scaled.
        add("design.pace", [this](const Value& args) -> VerbResult {
            const Value* given = args.get("pace");
            if (!given || !given->is_number() || !std::isfinite(given->as_double()) || given->as_double() < 0.25 || given->as_double() > 4)
                return std::unexpected(std::string("pace is a number from 0.25 to 4 (1 as made; 2 twice as fast)"));
            const double before = pace;
            pace = given->as_double();
            rt.set_design_presentation({Runtime::DesignPresentation::Kind::edit, pace});
            return Value{}.set("pace", pace).set("before", before);
        });
        // Live Design's history (D-151): states of the design as commits on branches, under .omui/design/.
        held.add_to(add);
        // Previews and the variants board, in a stand-in (D-149 §2.12.3).
        add("design.preview", [this](const Value& args) -> VerbResult {
            auto texts = with_edits(args);
            if (!texts) return std::unexpected(texts.error());
            auto shown = stand_in(*texts);
            if (!shown) return std::unexpected(shown.error());
            // Split: what is there now beside it (the design's own texts, in a stand-in too).
            std::filesystem::path now;
            if (args.get("split") && args.get("split")->as_bool()) {
                auto current = stand_in(design_texts());
                if (!current) return std::unexpected(current.error());
                now = current->picture;
            }
            previewing = std::move(*texts);
            show_preview(shown->picture, now);
            Value answer = Value{}.set("picture", generic_utf8(shown->picture)).set("changes", shown->changes);
            if (!now.empty()) answer.set("now", generic_utf8(now));
            return answer;
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
                if (!mlibs::agent::valid_ref_name(name) || name.contains('/')) return std::unexpected(std::format("`{}` cannot name a variant (letters, digits, - _ .)", name));
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
