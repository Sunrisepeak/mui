// gongfu.present2d — a gongfu match drawn in 2D (D-85 §8.4).
//
// A Look is how a fighter appears: a skeleton (bones, each with a joint on
// its parent and a length), vector parts on the bones (capsules, ellipses,
// polygons, an outline drawn behind them), and clips — each bone's angle and
// the body's rise, keyed by frame, with hanim's easing. Which clip plays is
// what the fighter does: its move by name ("5LP", "236P"; a clip 236P also
// plays for 236LP, 236MP and 236HP), else its mode
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
    bool         limb = false; // a limb starts here (an arm, a leg): its parts can reach out of the body's hurtbox
    bool         prop = false; // a thing held (a brush, a sword): its parts and its children's are never hurtboxes
    // Secondary motion (a sleeve, hair, a sash): the bone lags its own swing,
    // turned back by `amount` of how far it swung over the last `lag` frames
    // (at most `max` degrees). Drawn only — hurtboxes never follow — and a
    // pure function of the clip's time, like the rest of a pose.
    // `drag` (degrees a pixel) trails it behind where its joint went over those
    // frames, the body's own travel included: cloth behind a running fighter.
    struct Follow {
        std::int32_t lag = 0;
        float        amount = 0;
        float        max = 60;
        float        drag = 0;
    } follow;
    // Hanging (cloth, hair, a sash's end): its angle is to the world, not to
    // its parent — it hangs whichever way the parent turns (rest −90: straight down).
    bool         hang = false;
};

// A curve is a closed, smooth shape through its points (Catmull-Rom): a
// sleeve, a lock of hair, a fist drawn as it should look.
// An image is a picture's region on a bone (drawn art: an artist's or a
// generated part), bent by skinning over a grid of `mesh` cells.
enum class Shape : std::uint8_t { capsule, ellipse, polygon, curve, image };

struct Part {
    std::int32_t       bone = 0;
    Shape              shape = Shape::capsule;
    float              start = 8, end = 8;  // a capsule's radius at the joint and at the tip; an ellipse's half-axes
    std::vector<Point> points;              // a polygon's or a curve's, in the bone's frame
    Color              color;
    std::int32_t       depth = 0;           // drawn back to front
    std::int32_t       slot = -1;           // its colour's slot in the look's palettes (−1: `color` as it is)
    std::string        name;                // what a clip's swaps call it (none: unnamed)
    // Drawings it can swap to (a polygon's or a curve's other points): a key
    // pose drawn by hand where turning bones is not enough — a fist in
    // perspective, a sleeve flung out. A clip's swaps choose (0: `points`).
    std::vector<std::vector<Point>> variants;
    bool               shade = true;        // lit by the look's light (when it has one)
    // An image part: a region of picture `picture` (pixels), its `anchor` on
    // the bone's joint and its `tip` along the bone (they set its turn and its
    // size: the bone's length from anchor to tip); a grid of `mesh` cells,
    // skinned to its bone and to `bend` (the next bones along: an elbow, a
    // knee, a hem), each vertex's weight going over to a bend bone across
    // `blend` pixels about that bone's joint.
    std::int32_t              picture = -1;
    omui::geom::Rect          source;
    Point                     anchor, tip;
    std::array<std::int32_t, 2> mesh{1, 1};
    std::vector<std::int32_t> bend;
    float                     blend = 16;
    bool                      fit = false;  // sized to its bone (anchor to tip = the bone's length) instead of the look's art scale (a prop)
    // An image part's other drawings (a face's expressions, a fist in
    // perspective), each its own region, anchor and tip; a clip's swaps
    // choose (0: its own).
    struct Drawing {
        std::int32_t     picture = -1;
        omui::geom::Rect source;
        Point            anchor, tip;
    };
    std::vector<Drawing> drawings;
    bool                 mirror = false;  // the art reflected across its anchor-to-tip line (drawn facing the other way)
    bool                 shown = true;    // drawn unless a clip says (a brush in the hand only while it is drawn)
    // An image part's material, lit by the stage's light where its picture
    // has a relief (a normal map): the look's `material.<name>` (empty: its
    // `body`, when it has one; "none": unlit).
    std::string          material;
};

// How a contact feels, drawn (D-85 §8.4). A fighter file's hits name a
// profile (`impact = "heavy"`); the look says what it is: the shake, the
// camera's punch in, a flash, the spark, the sounds (names a sound bank
// resolves) and the rumble — a pure function of the event and its age.
enum class Spark : std::uint8_t { rays, ring, splash, shards, bolt, cracks };
enum class Flash : std::uint8_t { none, white, invert };

// Effects drawn from a look's pictures — an ink splash, a slash's stroke,
// droplets thrown — each a pure function of its age in frames, so a rollback
// draws it again the same. Named in the look ([fx.<name>]); an impact, a
// projectile, a clip and a cinematic name the ones they play. Places are
// from the effect's origin, forward and up (mirrored with the facing).
struct FxSprite {  // a picture's region at a point (or travelling to another along an arc), growing, turning, fading
    std::int32_t         picture = -1;
    omui::geom::Rect     source;
    Point                at;
    std::optional<Point> to;             // where it travels to over its frames (a ball passed, a blade thrown)
    float                lift = 0;       // the arc's height at the middle of the way (stage units; negative: a dip)
    std::int32_t         from = 0, frames = 12;
    std::array<float, 2> size{40, 40};   // its height, stage units: from → to
    std::array<float, 2> turn{0, 0};     // degrees, counter-clockwise
    std::array<float, 2> alpha{1, 0};
    hanim::Ease          ease = hanim::ease::out_cubic;
    std::optional<Color> color;          // drawn as the region's shape in this colour, not its own
};
struct FxStroke {  // a picture's long region (a brush stroke) laid along a curve: drawn out, held, faded
    std::int32_t       picture = -1;
    omui::geom::Rect   source;
    std::vector<Point> points;  // the curve (Catmull-Rom) through them
    float              width = 30;
    std::int32_t       from = 0, draw = 4, hold = 6, fade = 10;
    std::optional<Color> color;  // drawn as the region's shape in this colour, not its own
};
struct FxSpray {  // droplets thrown from a point, falling (each one's own from a seed)
    std::int32_t                  picture = -1;
    std::vector<omui::geom::Rect> sources;
    Point                         at;
    std::int32_t                  from = 0, frames = 24, count = 8;
    std::array<float, 2>          speed{2, 8};   // stage units a frame
    std::array<float, 2>          angle{-30, 60};  // degrees: 0 forward, 90 up
    std::array<float, 2>          size{4, 12};
    float                         gravity = 0.4f;
    std::uint32_t                 seed = 1;
    std::optional<Color>          color;  // drawn as the regions' shapes in this colour, not their own
};
struct Fx {
    std::vector<FxSprite> sprites;
    std::vector<FxStroke> strokes;
    std::vector<FxSpray>  sprays;
    [[nodiscard]] std::int32_t length() const noexcept;  // frames until every part is gone
};
// An effect played at a frame (of a clip, of a cinematic), from a place (forward, up).
struct FxAt {
    std::string  name;
    std::int32_t frame = 0;
    Point        at;
    float        scale = 1;
};

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
    std::string                fx;                // an effect of the look's played at the contact instead of the spark
    std::string                fx_counter, fx_punish;  // its stronger tiers (a counter hit, a punish counter; empty: `fx`, larger)
};

// What a contact without a profile of its own looks like: a hit's, a block's, a KO's.
[[nodiscard]] const Impact& default_impact(Event::Kind kind) noexcept;

// A key pose, by name (`[pose.<name>]`): what a clip's key sets — bones'
// angles to the world (`aim`), the body's rise, grips' hold and places, the
// drawings parts swap to — built on another pose (`from`) and changing only
// what it names. A clip keyed by poses (`poses = [[frame, "name"], …]`)
// becomes the tracks they make: a key pose written once, used by every clip.
struct KeyPose {
    std::string                    from;
    std::map<std::int32_t, double> aim;                            // by bone, degrees
    std::optional<double>          rise;
    std::map<std::int32_t, double> grip, grip_at, grip_across;     // by grip
    std::map<std::int32_t, double> swap;                           // by part
    std::map<std::int32_t, double> show;                           // by part: 1 drawn, 0 not
};
struct PoseKey {
    std::int32_t               frame = 0;
    std::string                pose;
    std::optional<hanim::Ease> ease;    // its way to the next key (none: the clip's)
    KeyPose                    change;  // what this key changes of the pose
};

// Each bone's angle (degrees, to its parent; its rest added) and the body's
// rise (pixels), keyed by frame (a key at frame f is at f/60 s).
struct Clip {
    std::int32_t                    frames = 1;
    bool                            loop = false;
    std::vector<hanim::Track<double>> angles;  // by bone; a missing one keeps its rest
    hanim::Track<double>            rise;
    std::vector<hanim::Track<double>> swaps;   // by part: which drawing (0 its points, n its variant n), held between keys
    std::vector<hanim::Track<double>> grips;   // by grip: how much it holds, 0–1 (a missing one: fully)
    // By bone: its angle to the world (degrees, as drawn facing right), not to
    // its parent — a key pose as an animator sets it, the body turning under it.
    std::vector<hanim::Track<double>> aims;
    std::vector<hanim::Track<double>> grip_at, grip_across;  // by grip: where on its target, moving (a foot stepping)
    std::vector<FxAt>                 fx;  // effects played from the fighter's feet as the clip reaches their frames
    std::vector<PoseKey>              keys;  // keyed by poses (its tracks made from them when read)
    std::optional<hanim::Ease>        ease;  // its keys' way to the next, unless a key says (as its file said)
    std::vector<hanim::Track<double>> shows;  // by part: drawn (1) or not (0), held between keys
    // Whole-body drawings (D-90): from each frame, the look's cel shown in
    // place of the rig (−1: the rig again), held until the next — exposures
    // as an animator times them, never blended, never turned by a bone.
    std::vector<std::pair<std::int32_t, std::int32_t>> exposures;  // (from frame, cel)
};

// A whole-body drawing (D-90): a picture's region drawn as the fighter in
// one pose, in place of its parts — the decisive poses a rig cannot make
// (a heavy's contact, a turn, a compression, a reaction). Its `pivot` is the
// pixel over the fighter's root on the ground (where the rig's feet stand),
// so a cel and the rig share their place; `scale` stage units a pixel (0:
// the look's art scale). Its sockets name points of the drawing — a fist's
// knuckles, a forehead, a sole — with the way their surface faces (picture
// coordinates, y down): where its contacts and effects belong.
struct Cel {
    std::string  name;
    std::int32_t     picture = -1;
    omui::geom::Rect source;
    Point            pivot;
    float            scale = 0;
    struct Socket {
        Point at;
        Point normal{1, 0};
    };
    std::map<std::string, Socket, std::less<>> sockets;
};

// A grip (two-bone IK): the chain `upper` → `lower` (a shoulder and an elbow,
// a hip and a knee) turned so the lower bone's tip reaches a point on bone
// `target` — `at` along it, `across` it — whatever the clip did to them: the
// back hand stays on the brush wherever the front hand swings it. With no
// target bone (`target = "ground"`) the point is on the fighter's ground, `at`
// ahead of where it stands and `across` up: a planted foot. `bend`: the way
// the middle joint points (+1 or −1, as drawn facing right). A clip's
// `grip.<name>` keys let go (0) and take hold again (1); its `grip_at.<name>`
// and `grip_across.<name>` keys move the point (a step).
struct Grip {
    std::string  name;
    std::int32_t upper = -1, lower = -1, target = -1;
    float        at = 0, across = 0;
    float        bend = 1;
};

// A brush stroke of a glyph: a path (in the glyph's 100 × 100 box, y up)
// and its width at the start, the middle and the end — a calligrapher's
// press, carry and lift. Written along its length, frame by frame.
struct Stroke {
    std::vector<Point>   points;
    std::array<float, 3> width{8, 8, 8};
};

// A level 3's cinematic as it is drawn (system 0.3, P-6): a timeline keyed
// by the lock's frame. What it plays for each fighter; a backdrop fading in
// and out; the camera's keys (zoom, and the point it centres, from where the
// attacker stood, forward and up); a glyph written stroke by stroke (each
// stroke ending on its frame) over the defender; a seal; flashes. Like
// everything here, a pure function of the lock's frame.
struct CinematicLook {
    std::string attacker_clip, defender_clip = "hitstun";
    Color       backdrop = Color::hex(0xefe6d2);
    std::array<std::int32_t, 2> backdrop_in{0, 0}, backdrop_out{0, 0};  // frames it fades in over, and out over
    // Clouds with the backdrop: puffs placed in the world above where the lock
    // began, from `low` to `high` up, so a camera that rises passes them.
    struct Clouds {
        Color        color = Color::hex(0xffffff);
        std::int32_t count = 0;
        float        low = 200, high = 900;
    };
    Clouds clouds;
    struct CameraKey {
        std::int32_t frame = 0;
        float        zoom = 1;
        Point        at;  // from the attacker's start: forward, up
    };
    std::vector<CameraKey> camera;
    std::string               glyph;        // a glyph of the look, written over the defender
    Point                     glyph_at;     // its centre, from the attacker's start
    float                     glyph_size = 300;
    std::int32_t              glyph_from = 0;
    std::vector<std::int32_t> glyph_ends;   // the frame each stroke is finished on
    Color                     glyph_color = Color::hex(0x101114);
    std::int32_t              seal_frame = -1;  // a square seal stamped on the glyph (−1: none)
    Point                     seal_at;
    float                     seal_size = 60;
    Color                     seal_color = Color::hex(0xc0392b);
    struct FlashKey {
        std::int32_t frame = 0;
        Flash        kind = Flash::white;
        std::int32_t frames = 6;
    };
    std::vector<FlashKey> flashes;
    bool                  scanlines = false;  // a screen's lines over the picture while it plays
    // Where each is drawn, from where the lock began (the attacker's feet;
    // forward, up), keyed by frame: a lift, a leap off the top of the picture.
    // Without one, a fighter stays where the simulation holds it. A key's
    // ease is the way there from the key before (none: eased in and out, a
    // stop at every key; out_quad up to a height and in_quad down from it: a
    // body flung and falling).
    struct PathKey {
        std::int32_t               frame = 0;
        Point                      at;
        std::optional<hanim::Ease> ease;
    };
    std::vector<PathKey> attacker_path, defender_path;
    // Frames [from, to) each is drawn facing the other way — a spin, the back
    // turned to the other (a shoulder driven in backwards, a catch behind the
    // back) — its clip's forward then away from where it faced.
    std::vector<std::array<std::int32_t, 2>> attacker_turn, defender_turn;
    // Hitstops: from a blow's `frame`, for `frames`, the struck stands in it —
    // shaking along its line, its reaction waiting till the hitstop is over
    // (the attacker holds its contact by its clip's keys).
    std::vector<std::array<std::int32_t, 2>> hitstops;
    // Frames [from, to) the attacker leaves afterimages: drawn again as it was
    // its look's trail frames before, in the trail's colour, fading.
    std::vector<std::array<std::int32_t, 2>> afterimages;
    // Frames [from, to) the attacker is drawn over the defender (a knee driven
    // into a body, a heel on a chest), not by side.
    std::vector<std::array<std::int32_t, 2>> in_front;
    // Quakes (the picture shaken and swelling: a crash) and shock rings
    // spreading from a point (from where the attacker began, forward and up).
    struct Quake {
        std::int32_t frame = 0;
        float        strength = 10;
        std::int32_t frames = 20;
    };
    std::vector<Quake> quakes;
    struct Ring {
        std::int32_t frame = 0;
        Point        at;
        float        size = 400;
        std::int32_t frames = 24;
        Color        color = Color::hex(0xf2e6c8);
    };
    std::vector<Ring> rings;
    // A prism (璃's): the defender drawn again, `images` times either side,
    // `spread` apart and tinted, over the frames [from, to].
    struct Prism {
        std::int32_t images = 0;
        float        spread = 40;
        std::int32_t from = 0, to = 0;
        Color        tint = Color::hex(0x9fd8ff, 0.35f);
    } prism;
    // Cracks across the picture: one more for each of the cinematic's hits
    // landed so far (as the simulation plays them).
    struct Cracks {
        bool  on = false;
        Color color = Color::hex(0xffffff, 0.8f);
    } cracks;
    // Time standing still (刻's): over the frames [from, to) the picture is
    // drawn grey (toward `tint`, openmui.render's `tone`), the struck stands
    // as it was when time stopped, and the sparks and effects of hits in it
    // hang where they are, a few frames in; blows landing in it take effect
    // from `to` (the bell), when everything goes on and the colour is back.
    struct Stop {
        std::int32_t from = -1, to = -1;
        Color        tint = Color::hex(0xc9c3b6);
    } stop;
    // A clock's face over [from, to]: from where the lock began (forward, up),
    // `size` across; its hands go round while time runs and stand while it
    // stands.
    struct ClockFace {
        std::int32_t from = -1, to = -1;
        Point        at;
        float        size = 200;
        Color        color = Color::hex(0xd4af37);
    } clock;
    // The picture falling apart: from `frame`, `count` pieces of it fall,
    // turning and fading.
    struct Fall {
        std::int32_t frame = -1;
        std::int32_t count = 48;
        Color        color = Color::hex(0xe8f6ff, 0.9f);
    } fall;
    // Cut-ins: a picture's region shown across the screen in a slanted band
    // over the frames [from, to], sliding in from the attacker's side and
    // closing; its art drifting slowly (`pan`: source pixels a frame).
    struct Panel {
        std::int32_t     picture = -1;
        omui::geom::Rect source;
        std::int32_t     from = 0, to = 0, in = 6, out = 6;
        float            y = 0.5f, height = 0.42f;  // the band's middle and height, of the screen's
        float            slant = 0.08f;             // its ends leaning, of the screen's width
        Point            pan;
        Color            band = Color::hex(0x0b0b0d);
    };
    std::vector<Panel> panels;
    std::vector<FxAt>  fx;  // effects from where the lock began
    std::vector<FxAt>  behind;  // and effects drawn behind the fighters (a dial standing behind them)
    // Frames [from, to) the attacker is not drawn (a picture of them stands in: a finishing pose).
    std::array<std::int32_t, 2> hide{-1, -1};
    // The defender's reaction to each of the cinematic's hits, in turn (a
    // reaction clip's name — a hit reaction or a taken one, see below; empty:
    // by default). A taken reaction the defender's look has not falls back to
    // `hitstun.heavy`, then `hitstun`; one asked for again in a row goes on
    // from where it began (kicked while launched, a body keeps turning over).
    std::vector<std::string> reactions;
    // Held (a grab anchor): over [from, to) the defender is where a bone of
    // the attacker is — `along` that bone from its joint — by its own point
    // `body` (from its feet: forward, up), playing `clip` from the hold's
    // first frame (an agreed reaction, `held` by default): taken by the
    // collar, lifted, thrown down. Holds one after another on the same bone
    // keep the body in hand while it reacts to each blow (`held`, then
    // `hitstun.body` from a knee's contact, …).
    struct Hold {
        std::int32_t from = 0, to = 0;
        std::int32_t bone = -1;
        float        along = 0;
        Point        body{0, 110};
        std::string  clip = "held";
    };
    std::vector<Hold> holds;
};

// Hit reactions: a look's hitstun clips by where and how hard the latest
// blow landed — `hitstun.head`, `hitstun.body`, `hitstun.low`,
// `hitstun.heavy` — each falling back to `hitstun`. Every look may have them
// (the name is the convention), so a cinematic can ask any defender for one.
inline constexpr std::array<std::string_view, 4> hit_reactions{"hitstun.head", "hitstun.body", "hitstun.low", "hitstun.heavy"};
// And what a cinematic or a throw asks of the one it takes: held, launched (spinning up), slammed (into the floor).
inline constexpr std::array<std::string_view, 3> taken_reactions{"held", "launched", "slammed"};

// A picture a look's image parts are cut from: its file (resolved by
// whoever carries the look's files), its size, and once registered with the
// renderer's image store, its reference.
//
// Its relief: the shape the light reads its parts by — each region's
// silhouette rounded `bevel` pixels in, folds `detail` pixels deep where it
// is drawn darker (openmui.image's normal_map) — made once by whoever
// carries the pictures, registered as `normal`.
struct Picture {
    std::string                name;
    std::string                file;
    std::int32_t               width = 0, height = 0;
    omui::render::ImageRef     ref;
    struct Relief {
        float        bevel = 12, detail = 0;
        std::int32_t smooth = 2;
        float        strength = 1;
        friend constexpr bool operator==(const Relief&, const Relief&) noexcept = default;
    };
    std::optional<Relief>      relief;
    omui::render::ImageRef     normal;
};

struct Look {
    std::string                 name;
    std::vector<Picture>        pictures;
    // Units of the stage a picture's pixel is (all of a fighter's art at one
    // scale, as drawn); 0: each image part sized to its bone.
    float                       art_scale = 0;
    std::vector<Bone>           bones;
    std::vector<Part>           parts;
    std::vector<Grip>           grips;
    std::map<std::string, KeyPose, std::less<>> poses;
    std::map<std::string, Clip, std::less<>> clips;
    std::vector<Cel>            cels;  // whole-body drawings, by their place (a clip's exposures name them)
    Color                       outline = Color::hex(0x101014);
    float                       outline_width = 3;
    // Light (the world's rules: one light for every fighter on a stage): each
    // part is its colour, a shadow on the side away from the light `shade`
    // pixels deep (the colour toward `shadow` by `darken`), and a rim of
    // `rim_color` `rim` pixels wide on the side toward it. 0 deep: flat.
    struct Light {
        Point from{-0.6f, -0.8f};  // where the light comes from (picture coordinates, y down; normalised)
        float shade = 0;
        float darken = 0.3f;
        Color shadow = Color::hex(0x1a1430);
        float rim = 0;
        Color rim_color = Color::hex(0xfff4e0, 0.6f);
    } light;
    // Colour schemes: each a colour per slot (`slots` names them); a part with
    // a slot takes its colour from the palette in use.
    std::vector<std::string>              slots;
    std::vector<std::vector<Color>>       palettes;
    std::vector<std::string>              palette_names;
    std::vector<Impact>                   impacts;  // by name; "block" and "ko" for those contacts
    Color                                 accent = Color::hex(0xf2c14e);  // a super's flash and close-up band
    // Afterimages (G-12): during a move, the poses of these frames before, in
    // `color`, fading — drawn from the states the Effects keep, so a
    // rollback draws them again the same. No frames: none.
    struct Trail {
        Color                     color = Color::hex(0xff3ea5);
        std::vector<std::int32_t> frames;
        float                     alpha = 0.3f;
    } trail;
    // A glow around the outline (neon): its colour and width (0: none).
    struct Glow {
        Color color = Color::hex(0xff3ea5);
        float width = 0;
    } glow;
    // Its set pieces (G-15, panes): the glass, its edge (a pane that turns
    // projectiles back: `reflect`), the shine across it; cracks as it loses
    // durability. Or drawn art: a picture's region standing where the pane
    // stands (`whole`; `cracked` once it has lost durability), the edge of
    // one that turns projectiles back drawn over it.
    struct Pane {
        Color            glass = Color::hex(0xcfe8ff, 0.22f);
        Color            edge = Color::hex(0xe8f6ff, 0.85f);
        Color            shine = Color::hex(0xffffff, 0.5f);
        Color            reflect = Color::hex(0xffd36b, 0.9f);
        std::int32_t     picture = -1;
        omui::geom::Rect whole, cracked;
    } pane;
    // Time (刻): its clone drawn in a tint; a rewind leaving `ghosts` along
    // the way back, fading over `frames`; where its mark stands (G-20), the
    // look's effect `mark` played over and over on the floor.
    struct CloneLook {
        Color tint = Color::hex(0xd4af37, 0.45f);
    } clone;
    struct Rewind {
        Color        color = Color::hex(0xd4af37, 0.5f);
        std::int32_t ghosts = 0;
        std::int32_t frames = 16;
        std::string  mark;
    } rewind;
    // A heavy fighter's footfalls (a giant): walking, the camera dips on each
    // step (every `every` frames of its walk clip) by `dip`, back over `frames`.
    struct Footfall {
        std::int32_t every = 24;
        float        dip = 0;
        std::int32_t frames = 8;
    } footfall;
    std::map<std::string, std::vector<Stroke>, std::less<>>  glyphs;      // by name: 永, 一, 人
    std::map<std::string, CinematicLook, std::less<>>        cinematics;  // by the level 3's move name
    std::map<std::string, Fx, std::less<>>                   fx;          // effects, by name
    // Materials of image parts (`[material.<name>]`): how a part is lit — its
    // bands, their edges' softness, the shadow's and the rim's strengths (the
    // light's place and colours are the stage's, else these). Stylised per
    // fighter: ink's two soft washes, neon's hard bands and a bright rim.
    std::map<std::string, omui::render::Material, std::less<>> materials;
    // A super's flash, by its move's name: a cut-in over the freeze (frames of
    // it), instead of the band of the accent colour.
    std::map<std::string, CinematicLook::Panel, std::less<>> cutins;
    // A projectile drawn as a glyph (G-11), by its move's name (236P serves
    // 236LP, 236MP and 236HP): written in front of the fighter over the
    // move's frames before it flies, then flying whole, its ink bleeding.
    struct GlyphProjectile {
        std::string glyph;
        float       size = 60;
        Color       color = Color::hex(0x101114);
        Color       halo = Color::transparent();  // a glow behind it, to read on a dark stage (transparent: none)
        float       spin = 0;                      // degrees a frame it turns as it flies (a clock's hand)
        std::string fx;                            // an effect of the look's, played over and over as it flies (instead of a glyph)
    };
    std::map<std::string, GlyphProjectile, std::less<>>      projectiles;
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
    double local = 0;  // to its parent (to the world: the root, a hanging bone), radians, as drawn facing right
};
using Pose = std::vector<BonePlace>;

[[nodiscard]] std::string_view clip_name(const Fighter& fighter, const FighterState& state) noexcept;
[[nodiscard]] Pose pose(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame);
// The same into `out` (its storage reused: no allocation once it has the bones' room).
void pose(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame, Pose& out);
// A clip by name at frame `t` (looping or held at its end), for the fighter where and as it faces in `state`.
void pose_clip(const Look& look, std::string_view clip, std::int32_t t, const FighterState& state, Pose& out);
// The whole-body drawing a clip exposes at its frame t (−1: the rig).
[[nodiscard]] std::int32_t exposed(const Clip* clip, std::int32_t t) noexcept;
// A pixel of a cel where the fighter stands: on the stage (y up).
[[nodiscard]] Point cel_point(const Look& look, const Cel& cel, const FighterState& s, Point pixel) noexcept;
// A named socket of the drawing the fighter shows now (a fist's knuckles, a sole), on the stage; none while
// the rig is shown or the drawing has no such socket.
[[nodiscard]] std::optional<Point> socket(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame, std::string_view name);

// A look as a file, `*.look.gongfu.toml` — what gongfu.studio writes and a
// mod will carry: the palettes, the bones (angles in degrees), the parts, the
// clips (keys by frame) and the impact profiles, by name. Every problem at
// once with its line and column, a misspelt key with the nearest right one.
// write_look gives the file back; what it writes reads back as the same look.
[[nodiscard]] std::expected<Look, std::vector<data::Diagnostic>> read_look(std::string_view source, std::string_view file);
[[nodiscard]] std::string write_look(const Look& look);

// Where the body is at a pose, for hurtboxes (gongfu.rules bakes these,
// G-5): `base` (the stance's hurtbox, the fighter's own coordinates: x
// forward, y up from its feet), then each limb whose parts reach out of it —
// the bounds of the limb's parts, their thickness scaled by `scale` — up to
// three, the farthest-reaching first. A limb counts only where it reaches
// out by `margin` pixels or more past both the box and where it lies in
// `rest` (the stance's resting pose: what the box was drawn around), so a
// leg planted wide at rest does not count. The poses are of the fighter at
// x 0 on the ground, facing right. Answers how many boxes (1: nothing reaches out).
[[nodiscard]] std::size_t reach(const Look& look, const Pose& pose, const Pose& rest, Box base, float scale, float margin, std::array<Box, 4>& out);

// The stage's view: what lies at the middle of the picture, and how large.
struct Camera {
    double x = 0, y = 0, zoom = 1;
};

// A stage's art (`*.stage.gongfu.toml`): layers of shapes behind the
// fighters and in front of them, each moving with the camera by its
// parallax (0 still, 1 with the ground, more in front), with motion that is
// a pure function of the match's frame (a sway, a flicker), and the light
// every fighter on it is lit by — the world's rules. Shapes are drawn by
// hand or made by a generator (bamboo, a lattice window, planks, rain) from
// a seed when the file is read: the same shapes every time.
struct StageShape {
    Shape              shape = Shape::polygon;  // polygon or curve
    std::vector<Point> points;                  // the layer's units: x along the stage, y up from the ground
    Color              color = Color::hex(0x000000);
    Color              color2 = Color::transparent();  // not transparent: a gradient from `color` at its top to this at its bottom
    float              sway = 0;                // degrees each way about `pivot`
    float              period = 4;              // seconds a sway takes
    float              phase = 0;               // 0–1 of a period
    Point              pivot;
    float              flicker = 0;             // how far its alpha dips (a lantern, a sign)
    Point              drift;                   // units a second it moves (rain falling, a cloud)
    float              wrap = 0;                // > 0: its drift wraps round every this many units (along the drift)
    float              glow = 0;                // > 0: a halo this wide around it in its colour (a neon sign)
};

// A layer's shapes are placed in the stage's units (x along it, y up from the
// ground): with the camera at the stage's middle, a point of any layer is
// where a point of the ground would be; as the camera moves, a layer moves
// `parallax` of the way.
// A painted picture's region standing in a stage's layer: its bottom left at
// `at` (x along the stage, y up from the ground; the layer's units),
// `width` wide (its height by the region's proportions, or `height`),
// mirrored when `flip`; its opacity can flicker (a lantern).
struct StageImage {
    std::int32_t     picture = -1;  // the stage's pictures
    omui::geom::Rect source;
    Point            at;
    float            width = 100;
    float            height = 0;    // 0: by the region's proportions (else stretched: a floor drawn long)
    bool             flip = false;
    float            flicker = 0;   // its opacity breathing (a lantern), 0–1
};

struct StageLayer {
    std::string             name;
    float                   parallax = 1;
    float                   repeat = 0;    // > 0: drawn again every this many units across the picture
    float                   opacity = 1;
    bool                    front = false; // drawn over the fighters
    std::optional<std::array<float, 4>> clip;  // drawn only inside [x0, y0, x1, y1] of the layer's units (a shadow on a window's paper)
    std::vector<StageShape> shapes;
    std::vector<StageImage> images;  // drawn under its shapes
};

struct Stage {
    std::string             name;
    Color                   sky_top = Color::hex(0x1b1f2a), sky_bottom = Color::hex(0x3a3f52), floor = Color::hex(0x23252e), line = Color::hex(0x8a8fa3);
    bool                    marks = true;  // the ground's distance marks
    std::vector<StageLayer> layers;
    // The world's light: where it comes from, the shadow's and the rim's colours; a fighter's look keeps its depths.
    std::optional<Point>    light_from;
    std::optional<Color>    shadow, rim_color;
    std::optional<Color>    light_color;  // the light's own colour on the fighters' lit side (a sunset's warmth; none: as painted)
    // The fighters' contact shadows on its floor (transparent: none).
    Color                   contact = Color::hex(0x000000, 0.5f);
    // The painted pictures its layers' images are cut from (registered by
    // whoever carries the stage's files, as a look's are).
    std::vector<Picture>    pictures;
};

// A stage file: every problem at once with its line and column; what
// write_stage writes reads back as the same stage (generators' shapes written out).
[[nodiscard]] std::expected<Stage, std::vector<data::Diagnostic>> read_stage(std::string_view source, std::string_view file);
[[nodiscard]] std::string write_stage(const Stage& stage);

struct Scene {
    float width = 960, height = 540;  // the picture, pixels
    float ground = 470;               // where the stage's ground lies on it
    Color sky_top = Color::hex(0x1b1f2a), sky_bottom = Color::hex(0x3a3f52), floor = Color::hex(0x23252e), line = Color::hex(0x8a8fa3);
    const Stage* stage = nullptr;     // its art (none: the plain sky and floor above)
};

// Keeps both fighters in view, zoomed out as they part, following a jump.
[[nodiscard]] Camera frame_camera(const Match& match, const Scene& scene);

class Effects {
public:
    // After each tick: its events, at its frame. With the looks, a contact
    // made by a whole-body drawing keeps where the drawing touched (its
    // `contact` socket, else its first).
    void record(const Match& match, const Look* left = nullptr, const Look* right = nullptr);
    // After a rollback to `frame`: the events of frames from it on are played again.
    void rewind(std::int32_t frame);
    struct Kept {
        std::int32_t frame;
        Event        event;
        // Where the striking drawing touched, when the attacker showed a whole-body drawing with a contact
        // socket on that frame (D-90): the spark goes there, on the fist drawn, not between the boxes.
        std::optional<Point> drawn;
    };
    [[nodiscard]] std::span<const Kept> kept() const noexcept { return kept_; }
    // The fighters as they were `ago` frames before the latest recorded (for
    // afterimages); none when not kept (beyond 8 frames, or before a rewind).
    [[nodiscard]] const FighterState* past(std::int32_t fighter, std::int32_t frame) const noexcept;

private:
    std::vector<Kept> kept_;
    struct Past {
        std::int32_t                 frame = -1;
        std::array<FighterState, 2> f{};
    };
    std::array<Past, 8> past_{};
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
    // One fighter's body as `draw` draws it, through `camera` (a studio's
    // contact sheet, a character select); `clip`/`t` to play a clip by name instead.
    void body(const Look& look, const Fighter& fighter, const FighterState& state, std::int32_t frame, const Scene& scene, const Camera& camera,
              omui::render::DisplayList& out, std::string_view clip = {}, std::int32_t t = 0, std::optional<Color> tint = std::nullopt);
    // A glyph written to `progress` (0–1 of its strokes' total length) at `centre`, `size` pixels square;
    // its strokes `thicken` times as wide (a halo behind it).
    void glyph(const std::vector<Stroke>& strokes, double progress, Point centre, float size, Color color, omui::render::DisplayList& out,
               float thicken = 1);
    // A glyph whose stroke k is written over the frames up to ends[k], from `from`, at `frame`.
    // An effect of `look` at `age` frames, from `origin` (stage units), mirrored by `facing`.
    // `paint`: every piece drawn as its shape in that colour (a counter's tier).
    void fx(const Look& look, const Fx& fx, double age, Point origin, double facing, float scale, const Scene& scene, const Camera& camera,
            omui::render::DisplayList& out, float alpha = 1, std::optional<Color> paint = std::nullopt);
    // A cut-in panel of `look` at the cinematic's frame `t`; `facing` the attacker's (it enters from behind them).
    void panel(const Look& look, const CinematicLook::Panel& p, std::int32_t t, double facing, const Scene& scene, omui::render::DisplayList& out);
    void glyph_by_frames(const std::vector<Stroke>& strokes, std::span<const std::int32_t> ends, std::int32_t from, std::int32_t frame, Point centre,
                         float size, Color color, omui::render::DisplayList& out);

    // A stage's layers (behind the fighters, or in front of them) through
    // `camera`, at the match's frame, `alpha` of their opacity; `middle` the
    // stage's (a layer moves `parallax` of the camera's way from it). A
    // stage editor's view, and a frame's.
    void stage_layers(const Stage& stage, bool front, const Scene& scene, const Camera& camera, double middle, std::int32_t frame, float alpha,
                      omui::render::DisplayList& out);

private:
    // A whole-body drawing at the fighter's place (its pivot over the root on the ground; mirrored facing left).
    void draw_cel(const Look& look, const Cel& cel, const FighterState& s, const Scene& scene, const Camera& camera, std::optional<Color> tint,
                  omui::render::DisplayList& out);
    void picture_part(const Look& look, const Part& part, std::size_t drawing, const FighterState& s, const Scene& scene, const Camera& camera,
                      std::optional<Color> tint, omui::render::DisplayList& out);
    Pose                     pose_, past_, rest_, held_;
    std::vector<Point>       mesh_at_, mesh_uv_;
    std::vector<std::uint16_t> mesh_index_;
    std::vector<double>      follow_;  // each bone's secondary motion this frame, degrees
    std::vector<std::size_t> order_;
    omui::render::Path       path_;
    omui::render::Path       ring_;  // the contact shadows' rings
};

// One frame with a painter of its own (the same picture; allocates).
void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, omui::render::DisplayList& out);

}  // namespace gongfu::present2d
