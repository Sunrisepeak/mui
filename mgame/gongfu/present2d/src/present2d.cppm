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
    float        rest = 0;     // its angle to its parent, radians, when no clip turns it
};

enum class Shape : std::uint8_t { capsule, ellipse, polygon };

struct Part {
    std::int32_t       bone = 0;
    Shape              shape = Shape::capsule;
    float              start = 8, end = 8;  // a capsule's radius at the joint and at the tip; an ellipse's half-axes
    std::vector<Point> points;              // a polygon, in the bone's frame
    Color              color;
    std::int32_t       depth = 0;           // drawn back to front
};

// Each bone's angle (radians, to its parent; its rest added) and the body's
// rise (pixels), keyed by frame.
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
};

// Where each bone lies on the stage at a frame: its joint and its tip.
struct BonePlace {
    Point  joint, tip;
    double angle = 0;  // on the stage, radians, counter-clockwise from +x
};
using Pose = std::vector<BonePlace>;

[[nodiscard]] std::string_view clip_name(const Fighter& fighter, const FighterState& state) noexcept;
[[nodiscard]] Pose pose(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame);

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

// The camera a frame is drawn with: frame_camera's, shaken by a heavy hit.
[[nodiscard]] Camera view_camera(const Match& match, const Effects& effects, const Scene& scene);
// A point of the stage (x along it, y up) on the picture (y down), through `camera`:
// what an overlay (the boxes of training) lines up with.
[[nodiscard]] Point on_picture(const Scene& scene, const Camera& camera, double x, double y) noexcept;

// The frame: the stage, both fighters, the sparks of recent events and the
// shake of a heavy hit, into `out` (picture coordinates, y down).
void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, omui::render::DisplayList& out);

}  // namespace gongfu::present2d
