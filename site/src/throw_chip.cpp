module omui.site;

import std;
import omui;
import hanim.engine;
import mlibs.hash;

namespace omui::site {
namespace {

constexpr float chip_width = 132, chip_height = 48;

}  // namespace

ThrowChip::~ThrowChip() {
    if (drive_ && attached()) (void)drive_->release();
}

void ThrowChip::mounted() {
    label_.set_style("label");
    label_.set_max_lines(1);
}

Size ThrowChip::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float w = width.value_or(aw.is_definite() ? aw.value : 320);
    return {w, height.value_or(std::round(w * 0.56f))};
}

// Its channel on the host's engine, made the first time: an offset from home, at rest at none.
hanim::Drive<hanim::Vec2>* ThrowChip::drive() const {
    if (!drive_ && attached()) {
        const hanim::DriveKey key = mlibs::hash::Fnv1a{}.str("site.throw_chip").u64(node().index).u64(node().generation).value();
        if (auto made = services().engine().drive(key, hanim::ch::position)) drive_ = *made;
    }
    return drive_ ? &*drive_ : nullptr;
}

ThrowChip::Now ThrowChip::now() const {
    Now out;
    out.held = held_;
    if (auto* d = drive()) {
        const hanim::seconds t = services().now();
        out.offset = d->value(t).value_or(hanim::Vec2{});
        if (const auto v = d->velocity(t)) out.velocity = {(*v)[0], (*v)[1]};
        out.moving = !d->settled(t).value_or(true);
    }
    return out;
}

Rect ThrowChip::chip(hanim::Vec2 offset) const {
    return {room_.width / 2 - chip_width / 2 + static_cast<float>(offset.x), room_.height / 2 - chip_height / 2 + static_cast<float>(offset.y), chip_width, chip_height};
}

// From where it is, at `velocity`: a glide handing its speed to a spring home, at one instant.
void ThrowChip::spring_home(hanim::Vec2 velocity) {
    auto* d = drive();
    if (!d) return;
    const hanim::seconds t = services().now();
    (void)d->glide(velocity, {}, t);
    (void)d->spring_to(hanim::Vec2{}, hanim::springs::wobbly, t);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

bool ThrowChip::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    auto* d = drive();
    if (!d) return false;
    const double t = services().now().count();
    const hanim::Vec2 at{ctx.local.x - room_.width / 2, ctx.local.y - room_.height / 2};
    switch (p->kind) {
        case PointerKind::down: {
            const hanim::Vec2 offset = now().offset;
            if (!chip(offset).contains(ctx.local)) return false;
            // Taken from where it is drawn (mid-flight too): it stops there, under the pointer.
            held_ = true;
            grab_ = {at.x - offset.x, at.y - offset.y};
            (void)d->jump(offset);
            trail_count_ = 0;
            trail_[trail_count_++] = {t, offset};
            services().capture_pointer(node());
            services().invalidate_paint(node());
            return true;
        }
        case PointerKind::move: {
            if (!held_) return false;
            const hanim::Vec2 offset{at.x - grab_.x, at.y - grab_.y};
            (void)d->jump(offset);
            if (trail_count_ == trail_.size()) std::ranges::rotate(trail_, trail_.begin() + 1), --trail_count_;
            trail_[trail_count_++] = {t, offset};
            services().invalidate_paint(node());
            services().invalidate_semantics(node());
            return true;
        }
        case PointerKind::up:
        case PointerKind::cancel: {
            if (!held_) return false;
            held_ = false;
            services().release_pointer();
            // Its speed as it was let go: over the drag's last tenth of a second.
            hanim::Vec2 velocity{};
            if (trail_count_ >= 2) {
                const auto& last = trail_[trail_count_ - 1];
                std::size_t first = trail_count_ - 2;
                while (first > 0 && last.first - trail_[first - 1].first <= 0.1) --first;
                const double span = last.first - trail_[first].first;
                if (span > 1e-6 && t - last.first <= 0.1)
                    velocity = {(last.second.x - trail_[first].second.x) / span, (last.second.y - trail_[first].second.y) / span};
            }
            spring_home(velocity);
            return true;
        }
        default: return false;
    }
}

void ThrowChip::paint(PaintContext& ctx) const {
    room_ = ctx.size;
    const Theme& theme = ctx.theme;
    const Now here = now();
    // Home: a faint ring where it rests.
    const Rect rest = chip({});
    ctx.list.stroke_round_rect(rest, CornerRadii::all(chip_height / 2), 1.5f, xrender::Paint::solid(theme.color("border")));
    const Rect drawn = chip(here.offset);
    ctx.part({.role = "chip", .allocation = drawn, .kind = xrender::PartKind::box}, [&] {
        if (auto it = theme.shadows.find("md"); it != theme.shadows.end()) paint_shadow(ctx.list, drawn, CornerRadii::all(chip_height / 2), it->second);
        ctx.list.fill_round_rect(drawn, CornerRadii::all(chip_height / 2), xrender::Paint::solid(theme.color("accent")));
        label_.set_text(props.label);
        const Color ink = theme.color("on_accent", Color::hex(0xffffff));
        const Size size = label_.measure(services(), ink, chip_width - 24);
        label_.paint(ctx, {drawn.x + (chip_width - size.width) / 2, drawn.y + (chip_height - size.height) / 2}, ink);
    });
    // Frames while it moves on its own, none at rest (D-70).
    if (here.moving && !held_) {
        services().moving(node());
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
}

void ThrowChip::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    const Now here = now();
    node.value = Value{}
                     .set("x", here.offset.x)
                     .set("y", here.offset.y)
                     .set("speed", std::sqrt(here.velocity.x * here.velocity.x + here.velocity.y * here.velocity.y))
                     .set("moving", here.moving)
                     .set("held", here.held);
}

ActResult ThrowChip::fling(ActContext&, const FlingArgs& args) {
    if (!args.vx.is_number() || !args.vy.is_number()) return ActResult::fail("fling takes a speed: vx, vy (pixels a second)");
    spring_home({args.vx.as_double(), args.vy.as_double()});
    return ActResult::done();
}

ActResult ThrowChip::home(ActContext&) {
    spring_home(now().velocity);
    return ActResult::done();
}

}  // namespace omui::site
