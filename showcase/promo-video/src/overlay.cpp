// What lies over every shot: the lines, the chapter, the timeline along the
// bottom, and the tape's look over the film's own frames played back.
module openmui.showcase.promo_video;

import std;
import openmui;

namespace omui::promo {

namespace {

constexpr float subtitle_zh = 40, subtitle_en = 25;

std::string timecode(double s) {
    const auto cs = static_cast<std::int64_t>(std::floor(s * 100 + 1e-9));
    return std::format("{:02}:{:02}.{:02}", cs / 6000, cs / 100 % 60, cs % 100);
}

std::string chapter_title(std::string_view name) {
    std::string up(name);
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up;
}

std::string_view chapter_zh(std::string_view name) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 8> zh{{{"hook", "像素"},
                                                                              {"title", "标题"},
                                                                              {"code", "代码"},
                                                                              {"infinite", "无限"},
                                                                              {"styles", "风格"},
                                                                              {"agent", "Agent"},
                                                                              {"time", "时间"},
                                                                              {"end", "结尾"}}};
    for (const auto& [en, z] : zh)
        if (en == name) return z;
    return name;
}

}  // namespace

// What the overlay's painters read, set before each frame.
struct Overlay::State {
    std::int64_t                    frame = 0;
    double                          seconds = 0;
    std::optional<render::ImageRef> picture;  // the film's own frame, played back
    double                          speed = 1;
};

Overlay::Overlay(const Cut& cut, const Subtitles& subtitles, Size size, OverlayOptions options)
    : cut_(&cut), subtitles_(&subtitles), size_(size), options_(options) {
    if (cut.subtitles >= 0)
        for (const tl::Clip& c : cut.timeline.tracks[static_cast<std::size_t>(cut.subtitles)].clips)
            if (c.source.starts_with("line:"))
                if (const Line* l = subtitles.find(std::string_view(c.source).substr(5))) lines_.push_back({l, &c});
    chapters_ = chapters(cut);
}

Rect Overlay::safe() const noexcept {
    return {std::round(size_.width * 0.05f), std::round(size_.height * 0.05f), std::round(size_.width * 0.9f), std::round(size_.height * 0.9f)};
}

View Overlay::view() {
    const Rect safe = this->safe();
    const bool portrait = size_.height > size_.width;
    auto state = std::make_shared<State>();
    // Each line: Chinese above English on a drawing's label, its words rising in on its moment.
    std::vector<Element> lines;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const Showing& s = lines_[i];
        const double from = tl::to_seconds(s.clip->at);
        GlyphMotion rise = GlyphMotion::rise(hanim::seconds(0.05));
        rise.transition.delay = hanim::seconds(from);
        Element zh = text({.id = "zh", .text = s.line->zh, .style = "title", .color = Fill(hex(ink::ice)), .align = "center", .wrap = false,
                           .size = subtitle_zh, .weight = 600.0f});
        GlyphMotion later = rise;
        later.transition.delay = hanim::seconds(from + 0.12);
        Element en = text({.id = "en", .text = s.line->en, .style = "body", .color = Fill(hex(ink::muted)), .align = "center", .wrap = false,
                           .size = subtitle_en, .weight = 450.0f});
        lines.push_back(column({.id = std::format("line-{}", i),
                                .padding = Edges(10.0f, 26.0f),
                                .gap = 2,
                                .align = "center",
                                .background = Fill(hex(ink::paper, 0.74f)),
                                .border_color = Fill(hex(ink::cyan, 0.22f)),
                                .border_width = 1,
                                .radius = 6,
                                .opacity = 0.0f,
                                .visible = false},
                               {animate_glyphs(std::move(zh), rise), animate_glyphs(std::move(en), later)}));
    }
    // Each chapter's label, typed in as it begins.
    std::vector<Element> labels;
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        const Chapter& c = chapters_[i];
        GlyphMotion type = GlyphMotion::typewriter(hanim::seconds(0.025));
        type.transition.delay = hanim::seconds(cut_->seconds_of(c.frame) + 0.15);
        labels.push_back(row({.id = std::format("chapter-{}", i), .gap = 12, .align = "center", .visible = false},
                             {box({.width = 28, .height = 2, .background = Fill(hex(ink::cyan))}),
                              animate_glyphs(text({.id = "name",
                                                   .text = std::format("{:02} / {:02}   {}   {}", i + 1, chapters_.size(), chapter_title(c.name), chapter_zh(c.name)),
                                                   .style = "code",
                                                   .color = Fill(hex(ink::muted)),
                                                   .wrap = false,
                                                   .size = 15.0f}),
                                             type)}));
    }
    // The cut along the bottom: its picture clips, its hits, the playhead.
    const Cut* cut = cut_;
    Element bar = painter({.id = "timebar",
                           .height = 14,
                           .grow = 1,
                           .role = Role::figure,
                           .label = "The cut's timeline",
                           .paint = [cut, state](PaintContext& p) {
                               const float w = p.size.width, h = p.size.height;
                               const double total = tl::to_seconds(cut->timeline.duration());
                               if (total <= 0 || w <= 0) return;
                               const auto x_of = [&](double s) { return static_cast<float>(s / total) * w; };
                               p.list.fill_rect({0, h / 2, w, 1}, hex(ink::line, 0.7f));
                               const tl::Track& pic = cut->timeline.tracks[static_cast<std::size_t>(cut->picture)];
                               for (std::size_t i = 0; i < pic.clips.size(); ++i) {
                                   const tl::Clip& c = pic.clips[i];
                                   const float x0 = x_of(tl::to_seconds(c.at)) + 1, x1 = x_of(tl::to_seconds(c.end())) - 1;
                                   const bool film = c.source == "film";
                                   const Color col = film ? hex(ink::amber, 0.85f) : hex(i % 2 ? ink::line : 0x3b82c4, 0.9f);
                                   p.list.fill_round_rect({x0, h / 2 - 3, std::max(1.0f, x1 - x0), 6}, CornerRadii::all(2), render::Paint::solid(col));
                               }
                               for (const tl::Marker& m : cut->timeline.markers)
                                   if (m.name.starts_with("hit:")) p.list.fill_rect({std::round(x_of(tl::to_seconds(m.at))), 0, 1, 3}, hex(ink::cyan, 0.8f));
                               const float x = x_of(state->seconds);
                               p.list.fill_rect({0, h / 2 - 1, x, 2}, hex(ink::cyan, 0.85f));
                               p.list.fill_round_rect({x - 1.5f, 0, 3, h}, CornerRadii::all(1.5f), render::Paint::solid(hex(ink::ice)));
                           }});
    // The film's own frame, played back: the tape's colours drifting apart, its rows wavering.
    Element playback = painter({.id = "playback",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::none,
                                .paint = [state](PaintContext& p) {
                                    if (!state->picture) return;
                                    const raster::Surface* img = p.services.images().get(*state->picture);
                                    if (!img) return;
                                    p.list.draw_image(*state->picture, {0, 0, static_cast<float>(img->width()), static_cast<float>(img->height())},
                                                      {0, 0, p.size.width, p.size.height}, render::Sampling::linear);
                                    // A tracking band rolling up the picture.
                                    const float band = static_cast<float>(std::fmod(state->seconds * 2.3, 1.0)) * p.size.height;
                                    for (int k = 0; k < 3; ++k) {
                                        const float y = std::fmod(p.size.height - band + static_cast<float>(k) * p.size.height / 3, p.size.height);
                                        p.list.fill_rect({0, y, p.size.width, 3 + static_cast<float>(k)}, hex(ink::ice, 0.07f));
                                    }
                                }});
    Element tape = column({.id = "tape", .gap = 2, .align = "center", .visible = false},
                          {text({.id = "speed", .text = "◀◀  ×5", .style = "code", .color = Fill(hex(ink::amber)), .wrap = false, .size = 44.0f,
                                 .weight = 700.0f}),
                           text({.id = "word", .text = "REWIND · 倒带", .style = "code", .color = Fill(hex(ink::amber, 0.85f)), .wrap = false, .size = 18.0f})});
    std::vector<Element> top;
    top.push_back(stack({.id = "chapters", .grow = 1}, std::move(labels)));
    Element bottom = row({.id = "timeline", .width = Dim(layout::pct(100)), .gap = 16, .align = "center"},
                         {std::move(bar), text({.id = "timecode", .text = "00:00.00", .style = "code", .color = Fill(hex(ink::muted)), .wrap = false, .size = 14.0f})});
    Element type = column({.id = "type",
                           .width = Dim(layout::pct(100)),
                           .height = Dim(layout::pct(100)),
                           .padding = Edges(Insets{safe.y, size_.width - safe.x - safe.width, size_.height - safe.y - safe.height, safe.x}),
                           .gap = portrait ? 18.0f : 14.0f,
                           .align = "stretch"},
                          {row({.id = "top", .align = "start"}, std::move(top)), row({.id = "tape-row", .justify = "center"}, {std::move(tape)}), spacer(),
                           stack({.id = "lines", .justify = "end", .align = "center"}, std::move(lines)), std::move(bottom)});
    Element root = stack({.id = "overlay", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                         {box({.id = "tape-look", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))},
                              {box({.id = "tape-colour", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(playback)})}),
                          std::move(type)});
    state_ = state;
    found_ = false;
    return root;
}

void Overlay::at(Runtime& rt, std::int64_t frame, const std::optional<Shown>& shown, std::optional<render::ImageRef> played_back) {
    if (!found_) {
        line_nodes_.clear();
        chapter_nodes_.clear();
        for (std::size_t i = 0; i < lines_.size(); ++i) line_nodes_.push_back(rt.find(std::format("main/type/lines/line-{}", i)));
        for (std::size_t i = 0; i < chapters_.size(); ++i) chapter_nodes_.push_back(rt.find(std::format("main/type/top/chapters/chapter-{}", i)));
        tape_ = rt.find("main/type/tape-row/tape");
        look_ = rt.find("main/tape-look");
        colour_ = rt.find("main/tape-look/tape-colour");
        playback_ = rt.find("main/tape-look/tape-colour/playback");
        timecode_ = rt.find("main/type/timeline/timecode");
        timebar_ = rt.find("main/type/timeline/timebar");
        timeline_ = rt.find("main/type/timeline");
        found_ = true;
    }
    const double t = cut_->seconds_of(frame);
    const bool film = shown && shown->film;
    state_->frame = frame;
    state_->seconds = t;
    state_->picture = film ? played_back : std::nullopt;
    state_->speed = film ? shown->clip->speed : 1;
    // The lines: up while their clips cover the frame, in and out in a fifth of a second. Over the film's
    // own frames none: those frames carry their own.
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const tl::Clip& c = *lines_[i].clip;
        const double from = tl::to_seconds(c.at), to = tl::to_seconds(c.end());
        const bool up = options_.subtitles && !film && t >= from && t < to;
        const double fade = std::min(ramp(t, from, from + 0.16), 1 - ramp(t, to - 0.2, to));
        rt.set(line_nodes_[i], "visible", Value(up));
        rt.set(line_nodes_[i], "opacity", Value(up ? fade : 0.0));
        rt.set(line_nodes_[i], "translate_y", Value(up ? (1 - smooth(t, from, from + 0.3)) * 10 : 0.0));
    }
    // The chapter: from its marker to the next; not in the first three seconds (the hook is dark).
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        const double from = cut_->seconds_of(chapters_[i].frame);
        const double to = i + 1 < chapters_.size() ? cut_->seconds_of(chapters_[i + 1].frame) : 1e9;
        const bool on = !film && t >= std::max(from, 3.0) && t < to;
        rt.set(chapter_nodes_[i], "visible", Value(on));
        rt.set(chapter_nodes_[i], "opacity", Value(on ? smooth(t, std::max(from, 3.0), std::max(from, 3.0) + 0.3) : 0.0));
    }
    // The timeline along the bottom, from the title on; over played-back frames theirs shows.
    const bool bar = options_.timebar && !film && t >= 3.4;
    rt.set(timeline_, "opacity", Value(bar ? smooth(t, 3.4, 4.0) : 0.0));
    rt.set(timecode_, "text", Value(timecode(t)));
    rt.invalidate_paint(timebar_);
    // The tape: its speed, its colours drifting apart and its rows wavering, the more the faster.
    rt.set(tape_, "visible", Value(film));
    if (film) {
        const double speed = shown->clip->speed;
        rt.set(rt.find("main/type/tape-row/tape/speed"), "text",
               Value(speed < 0 ? std::format("◀◀  ×{}", static_cast<int>(-speed)) : std::format("▶  ×{}", static_cast<int>(speed))));
        const double wave = 0.5 + 0.5 * hanim::dm::sin(t * 23.0);
        rt.set(look_, "pixel_effect", Value{}.set("name", "displace").set("amount", 3.0 + 3.0 * wave).set("wavelength", 170.0).set("phase", std::fmod(t * 3.1, 1.0)));
        rt.set(colour_, "pixel_effect", Value{}.set("name", "chromatic").set("offset", 5.0).set("angle", 0.0));
    } else {
        rt.set(look_, "pixel_effect", Value{});
        rt.set(colour_, "pixel_effect", Value{});
    }
    rt.invalidate_paint(playback_);
}

}  // namespace omui::promo
