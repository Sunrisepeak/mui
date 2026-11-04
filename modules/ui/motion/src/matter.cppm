// openmui.motion:matter — what a node drew, as matter a motion moves
// (universal motion §4). A range of a display list (a node's: its BeginNode
// to its EndNode) is copied and taken apart into pieces: each drawable
// command, or a group drawn whole (an effect's layer inside it, a nested
// root's range), with what it is drawn under — the transform from the
// range's start, its clips, the opacity of the plain layers around it — and
// whose it is: the node that drew it (keys the caller gives for each node
// marker, stable from frame to frame) and its place among that node's
// pieces of its class. Two matters of the same node are what a morph moves
// between (:morph).
export module openmui.motion:matter;

import std;
import openmui.geom;
import openmui.render.vector;

export namespace omui::motion {

using geom::Affine;
using geom::Color;
using geom::CornerRadii;
using geom::Point;
using geom::Rect;
using geom::Size;

// What a piece is, for matching: two pieces of one node pair only when they are of one class.
enum class PieceClass : std::uint8_t { fill, stroke, shadow, path_fill, path_stroke, image, glyphs, texture, group, count_ };

struct Piece {
    PieceClass    cls = PieceClass::fill;
    std::uint32_t node = 0;     // its node: an index into Matter::nodes() (the innermost node it was drawn in)
    std::uint32_t ordinal = 0;  // its place among its node's pieces of its class
    std::size_t   command = 0;  // its command in the matter's list (a group: the first)
    std::size_t   end = 0;      // one past its last command
    Affine        transform;    // from the range's start to the space it is drawn in
    int           clip = -1;    // the innermost clip it is drawn under (Matter::clips()); -1: none
    float         opacity = 1;  // the plain layers' around it, multiplied
    Rect          bounds;       // in the range's space (a glyph run's from its glyphs' origins and size)
    std::uint32_t nested = 0;   // a nested root's range drawn whole: its marker (0: not one)
    // A nested root may delegate its external placement to this owner's
    // curve, while retaining its own local state animation. Its original
    // paint keeps that placement; cancel it only when sampling an in-between
    // envelope. Exact endpoint emission retains the original parent matrix.
    bool nested_envelope = false;
    Affine nested_inverse;
    Affine nested_parent;
    int part = -1; // declarative paint part; never an extra drawable/stagger rank
    std::uint32_t part_ordinal = 0;
};

// Border metadata is kept outside drawable pieces: it must not add stagger
// ranks, move visual bounds/centres, count in statistics or consume tier budget.
struct Frame {
    std::uint32_t node = 0;
    Rect rect;
    Affine transform;
    int clip = -1;
    float opacity = 1;
    Rect placement;
    Size constraint;
    Affine visual; // own state transform, without external layout translation
};

struct Reference {
    std::uint32_t node = 0;
    std::string key;
    Rect allocation; // owner-border coordinates, not a sampled window AABB
};
struct Part {
    std::uint32_t node = 0;
    std::string role, id;
    Rect allocation;
    Point anchor{0.5f,0.5f};
    render::PartKind kind = render::PartKind::content;
    std::vector<render::ReferenceWeight> lineage;
    [[nodiscard]] render::PartFrame view(std::uint32_t owner) const {
        return {owner,role,id,allocation,anchor,kind,lineage};
    }
};

// A clip as pushed: its command and the transform it was pushed under.
struct ClipEntry {
    int         parent = -1;
    Affine      transform;
    std::size_t command = 0;
    bool transient = false; // allocation crop; exact target drawing omits it
};

// A node marker met in the range, and its keys.
struct NodeKeys {
    std::uint32_t marker = 0;
    std::string   key;       // stable from frame to frame (its path)
    std::string   semantic;  // what it is (role and name): matches a node whose key changed
};

// What extract asks of the caller.
struct Resolver {
    // A marker's keys (either may stay empty).
    std::function<void(std::uint32_t marker, std::string& key, std::string& semantic)> keys;
    // A marker inside the range whose range is drawn whole (a nested root: it moves itself).
    std::function<bool(std::uint32_t marker)> nested;
    // A nested root whose layout allocation follows its containing motion.
    // This is a typed placement contract, never inferred from a type/name.
    std::function<bool(std::uint32_t marker)> nested_placement;
};

// The range's own effect layer around all it draws (a style's neon or glass
// on the node): kept around the moving pieces, not moved as one group.
struct Wrapper {
    bool        present = false;
    std::size_t command = 0;  // its PushLayer
    Affine      transform;    // what it is pushed under
};

class Matter {
public:
    Matter() = default;
    Matter(Matter&&) noexcept = default;
    Matter& operator=(Matter&&) noexcept = default;
    Matter(const Matter&) = delete;
    Matter& operator=(const Matter&) = delete;

    // The commands [begin, end) of `source`, copied and taken apart.
    [[nodiscard]] static Matter extract(const render::DisplayList& source, std::size_t begin, std::size_t end, const Resolver& resolver);
    // A whole list (a morph's drawing, taken again when it is interrupted).
    [[nodiscard]] static Matter extract(const render::DisplayList& source, const Resolver& resolver) {
        return extract(source, 0, source.size(), resolver);
    }

    [[nodiscard]] const render::DisplayList& list() const noexcept { return list_; }
    [[nodiscard]] std::span<const Piece> pieces() const noexcept { return pieces_; }
    [[nodiscard]] std::span<const Frame> frames() const noexcept { return frames_; }
    [[nodiscard]] std::span<const Reference> references() const noexcept { return references_; }
    [[nodiscard]] std::span<const Part> parts() const noexcept { return parts_; }
    [[nodiscard]] std::span<const ClipEntry> clips() const noexcept { return clips_; }
    [[nodiscard]] std::span<const NodeKeys> nodes() const noexcept { return nodes_; }
    [[nodiscard]] const Wrapper& wrapper() const noexcept { return wrapper_; }
    [[nodiscard]] bool empty() const noexcept { return pieces_.empty(); }
    // Of the copied commands.
    [[nodiscard]] std::uint64_t hash() const noexcept { return list_.hash(); }
    // Of every piece.
    [[nodiscard]] Rect bounds() const noexcept;
    // Rebase allocation, never glyph/font scale or local state geometry. Full
    // border carriers adopt the new allocation; outgoing reflow text is clipped
    // to its current owner. Only callers that own the layout policy use this.
    void follow_layout(Matter& target, std::span<const std::string> scopes = {});

private:
    render::DisplayList    list_;
    std::vector<Piece>     pieces_;
    std::vector<Frame>     frames_;
    std::vector<Reference> references_;
    std::vector<Part>      parts_;
    std::list<std::string> reference_names_; // stable storage across arena rebuilds/moves
    std::vector<ClipEntry> clips_;
    std::vector<NodeKeys>  nodes_;
    Wrapper                wrapper_;
};

// The range's wrapper (its own effect layer around all it draws): the index
// of its PushLayer in [begin, end), when it has one.
[[nodiscard]] std::optional<std::size_t> wrapper_of(std::span<const render::Command> commands, std::size_t begin, std::size_t end);

// A command's class (drawable ones; nullopt for pushes, pops and markers).
[[nodiscard]] std::optional<PieceClass> class_of(const render::Command& c) noexcept;

// Where [begin, end) holds the range of the node marker `marker`: its
// BeginNode to the EndNode that closes it. Nullopt when it is not there.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> find_range(std::span<const render::Command> commands, std::uint32_t marker,
                                                                             std::size_t from = 0);

}  // namespace omui::motion
