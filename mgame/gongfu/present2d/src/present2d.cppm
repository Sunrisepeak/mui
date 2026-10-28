// gongfu.present2d — a gongfu match drawn in 2D (D-85 §8.4).
//
// A Look is how a fighter appears: a skeleton (bones, each with a joint on
// its parent and a length), vector parts on the bones (capsules, ellipses,
// polygons, an outline drawn behind them), and clips — each bone's angle and
// the body's rise, keyed by frame, with hanim's easing. Which clip plays is
// what the fighter does: its move by name ("5LP", "236P"), else its mode
// ("idle", "walk_forward", "crouch", "hitstun", "knockdown", …); a move's
// clip is read at the move's frame, a stun's at the frames it has taken, a
// neutral clip at the match's frame, looping. So a pose is a pure function of
// the state: a rollback restores the state and the picture follows.
//
// The Effects keep the match's events by frame (hits, blocks, counters,
// throws, KOs) for the sparks and shakes that last a few frames; after a
// rollback, rewind() forgets the events of the frames played again. All
// trigonometry is hanim's (hanim::dm), so a frame is the same bytes on every
// machine.
export module gongfu.present2d;

import std;
export import gongfu;
export import gongfu.data;
import hanim;
import openmui.geom;
import openmui.render.vector;

export namespace gongfu::present2d {

using Color = omui::geom::Color;
using Point = omui::geom::Point;

struct Bone {
    std::string  name;
    std::int32_t parent = -1;  // the root's parent is the fighter's feet
    Point        joint;        // where it attaches: on its parent, in the parent's frame (x along it); for the root, above the feet
    float        length = 0;   // pixels
    float        rest = 0;     // its angle to its parent, degrees counter-clockwise (the root's: to the stage's +x), when no clip turns it
};

enum class Shape : std::uint8_t { capsule, ellipse, polygon };

struct Part {
    std::int32_t       bone = 0;
    Shape              shape = Shape::capsule;
    float              start = 8, end = 8;  // a capsule's radius at the joint and at the tip; an ellipse's half-axes
    std::vector<Point> points;              // a polygon, in the bone's frame
    Color              color;
    std::int32_t       depth = 0;           // drawn back to front
    std::int32_t       slot = -1;           // its colour's slot in the look's palettes (−1: `color` as it is)
};

// How a contact feels, drawn (D-85 §8.4). A fighter file's hits name a
// profile (`impact = "heavy"`); the look says what it is: the shake, the
// camera's punch in, a flash, the spark, the sounds (names a sound bank
// resolves) and the rumble — a pure function of the event and its age.
enum class Spark : std::uint8_t { rays, ring, splash, shards, bolt };
enum class Flash : std::uint8_t { none, white, invert };

struct Impact {
    std::string                name;
    float                      shake = 0;         // pixels at contact, easing to nothing
    std::int32_t               shake_frames = 0;
    float                      zoom = 0;          // the camera closer by this fraction at contact
    std::int32_t               zoom_frames = 0;
    Flash                      flash = Flash::none;
    std::int32_t               flash_frames = 0;
    Spark                      spark = Spark::rays;
    float                      spark_size = 28;   // pixels at full reach
    std::int32_t               spark_count = 8;
    std::int32_t               spark_frames = 16;
    Color                      spark_color = Color::hex(0xffd166);
    Color                      counter_color = Color::hex(0xff4d5e);  // the spark of a counter hit
    std::array<std::string, 3> sound;             // attack, body, tail
    float                      rumble = 0;        // 0–1
    std::int32_t               rumble_frames = 0;
};

// What a contact without a profile of its own looks like: a hit's, a block's, a KO's.
[[nodiscard]] const Impact& default_impact(Event::Kind kind) noexcept;

// Each bone's angle (degrees, to its parent; its rest added) and the body's
// rise (pixels), keyed by frame (a key at frame f is at f/60 s).
struct Clip {
    std::int32_t                    frames = 1;
    bool                            loop = false;
    std::vector<hanim::Track<double>> angles;  // by bone; a missing one keeps its rest
    hanim::Track<double>            rise;
};

struct Look {
    std::string                 name;
    std::vector<Bone>           bones;
    std::vector<Part>           parts;
    std::map<std::string, Clip, std::less<>> clips;
    Color                       outline = Color::hex(0x101014);
    float                       outline_width = 3;
    // Colour schemes: each a colour per slot (`slots` names them); a part with
    // a slot takes its colour from the palette in use.
    std::vector<std::string>              slots;
    std::vector<std::vector<Color>>       palettes;
    std::vector<std::string>              palette_names;
    std::vector<Impact>                   impacts;  // by name; "block" and "ko" for those contacts
    // Bound to a fighter (bind): its FighterDef::impacts index → impacts' (−1: none).
    std::vector<std::int32_t>             impact_of;
};

// Colours the parts from palette `index` (none: unchanged).
void use_palette(Look& look, std::size_t index);
// Matches the fighter's impact names to the look's profiles.
void bind(Look& look, const Fighter& fighter);
// The profile of a contact by the fighter this look draws: its hit's, the
// look's "block" or "ko", else default_impact.
[[nodiscard]] const Impact& impact_for(const Look& look, const Event& event) noexcept;

// Where each bone lies on the stage at a frame: its joint and its tip.
struct BonePlace {
    Point  joint, tip;
    double angle = 0;  // on the stage, radians, counter-clockwise from +x
};
using Pose = std::vector<BonePlace>;

[[nodiscard]] std::string_view clip_name(const Fighter& fighter, const FighterState& state) noexcept;
[[nodiscard]] Pose pose(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame);
// The same into `out` (its storage reused: no allocation once it has the bones' room).
void pose(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame, Pose& out);

// A look as a file, `*.look.gongfu.toml` — what gongfu.studio writes and a
// mod will carry: the palettes, the bones (angles in degrees), the parts, the
// clips (keys by frame) and the impact profiles, by name. Every problem at
// once with its line and column, a misspelt key with the nearest right one.
// write_look gives the file back; what it writes reads back as the same look.
[[nodiscard]] std::expected<Look, std::vector<data::Diagnostic>> read_look(std::string_view source, std::string_view file);
[[nodiscard]] std::string write_look(const Look& look);

// The stage's view: what lies at the middle of the picture, and how large.
struct Camera {
    double x = 0, y = 0, zoom = 1;
};

struct Scene {
    float width = 960, height = 540;  // the picture, pixels
    float ground = 470;               // where the stage's ground lies on it
    Color sky_top = Color::hex(0x1b1f2a), sky_bottom = Color::hex(0x3a3f52), floor = Color::hex(0x23252e), line = Color::hex(0x8a8fa3);
};

// Keeps both fighters in view, zoomed out as they part, following a jump.
[[nodiscard]] Camera frame_camera(const Match& match, const Scene& scene);

class Effects {
public:
    // After each tick: its events, at its frame.
    void record(const Match& match);
    // After a rollback to `frame`: the events of frames from it on are played again.
    void rewind(std::int32_t frame);
    struct Kept {
        std::int32_t frame;
        Event        event;
    };
    [[nodiscard]] std::span<const Kept> kept() const noexcept { return kept_; }

private:
    std::vector<Kept> kept_;
};

// The camera a frame is drawn with: frame_camera's, punched in and shaken by
// the latest contact as its impact profile says.
[[nodiscard]] Camera view_camera(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene);
// A point of the stage (x along it, y up) on the picture (y down), through `camera`:
// what an overlay (the boxes of training) lines up with.
[[nodiscard]] Point on_picture(const Scene& scene, const Camera& camera, double x, double y) noexcept;

// Draws frames, keeping what a frame needs between them (poses, the parts'
// order, a path): after the first frame, drawing one allocates nothing (the
// display list's arena aside, itself reused when the list is cleared).
class Painter {
public:
    // The frame: the stage, both fighters, the sparks of recent events and the
    // shake of a heavy hit, into `out` (picture coordinates, y down).
    void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, omui::render::DisplayList& out);

private:
    Pose                     pose_;
    std::vector<std::size_t> order_;
    omui::render::Path       path_;
};

// One frame with a painter of its own (the same picture; allocates).
void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, omui::render::DisplayList& out);

}  // namespace gongfu::present2d
