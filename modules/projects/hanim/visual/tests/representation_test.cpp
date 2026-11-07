// The representations a drawing's motion reaches (convergence plan §6.8;
// universal motion §13.1): an appearing stroke written along its length (a
// focus ring traced round), what appears dropping in and flattening as it
// lands, a moving piece wandering on its way — exact at both ends.
import std;
import hanim.core;
import mlibs.geom;
import xrender.vector;
import hanim.visual;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;
namespace mo = hanim::visual;
namespace geom = mlibs::geom;

namespace {
mo::Resolver resolver() {
    return {.keys = [](std::uint32_t, std::string& key, std::string&) { key = "main/piece"; }};
}
mo::Options linear() {
    mo::Options options;
    options.curve.kind = mo::Curve::Kind::tween;
    options.curve.duration = 1;
    options.curve.ease = hanim::ease::linear;
    return options;
}
xrender::DisplayList blank() {
    xrender::DisplayList list;
    list.begin_node(1);
    list.end_node();
    return list;
}
xrender::DisplayList ring() {
    xrender::DisplayList list;
    list.begin_node(1);
    list.stroke_round_rect({10, 10, 80, 40}, geom::CornerRadii::all(8), 2,
                           xrender::Paint::solid(geom::Color::hex(0x3366ff)));
    list.end_node();
    return list;
}
xrender::DisplayList card(geom::Affine place = {}) {
    xrender::DisplayList list;
    list.begin_node(1);
    list.push_transform(place);
    list.fill_rect({30, 60, 40, 20}, xrender::Paint::solid(geom::Color::hex(0x182030)));
    list.pop_transform();
    list.end_node();
    return list;
}
xrender::DisplayList drawn(const mo::Morph& morph, double time) {
    xrender::DisplayList out;
    morph.draw(time, out);
    return out;
}
// The fill's rectangle where it is drawn, through the transforms around it.
std::optional<geom::Rect> placed_fill(const xrender::DisplayList& list) {
    std::vector<geom::Affine> transforms{geom::Affine{}};
    for (const auto& command : list.commands()) {
        if (const auto* push = std::get_if<xrender::PushTransform>(&command))
            transforms.push_back(transforms.back() * push->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command))
            transforms.pop_back();
        else if (const auto* fill = std::get_if<xrender::FillRect>(&command))
            return transforms.back().map(fill->rect);
    }
    return std::nullopt;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"hanim.visual.representation"};

    suite.test("an appearing stroke is written along its length, then is the stroke itself", [] {
        auto options = linear();
        options.write = true;
        const mo::Morph morph(mo::Matter::extract(blank(), resolver()),
                              mo::Matter::extract(ring(), resolver()), options, 0);
        for (const double time : {.25, .5, .75}) {
            const auto frame = drawn(morph, time);
            const xrender::StrokePath* written = nullptr;
            for (const auto& command : frame.commands())
                if (const auto* stroke = std::get_if<xrender::StrokePath>(&command))
                    written = stroke;
            check(written && written->style.trim_start == 0 &&
                      std::abs(written->style.trim_end - time) < 1e-6 && written->style.width == 2,
                  std::format("at {} the ring is drawn to {}", time,
                              written ? written->style.trim_end : -1));
        }
        const auto end = drawn(morph, 1.5);
        check(std::ranges::any_of(end.commands(),
                                  [](const auto& command) {
                                      return std::holds_alternative<xrender::StrokeRoundRect>(
                                          command);
                                  }),
              "settled: the ring as drawn");
        // And going, unwritten.
        const mo::Morph going(mo::Matter::extract(ring(), resolver()),
                              mo::Matter::extract(blank(), resolver()), options, 0);
        bool shrinking = false;
        for (const auto& command : drawn(going, .3).commands())
            if (const auto* stroke = std::get_if<xrender::StrokePath>(&command))
                shrinking = stroke->style.trim_end < 1 && stroke->style.trim_end > 0;
        check(shrinking, "a going ring is undrawn");
    });

    suite.test("what appears drops in, flattens about its bottom as it lands, and rests exactly",
               [] {
                   auto options = linear();
                   options.drop = 24;
                   const mo::Morph morph(mo::Matter::extract(blank(), resolver()),
                                         mo::Matter::extract(card(), resolver()), options, 0);
                   const auto early = placed_fill(drawn(morph, .05));
                   check(early && early->y < 60 - 20,
                         std::format("high above at first: y {}", early ? early->y : 0));
                   // Half-way through the first landing (fall(0.45, 2): 1.05 of 2.45 falls).
                   const auto landed = placed_fill(drawn(morph, 1.05 / 2.45));
                   check(landed && landed->width > 40 && landed->height < 20 &&
                             std::abs(landed->bottom() - 80) < 1e-3,
                         std::format("pressed flat on its bottom: {} × {} down to {}",
                                     landed ? landed->width : 0, landed ? landed->height : 0,
                                     landed ? landed->bottom() : 0));
                   const auto rest = placed_fill(drawn(morph, 1.5));
                   check(rest && rest->x == 30 && rest->y == 60 && rest->width == 40 &&
                             rest->height == 20,
                         "at rest where it is drawn");
               });

    suite.test("a moving piece wanders on its way under a translation path, exact at both ends",
               [] {
                   auto plain = linear(), wandering = linear();
                   wandering.translation_path = hanim::PointPath::jitter(6, 20, 3);
                   const auto from = card(), to = card(geom::Affine::translate(100, 0));
                   const mo::Morph straight(mo::Matter::extract(from, resolver()),
                                            mo::Matter::extract(to, resolver()), plain, 0);
                   const mo::Morph noisy(mo::Matter::extract(from, resolver()),
                                         mo::Matter::extract(to, resolver()), wandering, 0);
                   const auto middle_straight = placed_fill(drawn(straight, .5)),
                              middle_noisy = placed_fill(drawn(noisy, .5));
                   check(middle_straight && middle_noisy &&
                             (std::abs(middle_noisy->x - middle_straight->x) > .1 ||
                              std::abs(middle_noisy->y - middle_straight->y) > .1),
                         "off the straight way half-way");
                   const auto start = placed_fill(drawn(noisy, 0)),
                              end = placed_fill(drawn(noisy, 1.5));
                   check(start && end && start->x == 30 && end->x == 130 && end->y == 60,
                         "exact where it leaves and arrives");
               });

    suite.test("a replaced glyph reshapes into its replacement, holes and all, then is the glyph",
               [] {
                   // "A" (an outline with a hole) becomes "B" (an outline with two): stand-in em
                   // outlines.
                   const auto outline_of = [](std::uint32_t id) {
                       xrender::Path path;
                       path.add_rect({0, -0.7f, 0.6f, 0.7f});
                       if (id == 1)
                           path.add_rect({0.2f, -0.5f, 0.2f, 0.2f});
                       else {
                           path.add_rect({0.2f, -0.6f, 0.2f, 0.15f});
                           path.add_rect({0.2f, -0.3f, 0.2f, 0.15f});
                       }
                       return path;
                   };
                   auto options = linear();
                   options.glyph_outline = [&](const xrender::DrawGlyphs& run,
                                               std::size_t glyph) -> std::optional<xrender::Path> {
                       const auto& placed = run.glyphs[glyph];
                       return outline_of(placed.id).transformed(
                           geom::Affine::translate(run.origin.x + placed.position.x,
                                                   run.origin.y + placed.position.y) *
                           geom::Affine::scale(run.size, run.size));
                   };
                   const auto letter = [](std::uint32_t id) {
                       xrender::DisplayList list;
                       list.begin_node(1);
                       const std::array glyphs{xrender::Glyph{.id = 7,
                                                              .position = {0, 0},
                                                              .cluster = 0,
                                                              .grapheme = 0},
                                               xrender::Glyph{.id = id,
                                                              .position = {12, 0},
                                                              .cluster = 1,
                                                              .grapheme = 1}};
                       list.draw_glyphs(xrender::FontRef{1}, 20, {20, 40}, glyphs,
                                        xrender::Paint::solid(geom::Color::hex(0x182030)));
                       list.end_node();
                       return list;
                   };
                   const mo::Morph morph(mo::Matter::extract(letter(1), resolver()),
                                         mo::Matter::extract(letter(2), resolver()), options, 0);
                   const auto contours = [](const xrender::DisplayList& list) {
                       std::size_t most = 0;
                       for (const auto& command : list.commands())
                           if (const auto* fill = std::get_if<xrender::FillPath>(&command))
                               most = std::max(most, static_cast<std::size_t>(
                                                         std::ranges::count(fill->path.verbs,
                                                                            geom::PathVerb::move)));
                       return most;
                   };
                   const auto middle = drawn(morph, .5);
                   check(contours(middle) == 3, std::format("half-way one outline of {} contours: "
                                                            "the hole kept, the second growing",
                                                            contours(middle)));
                   bool replaced_hidden = true;
                   for (const auto& command : middle.commands())
                       if (const auto* run = std::get_if<xrender::DrawGlyphs>(&command))
                           for (std::size_t index = 0; index < run->glyphs.size(); ++index)
                               if (run->glyphs[index].id != 7 && !run->styles.empty() &&
                                   run->styles[index].opacity > 0)
                                   replaced_hidden = false;
                   check(replaced_hidden, "neither letter fades meanwhile");
                   check(contours(drawn(morph, 1.5)) == 0, "settled: the glyph as drawn");
               });

    suite
        .test("a press presses the drawing in about the finger and rings out to nothing by the end",
              [] {
                  auto options = linear();
                  options.pulse_at = geom::Point{0.25f, 1};  // the card's lower left quarter
                  options.pulse = .1f;
                  const auto from = card(), to = card();
                  const mo::Morph morph(mo::Matter::extract(from, resolver()),
                                        mo::Matter::extract(to, resolver()), options, 0);
                  const auto pressed = placed_fill(drawn(morph, 1.0 / 6));  // the first crest
                  check(pressed && std::abs(pressed->width - 36) < 1e-3 &&
                            std::abs(pressed->bottom() - 80) < 1e-3 &&
                            std::abs(pressed->x - 31) < 1e-3,
                        std::format("pressed to {} wide about (40, 80)",
                                    pressed ? pressed->width : 0));
                  const auto ringing = placed_fill(drawn(morph, .5));
                  check(ringing && ringing->width > 40, "ringing back out past its size");
                  const auto rest = placed_fill(drawn(morph, 1.5));
                  check(rest && rest->width == 40 && rest->x == 30, "at rest it is not there");
              });

    return suite.run(argc, argv);
}
