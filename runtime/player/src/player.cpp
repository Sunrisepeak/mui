module openmui.player;

import std;
import openmui.core;
import openmui.view;
import openmui.abi;
import openmui.theme;
import openmui.trace;

namespace omui::player {

namespace {

constexpr Log log{"player"};

abi::OmuiString str(std::string_view s) { return {s.data(), s.size()}; }

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

std::optional<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::filesystem::file_time_type stamp_of(const std::filesystem::path& file) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

// What a check of a document said, as lines of text.
std::string diagnostics_text(std::string_view ndjson, std::string_view file) {
    std::string out;
    for (auto line : ndjson | std::views::split('\n')) {
        auto v = parse_json(std::string_view(line.begin(), line.end()));
        if (!v) continue;
        const Value* l = v->get("line");
        const Value* c = v->get("column");
        out += std::format("{}{}: {}: {}{}\n", file, l && c ? std::format(":{}:{}", l->as_int(), c->as_int()) : std::string(),
                           v->get("level") ? v->get("level")->as_string() : "error", v->get("message") ? v->get("message")->as_string() : "",
                           v->get("hint") ? std::format(" ({})", v->get("hint")->as_string()) : std::string());
    }
    return out;
}

}  // namespace

struct Player::Impl {
    struct File {
        enum class Kind : std::uint8_t { component, view, script, theme };
        Kind                              kind = Kind::view;
        std::filesystem::path             path;
        std::filesystem::file_time_type   stamp{};
        std::string                       slot;    // view
        const Language*                   lang = nullptr;  // script
        std::unique_ptr<Engine>           engine;  // script
    };

    Runtime&                          rt;
    Manifest                          manifest;
    std::vector<Language>             languages;
    abi::OmuiRuntime                  handle = nullptr;
    std::vector<File>                 files;
    std::vector<std::pair<std::string, std::future<Status>>> pending;  // scripts running, by path
    std::vector<std::string>          problems;
    std::uint64_t                     timer = 0;
    seconds                           period{0.1};

    Impl(Runtime& r, Manifest m, std::vector<Language> l) : rt(r), manifest(std::move(m)), languages(std::move(l)) {
        handle = abi::attach(rt, manifest.id);
    }
    ~Impl() {
        if (timer) rt.cancel(timer);
        files.clear();  // engines first: they unsubscribe through the handle
        pending.clear();
        abi::detach(handle);
    }

    std::filesystem::path full(const std::filesystem::path& p) const { return p.is_absolute() ? p : manifest.base / p; }
    std::string name(const std::filesystem::path& p) const { return generic_utf8(p); }

    void problem(std::string text) {
        while (!text.empty() && text.back() == '\n') text.pop_back();
        log.warn("{}", text);
        problems.push_back(std::move(text));
    }

    const Language* language(const Script& s) const {
        for (const Language& l : languages)
            if (l.name == s.lang) return &l;
        const std::string ext = generic_utf8(s.entry.extension());
        for (const Language& l : languages)
            if (std::ranges::find(l.extensions, ext) != l.extensions.end()) return &l;
        return nullptr;
    }

    bool define(File& f) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        std::string out;
        if (abi::omuiDefineComponent(handle, str(*text), collect, &out) != abi::OMUI_SUCCESS) {
            problem(diagnostics_text(out, name(f.path)));
            return false;
        }
        return true;
    }

    // A theme file into the program's library (again: a saved edit, which
    // replaces what it declared); the runtime's composition is made again.
    bool add_theme(File& f, bool again) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        if (auto r = theme::library().add(*text, name(f.path)); !r) {
            std::string all;
            for (const auto& x : r.error()) all += x.text() + "\n";
            problem(all);
            return false;
        }
        if (!again) return true;
        std::vector<std::string> layers;
        for (auto part : rt.theme().name | std::views::split('+')) layers.emplace_back(part.begin(), part.end());
        if (auto r = theme::apply(rt, layers); !r) {
            problem(std::format("{}: {}", name(f.path), r.error()));
            return false;
        }
        return true;
    }

    bool mount(File& f) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        if (abi::omuiMount(handle, str(f.slot), str(*text)) != abi::OMUI_SUCCESS) {
            std::string out;
            (void)abi::omuiCheck(handle, str(*text), collect, &out);
            problem(diagnostics_text(out.empty() ? std::string("{\"message\": \"refused\"}") : out, name(f.path)));
            return false;
        }
        return true;
    }

    bool run(File& f, bool again) {
        auto code = f.lang->load ? f.lang->load(full(f.path)) : [&]() -> std::expected<std::string, std::string> {
            auto t = read_file(full(f.path));
            if (!t) return std::unexpected(std::string("cannot be read"));
            return *t;
        }();
        if (!code) {
            problem(std::format("{}: {}", name(f.path), code.error()));
            return false;
        }
        if (!f.engine) f.engine = f.lang->make(handle);
        if (!f.engine) {
            problem(std::format("{}: the {} language could not start", name(f.path), f.lang->name));
            return false;
        }
        const std::string script = name(f.path);
        pending.emplace_back(script, again ? f.engine->reload(std::move(*code), script) : f.engine->run(std::move(*code), script));
        return true;
    }

    // Scripts that finished: their errors are problems.
    void collect_runs() {
        std::erase_if(pending, [&](auto& p) {
            if (p.second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
            if (Status s = p.second.get(); !s) problem(std::format("{}: {}", p.first, s.error()));
            return true;
        });
    }
};

Player::Player(Runtime& runtime, Manifest manifest, std::vector<Language> languages)
    : impl_(std::make_unique<Impl>(runtime, std::move(manifest), std::move(languages))) {}

Player::~Player() = default;

Status Player::start() {
    trace::Span span("player.start", "player");
    Impl& d = *impl_;
    d.problems.clear();
    // Themes first: what the views and scripts show is themed from the start.
    for (const auto& t : d.manifest.themes) {
        d.files.push_back({Impl::File::Kind::theme, t, stamp_of(d.full(t)), {}, nullptr, nullptr});
        (void)d.add_theme(d.files.back(), false);
    }
    if (!d.manifest.theme.empty())
        if (auto r = theme::apply(d.rt, d.manifest.theme); !r) d.problem(std::format("theme: {}", r.error()));
    for (const auto& c : d.manifest.components) d.files.push_back({Impl::File::Kind::component, c, stamp_of(d.full(c)), {}, nullptr, nullptr});
    for (const auto& v : d.manifest.views) {
        // ui/main.view.omui.json mounts at "main".
        std::string slot = generic_utf8(v.filename());
        slot = slot.substr(0, slot.find('.'));
        d.files.push_back({Impl::File::Kind::view, v, stamp_of(d.full(v)), slot, nullptr, nullptr});
    }
    for (const Script& s : d.manifest.scripts) {
        const Language* l = d.language(s);
        if (!l) {
            d.problem(std::format("{}: no language `{}` is registered with the player", d.name(s.entry), s.lang));
            continue;
        }
        d.files.push_back({Impl::File::Kind::script, s.entry, stamp_of(d.full(s.entry)), {}, l, nullptr});
    }
    // Components may need each other: define them until no more can be.
    std::vector<Impl::File*> undefined;
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::component) undefined.push_back(&f);
    for (bool progress = true; progress && !undefined.empty();) {
        progress = false;
        const std::size_t before = d.problems.size();
        std::erase_if(undefined, [&](Impl::File* f) {
            if (!d.define(*f)) return false;
            progress = true;
            return true;
        });
        if (progress) d.problems.resize(before);  // what failed may succeed on the next round
    }
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::view) (void)d.mount(f);
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::script) (void)d.run(f, false);
    if (d.problems.empty()) return {};
    std::string all;
    for (const auto& p : d.problems) all += p + "\n";
    return std::unexpected(std::move(all));
}

std::vector<std::string> Player::poll() {
    Impl& d = *impl_;
    d.collect_runs();
    std::vector<std::string> reloaded;
    for (auto& f : d.files) {
        const auto now = stamp_of(d.full(f.path));
        if (now == f.stamp) continue;
        f.stamp = now;
        trace::Span span("player.reload", "player");
        bool ok = false;
        switch (f.kind) {
            case Impl::File::Kind::component: ok = d.define(f); break;
            case Impl::File::Kind::view: ok = d.mount(f); break;
            case Impl::File::Kind::script: ok = d.run(f, true); break;
            case Impl::File::Kind::theme: ok = d.add_theme(f, true); break;
        }
        if (ok) {
            log.info("{} reloaded", d.name(f.path));
            reloaded.push_back(d.name(f.path));
        }
    }
    return reloaded;
}

void Player::watch(seconds period) {
    Impl& d = *impl_;
    d.period = period;
    d.timer = d.rt.after(period, [this] {
        impl_->timer = 0;
        (void)poll();
        watch(impl_->period);
    });
}

bool Player::idle() const {
    Impl& d = *impl_;
    d.collect_runs();
    if (!d.pending.empty()) return false;
    for (const auto& f : d.files)
        if (f.engine && !f.engine->idle()) return false;
    return true;
}

std::span<const std::string> Player::problems() const noexcept { return impl_->problems; }
const Manifest& Player::manifest() const noexcept { return impl_->manifest; }
abi::OmuiRuntime Player::handle() const noexcept { return impl_->handle; }

}  // namespace omui::player
