// The effect library: files read, shaders compiled for the device, effects
// registered with the renderer and loaded again when their files change.
module openmui.render.effect;

import std;
import openmui.rhi;
import openmui.render.vector;
import openmui.shader;
import openmui.trace;

namespace omui::effect {

namespace {

constexpr std::string_view suffix = ".effect.omui.toml";

std::optional<std::string> read_text(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), {}};
}

std::string utf8(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

// The one format an effect is compiled to: the first the device takes.
std::optional<rhi::ShaderFormat> format_for(rhi::ShaderFormats formats) {
    for (rhi::ShaderFormat f : {rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl, rhi::ShaderFormat::dxil, rhi::ShaderFormat::dxbc})
        if (formats.has(f)) return f;
    return std::nullopt;
}

// slangc's messages as diagnostics where they are. Two layouts: the current
//
//     error[E20001]: unexpected token
//      --> scanlines.slang:14:15
//       |
//     14 | float broken( {
//        |               ^ unexpected '{', expected identifier
//
// and the older `scanlines.slang(14): error 20001: unexpected token`. A
// message that names no place is one about the shader.
Diagnostics slangc_diagnostics(std::string_view message, const std::filesystem::path& shader) {
    Diagnostics out;
    std::vector<std::string_view> lines;
    for (std::size_t from = 0; from <= message.size();) {
        std::size_t end = message.find('\n', from);
        if (end == std::string_view::npos) end = message.size();
        std::string_view line = message.substr(from, end - from);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        from = end + 1;
    }
    auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
        return v;
    };
    auto number = [](std::string_view v) {
        int n = 0;
        return std::from_chars(v.data(), v.data() + v.size(), n).ec == std::errc{} ? n : -1;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        // A message may follow a prefix (`file (entry for spirv): error[…]: …`).
        const std::string_view line = lines[i];
        if (const std::size_t at = line.find("error["); at != std::string_view::npos || trim(line).starts_with("error:")) {
            const std::string_view rest = at != std::string_view::npos ? line.substr(at) : trim(line);
            const std::size_t colon = rest.find("]: ") != std::string_view::npos ? rest.find("]: ") + 3 : rest.find(": ") + 2;
            Diagnostic d{utf8(shader), 0, 0, std::string(trim(rest.substr(colon))), ""};
            for (std::size_t j = i + 1; j < lines.size() && j < i + 8; ++j) {
                const std::string_view l = trim(lines[j]);
                if (l.starts_with("error")) break;
                if (l.starts_with("--> ")) {
                    // path:line:column, the path perhaps with a drive's colon.
                    const std::string_view where = l.substr(4);
                    const std::size_t c2 = where.rfind(':'), c1 = c2 == std::string_view::npos ? c2 : where.rfind(':', c2 - 1);
                    if (c1 != std::string_view::npos && number(where.substr(c1 + 1, c2 - c1 - 1)) > 0) {
                        d.file = std::string(where.substr(0, c1));
                        d.line = number(where.substr(c1 + 1, c2 - c1 - 1));
                        d.column = std::max(0, number(where.substr(c2 + 1)));
                    }
                } else if (const std::size_t caret = l.find('^'); caret != std::string_view::npos && l.starts_with("|")) {
                    std::string_view label = l.substr(caret);
                    while (!label.empty() && (label.front() == '^' || label.front() == '-' || label.front() == '~')) label.remove_prefix(1);
                    d.hint = std::string(trim(label));
                }
            }
            out.push_back(std::move(d));
            continue;
        }
        const std::size_t open = line.find('('), close = line.find("): ");
        if (open == std::string_view::npos || close == std::string_view::npos || close < open) continue;
        const std::string_view rest = line.substr(close + 3);
        if (!rest.starts_with("error")) continue;
        const int n = number(line.substr(open + 1, close - open - 1));
        if (n < 0) continue;
        const std::size_t colon = rest.find(':');
        out.push_back({std::string(line.substr(0, open)), n, 0, std::string(trim(rest.substr(colon == std::string_view::npos ? 0 : colon + 1))), ""});
    }
    if (out.empty()) out.push_back({utf8(shader), 0, 0, std::string(message), ""});
    return out;
}

// What an effect owns while it is registered: its declaration, its compiled
// shader and the description the renderer reads (which points into both).
struct Owned {
    std::string                 file;
    Declaration                 declaration;
    shader::Program              program;
    std::vector<rhi::ShaderCode> codes;
    rhi::ShaderDesc             desc;
    std::string                 label;
};

}  // namespace

struct Library::Impl {
    Options               options;
    std::filesystem::path template_dir;  // where openmui_effect.slang is, for slangc's -I

    struct Entry {
        std::filesystem::path file;
        std::string           name;
        render::EffectRef     ref;
        // What the effect was made of, and when each last changed.
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> watched;
    };
    std::vector<Entry> entries;

    // The template's text in a directory of its own (named by its content,
    // so a program built with another template never reads a stale one).
    std::expected<std::filesystem::path, Diagnostics> templates() {
        if (!template_dir.empty()) return template_dir;
        const std::string_view text = render::effect_template;
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec) / std::format("openmui-effect-template-{:016x}", std::hash<std::string_view>{}(text));
        const auto file = dir / "openmui_effect.slang";
        if (read_text(file) != text) {
            std::filesystem::create_directories(dir, ec);
            const auto part = dir / std::format("openmui_effect.slang.{}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
            {
                std::ofstream out(part, std::ios::binary);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!out) return std::unexpected(Diagnostics{{utf8(file), 0, 0, "the effect template could not be written", "a writable temporary directory"}});
            }
            std::filesystem::rename(part, file, ec);  // whole, or not at all: another process may be reading it
            if (ec) return std::unexpected(Diagnostics{{utf8(file), 0, 0, std::format("the effect template could not be placed: {}", ec.message()), ""}});
        }
        template_dir = dir;
        return template_dir;
    }

    static std::filesystem::file_time_type changed(const std::filesystem::path& f) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(f, ec);
        return ec ? std::filesystem::file_time_type::min() : t;
    }

    std::expected<Loaded, Diagnostics> load(const std::filesystem::path& file) {
        trace::Span span("effect.load", "effect");
        const auto from = std::chrono::steady_clock::now();
        const std::string where = utf8(file);
        const auto text = read_text(file);
        if (!text) return std::unexpected(Diagnostics{{where, 0, 0, "the file cannot be read", ""}});
        auto declared = read(*text, where);
        if (!declared) return std::unexpected(std::move(declared.error()));
        auto owned = std::make_shared<Owned>();
        owned->declaration = std::move(*declared);
        const Declaration& d = owned->declaration;
        if (utf8(file.filename()) != d.name + std::string(suffix))
            return std::unexpected(Diagnostics{{where, 1, 1, std::format("the effect `{}` is in {}", d.name, utf8(file.filename())),
                                                std::format("name the file {}{}", d.name, suffix)}});

        // The shader, for the device.
        const auto format = format_for(options.formats);
        if (!format) return std::unexpected(Diagnostics{{where, 0, 0, "the device takes no shader format an effect can be compiled to", "SPIR-V, MSL, DXIL or DXBC"}});
        auto dir = templates();
        if (!dir) return std::unexpected(std::move(dir.error()));
        const auto shader = file.parent_path() / d.shader;
        auto program = shader::compile(shader, {.slangc = options.slangc,
                                               .formats = rhi::formats({*format}),
                                               .includes = {*dir},
                                               .entries = {{d.entry, rhi::Stage::fragment}}});
        if (!program) return std::unexpected(slangc_diagnostics(program.error(), shader));
        owned->program = std::move(*program);
        const shader::Entry* entry = owned->program.entry(d.entry);
        if (!entry) return std::unexpected(Diagnostics{{utf8(shader), 0, 0, std::format("no fragment entry point `{}`", d.entry), "[shader(\"fragment\")]"}});
        // Only what the template offers (architecture §8): the source and aux
        // textures, the uniforms, nothing written.
        const rhi::Counts c = entry->counts;
        if (c.samplers > 2 || c.uniform_buffers > 1 || c.storage_textures || c.storage_buffers || c.rw_storage_textures || c.rw_storage_buffers)
            return std::unexpected(Diagnostics{{utf8(shader), 0, 0, std::format("`{}` binds resources of its own", d.entry),
                                                "an effect reads what the template declares (effect_source, effect_aux, effect) and nothing else"}});
        owned->label = std::format("effect {}", d.name);
        owned->desc = shader::desc(*entry, owned->codes, owned->label);

        // The registry's entry: the names point into what it owns.
        render::EffectInfo info;
        info.name = d.name;
        info.doc = d.doc;
        for (const Param& p : d.params) info.params.push_back({p.name, p.value, p.min, p.max, p.doc});
        if (!d.reach_param.empty()) {
            const auto at = static_cast<std::size_t>(std::ranges::find(d.params, d.reach_param, &Param::name) - d.params.begin());
            info.reach = [at](std::span<const float> params) { return at < params.size() ? std::abs(params[at]) : 0.0f; };
        } else {
            info.reach = [r = d.reach](std::span<const float>) { return r; };
        }
        info.gpu = {render::EffectPass::pixel, &owned->desc, nullptr, nullptr};
        owned->file = where;
        info.source = owned->file;
        info.storage = owned;

        render::EffectRegistry& registry = render::effects();
        auto mine = std::ranges::find(entries, d.name, &Entry::name);
        render::EffectRef ref;
        if (mine != entries.end()) {
            ref = mine->ref;
            registry.replace(ref, std::move(info));
        } else {
            // A name taken by a built-in or by code stays theirs; one an
            // effect file took (this library's before, or another's) is
            // replaced in place, its ref unchanged.
            if (const auto existing = registry.find(d.name)) {
                const render::EffectInfo* other = registry.info(*existing);
                if (!other || other->source.empty())
                    return std::unexpected(Diagnostics{{where, 1, 1, std::format("an effect named `{}` is registered already", d.name),
                                                        "the built-ins are dissolve, wipe, reveal, displace, noise, chromatic, blur, pixelate, particles"}});
                ref = *existing;
                registry.replace(ref, std::move(info));
            } else {
                ref = registry.add(std::move(info));
            }
            entries.push_back({file, d.name, ref, {}});
            mine = entries.end() - 1;
        }
        mine->file = file;
        mine->watched.clear();
        mine->watched.emplace_back(file, changed(file));
        mine->watched.emplace_back(shader, changed(shader));
        for (const auto& dep : owned->program.dependencies)
            if (std::ranges::find(mine->watched, dep, &std::pair<std::filesystem::path, std::filesystem::file_time_type>::first) == mine->watched.end())
                mine->watched.emplace_back(dep, changed(dep));
        const auto took = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - from);
        trace::count("effect.load_ms", static_cast<double>(took.count()) / 1000.0, "effect");
        return Loaded{file, d.name, ref, took};
    }
};

Library::Library(Options options) : impl_(std::make_unique<Impl>()) { impl_->options = std::move(options); }
Library::Library(Library&&) noexcept = default;
Library& Library::operator=(Library&&) noexcept = default;
Library::~Library() = default;

std::expected<Loaded, Diagnostics> Library::load(const std::filesystem::path& file) { return impl_->load(file); }

std::expected<std::vector<Loaded>, Diagnostics> Library::load_directory(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (utf8(e.path().filename()).ends_with(suffix)) files.push_back(e.path());
    if (ec) return std::unexpected(Diagnostics{{utf8(dir), 0, 0, std::format("the directory cannot be read: {}", ec.message()), ""}});
    std::ranges::sort(files);
    std::vector<Loaded> loaded;
    Diagnostics problems;
    for (const auto& f : files) {
        auto r = impl_->load(f);
        if (r) loaded.push_back(std::move(*r));
        else problems.insert(problems.end(), r.error().begin(), r.error().end());
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return loaded;
}

std::vector<Reload> Library::poll() {
    std::vector<Reload> out;
    for (std::size_t i = 0; i < impl_->entries.size(); ++i) {
        Impl::Entry& e = impl_->entries[i];
        const bool stale = std::ranges::any_of(e.watched, [](const auto& w) { return Impl::changed(w.first) != w.second; });
        if (!stale) continue;
        trace::mark("effect.reload", "effect");
        const auto from = std::chrono::steady_clock::now();
        const std::filesystem::path file = e.file;
        const std::string name = e.name;
        const render::EffectRef ref = e.ref;
        auto r = impl_->load(file);
        Reload reload{file, name, ref, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - from), {}};
        if (!r) {
            reload.problems = std::move(r.error());
            // Not again until a file changes again: the effect stays as it was.
            for (auto& w : impl_->entries[i].watched) w.second = Impl::changed(w.first);
        } else {
            reload.took = r->took;
        }
        out.push_back(std::move(reload));
    }
    return out;
}

std::vector<std::string> Library::names() const {
    std::vector<std::string> out;
    for (const auto& e : impl_->entries) out.push_back(e.name);
    return out;
}

}  // namespace omui::effect
