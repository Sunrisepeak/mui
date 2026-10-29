module openmui.ui.timeline;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import openmui.timeline;

namespace omui {

namespace {

namespace tl = omui::timeline;

// A track's colour by what it carries.
Color kind_colour(const Theme& theme, tl::Kind k) {
    switch (k) {
        case tl::Kind::video: return theme.color("#3b82f6");
        case tl::Kind::audio: return theme.color("#10b981");
        case tl::Kind::animation: return theme.color("#f59e0b");
        case tl::Kind::events: return theme.color("#a855f7");
    }
    return theme.color("#64748b");
}

constexpr std::array<std::string_view, 4> kind_names{"video", "audio", "animation", "events"};
constexpr float edge = 6;  // how near a clip's end the pointer trims instead of moving

}  // namespace

void TimelineEditor::mounted() { read(); }

void TimelineEditor::prop_changed(TimelineEditorProp p) {
    if (p == TimelineEditorProp::document) read();
    changed();
}

void TimelineEditor::read() {
    kept_.clear(), selected_.reset(), last_.clear();  // the window's history may still name them: undoing those is refused
    problems_.clear();
    if (props.document.empty()) {
        model_ = {};
    } else if (auto t = tl::read_timeline(props.document, "document.timeline.omui.toml")) {
        model_ = std::move(*t);
    } else {
        model_ = {};
        for (const auto& p : t.error()) problems_.push_back(p.text());
    }
    names_.clear(), tracks_.clear();
}

void TimelineEditor::changed() {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size TimelineEditor::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    const float tracks = static_cast<float>(std::max<std::size_t>(1, model_.tracks.size()));
    size_ = {width.value_or(720), height.value_or(ruler + tracks * track_height)};
    return size_;
}

float TimelineEditor::x_of(tl::Flicks t) const noexcept {
    return header + static_cast<float>((tl::to_seconds(t) - scroll_) * props.zoom);
}

tl::Flicks TimelineEditor::time_at(float x) const noexcept {
    return tl::from_seconds(static_cast<double>(x - header) / std::max(1e-6, props.zoom) + scroll_);
}

tl::Flicks TimelineEditor::snapped(tl::Flicks t) const noexcept {
    const tl::Flicks f = tl::frame_length(model_.rate);
    return (t >= 0 ? (t + f / 2) / f : -((-t + f / 2) / f)) * f;
}

std::optional<std::size_t> TimelineEditor::track_at(float y) const noexcept {
    if (y < ruler) return std::nullopt;
    const auto k = static_cast<std::size_t>((y - ruler) / track_height);
    return k < model_.tracks.size() ? std::optional<std::size_t>(k) : std::nullopt;
}

void TimelineEditor::set_frame(std::int64_t frame) {
    const auto f = static_cast<std::int32_t>(std::clamp<std::int64_t>(frame, 0, std::numeric_limits<std::int32_t>::max()));
    if (f == props.frame) return;
    props.frame = f;
    if (attached()) services().write_back(node(), "frame", Value(f));
    changed();
}

ActResult TimelineEditor::edit(std::expected<tl::Change, std::string> change) {
    if (!change) return ActResult::fail(change.error());
    auto undo = tl::apply(model_, *change);
    if (!undo) return ActResult::fail(undo.error());
    names_.clear();
    last_ = change->what;
    if (attached()) emit_edited(tl::write_timeline(model_), change->what);
    changed();
    // Its inverse kept here; the window's history holds the action that makes it.
    const std::int64_t id = next_++;
    kept_.emplace(id, std::move(*undo));
    return ActResult::done(Value(change->what)).undo_by("revert", Value{}.set("change", id));
}

// ── Actions ─────────────────────────────────────────────────────────────

ActResult TimelineEditor::seek(ActContext&, const Value& frame) {
    set_frame(frame.as_int(0));
    return ActResult::done(Value(props.frame));
}

ActResult TimelineEditor::select(ActContext&, const SelectArgs& args) {
    const std::int64_t track = args.track.as_int(-1), clip = args.clip.as_int(-1);
    if (track < 0) {
        selected_.reset();
    } else {
        if (static_cast<std::size_t>(track) >= model_.tracks.size() || clip < 0 || static_cast<std::size_t>(clip) >= model_.tracks[static_cast<std::size_t>(track)].clips.size())
            return ActResult::fail(std::format("there is no clip {} on track {}", clip, track));
        selected_ = {static_cast<std::size_t>(track), static_cast<std::size_t>(clip)};
    }
    if (attached()) emit_selected(static_cast<std::int32_t>(clip), static_cast<std::int32_t>(track));
    changed();
    return ActResult::done();
}

ActResult TimelineEditor::move(ActContext&, const MoveArgs& args) {
    const auto track = static_cast<std::size_t>(std::max<std::int64_t>(0, args.track.as_int(0)));
    const auto clip = static_cast<std::size_t>(std::max<std::int64_t>(0, args.clip.as_int(0)));
    const auto to = static_cast<std::size_t>(std::max<std::int64_t>(0, args.to_track.as_int(static_cast<std::int64_t>(track))));
    ActResult r = edit(tl::move(model_, track, clip, to, tl::frame_time(model_.rate, args.frame.as_int(0))));
    if (r.ok) {
        // The moved clip stays selected where it landed.
        const tl::Flicks at = tl::frame_time(model_.rate, args.frame.as_int(0));
        const auto& clips = model_.tracks[to].clips;
        const auto it = std::ranges::find(clips, at, &tl::Clip::at);
        if (it != clips.end()) selected_ = {to, static_cast<std::size_t>(it - clips.begin())};
    }
    return r;
}

ActResult TimelineEditor::trim(ActContext&, const TrimArgs& args) {
    return edit(tl::trim(model_, static_cast<std::size_t>(std::max<std::int64_t>(0, args.track.as_int(0))), static_cast<std::size_t>(std::max<std::int64_t>(0, args.clip.as_int(0))),
                         tl::frame_time(model_.rate, args.frame.as_int(0)), tl::frame_time(model_.rate, args.frames.as_int(0))));
}

ActResult TimelineEditor::split(ActContext&) {
    if (!selected_) return ActResult::fail("no clip is selected");
    return edit(tl::split(model_, selected_->first, selected_->second, tl::frame_time(model_.rate, props.frame)));
}

ActResult TimelineEditor::remove(ActContext&, const Value& ripple) {
    if (!selected_) return ActResult::fail("no clip is selected");
    const auto [track, clip] = *selected_;
    ActResult r = edit(ripple.as_bool(false) ? tl::ripple_remove(model_, track, clip) : tl::remove(model_, track, clip));
    if (r.ok) selected_.reset();
    return r;
}

ActResult TimelineEditor::revert(ActContext&, const Value& change) {
    const auto it = kept_.find(change.as_int(0));
    if (it == kept_.end()) return ActResult::fail("that change is no longer kept (the document was read again)");
    tl::Change c = std::move(it->second);
    kept_.erase(it);
    selected_.reset();
    return edit(std::move(c));
}

// ── Pointer and keys ────────────────────────────────────────────────────

bool TimelineEditor::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        const std::int64_t step = k->mods.has(Modifier::shift) ? model_.rate.num / std::max(1, model_.rate.den) : 1;  // a second with Shift
        if (k->key == Key::left) set_frame(props.frame - step);
        else if (k->key == Key::right) set_frame(props.frame + step);
        else if (k->key == Key::home) set_frame(0);
        else if (k->key == Key::end) set_frame(tl::frame_at(model_.rate, model_.duration()));
        else if (k->key == Key::delete_ || k->key == Key::backspace) {
            (void)services().perform(node(), "remove", Value{}.set("ripple", k->mods.has(Modifier::shift)));  // as the person: into the history
        } else {
            return false;
        }
        ctx.handled = true;
        return true;
    }
    if (const auto* w = std::get_if<WheelEvent>(&e)) {
        if (w->mods.has(Modifier::ctrl)) {  // zoom about the pointer
            const tl::Flicks under = time_at(ctx.local.x);
            const double zoom = std::clamp(props.zoom * std::pow(1.0015, static_cast<double>(w->delta.y)), 4.0, 4000.0);
            props.zoom = zoom;
            if (attached()) services().write_back(node(), "zoom", Value(zoom));
            scroll_ = static_cast<float>(tl::to_seconds(under) - static_cast<double>(ctx.local.x - header) / zoom);
        } else {
            scroll_ = std::max(0.0f, scroll_ - static_cast<float>((w->delta.x != 0 ? w->delta.x : w->delta.y) / props.zoom));
        }
        changed();
        ctx.handled = true;
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const Point at = ctx.local;
    if (p->kind == PointerKind::down) {
        services().focus(node());
        drag_ = {};
        if (at.y < ruler || at.x < header) {  // the ruler (or the headers): the playhead
            if (at.x >= header) {
                drag_.grip = Grip::playhead;
                set_frame(tl::frame_at(model_.rate, snapped(time_at(at.x))));
                services().capture_pointer(node());
            }
            ctx.handled = true;
            return true;
        }
        const auto track = track_at(at.y);
        std::optional<std::size_t> hit;
        if (track)
            for (std::size_t i = 0; i < model_.tracks[*track].clips.size(); ++i) {
                const tl::Clip& c = model_.tracks[*track].clips[i];
                if (at.x >= x_of(c.at) - 2 && at.x <= x_of(c.end()) + 2) hit = i;
            }
        if (!hit) {
            selected_.reset();
            if (attached()) emit_selected(-1, -1);
        } else {
            const tl::Clip& c = model_.tracks[*track].clips[*hit];
            selected_ = {*track, *hit};
            if (attached()) emit_selected(static_cast<std::int32_t>(*hit), static_cast<std::int32_t>(*track));
            drag_ = {at.x - x_of(c.at) <= edge ? Grip::start : x_of(c.end()) - at.x <= edge ? Grip::end : Grip::clip, *track, *hit, at.x, at.y, 0, *track};
            services().capture_pointer(node());
        }
        changed();
        ctx.handled = true;
        return true;
    }
    if (p->kind == PointerKind::move && drag_.grip != Grip::none) {
        if (drag_.grip == Grip::playhead) {
            set_frame(tl::frame_at(model_.rate, snapped(time_at(at.x))));
        } else {
            drag_.shift = snapped(tl::from_seconds(static_cast<double>(at.x - drag_.from_x) / props.zoom));
            if (drag_.grip == Grip::clip)
                if (const auto t = track_at(at.y); t && model_.tracks[*t].kind == model_.tracks[drag_.track].kind) drag_.to_track = *t;
        }
        changed();
        ctx.handled = true;
        return true;
    }
    if ((p->kind == PointerKind::up || p->kind == PointerKind::cancel) && drag_.grip != Grip::none) {
        const Drag d = std::exchange(drag_, {});
        services().release_pointer();
        if (p->kind == PointerKind::up && d.grip != Grip::playhead && (d.shift != 0 || d.to_track != d.track)) {
            // The gesture's edit made as the person's action, so it joins the history (S-12) as a key's or an agent's would.
            const tl::Clip& c = model_.tracks[d.track].clips[d.clip];
            const auto frame = [&](tl::Flicks t) { return static_cast<std::int64_t>(tl::frame_at(model_.rate, t)); };
            const auto track = static_cast<std::int64_t>(d.track), clip = static_cast<std::int64_t>(d.clip);
            if (d.grip == Grip::clip)
                (void)services().perform(node(), "move", Value{}.set("track", track).set("clip", clip).set("to_track", static_cast<std::int64_t>(d.to_track)).set("frame", frame(c.at + d.shift)));
            else if (d.grip == Grip::start)
                (void)services().perform(node(), "trim", Value{}.set("track", track).set("clip", clip).set("frame", frame(c.at + d.shift)).set("frames", frame(c.length - d.shift)));
            else
                (void)services().perform(node(), "trim", Value{}.set("track", track).set("clip", clip).set("frame", frame(c.at)).set("frames", frame(c.length + d.shift)));
        }
        changed();
        ctx.handled = true;
        return true;
    }
    return false;
}

// ── The picture ─────────────────────────────────────────────────────────

void TimelineEditor::paint(PaintContext& ctx) const {
    const Theme& theme = ctx.theme;
    auto& out = ctx.list;
    const float w = ctx.size.width, h = ctx.size.height;
    out.fill_rect({0, 0, w, h}, theme.color(theme.dark ? "#0f141c" : "#f6f7f9"));
    if (!problems_.empty()) {  // a document that does not read: its first problem, in red, where the tracks would be
        if (tracks_.empty()) tracks_.emplace_back();
        tracks_[0].set_text(problems_.front());
        tracks_[0].paint(ctx, {8, ruler + 8}, theme.color("#dc2626"));
        return;
    }
    const tl::Rate rate = model_.rate;
    const Color line = theme.color(theme.dark ? "#2a3240" : "#d8dde5"), text = theme.color(theme.dark ? "#cbd5e1" : "#334155");
    out.push_clip({header, 0, std::max(0.0f, w - header), h});
    // The ruler: a tick every frame when frames are far enough apart, every second always, labelled.
    out.fill_rect({header, 0, w - header, ruler}, theme.color(theme.dark ? "#161c26" : "#eceff3"));
    const double frame_px = props.zoom / (static_cast<double>(rate.num) / rate.den);
    const tl::Flicks from = std::max<tl::Flicks>(0, time_at(header)), to = time_at(w);
    if (frame_px >= 5)
        for (std::int64_t f = tl::frame_at(rate, from); tl::frame_time(rate, f) <= to; ++f)
            out.fill_rect({x_of(tl::frame_time(rate, f)), ruler - 5, 1, 5}, line);
    const std::int64_t every = props.zoom >= 40 ? 1 : props.zoom >= 8 ? 5 : 30;  // seconds between labels
    std::size_t numbers = 0;
    for (std::int64_t s = static_cast<std::int64_t>(tl::to_seconds(from)) / every * every; s * tl::flicks_per_second <= to; s += every) {
        const float x = x_of(s * tl::flicks_per_second);
        out.fill_rect({x, 0, 1, ruler}, line);
        if (numbers >= seconds_.size()) seconds_.emplace_back(), seconds_.back().set_style("caption");
        seconds_[numbers].set_text(std::format("{}s", s));
        seconds_[numbers++].paint(ctx, {x + 4, 4}, text.with_alpha(0.75f));
    }
    // The tracks: a band each, its clips on it.
    std::size_t label = 0;
    for (std::size_t k = 0; k < model_.tracks.size(); ++k) {
        const tl::Track& track = model_.tracks[k];
        const float y = ruler + static_cast<float>(k) * track_height;
        out.fill_rect({header, y + track_height - 1, w - header, 1}, line);
        const Color colour = kind_colour(theme, track.kind);
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            const tl::Clip& c = track.clips[i];
            const bool picked = selected_ && selected_->first == k && selected_->second == i;
            const bool dragged = drag_.grip != Grip::none && drag_.grip != Grip::playhead && drag_.track == k && drag_.clip == i;
            tl::Flicks at = c.at, length = c.length;
            float row = y;
            if (dragged) {
                if (drag_.grip == Grip::clip) at += drag_.shift, row = ruler + static_cast<float>(drag_.to_track) * track_height;
                else if (drag_.grip == Grip::start) at += drag_.shift, length -= drag_.shift;
                else length += drag_.shift;
            }
            const Rect box{x_of(at), row + 4, std::max(2.0f, static_cast<float>(tl::to_seconds(length) * props.zoom)), track_height - 9};
            out.fill_round_rect(box, CornerRadii::all(4), render::Paint::solid(colour.with_alpha(track.muted ? 0.35f : picked ? 1.0f : 0.78f)));
            if (picked) out.stroke_round_rect(box, CornerRadii::all(4), 2, render::Paint::solid(theme.color(theme.dark ? "#ffffff" : "#0f172a")));
            // Its fades: the level's ramps drawn as shade across its corners.
            auto ramp = [&](tl::Flicks span, bool start) {
                if (span <= 0) return;
                const float fw = static_cast<float>(tl::to_seconds(span) * props.zoom);
                render::Path tri;
                if (start) tri.move_to({box.x, box.y}).line_to({box.x + fw, box.y}).line_to({box.x, box.y + box.height}).close();
                else tri.move_to({box.x + box.width, box.y}).line_to({box.x + box.width - fw, box.y}).line_to({box.x + box.width, box.y + box.height}).close();
                out.fill_path(tri, render::FillRule::nonzero, render::Paint::solid(Color{0, 0, 0, 0.28f}));
            };
            ramp(c.fade.in, true);
            ramp(c.fade.out, false);
            // Its keyed properties: a small diamond at each key.
            for (const tl::Property& p : c.properties)
                for (const auto& key : p.keys.keys()) {
                    const float kx = x_of(at + tl::from_seconds(key.time.count())), ky = box.y + box.height - 6;
                    if (kx < box.x || kx > box.x + box.width) continue;
                    render::Path d;
                    d.move_to({kx, ky - 3.5f}).line_to({kx + 3.5f, ky}).line_to({kx, ky + 3.5f}).line_to({kx - 3.5f, ky}).close();
                    out.fill_path(d, render::FillRule::nonzero, render::Paint::solid(Color{1, 1, 1, 0.9f}));
                }
            // Its name, when there is room.
            if (label >= names_.size()) names_.emplace_back();
            names_[label].set_text(c.name);
            if (box.width > 24) {
                out.push_clip(box);
                names_[label].paint(ctx, {box.x + 6, box.y + 4}, Color{1, 1, 1, 0.95f});
                out.pop_clip();
            }
            ++label;
        }
    }
    // The markers: a flag on the ruler and a line down.
    for (const tl::Marker& m : model_.markers) {
        const float x = x_of(m.at);
        out.fill_rect({x, ruler, 1, h - ruler}, theme.color("#a855f7").with_alpha(0.5f));
        render::Path flag;
        flag.move_to({x, 2}).line_to({x + 8, 6}).line_to({x, 10}).close();
        out.fill_path(flag, render::FillRule::nonzero, render::Paint::solid(theme.color("#a855f7")));
    }
    // The playhead.
    const float px = x_of(tl::frame_time(rate, props.frame));
    out.fill_rect({px - 1, 0, 2, h}, theme.color("#ef4444"));
    render::Path head;
    head.move_to({px - 6, 0}).line_to({px + 6, 0}).line_to({px, 8}).close();
    out.fill_path(head, render::FillRule::nonzero, render::Paint::solid(theme.color("#ef4444")));
    out.pop_clip();
    // The headers: each track's name and kind, over its band.
    out.fill_rect({0, 0, header, h}, theme.color(theme.dark ? "#121821" : "#e6e9ee"));
    out.fill_rect({header - 1, 0, 1, h}, line);
    if (tracks_.size() < model_.tracks.size()) tracks_.resize(model_.tracks.size());
    for (std::size_t k = 0; k < model_.tracks.size(); ++k) {
        const float y = ruler + static_cast<float>(k) * track_height;
        out.fill_rect({4, y + 8, 4, track_height - 16}, kind_colour(theme, model_.tracks[k].kind));
        tracks_[k].set_text(model_.tracks[k].name);
        tracks_[k].paint(ctx, {14, y + 12}, model_.tracks[k].muted ? text.with_alpha(0.45f) : text);
    }
}

void TimelineEditor::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = model_.name;
    Value v;
    const tl::Rate rate = model_.rate;
    v.set("frame", props.frame).set("rate", std::format("{}/{}", rate.num, rate.den)).set("frames", tl::frame_at(rate, model_.duration()));
    if (selected_) v.set("selected", Value{}.set("track", static_cast<std::int64_t>(selected_->first)).set("clip", static_cast<std::int64_t>(selected_->second)));
    if (!last_.empty()) v.set("last", last_);
    Value::List tracks;
    for (const tl::Track& track : model_.tracks) {
        Value::List clips;
        for (const tl::Clip& c : track.clips)
            clips.push_back(Value{}.set("name", c.name).set("source", c.source).set("frame", tl::frame_at(rate, c.at)).set("frames", tl::frame_at(rate, c.length)));
        tracks.push_back(Value{}.set("name", track.name).set("kind", std::string(kind_names[static_cast<std::size_t>(track.kind)])).set("clips", Value(std::move(clips))));
    }
    v.set("tracks", Value(std::move(tracks)));
    if (!problems_.empty()) v.set("problems", Value(Value::List(problems_.begin(), problems_.end())));
    node.value = std::move(v);
}

}  // namespace omui
