// The development host (openmui:devhost).
module omui;

import std;
import omui.core;
import omui.view;
import omui.agent;
import omui.semantic;
import xrender.vector;
import xrender.raster;
import xrender.text;
import mlibs.text;
import mcpplibs.os;

namespace omui {

std::vector<DevProblem> problems_in(std::string_view output) {
    std::vector<DevProblem> out;
    for (const auto part : std::views::split(output, '\n')) {
        const std::string_view line(part.begin(), part.end());
        const std::size_t marker = line.find(": error: ");
        if (marker == std::string_view::npos) continue;
        // file:line:column before the marker (a Windows drive's colon is part of the file).
        const std::string_view where = line.substr(0, marker);
        const std::size_t column_at = where.rfind(':');
        const std::size_t line_at = column_at == std::string_view::npos ? std::string_view::npos : where.rfind(':', column_at - 1);
        if (line_at == std::string_view::npos) continue;
        DevProblem problem;
        problem.file = std::string(where.substr(0, line_at));
        (void)std::from_chars(where.data() + line_at + 1, where.data() + column_at, problem.line);
        (void)std::from_chars(where.data() + column_at + 1, where.data() + where.size(), problem.column);
        problem.text = std::string(line.substr(marker + 9));
        if (problem.line > 0) out.push_back(std::move(problem));
    }
    return out;
}

struct DevHost::Impl {
    Runtime&       runtime;
    DevHostOptions options;
    std::optional<mcpplibs::os::ipc::listener> listener;
    // The fonts the applications draw with, as they load them (the defaults, in their order): a font
    // reference in their lists names the same face here. (An application's own fonts are not carried yet.)
    mlibs::text::FontCollection fonts = [] {
        mlibs::text::FontCollection loaded;
        (void)loaded.add_defaults();
        return loaded;
    }();
    xrender::text::GlyphCache glyphs;
    xrender::CpuRenderer      renderer;

    // An application shown: its presentation, the last frame drawn as the host's image.
    struct Shown {
        int                                 number = 0;
        std::unique_ptr<agent::Presentation> presentation;
        xrender::ImageRef                   image;
        std::uint64_t                       frames_drawn = 0;
    };
    std::optional<Shown> current, next, leaving;
    enum class Handover : std::uint8_t { asking, given } handover = Handover::given;
    std::uint64_t        next_frames_at_handover = 0;
    seconds              fade_began{0};
    int                  started = 0;
    std::uint64_t        timer = 0;
    std::vector<DevProblem> problems;
    bool                    ended_said = false;

    Impl(Runtime& rt, DevHostOptions given) : runtime(rt), options(std::move(given)), glyphs(fonts) {}

    std::filesystem::path socket() const { return options.directory / "app"; }

    // The application's latest frame, drawn as it would draw it, into the host's image.
    void draw(Shown& shown) {
        bool any = false;
        while (shown.presentation->receive(std::chrono::milliseconds(0))) any = true;
        if (!any || shown.presentation->frames() == shown.frames_drawn) return;
        shown.frames_drawn = shown.presentation->frames();
        const Size size = runtime.size();
        const float scale = runtime.scale();
        auto surface = std::make_shared<xrender::raster::Surface>(static_cast<int>(std::ceil(size.width * scale)), static_cast<int>(std::ceil(size.height * scale)));
        (void)renderer.render(shown.presentation->list(), *surface, {&shown.presentation->images(), &glyphs}, {.scale = scale});
        if (shown.image.id == 0) shown.image = runtime.images().add(std::move(surface));
        else if (!runtime.images().update(shown.image, surface)) shown.image = runtime.images().add(std::move(surface));
    }

    void step() {
        if (listener)
            if (auto accepted = listener->accept_for(std::chrono::milliseconds(0)); accepted && *accepted) {
                Shown shown{.number = ++started, .presentation = std::make_unique<agent::Presentation>(std::move(**accepted), &fonts), .image = {}, .frames_drawn = 0};
                (void)shown.presentation->resize(runtime.size(), runtime.scale());
                if (!current) {
                    current = std::move(shown);
                } else {
                    next = std::move(shown);
                    handover = Handover::asking;
                    (void)current->presentation->ask_state();
                }
            }
        if (current) draw(*current);
        if (leaving) draw(*leaving);
        if (next) {
            draw(*next);
            if (handover == Handover::asking) {
                // What the person set, from the one running to the new one (or nothing, when the old is gone).
                if (current->presentation->state()) {
                    (void)next->presentation->restore(*current->presentation->state());
                    handover = Handover::given;
                    next_frames_at_handover = next->presentation->frames();
                } else if (current->presentation->gone()) {
                    handover = Handover::given;
                    next_frames_at_handover = next->presentation->frames();
                }
            } else if (next->frames_drawn > next_frames_at_handover && next->image.id != 0) {
                // Its first frame after the handover is drawn (unseen): it fades in over the old.
                leaving = std::move(current);
                current = std::move(next);
                next.reset();
                fade_began = runtime.now();
            }
        }
        if (leaving && runtime.now() - fade_began >= options.fade) leaving.reset();  // the old one's connection closes: it ends
        // The application ended (or crashed): its last frame stays, said — and unsaid when one runs again.
        const bool ended = current && current->presentation->gone() && !next;
        if (ended != ended_said) {
            ended_said = ended;
            constexpr std::string_view note = "the application ended; a new build starts it again";
            if (ended && problems.empty()) problems.push_back({.text = std::string(note)});
            if (!ended) std::erase_if(problems, [&](const DevProblem& problem) { return problem.file.empty() && problem.text == note; });
            show_problems_now();
        }
        if (const NodeId shown = runtime.find("main")) runtime.invalidate_paint(shown);
    }

    void paint(PaintContext& ctx) const {
        const Rect whole{0, 0, ctx.size.width, ctx.size.height};
        const auto image = [&](const std::optional<Shown>& shown, float opacity) {
            if (!shown || shown->image.id == 0) return;
            const auto* surface = runtime.images().get(shown->image);
            if (!surface) return;
            ctx.list.draw_image(shown->image, {0, 0, static_cast<float>(surface->width()), static_cast<float>(surface->height())}, whole,
                                xrender::Sampling::linear, opacity);
        };
        if (leaving) {
            const double fade = options.fade.count() > 0 ? std::clamp((runtime.now() - fade_began).count() / options.fade.count(), 0.0, 1.0) : 1.0;
            image(leaving, 1);
            image(current, static_cast<float>(fade));
        } else {
            image(current, 1);
        }
    }

    bool forward(const InputEvent& event) {
        if (!current) return false;
        (void)current->presentation->send_input(std::span(&event, 1));
        return true;
    }

    void show_problems_now() {
        if (problems.empty()) {
            runtime.unmount("dev-problems");
            return;
        }
        std::vector<Element> cards;
        for (std::size_t index = 0; index < problems.size() && index < 8; ++index) {
            const DevProblem& problem = problems[index];
            const std::string where = problem.file.empty() ? std::string() : std::format("{}:{}:{}", problem.file, problem.line, problem.column);
            std::vector<Element> lines;
            if (!where.empty()) lines.push_back(text({.text = where, .style = "caption", .color = Fill(std::string("text_muted")), .wrap = false, .max_lines = 1}));
            lines.push_back(text({.text = problem.text, .style = "body", .max_lines = 3}));
            cards.push_back(column({.id = std::format("problem-{}", index), .padding = 10, .gap = 2, .background = Fill(std::string("surface")),
                                    .border_color = Fill(std::string("danger")), .border_width = 1, .radius = 8,
                                    .label = where.empty() ? problem.text : std::format("{}: {}", where, problem.text)},
                                   std::move(lines)));
        }
        runtime.mount("dev-problems", column({.width = runtime.size().width, .padding = 12, .gap = 8}, std::move(cards)));
    }
};

DevHost::DevHost(Runtime& runtime, DevHostOptions options) : impl_(std::make_shared<Impl>(runtime, std::move(options))) {
    Impl& d = *impl_;
    std::error_code error;
    std::filesystem::create_directories(d.options.directory, error);
    if (auto listening = mcpplibs::os::ipc::listener::listen(d.options.directory, "app")) d.listener.emplace(std::move(*listening));
    else mlibs::log::warn("dev host: no socket at {}: {}", generic_utf8(d.socket()), listening.error().message());
    const std::weak_ptr<Impl> alive = impl_;
    runtime.mount("main", painter({.id = "app",
                                   .label = "The application under development",
                                   .focusable = true,
                                   .paint = [alive](PaintContext& ctx) {
                                       if (auto impl = alive.lock()) impl->paint(ctx);
                                   },
                                   .on_event = [alive](EventContext&, const InputEvent& event) {
                                       auto impl = alive.lock();
                                       return impl && impl->forward(event);
                                   }}));
    // A frame's worth of the applications' frames, on the host's own clock.
    const auto tick = std::make_shared<std::function<void()>>();
    *tick = [alive, tick] {
        auto impl = alive.lock();
        if (!impl) return;
        impl->step();
        impl->timer = impl->runtime.after(seconds(1.0 / 60), *tick);
    };
    d.timer = runtime.after(seconds(1.0 / 60), *tick);
}

DevHost::~DevHost() {
    if (impl_->timer) impl_->runtime.cancel(impl_->timer);
    impl_->runtime.unmount("dev-problems");
}

bool DevHost::restart() {
    if (!impl_->options.start || !impl_->listener) return false;
    return impl_->options.start(impl_->socket());
}

void DevHost::show_problems(std::vector<DevProblem> problems) {
    impl_->problems = std::move(problems);
    impl_->show_problems_now();
}

Value DevHost::status() const {
    const Impl& d = *impl_;
    Value out = Value{}.set("started", static_cast<std::int64_t>(d.started)).set("fading", d.leaving.has_value()).set("problems", static_cast<std::int64_t>(d.problems.size()));
    if (d.current) out.set("shown", static_cast<std::int64_t>(d.current->number)).set("frames", static_cast<std::int64_t>(d.current->frames_drawn));
    if (d.next) out.set("waiting", static_cast<std::int64_t>(d.next->number));
    return out;
}

}  // namespace omui
