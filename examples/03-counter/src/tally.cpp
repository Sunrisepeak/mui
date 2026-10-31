module tally;

import std;
import openmui;

namespace tally {

omui::View view(const State& s) {
    using namespace omui;
    const Signal<int> count = s.count, goal = s.goal;
    return column({.padding = 24, .gap = 12}, {
        text({.id = "count", .text = derive([=] { return std::to_string(count()); }), .style = "display"}),
        progress({.id = "progress", .label = "Towards the goal",
                  .value = derive([=] { return std::clamp(static_cast<double>(count()) / std::max(goal(), 1), 0.0, 1.0); })}),
        text({.id = "status", .text = derive([=] {
                  return count() >= goal() ? std::format("Goal of {} reached", goal()) : std::format("{} to go", goal() - count());
              })}),
        row({.id = "controls", .gap = 8}, {
            button({.id = "minus", .disabled = derive([=] { return count() == 0; }), .text = "−1",
                    .on_activated = [=] { count.set(std::max(count.peek() - 1, 0)); }}),
            button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary, .on_activated = [=] { count.set(count.peek() + 1); }}),
            button({.id = "reset", .text = "Reset", .variant = ButtonVariant::ghost, .on_activated = [=] { count.set(0); }}),
        }),
        row({.id = "goal", .gap = 8}, {
            text({.text = "Goal"}),
            slider({.id = "value", .label = "Goal", .max = 100, .min = 1, .step = 1, .value = goal}),
        }),
    });
}

std::expected<Document, std::string> Document::open(omui::Runtime& rt, std::filesystem::path file) {
    auto v = omui::viewdoc::View::load_file(rt, file);
    if (!v) {
        std::string why;
        for (const auto& d : v.error()) why += d.text(omui::utf8(file)) + "\n";
        return std::unexpected(std::move(why));
    }
    Document d;
    d.rt_ = &rt;
    d.file_ = std::move(file);
    d.view_ = std::move(*v);
    std::error_code ec;
    d.stamp_ = std::filesystem::last_write_time(d.file_, ec);
    return d;
}

void Document::start(omui::Runtime& rt, omui::seconds poll) {
    rt_ = &rt;
    poll_ = poll;
    view_->mount(rt, "main");
    watch();
}

void Document::watch() {
    (void)rt_->after(poll_, [this] {
        (void)reload_if_changed();
        watch();
    });
}

bool Document::reload_if_changed() {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(file_, ec);
    if (ec || stamp == stamp_) return false;
    stamp_ = stamp;
    auto r = view_->reload_file(file_);
    if (!r) {
        status_.clear();
        for (const auto& d : r.error()) status_ += d.text(omui::utf8(file_)) + "\n";
        omui::warn("{}", status_);
        return false;
    }
    status_.clear();
    return true;
}

}  // namespace tally
