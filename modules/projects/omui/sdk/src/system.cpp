// An application's surfaces outside its own view (openmui:system).
module omui;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import mcpplibs.os;

namespace omui {

namespace {

// The extensions a filter list lets through; none: everything.
std::vector<std::string> extensions_of(const std::vector<FileFilter>& filters) {
    std::vector<std::string> out;
    for (const FileFilter& filter : filters)
        for (const auto part : std::views::split(std::string_view(filter.pattern), ';')) {
            std::string extension(part.begin(), part.end());
            if (extension == "*") return {};
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!extension.empty()) out.push_back("." + extension);
        }
    return out;
}

bool shown_by(const std::vector<std::string>& extensions, const std::filesystem::path& file) {
    if (extensions.empty()) return true;
    std::string extension = mcpplibs::os::fs::utf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::contains(extensions, extension);
}

constexpr std::string_view files_slot = "system-files", notices_slot = "system-notices", menu_slot = "system-menu";

}  // namespace

struct System::Impl {
    Runtime&      runtime;
    SystemOptions options;

    // ── the drawn file picker ──
    struct Picker {
        FileChoice                         choice;
        FilesChosen                        chosen;
        std::filesystem::path              directory;
        std::vector<std::filesystem::path> entries;  // folders first, then the files shown
        std::set<std::size_t>              selected;
        std::string                        name;     // save: the name typed
        std::string                        problem;  // what the last step came to, when it failed
    };
    std::optional<Picker> picker;

    // ── drawn notices ──
    struct Shown {
        std::uint64_t id = 0;
        Notice        notice;
    };
    std::vector<Shown> notices;
    std::uint64_t      next_notice = 1;

    // ── the drawn menu bar ──
    std::vector<MenuGroup> groups;
    std::optional<std::size_t> open_group;

    Impl(Runtime& rt, SystemOptions given) : runtime(rt), options(std::move(given)) {}

    void invoke(std::string_view verb) {
        if (verb.empty()) return;
        Invocation call;
        call.actor = {"person", "user", {}};
        const Envelope done = runtime.invoke(verb, call);
        if (done.exit_code().value_or(1) != 0) mlibs::log::warn("system: {} did not run: {}", verb, done.ndjson());
    }

    void read_directory(Picker& open) {
        open.entries.clear();
        open.selected.clear();
        std::error_code error;
        std::vector<std::filesystem::path> folders, files;
        const auto extensions = extensions_of(open.choice.filters);
        for (const auto& entry : std::filesystem::directory_iterator(open.directory, std::filesystem::directory_options::skip_permission_denied, error)) {
            if (mcpplibs::os::fs::utf8(entry.path().filename()).starts_with(".")) continue;  // hidden
            std::error_code kind;
            if (entry.is_directory(kind)) folders.push_back(entry.path());
            else if (open.choice.kind != FileChoice::Kind::folder && shown_by(extensions, entry.path())) files.push_back(entry.path());
            if (folders.size() + files.size() >= 1000) break;
        }
        const auto by_name = [](const std::filesystem::path& left, const std::filesystem::path& right) { return left.filename() < right.filename(); };
        std::ranges::sort(folders, by_name);
        std::ranges::sort(files, by_name);
        open.entries = std::move(folders);
        open.entries.insert(open.entries.end(), files.begin(), files.end());
        open.problem = error ? std::format("{} cannot be read: {}", mcpplibs::os::fs::generic_utf8(open.directory), error.message()) : std::string();
    }

    void finish(std::vector<std::filesystem::path> paths) {
        if (!picker) return;
        FilesChosen chosen = std::move(picker->chosen);
        picker.reset();
        runtime.unmount(files_slot);
        if (chosen) chosen(std::move(paths));
    }

    void enter(std::size_t index, const std::shared_ptr<Impl>& self) {
        if (!picker || index >= picker->entries.size()) return;
        Picker& open = *picker;
        const std::filesystem::path entry = open.entries[index];
        std::error_code error;
        if (std::filesystem::is_directory(entry, error)) {
            open.directory = entry;
            read_directory(open);
        } else if (open.choice.many) {
            if (!open.selected.erase(index)) open.selected.insert(index);
        } else if (open.selected.contains(index)) {
            finish({entry});  // pressed again: chosen
            return;
        } else {
            open.selected = {index};
            if (open.choice.kind == FileChoice::Kind::save) open.name = mcpplibs::os::fs::utf8(entry.filename());
        }
        show_picker(self);
    }

    void accept() {
        if (!picker) return;
        Picker& open = *picker;
        switch (open.choice.kind) {
        case FileChoice::Kind::folder: finish({open.directory}); return;
        case FileChoice::Kind::save:
            if (open.name.empty()) {
                open.problem = "a name to save it as";
                return;
            }
            finish({open.directory / mcpplibs::os::fs::from_utf8(open.name)});
            return;
        case FileChoice::Kind::open: {
            std::vector<std::filesystem::path> paths;
            for (const std::size_t index : open.selected) paths.push_back(open.entries[index]);
            if (paths.empty()) {
                open.problem = "nothing chosen yet";
                return;
            }
            finish(std::move(paths));
            return;
        }
        }
    }

    void show_picker(const std::shared_ptr<Impl>& self) {
        if (!picker) return;
        const Picker& open = *picker;
        const std::weak_ptr<Impl> alive = self;
        const auto later = [alive](auto step) {
            return [alive, step](const Value&) {
                if (auto impl = alive.lock()) step(*impl, impl);
            };
        };
        std::vector<Element> rows;
        for (std::size_t index = 0; index < open.entries.size(); ++index) {
            const auto& entry = open.entries[index];
            std::error_code error;
            const bool folder = std::filesystem::is_directory(entry, error);
            const std::string name = mcpplibs::os::fs::utf8(entry.filename());
            rows.push_back(row({.id = std::format("entry-{}", index),
                                .padding = Edges(4.0f, 8.0f),
                                .gap = 8,
                                .background = Fill(open.selected.contains(index) ? std::string("accent_subtle") : std::string("surface")),
                                .radius = 6,
                                .label = folder ? std::format("{}/", name) : name,
                                .on = {{"press", later([index](Impl& impl, const std::shared_ptr<Impl>& again) { impl.enter(index, again); })}}},
                               {text({.text = folder ? "▸" : "·", .style = "label", .color = Fill(std::string("text_muted"))}), text({.text = name, .style = "body", .wrap = false, .max_lines = 1})}));
        }
        const std::string_view accept_text = open.choice.kind == FileChoice::Kind::save ? "Save" : open.choice.kind == FileChoice::Kind::folder ? "Choose" : "Open";
        std::vector<Element> panel{
            text({.id = "title", .text = open.choice.title.empty() ? std::string(accept_text) : open.choice.title, .style = "heading"}),
            row({.id = "where", .gap = 8, .align = "center"},
                {button({.id = "up", .text = "Up", .on_activated = [alive] {
                             if (auto impl = alive.lock(); impl && impl->picker) {
                                 impl->picker->directory = impl->picker->directory.parent_path();
                                 impl->read_directory(*impl->picker);
                                 impl->show_picker(impl);
                             }
                         }}),
                 text({.id = "directory", .text = mcpplibs::os::fs::generic_utf8(open.directory), .style = "label", .wrap = false, .max_lines = 1})}),
            scroll({.id = "entries", .height = 240}, {column({.id = "list", .gap = 2}, std::move(rows))}),
        };
        if (open.choice.kind == FileChoice::Kind::save)
            panel.push_back(text_box({.id = "name", .label = "Name", .text = open.name, .on_changed = [alive](std::string typed) {
                                          if (auto impl = alive.lock(); impl && impl->picker) impl->picker->name = std::move(typed);
                                      }}));
        if (!open.problem.empty()) panel.push_back(text({.id = "problem", .text = open.problem, .style = "caption", .color = Fill(std::string("danger"))}));
        panel.push_back(row({.id = "actions", .gap = 8, .justify = "end"},
                            {button({.id = "cancel", .text = "Cancel", .on_activated = [alive] {
                                         if (auto impl = alive.lock()) impl->finish({});
                                     }}),
                             button({.id = "accept", .text = std::string(accept_text), .variant = ButtonVariant::primary, .on_activated = [alive] {
                                         if (auto impl = alive.lock()) {
                                             impl->accept();
                                             impl->show_picker(impl);
                                         }
                                     }})}));
        const Size window = runtime.size();
        runtime.mount(files_slot, column({.width = window.width, .height = window.height, .justify = "center", .align = "center", .background = Fill(std::string("scrim"))},
                                         {column({.id = "panel", .width = std::min(window.width - 32.0f, 520.0f), .padding = 16, .gap = 10,
                                                  .background = Fill(std::string("surface")), .radius = 12, .label = "Choose a file"},
                                                 std::move(panel))}));
    }

    // ── notices ──
    void show_notices(const std::shared_ptr<Impl>& self) {
        if (notices.empty()) {
            runtime.unmount(notices_slot);
            return;
        }
        const std::weak_ptr<Impl> alive = self;
        std::vector<Element> cards;
        for (const Shown& shown : notices) {
            const std::uint64_t id = shown.id;
            const std::string verb = shown.notice.verb;
            std::vector<Element> lines{text({.text = shown.notice.title, .style = "label"})};
            if (!shown.notice.body.empty()) lines.push_back(text({.text = shown.notice.body, .style = "caption", .color = Fill(std::string("text_muted"))}));
            cards.push_back(column({.id = std::format("notice-{}", id),
                                    .width = 300,
                                    .padding = 12,
                                    .gap = 4,
                                    .background = Fill(std::string("surface")),
                                    .border_color = Fill(std::string("border")),
                                    .border_width = 1,
                                    .radius = 10,
                                    .label = shown.notice.title,
                                    .on = {{"press", [alive, id, verb](const Value&) {
                                        if (auto impl = alive.lock()) {
                                            impl->invoke(verb);
                                            impl->dismiss(id, impl);
                                        }
                                    }}}},
                                   std::move(lines)));
        }
        const Size window = runtime.size();
        runtime.mount(notices_slot, column({.width = window.width, .padding = 12, .gap = 8, .align = "end"}, std::move(cards)));
    }

    void dismiss(std::uint64_t id, const std::shared_ptr<Impl>& self) {
        std::erase_if(notices, [id](const Shown& shown) { return shown.id == id; });
        show_notices(self);
    }

    // ── the menu bar ──
    void show_menu_bar(const std::shared_ptr<Impl>& self) {
        if (groups.empty()) {
            runtime.unmount(menu_slot);
            runtime.set_slot_insets("main", {});
            return;
        }
        constexpr float bar_height = 32;
        const std::weak_ptr<Impl> alive = self;
        std::vector<Element> titles;
        for (std::size_t index = 0; index < groups.size(); ++index)
            titles.push_back(button({.id = std::format("group-{}", index), .text = groups[index].label, .variant = ButtonVariant::ghost, .on_activated = [alive, index] {
                                         if (auto impl = alive.lock()) {
                                             impl->open_group = impl->open_group == index ? std::nullopt : std::optional(index);
                                             impl->show_menu_bar(impl);
                                         }
                                     }}));
        std::vector<Element> parts{row({.id = "bar", .height = bar_height, .padding = Edges(0.0f, 8.0f), .gap = 2, .align = "center",
                                        .background = Fill(std::string("bar"))},
                                       std::move(titles))};
        if (open_group && *open_group < groups.size()) {
            std::vector<Element> entries;
            const auto& group = groups[*open_group];
            for (std::size_t index = 0; index < group.entries.size(); ++index) {
                const MenuEntry& entry = group.entries[index];
                const std::string verb = entry.verb;
                entries.push_back(button({.id = std::format("entry-{}", index), .text = entry.checked ? std::format("✓ {}", entry.label) : entry.label,
                                          .variant = ButtonVariant::ghost, .on_activated = [alive, verb] {
                                              if (auto impl = alive.lock()) {
                                                  impl->open_group.reset();
                                                  impl->show_menu_bar(impl);
                                                  impl->invoke(verb);
                                              }
                                          }}));
            }
            parts.push_back(column({.id = "open", .padding = 4, .gap = 2, .align_self = "start", .background = Fill(std::string("surface")),
                                    .border_color = Fill(std::string("border")), .border_width = 1, .radius = 8, .label = group.label},
                                   std::move(entries)));
        }
        runtime.set_slot_insets("main", {bar_height, 0, 0, 0});
        runtime.mount(menu_slot, column({.width = runtime.size().width, .align = "stretch", .background = Fill(Color::transparent())}, std::move(parts)));
    }
};

System::System(Runtime& runtime, SystemOptions options) : impl_(std::make_shared<Impl>(runtime, std::move(options))) {}

System::~System() {
    if (impl_->picker) impl_->finish({});
    impl_->runtime.unmount(files_slot);
    impl_->runtime.unmount(notices_slot);
    if (!impl_->groups.empty()) {
        impl_->runtime.unmount(menu_slot);
        impl_->runtime.set_slot_insets("main", {});
    }
}

void System::choose_files(FileChoice choice, FilesChosen chosen) {
    Impl& d = *impl_;
    if (d.picker) d.finish({});  // one at a time: the one before is cancelled
    if (d.options.files == Surfacing::native)
        if (const auto& native = d.runtime.native_surfaces().choose_files) {
            // The answer may come on another thread: it is posted to the UI thread.
            Runtime* runtime = &d.runtime;
            const auto shared = std::make_shared<FilesChosen>(std::move(chosen));
            if (native(choice, [runtime, shared](std::vector<std::filesystem::path> paths) {
                    runtime->queue().post([shared, paths = std::move(paths)]() mutable { (*shared)(std::move(paths)); });
                }))
                return;
            chosen = std::move(*shared);
        }
    Impl::Picker open;
    open.directory = choice.start.empty() ? std::filesystem::current_path() : choice.start;
    open.name = choice.name;
    open.choice = std::move(choice);
    open.chosen = std::move(chosen);
    d.picker = std::move(open);
    d.read_directory(*d.picker);
    d.show_picker(impl_);
}

void System::notify(Notice notice) {
    Impl& d = *impl_;
    if (d.options.notifications == Surfacing::native)
        if (const auto& native = d.runtime.native_surfaces().notify) {
            // Pressed may be heard on another thread: its verb runs on the UI thread.
            Runtime* runtime = &d.runtime;
            const std::weak_ptr<Impl> alive = impl_;
            if (native(notice, [runtime, alive, verb = notice.verb] {
                    runtime->queue().post([alive, verb] {
                        if (auto impl = alive.lock()) impl->invoke(verb);
                    });
                }))
                return;
        }
    const std::uint64_t id = d.next_notice++;
    d.notices.push_back({id, std::move(notice)});
    d.show_notices(impl_);
    const std::weak_ptr<Impl> alive = impl_;
    (void)d.runtime.after(d.options.notice_shown, [alive, id] {
        if (auto impl = alive.lock()) impl->dismiss(id, impl);
    });
}

bool System::set_tray(std::vector<MenuEntry> entries, std::string tooltip) {
    const auto& native = impl_->runtime.native_surfaces().tray;
    if (!native) return false;
    const std::weak_ptr<Impl> alive = impl_;
    return native(entries, tooltip, [alive](std::string_view verb) {
        if (auto impl = alive.lock()) impl->invoke(verb);
    });
}

void System::set_menu_bar(std::vector<MenuGroup> groups) {
    impl_->groups = std::move(groups);
    impl_->open_group.reset();
    impl_->show_menu_bar(impl_);
}

}  // namespace omui
