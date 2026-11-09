// Theme history is visual content, not another set of current node identities.
// These probes deliberately combine a live node morph and a scoped crossfade.
import std;
import omui.view;
import xrender.raster;
import mcpplibs.mtest;
import xrender.text;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
Theme themed(bool dark) {
    Theme theme = dark ? Theme::dark_theme() : Theme::light();
    theme.motion["motion.normal"] = Transition::tween(seconds(1), hanim::ease::linear);
    return theme;
}
RuntimeOptions options(mlibs::text::FontCollection& fonts, bool reduce = false) {
    RuntimeOptions out; out.fonts = &fonts; out.size = {260, 100}; out.theme = themed(false); out.reduce_motion = reduce; return out;
}
// A tiny native overlay exercises the current ranges painted after history.
struct QualityOverlay {
    static constexpr std::string_view name = "quality-overlay";
    static constexpr bool overlay = true;
    Placement placement() const { return {.side = Placement::Side::center, .offset = 0, .light_dismiss = false}; }
    void paint(PaintContext& context) const { context.list.fill_rect({0, 0, context.size.width, context.size.height}, context.theme.color("accent")); }
};
struct Probe {
    mlibs::text::FontCollection fonts;
    Runtime runtime{options(fonts)};
    xrender::text::GlyphCache glyphs{fonts};
    Signal<float> x = signal(0.0f);
    std::vector<std::uint32_t> pixel_owners;
    explicit Probe(bool reduce = false, bool overlay = false) {
        if (reduce) runtime.set_reduce_motion(true);
        runtime.set_scope("preview", themed(false)); runtime.set_scope("nested", themed(false));
        auto thumb = box({.id = "thumb", .width = 20, .height = 20, .background = "#e11d48"});
        thumb.prop("translate_x", Source(derive([value = x] { return Value(static_cast<double>(value.get())); })));
        auto track = stack({.id = "track", .width = 100, .height = 20, .background = "surface"}, {std::move(thumb)});
        track.prop("motion", Source(Value("morph motion.normal")));
        auto nested = box({.id = "nested", .width = 40, .height = 30, .background = "surface"});
        nested.prop("theme_scope", Source(Value("nested"))).prop("translate_x", Source(Value(110.0))).prop("translate_y", Source(Value(60.0)));
        std::vector<Element> children; children.push_back(std::move(track)); children.push_back(std::move(nested));
        if (overlay) {
            Element popup; popup.type = &component_type<QualityOverlay>(); popup.id = "popup";
            popup.prop("width", Source(Value(20.0))).prop("height", Source(Value(20.0))).prop("motion", Source(Value("morph motion.normal")));
            children.push_back(std::move(popup));
        }
        auto pane = stack({.id = "pane", .width = 160, .height = 100, .background = "surface"}, std::move(children));
        pane.prop("theme_scope", Source(Value("preview")));
        runtime.mount("main", row({.width = 260, .height = 100, .gap = 0}, {
            box({.id = "outside", .width = 100, .height = 100, .background = "#0891b2"}), std::move(pane)}));
        frame(0); frame(0.1);
    }
    void frame(double time) { expect(runtime.frame({seconds(time), {}}).failure.empty()); }
    void begin() { x.set(60); frame(1); frame(1.25); }
    std::span<const xrender::Command> current_commands() const {
        const auto commands = runtime.display_list().commands();
        std::size_t depth = 0; bool root = false;
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if (const auto* n = std::get_if<xrender::BeginNode>(&commands[i])) {
                const auto id = runtime.node_at_index(n->node);
                if (!root && runtime.valid(id) && runtime.path(id) == "main") root = true;
                if (root) ++depth;
            } else if (root && std::holds_alternative<xrender::EndNode>(commands[i])) {
                if (depth && --depth == 0) return commands.first(i + 1);
            }
        }
        return commands;
    }
    std::map<std::string, std::size_t> markers() const {
        std::map<std::string, std::size_t> out;
        for (const auto& command : current_commands()) if (const auto* n = std::get_if<xrender::BeginNode>(&command)) {
            const auto id = runtime.node_at_index(n->node);
            if (runtime.valid(id)) ++out[runtime.path(id)];
        }
        return out;
    }
    // Inspect only the current thumb's marked commands. Historical pixels may
    // overlay it, but must never supply its matching keys or its live geometry.
    std::vector<float> positions() const {
        std::vector<Affine> transforms(1); std::vector<bool> nodes;
        std::vector<float> out;
        for (const auto& command : current_commands()) {
            if (const auto* t = std::get_if<xrender::PushTransform>(&command)) transforms.push_back(transforms.back() * t->transform);
            else if (std::holds_alternative<xrender::PopTransform>(command)) { if (transforms.size() > 1) transforms.pop_back(); }
            else if (const auto* n = std::get_if<xrender::BeginNode>(&command)) {
                const auto id = runtime.node_at_index(n->node);
                nodes.push_back(runtime.valid(id) && runtime.path(id) == "main/pane/track/thumb");
            } else if (std::holds_alternative<xrender::EndNode>(command)) { if (!nodes.empty()) nodes.pop_back(); }
            else if (!nodes.empty() && nodes.back()) {
                if (const auto* f = std::get_if<xrender::FillRect>(&command); f && f->paint.color.r > 0.85f && f->paint.color.g < 0.2f)
                    out.push_back(transforms.back().apply({f->rect.x, f->rect.y}).x);
            }
        }
        return out;
    }
    xrender::raster::Surface pixels() {
        xrender::raster::Surface surface(260, 100);
        xrender::CpuRenderer renderer; xrender::CpuOptions opts; opts.background = Color::white(); opts.object_ids = true;
        const auto result = renderer.render(runtime.display_list(), surface, {&runtime.images(), &glyphs}, opts);
        expect(result.commands > 0 && result.missing_images == 0 && result.missing_glyphs == 0) << "the complete current/history display list renders natively";
        const auto owners = renderer.object_ids(); pixel_owners.assign(owners.begin(), owners.end());
        return surface;
    }
};
bool region_equal(const xrender::raster::Surface& a, const xrender::raster::Surface& b, int left = 0, int top = 0, int width = 260, int height = 100) {
    for (int y = top; y < top + height; ++y) for (int x = left; x < left + width; ++x) if (a.at(x, y) != b.at(x, y)) return false;
    return true;
}
bool same_positions(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 0.01f) return false;
    return true;
}
void current_matches(Probe& fading, Probe& reference) {
    expect(fading.runtime.display_list().balanced()) << "history must not corrupt layer, clip, transform or node boundaries";
    expect(fading.markers() == reference.markers()) << "history adds no current-node identities to an otherwise identical live morph";
    expect(same_positions(fading.positions(), reference.positions())) << "the current thumb follows the same live motion with or without historical compositing";
}
}

UTest cases{"view.theme_motion", [] {
    "the motion slots and the built-in recipes are the specification's (M-7, M-8)"_test = [] {
        expect(eq(motion_slots.size(), std::size_t{6}));
        expect(motion_slots[0] == "update" && motion_slots[5] == "replace") << "the registry's order";
        const auto library = default_motion_library();
        for (const auto& recipe : spec::motion_recipes)
            expect(recipe.name == "none" || library.contains(recipe.name)) << std::format("the spec's recipe `{}` is built in", recipe.name);
        for (const auto& [name, motion] : library) {
            const auto* recipe = spec::find_motion_recipe(name);
            expect(recipe != nullptr) << std::format("the built-in recipe `{}` is in the specification", name);
            if (!recipe) continue;
            for (std::string_view primitive : recipe->primitives | std::views::split(' ') | std::views::transform([](auto r) { return std::string_view(r); }))
                if (const auto p = motion_primitive(primitive); p && motion.primitives)
                    expect((motion.primitives & static_cast<std::uint16_t>(*p)) != 0) << std::format("`{}` composes {}", name, primitive);
            if (!recipe->effect.empty()) expect(motion.travel && motion.travel->effect == recipe->effect) << std::format("`{}` travels through {}", name, recipe->effect);
        }
    };
    "the representations' recipes carry their operations to the morph"_test = [] {
        const auto library = default_motion_library();
        expect(library.at("trace.focus").appear.writes && library.at("write.check").appear.writes) << "strokes written";
        expect(library.at("enter.drop").appear.drop > 0 && library.at("enter.drop").curve.kind == Transition::Kind::tween) << "a drop on a linear course";
        expect(!library.at("travel.noise").translation_path.linear()) << "a wandering way";
        expect(library.at("text.reshape").reshape && library.at("text.reshape").glyphs) << "replaced letters reshaped";
        expect(library.at("press.squash").press > 0) << "pressed in about the finger";
        expect(!library.at("default.focus").appear.writes && library.at("default.focus").appear.drop == 0) << "the defaults unchanged";
    };
    "a scoped theme crossfade cannot replace a running morph's marker ranges with historical ones"_test = [] {
        Probe fading, reference; fading.begin(); reference.begin();
        const auto before = fading.pixels();
        const auto captured_thumb = fading.positions();
        fading.runtime.crossfade(seconds(0.8), "preview");
        fading.runtime.set_scope("preview", themed(true)); reference.runtime.set_scope("preview", themed(true));
        fading.frame(1.3); reference.frame(1.3); current_matches(fading, reference);
        const auto first = fading.positions();
        fading.frame(1.55); reference.frame(1.55); current_matches(fading, reference);
        expect(!same_positions(first, fading.positions())) << "the ongoing morph still advances while theme history fades";
        if (fading.runtime.display_list().balanced()) {
            const auto middle = fading.pixels();
            const auto owner = fading.runtime.node_at_index(fading.pixel_owners[50 * 260 + 185]);
            expect(fading.runtime.valid(owner) && fading.runtime.path(owner) == "main/pane") << "historical pane pixels preserve their drawable owner";
            const auto now_thumb = fading.positions();
            if (captured_thumb.size() == 1 && now_thumb.size() == 1 && now_thumb.front() > captured_thumb.front() + 4) {
                const auto old_x = static_cast<std::size_t>(std::floor(captured_thumb.front()) + 1);
                const auto old_owner = fading.runtime.node_at_index(fading.pixel_owners[10 * 260 + old_x]);
                expect(fading.runtime.valid(old_owner) && fading.runtime.path(old_owner) == "main/pane/track/thumb") << "historical thumb pixels keep their original owner after the current thumb has moved away";
            } else expect(false) << "the ownership probe samples a real historical thumb position left behind by current motion";
            expect(region_equal(before, middle, 0, 0, 95, 100)) << "pixels outside the scope remain byte-for-byte unchanged";
            expect(region_equal(before, middle, 215, 65, 25, 20)) << "a nested scope keeps its own unchanged opaque material";
            expect(!region_equal(before, middle, 102, 25, 80, 65)) << "the scoped style really has an intermediate native frame";
        }
        fading.frame(3); reference.frame(3); fading.frame(3.01); reference.frame(3.01);
        current_matches(fading, reference);
        if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "after history ends the native target exactly matches a transition without history";
        expect(fading.runtime.motions().empty()) << "the morph returns to an unheld resting state";
    };
    "interrupting scoped history preserves one current identity stream and settles without stale ranges"_test = [] {
        Probe fading, reference; fading.begin(); reference.begin();
        const auto outside = fading.pixels();
        double time = 1.3;
        for (const bool dark : {true, false, true, false}) {
            fading.runtime.crossfade(seconds(0.5), "preview");
            fading.runtime.set_scope("preview", themed(dark)); reference.runtime.set_scope("preview", themed(dark));
            fading.frame(time); reference.frame(time); current_matches(fading, reference);
            if (fading.runtime.display_list().balanced()) expect(region_equal(outside, fading.pixels(), 0, 0, 95, 100)) << "reentry never composites history outside the requested scope";
            time += 0.13;
            fading.frame(time); reference.frame(time); current_matches(fading, reference);
        }
        fading.frame(4); reference.frame(4); fading.frame(4.01); reference.frame(4.01);
        current_matches(fading, reference);
        if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "interrupted history fully releases and recovers the exact current drawing";
    };
    "history cannot replace a paused or explicitly sought current motion"_test = [] {
        Probe fading, reference; fading.begin(); reference.begin();
        const auto moving = fading.runtime.find("main/pane/track"), other = reference.runtime.find("main/pane/track");
        expect(fading.runtime.hold_motion(moving) && reference.runtime.hold_motion(other));
        fading.runtime.pause_motion(moving); reference.runtime.pause_motion(other);
        fading.runtime.seek_motion(moving, 0.7); reference.runtime.seek_motion(other, 0.7);
        fading.frame(1.3); reference.frame(1.3);
        fading.runtime.crossfade(seconds(0.5), "preview");
        fading.frame(1.4); reference.frame(1.4); current_matches(fading, reference);
        const auto held = fading.positions();
        fading.runtime.seek_motion(moving, 0.2); reference.runtime.seek_motion(other, 0.2);
        fading.frame(1.5); reference.frame(1.5); current_matches(fading, reference);
        expect(!same_positions(held, fading.positions())) << "seek still controls the current drawing while history is visible";
        fading.frame(2); reference.frame(2); current_matches(fading, reference);
        expect(fading.runtime.motion_time(moving).has_value()) << "the explicit hold survives the end of a theme fade";
        fading.runtime.release_motion(moving); reference.runtime.release_motion(other);
        fading.frame(4); reference.frame(4); fading.frame(4.01); reference.frame(4.01);
        current_matches(fading, reference);
        expect(!fading.runtime.motion_time(moving)) << "releasing the handle restores normal settling";
    };
    "global history also stays outside the current marker stream"_test = [] {
        Probe fading, reference; fading.begin(); reference.begin();
        fading.runtime.crossfade(seconds(0.5));
        fading.runtime.set_scope("preview", themed(true)); reference.runtime.set_scope("preview", themed(true));
        fading.frame(1.3); reference.frame(1.3); current_matches(fading, reference);
        fading.frame(1.5); reference.frame(1.5); current_matches(fading, reference);
        fading.frame(3); reference.frame(3); fading.frame(3.01); reference.frame(3.01);
        current_matches(fading, reference);
        if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "global history also releases the exact target pixels";
    };
    "host reduced motion and motion-off end history without delaying the current target"_test = [] {
        for (const bool host_reduce : {false, true}) {
            Probe fading(host_reduce), reference(host_reduce);
            if (!host_reduce) { fading.runtime.set_motion_level(MotionLevel::off); reference.runtime.set_motion_level(MotionLevel::off); }
            fading.begin(); reference.begin();
            fading.runtime.crossfade(seconds(0.8), "preview");
            fading.runtime.set_scope("preview", themed(true)); reference.runtime.set_scope("preview", themed(true));
            fading.frame(1.3); reference.frame(1.3); current_matches(fading, reference);
            if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "reduced/off policy immediately shows the current target without historical pixels";
            fading.frame(1.31); reference.frame(1.31);
            const auto output = fading.runtime.frame({seconds(1.32), {}});
            expect(!output.animating && output.demand.idle()) << "cleared history does not keep asking the host for animation frames";
        }
    };
    "the reduced level retains its gentle transition and recovers cleanly with nested scopes"_test = [] {
        Probe fading, reference;
        fading.runtime.set_motion_level(MotionLevel::reduced); reference.runtime.set_motion_level(MotionLevel::reduced);
        fading.begin(); reference.begin();
        fading.runtime.crossfade(seconds(0.3), "preview");
        fading.runtime.set_scope("preview", themed(true)); reference.runtime.set_scope("preview", themed(true));
        fading.frame(1.3); reference.frame(1.3); current_matches(fading, reference);
        fading.frame(1.7); reference.frame(1.7); fading.frame(1.71); reference.frame(1.71);
        current_matches(fading, reference);
        if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "the reduced style transition restores exact target pixels after its bounded duration";
    };
    "current overlays remain above history and retarget from their current range after the old layer"_test = [] {
        Probe fading(false, true), reference(false, true); fading.begin(); reference.begin();
        double time = 1.3;
        for (const bool dark : {true, false, true}) {
            fading.runtime.crossfade(seconds(0.7), "preview");
            fading.runtime.set_scope("preview", themed(dark)); reference.runtime.set_scope("preview", themed(dark));
            fading.frame(time); reference.frame(time); current_matches(fading, reference);
            time += 0.2; fading.frame(time); reference.frame(time); current_matches(fading, reference);
            if (fading.runtime.display_list().balanced()) {
                const auto pixels = fading.pixels(), expected = reference.pixels();
                expect(region_equal(pixels, expected, 122, 42, 16, 16)) << "the current opaque overlay is never covered or retargeted from its historical copy";
                const auto owner = fading.runtime.node_at_index(fading.pixel_owners[50 * 260 + 130]);
                expect(fading.runtime.valid(owner) && fading.runtime.path(owner) == "main/pane/popup") << "current overlay pixel ownership stays above the history layer";
            }
        }
        fading.frame(4); reference.frame(4); fading.frame(4.01); reference.frame(4.01);
        current_matches(fading, reference);
        if (fading.runtime.display_list().balanced()) expect(region_equal(fading.pixels(), reference.pixels())) << "overlay and ordinary roots both restore the exact target after history finishes";
    };
}};
