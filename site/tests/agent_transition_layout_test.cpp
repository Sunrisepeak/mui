// Sample the post-motion DisplayList, not target screen_bounds. The latter
// intentionally describes the destination and cannot prove an intermediate
// drawing or its input geometry correct.
import std;
import openmui;
import openmui.site;
import openmui.theme;
import openmui.theme.styles;
import mlibs.testing;
import mcpplibs.os;
import xrender.text;

using namespace omui;
using namespace mlibs::testing;

namespace {
Value rect_value(Rect rect) {
    return Value(Value::List{rect.x, rect.y, rect.width, rect.height});
}
void unite(std::optional<Rect>& total, Rect rect) {
    if (!rect.empty()) total = total ? total->united(rect) : rect;
}
bool inside(Rect outer, Rect inner, float tolerance = 1.5f) {
    return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance &&
           inner.right() <= outer.right() + tolerance && inner.bottom() <= outer.bottom() + tolerance;
}
bool primary(std::string_view id) { return id.ends_with("/verify") || id.ends_with("/arrange"); }
bool playback(std::string_view id) { return id.ends_with("/pause") || id.ends_with("/replay"); }

struct PaintedBody {
    Rect rect;
    NodeId node;
    std::uint32_t marker = 0;
    float opacity = 1;
    Rect unclipped;
};
struct Drawing {
    NodeId node;
    std::vector<PaintedBody> bodies;
    std::optional<Rect> body, ink;
    std::vector<Rect> glyphs;
    std::vector<Rect> unclipped_glyphs;
};
using Drawings = std::map<std::string, Drawing, std::less<>>;

// Resolve markers with the public node_at_index API. Preserve ancestry when
// an exiting piece has marker zero: its enclosing root still owns it.
Drawings drawings(Runtime& runtime, xrender::text::GlyphCache& cache, std::string_view prefix, bool actions_only = true) {
    Drawings result;
    Affine transform;
    float opacity = 1;
    std::optional<Rect> clip;
    std::vector<Affine> transforms;
    std::vector<float> layers;
    std::vector<std::optional<Rect>> clips;
    std::vector<bool> path_clips; bool path_clip = false;
    std::vector<NodeId> owners;
    std::vector<std::uint32_t> markers;
    for (const auto& command : runtime.display_list().commands()) {
        if (const auto* c = std::get_if<xrender::BeginNode>(&command)) {
            const auto id = runtime.node_at_index(c->node);
            markers.push_back(runtime.valid(id) ? c->node : markers.empty() ? 0 : markers.back());
            owners.push_back(runtime.valid(id) ? id : owners.empty() ? NodeId{} : owners.back());
        } else if (std::holds_alternative<xrender::EndNode>(command)) {
            if (!owners.empty()) { owners.pop_back(); markers.pop_back(); }
        } else if (const auto* c = std::get_if<xrender::PushTransform>(&command)) {
            transforms.push_back(transform); transform = transform * c->transform;
        } else if (std::holds_alternative<xrender::PopTransform>(command)) {
            check(!transforms.empty()); if (transforms.empty()) continue;
            transform = transforms.back(); transforms.pop_back();
        } else if (const auto* c = std::get_if<xrender::PushLayer>(&command)) {
            layers.push_back(opacity); opacity *= c->opacity;
        } else if (std::holds_alternative<xrender::PopLayer>(command)) {
            check(!layers.empty()); if (layers.empty()) continue;
            opacity = layers.back(); layers.pop_back();
        } else if (const auto* c = std::get_if<xrender::PushClipRect>(&command)) {
            clips.push_back(clip); path_clips.push_back(path_clip); const auto rect = transform.map(c->rect);
            clip = clip ? clip->intersection(rect) : rect;
        } else if (std::holds_alternative<xrender::PushClipPath>(command)) {
            // Reject path clipping around sampled actions rather than
            // silently treating arbitrary path geometry as a rectangle.
            clips.push_back(clip); path_clips.push_back(path_clip); path_clip = true;
        } else if (std::holds_alternative<xrender::PopClip>(command)) {
            check(!clips.empty()); if (clips.empty()) continue;
            clip = clips.back(); clips.pop_back();
            path_clip = path_clips.back(); path_clips.pop_back();
        } else {
            if (owners.empty() || !runtime.valid(owners.back()) || opacity <= 0.03f) continue;
            const auto id = owners.back(); const auto path = runtime.path(id);
            if (!path.starts_with(prefix) || (actions_only && !primary(path) && !playback(path))) continue;
            check(!path_clip, "sampled action geometry is not approximated through an arbitrary path clip");
            auto& drawing = result[std::string(path)]; drawing.node = id;
            const auto visible = [&](Rect rect) { return clip ? rect.intersection(*clip) : rect; };
            if (const auto* c = std::get_if<xrender::FillRoundRect>(&command); c && c->paint.color.a * opacity > 0.03f) {
                const auto raw = transform.map(c->rect), rect = visible(raw);
                if (!rect.empty()) drawing.bodies.push_back({rect, id, markers.back(), opacity * c->paint.color.a, raw});
                unite(drawing.body, rect);
            } else if (const auto* c = std::get_if<xrender::FillRect>(&command); c && c->paint.color.a * opacity > 0.03f) {
                const auto raw = transform.map(c->rect), rect = visible(raw);
                if (!rect.empty()) drawing.bodies.push_back({rect, id, markers.back(), opacity * c->paint.color.a, raw});
                unite(drawing.body, rect);
            } else if (const auto* c = std::get_if<xrender::DrawGlyphs>(&command)) {
                for (std::size_t i = 0; i < c->glyphs.size(); ++i) {
                    const auto style = c->styles.empty() ? xrender::GlyphStyle{} : c->styles[i];
                    if (opacity * style.opacity * c->paint.color.a <= 0.03f) continue;
                    const auto& glyph = c->glyphs[i];
                    const auto mask = cache.mask(c->font, glyph.id, c->size, {});
                    if (!mask.coverage || mask.coverage->empty()) continue;
                    const auto bounds = mask.coverage->bounds;
                    const Rect local{static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.width), static_cast<float>(bounds.height)};
                    const Affine placed = transform * Affine::translate(c->origin.x + glyph.position.x, c->origin.y + glyph.position.y) * style.transform;
                    const auto raw = placed.map(local), ink = visible(raw);
                    drawing.unclipped_glyphs.push_back(raw);
                    if (ink.empty()) continue;
                    drawing.glyphs.push_back(ink); unite(drawing.ink, ink);
                }
            }
        }
    }
    check(transforms.empty() && layers.empty() && clips.empty() && owners.empty(), "the actual display state is balanced");
    return result;
}

struct Page {
    site::AgentFeature state;
    Signal<std::string> language = signal(std::string("en"));
    Signal<float> room;
    Headless h;
    Runtime* runtime;
    xrender::text::GlyphCache glyphs;
    site::AgentFeatureContext context;
    int width;
    std::size_t moving_samples = 0;
    Page(int window_width, std::string_view lang)
        : room(signal(std::min(1120.0f, static_cast<float>(window_width - 40)))),
          h(HeadlessOptions{.size = {static_cast<float>(window_width), 844}, .theme = site::app("agent/develop", lang).theme, .parallel_raster = false, .tier = mlibs::pipeline::Tier::t4}),
          runtime(&h.runtime()), glyphs(runtime->fonts()), width(window_width) {
        state.mode.set("develop"); language.set(std::string(lang));
        context.runtime = &runtime; context.lang = &language; context.root = "main/viewport/agent_feature";
        context.style = [this](std::string name) { check(theme::apply_style(*runtime, name, context.scope, false).has_value()); };
        h.mount("main", column({.width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(Insets{64, 20, 40, 20})}, {
            scroll({.id = "viewport", .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(24, 0)}, {
                dynamic([this] { return std::vector<Element>{site::agent_feature(state, context, room.get())}; })})}));
        h.step(); site::tick_agent_feature(state, context);
        check(h.click(path("preview/actions/pause"))); h.settle(seconds(0.8));
        site::tick_agent_feature(state, context); h.step();
        check(state.preview_paused.peek() && runtime->motion_level() == MotionLevel::full && !runtime->reduce_motion(),
              "only the scripted preview is paused; default component motion remains enabled");
    }
    std::string path(std::string_view suffix) const { return context.root + "/" + std::string(suffix); }
    void mode(std::string_view next) {
        const auto item = h.tree().node(path("modes/") + std::string(next));
        check(item && item->bounds); if (!item || !item->bounds) return;
        const Point at{item->bounds->x + item->bounds->width / 2, item->bounds->y + item->bounds->height / 2};
        h.click(at); site::tick_agent_feature(state, context);
        check(state.mode.peek() == next, "a real pointer selects the next task mode");
    }
    void step() { h.step(); site::tick_agent_feature(state, context); }
    void resize(int next_width) {
        width = next_width; room.set(std::min(1120.0f, static_cast<float>(next_width - 40)));
        runtime->resize({static_cast<float>(next_width), 844}, 1); step();
    }
    Drawings drawing() { return drawings(*runtime, glyphs, path("preview/actions/")); }
    Value sample(int frame, std::size_t& bodies, bool audit_containment = false) {
        const auto drawn = drawing(); Value::List nodes, motions;
        bool moving = false;
        for (const auto& motion : runtime->motions()) if (motion.path.starts_with(path("preview/actions/")) && primary(motion.path)) {
            motions.push_back(Value{}.set("id", motion.path).set("progress", motion.progress).set("start", motion.start).set("end", motion.end));
            moving |= motion.progress > 0 && motion.progress < 1 && motion.end > motion.start;
        }
        if (moving) ++moving_samples;
        for (const auto& [id, item] : drawn) {
            Value::List ink;
            for (const auto rect : item.glyphs) ink.push_back(rect_value(rect));
            Value entry; entry.set("id", id).set("glyph_ink", Value(std::move(ink)))
                .set("target", rect_value(runtime->screen_bounds(item.node)))
                .set("reported_drawn", rect_value(runtime->drawn_bounds(item.node)));
            if (const auto semantic = runtime->semantic_node(item.node); semantic.bounds) {
                const auto b = *semantic.bounds; entry.set("semantic", rect_value({b.x, b.y, b.width, b.height}));
            }
            if (item.ink) entry.set("ink", rect_value(*item.ink));
            // An interrupted morph may draw multiple fills under one path.
            // Their envelope can cover empty space; it is diagnostic only.
            if (item.body) entry.set("body_union", rect_value(*item.body));
            Value::List fills;
            if (primary(id)) for (const auto glyph : item.glyphs)
                check(std::ranges::any_of(item.bodies, [&](const auto& body) { return inside(body.rect, glyph); }),
                      std::format("{} frame {}: primary text stays in an actual painted body", id, frame));
            // A clipped fragment fitting the button is not proof that the
            // intended label fits. Inspect complete real glyph masks before
            // all clips, against the complete carrier before viewport clips.
            if (primary(id)) for (const auto glyph : item.unclipped_glyphs)
                check(std::ranges::any_of(item.bodies, [&](const auto& body) { return inside(body.unclipped, glyph); }),
                      std::format("{} frame {}: the complete intended glyph ink fits the button before clipping", id, frame));
            for (const auto& body : item.bodies) {
                Value fill; fill.set("rect", rect_value(body.rect)).set("unclipped_rect", rect_value(body.unclipped)).set("opacity", body.opacity).set("marker", static_cast<std::int64_t>(body.marker));
                const auto semantic = runtime->semantic_node(body.node);
                const bool removing = semantic.states.has(State::removing);
                fill.set("removing", removing).set("active", body.node == runtime->find(id))
                    .set("target", rect_value(runtime->screen_bounds(body.node)))
                    .set("reported_drawn", rect_value(runtime->drawn_bounds(body.node)));
                if (semantic.bounds) { const auto b = *semantic.bounds; fill.set("semantic", rect_value({b.x, b.y, b.width, b.height})); }
                if (primary(id)) {
                    ++bodies;
                    for (const auto& [other_id, other] : drawn) if (playback(other_id)) {
                        float overlap = 0;
                        for (const auto glyph : other.glyphs) {
                            const auto intersection = body.rect.intersection(glyph);
                            overlap += intersection.width * intersection.height;
                        }
                        if (overlap > 0) fill.set(other_id.ends_with("/pause") ? "pause_ink_overlap" : "replay_ink_overlap", overlap);
                        check(overlap < 0.5f, std::format("{} frame {}: actual primary fill never crosses {} glyph ink", id, frame, other_id));
                    }
                    // Leaving ghosts remain collision-tested, but are not
                    // interactive targets. Probe only the current live action.
                    if (!removing && body.node == runtime->find(id) && !semantic.states.has(State::hidden)) {
                        const auto at = body.rect.center();
                        const auto hit = runtime->hit_test(at), pixel = h.node_at_pixel(at);
                        fill.set("hit_at_painted_center", runtime->valid(hit) ? runtime->path(hit) : std::string())
                            .set("pixel_owner", runtime->valid(pixel) ? runtime->path(pixel) : std::string())
                            .set("hit_matches_body", hit == body.node).set("pixel_matches_body", pixel == body.node);
                        check(hit == body.node, std::format("{} frame {}: real hit agrees with an actual painted fill center", id, frame));
                    }
                }
                fills.push_back(std::move(fill));
            }
            entry.set("bodies", Value(std::move(fills)));
            nodes.push_back(std::move(entry));
        }
        Value::List containment;
        if (audit_containment) {
            const auto all = drawings(*runtime, glyphs, context.root, false);
            const auto parent = all.find(path("specimen"));
            check(parent != all.end() && !parent->second.bodies.empty(), "the sampled specimen has a real painted carrier");
            if (parent != all.end()) {
                Value::List carriers;
                for (const auto& body : parent->second.bodies) carriers.push_back(rect_value(body.rect));
                containment.push_back(Value{}.set("id", path("specimen")).set("carriers", Value(std::move(carriers))));
                for (const auto suffix : {"title", "reviewed", "record", "eyebrow", "receipt"}) {
                    const auto child = all.find(path(std::string("specimen/") + suffix));
                    if (child == all.end()) continue;
                    Value::List paint, ink;
                    const auto carried = [&](Rect rect) {
                        return std::ranges::any_of(parent->second.bodies, [&](const auto& body) { return inside(body.rect, rect); });
                    };
                    for (const auto& body : child->second.bodies) {
                        paint.push_back(rect_value(body.rect));
                        check(carried(body.rect), std::format("{} frame {}: control paint stays in the actually drawn parent", child->first, frame));
                    }
                    for (const auto rect : child->second.glyphs) {
                        ink.push_back(rect_value(rect));
                        check(carried(rect), std::format("{} frame {}: actual glyph ink stays in the actually drawn parent", child->first, frame));
                    }
                    containment.push_back(Value{}.set("id", child->first).set("bodies", Value(std::move(paint))).set("glyph_ink", Value(std::move(ink))));
                }
                if (const auto label = all.find(path("preview/label")); label != all.end()) {
                    float overlap = 0;
                    for (const auto& body : parent->second.bodies) for (const auto ink : label->second.glyphs) {
                        const auto intersection = body.rect.intersection(ink);
                        overlap += intersection.width * intersection.height;
                    }
                    check(overlap < 0.5f, std::format("frame {}: the actual specimen carrier never covers the following preview label", frame));
                    containment.push_back(Value{}.set("id", label->first).set("specimen_overlap", overlap));
                }
            }
            if (const auto status = all.find(path("preview/state")); status != all.end()) {
                for (const auto& [id, item] : drawn) if (primary(id)) for (const auto& body : item.bodies) {
                    float overlap = 0;
                    for (const auto ink : status->second.glyphs) {
                        const auto intersection = body.rect.intersection(ink);
                        overlap += intersection.width * intersection.height;
                    }
                    check(overlap < 0.5f, std::format("{} frame {}: primary paint never covers the actual status glyphs", id, frame));
                    containment.push_back(Value{}.set("id", id).set("status_overlap", overlap));
                }
            }
        }
        return Value{}.set("frame", frame).set("time", h.now().count()).set("width", width).set("motions", Value(std::move(motions))).set("nodes", Value(std::move(nodes))).set("containment", Value(std::move(containment)));
    }
};

void write_trace(const std::filesystem::path& file, Value::List frames) {
    const auto data = Value{}.set("source", "post-motion DisplayList with actual font glyph masks")
        .set("motion", "full").set("frames", Value(std::move(frames))).json() + '\n';
    check(mcpplibs::os::fs::write_atomic(file, data).has_value(), file.generic_string());
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.agent-transition-layout"); (void)themes::add_styles();
    const auto media = scratch_dir("agent-transition-layout");
    std::println("    transition evidence: {}", media.generic_string());

    suite.test("both task directions keep painted actions and glyphs apart throughout every intermediate frame", [media] {
        for (const int width : {360, 390, 1280}) for (const auto lang : {"en", "zh"}) {
            Page page(width, lang); Value::List frames; std::size_t bodies = 0;
            const auto action = page.runtime->screen_bounds(page.runtime->find(page.path("preview/actions")));
            const Rect film{0, std::max(0.0f, action.y - 40), static_cast<float>(width), std::min(240.0f, 844 - std::max(0.0f, action.y - 40))};
            int sample = 0;
            for (const auto mode : {"design", "develop"}) {
                page.mode(mode);
                for (int frame = 0; frame <= 30; ++frame) {
                    frames.push_back(page.sample(sample++, bodies)); page.h.record(film);
                    if (frame == 0 || frame == 5 || frame == 15 || frame == 30)
                        check(page.h.save_png(media / std::format("{}-{}-{}-{}.png", lang, width, mode, frame), Rect{0, 0, static_cast<float>(width), 844}).has_value());
                    if (frame != 30) page.step();
                }
            }
            check(bodies >= 40, "the sequence contains actual visible primary bodies, not only target boxes");
            check(page.moving_samples > 0, "default task motion has genuine intermediate progress and was not disabled to pass");
            check(page.h.save_recording(media / std::format("{}-{}.apng", lang, width), {.delay_num = 1, .delay_den = 60}).has_value());
            write_trace(media / std::format("{}-{}.motion.omui.json", lang, width), std::move(frames));
        }
    });

    suite.test("a real click during task replacement invokes the painted task action rather than Replay", [media] {
        for (const int width : {360, 390, 1280}) for (const auto lang : {"en", "zh"}) for (const int at_frame : {5, 9}) {
            Page page(width, lang); page.mode("design");
            for (int i = 0; i < at_frame; ++i) page.step();
            std::size_t bodies = 0; Value::List frames{page.sample(at_frame, bodies)};
            const auto drawn = page.drawing(); const auto id = page.path("preview/actions/arrange");
            const auto found = drawn.find(id);
            const PaintedBody* active_body = nullptr;
            if (found != drawn.end()) for (const auto& body : found->second.bodies)
                if (body.node == page.runtime->find(id) && !page.runtime->semantic_node(body.node).states.has(State::removing) &&
                    (!active_body || body.rect.width * body.rect.height > active_body->rect.width * active_body->rect.height)) active_body = &body;
            check(active_body != nullptr, "the new task button is really painted at the probe time");
            if (active_body) {
                const auto before = page.runtime->action_journal().size();
                page.h.click(active_body->rect.center());
                const auto& journal = page.runtime->action_journal();
                check(std::ranges::any_of(journal | std::views::drop(before), [&](const auto& entry) {
                    return entry.path == id && entry.action == "activate" && entry.actor == "person:user" && entry.exit_code == 0;
                }), "real pointer input records the arrange action, not a neighbouring playback action");
                check(page.state.layout.peek() == "cards", "the task really changed its layout");
            }
            write_trace(media / std::format("click-{}-{}-{}.motion.omui.json", lang, width, at_frame), std::move(frames));
        }
    });

    suite.test("mode interruption and viewport resize keep sampled geometry observable without closing the framework presentation audit", [media] {
        for (const int width : {390, 1280}) for (const auto lang : {"en", "zh"}) {
            Page page(width, lang); Value::List frames; std::size_t bodies = 0;
            page.mode("design");
            for (int frame = 0; frame <= 36; ++frame) {
                if (frame == 5) page.mode("develop");
                if (frame == 8) page.resize(360);
                if (frame == 16) page.mode("design");
                if (frame == 24) page.resize(width);
                frames.push_back(page.sample(frame, bodies, true));
                page.h.record(Rect{0, 0, static_cast<float>(width), 844});
                if (frame == 5 || frame == 8 || frame == 16 || frame == 24 || frame == 36)
                    check(page.h.save_png(media / std::format("interrupt-{}-{}-{}.png", lang, width, frame), Rect{0, 0, static_cast<float>(page.width), 844}).has_value());
                if (frame != 36) page.step();
            }
            check(page.h.save_recording(media / std::format("interrupt-{}-{}.apng", lang, width), {.delay_num = 1, .delay_den = 60}).has_value());
            check(bodies > 0); check(page.state.title.peek() == "Review an idea", "layout and interruption preserve the actual draft");
            write_trace(media / std::format("interrupt-{}-{}.motion.omui.json", lang, width), std::move(frames));
        }
    });
    return suite.run(argc, argv);
}
