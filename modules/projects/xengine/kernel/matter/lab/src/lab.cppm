// xengine.matter.lab — the material lab: matter designed live (D-149, D-160, D-173; the plan's T1.13).
//
// A lab is a directory of sources — a material a file, the laws, the look, a scene to try them in — and the
// world they make, running: a field of matter on the lattice an agent or a person places things in, steps and
// asks why. A source changed is the definition prepared again, the look checked and the scene played again to
// the same step before anything is replaced; what changed in the world is the answer. The first design adapter
// that is not an interface: its history (commits, branches, merges by line) and its claims are the agent
// kernel's, the same an interface's designer uses.
//
//     auto lab = matter::lab::Lab::create("my-lab");                      // the standard definition, 64×48 of air
//     mlibs::agent::SubjectSession session((*lab)->subjects());           // lab, field, matter, frame
//     // → {"op":"matter.place","material":"wood","region":[20,40,24,4]}
//     // → {"op":"lab.material","name":"coal","properties":{"density":1400,"ignition":700,"fuel":900,"heat":120,"burnt":"ash"}}
//     //      a 13th material, a row of data: it lies, lights and burns out by the laws every material has
//     // → {"op":"lab.try","material":"wood","properties":{"ignition":900},"steps":300}   what would be, nothing changed
//     // → {"op":"lab.branch","name":"wet"} … {"op":"lab.merge","from":"wet"}             two agents' rows, both kept
//     // → {"op":"frame.capture"}                                                         the picture, as pixels
//
// Sources (JSON, kept a row a line so that a change is a line's and two changes merge):
//     materials/<name>.json   a material's row: its properties, its tags, and `look` ({color, grain}) when the
//                             pack colours it itself
//     laws.json               the laws, in order, a law a line
//     channels.json, columns.json   when the definition adds its own
//     look.json               the appearance without its materials: phases, states, nothing
//     scene.json              {shape, filled, edits}: each edit with the step it was made at and by whom
// Beside them the lab writes what a world ships: world.matter.omui.json and world.appearance.omui.json, the
// snapshots the kernel reads. How far the scene is played is not a source: it is where the field is.
export module xengine.matter.lab;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
export import xengine.cellular.matter.agent;
export import xengine.matter.appearance;
export import xrender.frame;
export import :catalog;

export namespace xengine::matter::lab {

using Texts = std::map<std::string, std::string>;

struct Options {
    int          scale = 4;            // pixels a cell of the lab's picture (1 … 16)
    std::int64_t most_steps = 20000;   // the most steps a scene is played to
    std::size_t  most_cells = 32768;   // the largest field
    std::int64_t keep = 32;            // frames of the field's history kept, to seek within
    std::function<double()> clock;     // seconds, for the claims' leases (a steady clock when none)
};

// What a lab is begun from.
struct Begin {
    std::string     matter;  // a definition's snapshot (*.matter.omui.json's text); the standard one when empty
    std::string     look;    // an appearance's snapshot (*.appearance.omui.json's text); the standard one when empty
    cellular::Shape shape{64, 48};
    std::string     filled = "air";
};

// A source's text as the lab keeps it — JSON, a row a line. Error: it is no JSON, or not what its path says.
[[nodiscard]] std::expected<std::string, std::string> normalized(std::string_view path, std::string_view text);
// The sources two snapshots and an empty scene are.
[[nodiscard]] std::expected<Texts, std::string> sources_of(const Begin& begin);
// The snapshots a set of sources is: {"matter": …} and {"appearance": …}, what the kernel reads.
[[nodiscard]] std::string definition_of(const Texts& texts);
[[nodiscard]] std::string appearance_of(const Texts& texts);
// The part of the design a source is, as a claim names it ("materials/wood.json" → "materials/wood").
[[nodiscard]] std::string part_of(std::string_view path);

class Lab final : public mlibs::agent::Subject, public mlibs::agent::Sources, private cellular::agent::Witness {
public:
    // A new lab in `directory` (made; refused when a lab is there), its first state kept as a commit.
    [[nodiscard]] static std::expected<std::unique_ptr<Lab>, std::string> create(std::filesystem::path directory, const Begin& begin = {}, Options options = {});
    // The lab in `directory`: its sources read (as they are on disk — a file changed by hand is a change not
    // committed), its scene played to its last edit.
    [[nodiscard]] static std::expected<std::unique_ptr<Lab>, std::string> open(std::filesystem::path directory, Options options = {});
    ~Lab() override;
    Lab(const Lab&) = delete;
    Lab& operator=(const Lab&) = delete;

    // A session's subjects: `lab`, `field` (time), `matter` (what is there, why, what would be), `frame` (the
    // picture). They stay the same subjects as the world is made again.
    [[nodiscard]] std::vector<mlibs::agent::Subject*> subjects();
    // A session for one who connects (an endpoint's): the same subjects, and an identity of its own —
    // `agent:session-<n>`, in the order they came — so that claims and commits tell those at the lab apart.
    [[nodiscard]] std::unique_ptr<mlibs::agent::Session> session();
    // What was asked of this lab, kept in its directory (`.lab/facts.jsonl`): the same book for every session
    // and every run, so `log` answers what an earlier run did.
    [[nodiscard]] std::shared_ptr<mlibs::agent::Facts> facts();
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    [[nodiscard]] cellular::agent::Field& field() noexcept { return *world_.field; }
    [[nodiscard]] cellular::matter_agent::Matter& matter() noexcept { return *world_.matter; }
    [[nodiscard]] const matter::Look& look() const noexcept { return look_; }
    [[nodiscard]] mlibs::agent::History& history() noexcept { return history_; }
    [[nodiscard]] mlibs::agent::Claims& claims() noexcept { return claims_; }
    [[nodiscard]] std::int64_t steps() const noexcept { return world_.field->state().steps; }
    // Steps run by the owner (a host's loop, a batch), as field.step runs them.
    void run(std::int64_t steps);
    // The field as it looks now, a cell `scale` pixels (the lab's own when 0): PNG bytes.
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> png(int scale = 0);
    // What was placed and stepped since the last write, written (scene.json). An operation does it itself.
    void settle();

    // ── as sources (mlibs.agent's history works on these) ────────────────────────
    [[nodiscard]] Texts texts() override;
    // The whole design: a source that is not among `texts` is no longer there. Nothing is replaced unless the
    // definition prepares, the look fits it and the scene plays. Answers what changed in the world.
    std::expected<std::string, std::string> apply(const Texts& texts, Reason reason) override;
    [[nodiscard]] std::string state() override;

    // ── as a subject ────────────────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "lab"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.matter.lab@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        // Time is the field's; causes: who changed which row and what for; provenance: from a property that
        // runs back to the line that says it and the commit that wrote the line.
        return {.observation = true, .budget = true, .control = true, .time = false, .causes = true, .provenance = true, .preview = true, .history = true};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::matter::lab::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::matter::lab::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override;
    [[nodiscard]] std::string recorded_at() const override;  // the commit its history stands at
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    struct Screen;  // the picture: the presenter's image, the list, the surface, the frame
    class Stand;    // `field` and `matter` to a session: whichever world runs now
    // One edit of the scene: the frame it made on the field that runs, and what scene.json says of it.
    struct Entry {
        std::int64_t frame = 0, step = 0;
        std::string  text;
    };
    // Sources made into a world, the scene played.
    struct Built {
        cellular::matter_agent::World world;
        matter::Look                  look;
        cellular::Shape               shape;
        std::string                   filled;
        std::vector<Entry>            entries;
    };
    // Why a change is not made: `conflict` (another holds the part) or `invalid-argument` (the sources).
    struct Refusal {
        std::string_view code;
        std::string      text;
    };

    Lab(std::filesystem::path directory, Options options);
    [[nodiscard]] std::expected<Built, std::string> build(const Texts& texts, std::int64_t position) const;
    // `texts` made what runs, the scene played to `position` (where the field is when none); what changed.
    [[nodiscard]] std::expected<std::string, Refusal> change_to(const Texts& texts, std::optional<std::int64_t> position = std::nullopt);
    void install(Built built, Texts texts);
    void write() const;
    void draw();
    [[nodiscard]] std::string scene_text() const;
    // What a change did to the world, the scene played under both — as members of a JSON object: `differ`,
    // cells whose material is another (where the shapes are one), and `materials`, those there are more or
    // fewer of: [then, now].
    [[nodiscard]] static std::string difference(const cellular::matter_agent::World& then, const cellular::matter_agent::World& now);
    [[nodiscard]] double now() const;
    // The claim another holds over one of `parts`, to the one acting now (never to a person, or the owner's code).
    [[nodiscard]] std::optional<mlibs::agent::Claim> held(std::span<const std::string> parts) const;
    [[nodiscard]] std::string held_text(const mlibs::agent::Claim& claim) const;
    // The last commit's picture kept, when it has none.
    void picture_head();

    // ── the field's witness: the scene's edits as they are made ─────────────────────
    void stepped(const cellular::agent::FieldState&, std::span<const std::uint32_t>, std::span<const std::uint8_t>, std::int64_t) override {}
    void edited(const cellular::agent::FieldState& now, const cellular::agent::Edited& edit) override;
    void rewound(std::int64_t frame) override;

    std::filesystem::path          directory_;
    Options                        options_;
    Texts                          texts_;    // every source as kept; the scene's as of the last settle
    cellular::matter_agent::World  world_;
    matter::Look                   look_;
    cellular::Shape                shape_;
    std::string                    filled_;
    std::vector<Entry>             entries_;
    bool                           scene_changed_ = false;
    std::unique_ptr<Screen>        screen_;
    std::uint64_t                  drawn_ = 0;  // the field's hash the picture is of (0: none drawn)
    xrender::FrameSubject          picture_;
    mlibs::agent::History          history_;
    std::shared_ptr<mlibs::agent::Facts> facts_;
    mlibs::agent::HistoryOperations operations_;
    mlibs::agent::Claims           claims_;
    std::unique_ptr<Stand>         field_stand_, matter_stand_;
    const openas::Actor*           acting_ = nullptr;  // whose request is being answered
    int                            sessions_ = 0;      // how many have connected
    std::string_view               refused_;           // the code of the last refusal a change met
};

}  // namespace xengine::matter::lab
