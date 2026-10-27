module openmui.slang;

import std;
import mcpplibs.os;
import nlohmann.json;
import openmui.rhi;
import :session;

namespace omui::slang {

namespace {

namespace os = mcpplibs::os;
using Json = nlohmann::json;

// ── running slangc ───────────────────────────────────────────────────────

// slangc runs at the same time, each into files of its own (`file`), so
// what they share is locked.
struct Run {
    const Options&               options;
    std::filesystem::path        slangc;
    std::filesystem::path        source;
    std::filesystem::path        scratch;
    std::set<std::string>        dependencies;  // UTF-8, as the depfiles name them
    int                          files = 0;
    std::mutex                   lock;

    std::filesystem::path file(std::string_view ext) {
        std::scoped_lock g(lock);
        return scratch / std::format("out{}{}", files++, ext);
    }

    // slangc with the common arguments, then `extra`; its depfile is read.
    // `out` empty: what slangc writes goes to its standard output (a run for
    // its reflection only).
    std::expected<void, std::string> slangc_run(std::vector<std::string> extra, const std::filesystem::path& out) {
        if (InProcess* compiler = options.in_process ? InProcess::beside(slangc) : nullptr; compiler && compiler->usable()) {
            auto r = in_process(*compiler, extra, out);
            if (r || !InProcess::environment_failure(r.error())) return r;
            compiler->give_up();  // a piece of Slang did not load in this process: slangc from here on
        }
        os::process::spawn_options o;
        o.program = slangc;
        o.args = {os::fs::utf8(source)};
        for (const auto& i : options.includes) o.args.push_back("-I" + os::fs::utf8(i));
        for (const auto& d : options.defines) o.args.push_back("-D" + d);
        o.args.insert(o.args.end(), extra.begin(), extra.end());
        std::filesystem::path dep = out;
        dep += ".d";
        if (!out.empty()) o.args.insert(o.args.end(), {"-depfile", os::fs::utf8(dep), "-o", os::fs::utf8(out)});
        auto r = os::process::run(std::move(o));
        if (!r) return std::unexpected(std::format("slangc did not run ({}): {}", os::fs::utf8(slangc), r.error().message()));
        if (!r->status.success() || (!out.empty() && !std::filesystem::exists(out))) {
            std::string text = r->err.empty() ? r->out : r->err;
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            return std::unexpected(text.empty() ? std::format("slangc failed ({})", r->status.code) : text);
        }
        if (!out.empty()) read_depfile(dep);
        return {};
    }

    // The same run by Slang's library: what slangc would write (the code to
    // `out`, the reflection where -reflection-json says) written so, the
    // files it read noted as its depfile would name them.
    std::expected<void, std::string> in_process(InProcess& compiler, const std::vector<std::string>& extra, const std::filesystem::path& out) {
        std::vector<std::string> args{os::fs::utf8(source)};
        for (const auto& i : options.includes) args.push_back("-I" + os::fs::utf8(i));
        for (const auto& d : options.defines) args.push_back("-D" + d);
        std::filesystem::path reflection;
        for (std::size_t i = 0; i < extra.size(); ++i) {
            if (extra[i] == "-reflection-json" && i + 1 < extra.size()) {
                reflection = os::fs::from_utf8(extra[++i]);
                continue;
            }
            args.push_back(extra[i]);
        }
        auto r = compiler.run(args, !out.empty(), !reflection.empty());
        if (!r) return std::unexpected(r.error());
        auto write = [](const std::filesystem::path& f, std::span<const std::byte> bytes) -> std::expected<void, std::string> {
            std::ofstream o(f, std::ios::binary);
            o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!o) return std::unexpected(std::format("cannot write {}", os::fs::utf8(f)));
            return {};
        };
        if (!out.empty())
            if (auto w = write(out, r->code); !w) return w;
        if (!reflection.empty())
            if (auto w = write(reflection, std::as_bytes(std::span(r->reflection))); !w) return w;
        std::scoped_lock g(lock);
        for (std::string& d : r->dependencies) dependencies.insert(std::move(d));
        return {};
    }

    void read_depfile(const std::filesystem::path& dep) {
        std::ifstream in(dep, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        auto found = depfile_prerequisites(text);
        std::scoped_lock g(lock);
        for (std::string& d : found) dependencies.insert(std::move(d));
    }
};

std::expected<std::vector<std::byte>, std::string> read_bytes(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", os::fs::utf8(f)));
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), {});
    std::vector<std::byte> out(chars.size());
    std::memcpy(out.data(), chars.data(), chars.size());
    return out;
}

// ── reflection and the binding convention ────────────────────────────────

enum class Kind : std::uint8_t { sampler, storage_texture, storage_buffer, rw_texture, rw_buffer, uniform };

std::string_view what(Kind k) {
    switch (k) {
        case Kind::sampler: return "a sampled texture";
        case Kind::storage_texture: return "a read-only storage texture";
        case Kind::storage_buffer: return "a read-only storage buffer";
        case Kind::rw_texture: return "a read-write storage texture";
        case Kind::rw_buffer: return "a read-write storage buffer";
        case Kind::uniform: return "a constant buffer";
    }
    return "?";
}

struct Binding {
    std::uint32_t space = 0, index = 0;
};

struct Parameter {
    std::string name;
    Kind        kind = Kind::uniform;
    Binding     binding;
};

std::expected<Kind, std::string> kind_of(const std::string& name, const Json& type) {
    const std::string k = type.value("kind", "");
    if (k == "constantBuffer") return Kind::uniform;
    if (k == "samplerState")
        return std::unexpected(std::format("`{}` is a separate sampler: rhi binds a texture with its sampler, so declare a Sampler2D "
                                           "(or Sampler2DArray …) and drop the SamplerState", name));
    if (k == "array") return std::unexpected(std::format("`{}` is an array of resources, which rhi does not bind; declare each one", name));
    if (k == "parameterBlock") return std::unexpected(std::format("`{}` is a ParameterBlock, which rhi does not bind; use a ConstantBuffer", name));
    if (k != "resource") return std::unexpected(std::format("`{}` is a loose global uniform; put uniforms in a ConstantBuffer<T>", name));
    const std::string shape = type.value("baseShape", "");
    const std::string access = type.value("access", "");
    const bool writes = !access.empty() && access != "read";
    if (shape.starts_with("texture")) {
        if (writes) return Kind::rw_texture;
        return type.value("combined", false) ? Kind::sampler : Kind::storage_texture;
    }
    if (shape == "structuredBuffer" || shape == "byteAddressBuffer") return writes ? Kind::rw_buffer : Kind::storage_buffer;
    return std::unexpected(std::format("`{}` is a {} resource, which rhi does not bind", name, shape));
}

Binding binding_of(const Json& p) {
    const Json* b = p.contains("binding") ? &p["binding"] : nullptr;
    if (!b && p.contains("bindings") && p["bindings"].is_array() && !p["bindings"].empty()) b = &p["bindings"][0];
    if (!b) return {};
    return {b->value("space", 0u), b->value("index", 0u)};
}

std::expected<std::map<std::string, Parameter>, std::string> parameters(const Json& reflection) {
    std::map<std::string, Parameter> out;
    for (const Json& p : reflection.value("parameters", Json::array())) {
        const std::string name = p.value("name", "");
        auto k = kind_of(name, p.value("type", Json::object()));
        if (!k) return std::unexpected(k.error());
        out[name] = {name, *k, binding_of(p)};
    }
    return out;
}

// The sets of a stage: read-only resources, read-write resources, uniforms.
struct Sets {
    std::optional<std::uint32_t> read, write;
    std::uint32_t                uniforms = 0;
};
Sets sets_of(rhi::Stage s) {
    switch (s) {
        case rhi::Stage::vertex: return {0, std::nullopt, 1};
        case rhi::Stage::fragment: return {2, std::nullopt, 3};
        case rhi::Stage::compute: return {0, 1, 2};
    }
    return {};
}

std::string annotation(Binding b, Kind k) {
    const char reg = k == Kind::uniform ? 'b' : (k == Kind::rw_texture || k == Kind::rw_buffer) ? 'u' : 't';
    return std::format("[[vk::binding({}, {})]] … register({}{}, space{}){}", b.index, b.space, reg, b.index, b.space,
                       k == Kind::sampler ? std::format(" : register(s{}, space{})", b.index, b.space) : std::string());
}

// The entry's resources in the convention, counted; or what breaks it.
std::expected<rhi::Counts, std::string> check_entry(const std::string& entry, rhi::Stage stage, const std::vector<Parameter>& used,
                                                    const std::filesystem::path& source) {
    const Sets sets = sets_of(stage);
    std::map<Kind, std::vector<const Parameter*>> by;
    for (const Parameter& p : used) by[p.kind].push_back(&p);
    for (auto& [k, list] : by) std::ranges::sort(list, {}, [](const Parameter* p) { return p->binding.index; });
    const std::string where = std::format("{} ({}, the {} entry point `{}`)", os::fs::utf8(source.filename()), name(stage), name(stage), entry);

    // Kinds in the order their indices follow one another within one set.
    auto lay = [&](std::initializer_list<Kind> kinds, std::optional<std::uint32_t> set, std::string_view set_role) -> std::string {
        std::uint32_t next = 0;
        for (Kind k : kinds) {
            for (const Parameter* p : by[k]) {
                if (!set)
                    return std::format("{}: `{}` is {}, which only compute shaders may have", where, p->name, what(k));
                const Binding want{*set, next};
                if (p->binding.space != want.space || p->binding.index != want.index)
                    return std::format("{}: `{}` is {} at set {}, binding {}; {} it should be {} ({})", where, p->name, what(k),
                                       p->binding.space, p->binding.index, set_role.empty() ? "here" : set_role, annotation(want, k),
                                       "rhi's binding convention: each kind right after the kind before it, from 0");
                ++next;
            }
        }
        return {};
    };
    if (auto e = lay({Kind::sampler, Kind::storage_texture, Kind::storage_buffer}, sets.read, "as the stage's read-only resources"); !e.empty())
        return std::unexpected(e);
    if (auto e = lay({Kind::rw_texture, Kind::rw_buffer}, sets.write, "as the stage's read-write resources"); !e.empty())
        return std::unexpected(e);
    if (auto e = lay({Kind::uniform}, sets.uniforms, "as the stage's constant buffers"); !e.empty()) return std::unexpected(e);
    auto n = [&](Kind k) { return static_cast<std::uint32_t>(by[k].size()); };
    return rhi::Counts{n(Kind::sampler), n(Kind::storage_texture), n(Kind::storage_buffer), n(Kind::uniform), n(Kind::rw_texture), n(Kind::rw_buffer)};
}

// The (set, binding) of every resource a SPIR-V module declares: compiled for
// one entry point, the ones it uses (Slang drops what it cannot reach).
// Slang's reflection lists every global for every entry point.
std::set<std::pair<std::uint32_t, std::uint32_t>> bindings_in(std::span<const std::byte> spv) {
    std::vector<std::uint32_t> w(spv.size() / 4);
    std::memcpy(w.data(), spv.data(), w.size() * 4);
    std::set<std::pair<std::uint32_t, std::uint32_t>> out;
    if (w.size() < 5) return out;
    if (w[0] == 0x03022307u)
        for (auto& x : w) x = std::byteswap(x);
    if (w[0] != 0x07230203u) return out;
    constexpr std::uint32_t op_decorate = 71, decoration_binding = 33, decoration_set = 34;
    std::map<std::uint32_t, std::uint32_t> set, binding;
    for (std::size_t i = 5; i < w.size();) {
        const std::uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (len == 0 || i + len > w.size()) break;
        if (op == op_decorate && len >= 4) {
            if (w[i + 2] == decoration_set) set[w[i + 1]] = w[i + 3];
            if (w[i + 2] == decoration_binding) binding[w[i + 1]] = w[i + 3];
        }
        i += len;
    }
    for (const auto& [id, b] : binding) out.insert({set.contains(id) ? set[id] : 0u, b});
    return out;
}

std::optional<rhi::Stage> stage_of(std::string_view s) {
    if (s == "vertex") return rhi::Stage::vertex;
    if (s == "fragment") return rhi::Stage::fragment;
    if (s == "compute") return rhi::Stage::compute;
    return std::nullopt;
}

std::vector<std::string> target_args(rhi::ShaderFormat f) {
    switch (f) {
        case rhi::ShaderFormat::spirv: return {"-target", "spirv", "-profile", "spirv_1_3", "-fvk-use-entrypoint-name"};
        case rhi::ShaderFormat::msl: return {"-target", "metal"};
        case rhi::ShaderFormat::metallib: return {"-target", "metallib"};
        case rhi::ShaderFormat::dxbc: return {"-target", "dxbc", "-profile", "sm_5_1"};
        case rhi::ShaderFormat::dxil: return {"-target", "dxil", "-profile", "sm_6_0"};
    }
    return {};
}

std::filesystem::path scratch_dir() {
    static std::atomic<int> n{0};
    const auto pid = os::process::self();
    return std::filesystem::temp_directory_path() /
           std::format("omui-slang-{}-{}", pid ? pid->id : 0, n.fetch_add(1));
}

}  // namespace

std::expected<std::filesystem::path, std::string> find_slangc() {
    if (auto chosen = os::env::get("OMUI_SLANGC"); chosen && !chosen->empty()) {
        std::filesystem::path p = os::fs::from_utf8(*chosen);
        if (std::filesystem::exists(p)) return p;
        return std::unexpected(std::format("OMUI_SLANGC names {}, which does not exist", *chosen));
    }
    if (!provisioned_slangc.empty()) {
        std::filesystem::path p = os::fs::from_utf8(provisioned_slangc);
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    if (auto p = os::env::find_executable("slangc")) return *p;
    return std::unexpected(std::string("Slang shaders need slangc: xlings install slang, or OMUI_SLANGC=/path/to/slangc"));
}

std::vector<std::string> depfile_prerequisites(std::string_view text) {
    std::vector<std::string> out;
    // The target ends at the first `:` that is not escaped and not a drive
    // letter's (a colon followed by a blank or the end of a line).
    std::size_t i = 0;
    for (; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            continue;
        }
        if (text[i] == ':' && (i + 1 == text.size() || text[i + 1] == ' ' || text[i + 1] == '\t' || text[i + 1] == '\n' || text[i + 1] == '\r')) break;
    }
    if (i >= text.size()) return out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) out.push_back(std::exchange(cur, {}));
    };
    for (++i; i < text.size(); ++i) {
        const char c = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (c == '\\' && (next == ' ' || next == '#' || next == ':')) {
            cur += next;
            ++i;
        } else if (c == '\\' && (next == '\n' || next == '\r')) {
            flush();
            i += next == '\r' && i + 2 < text.size() && text[i + 2] == '\n' ? 2 : 1;  // the line goes on
        } else if (c == '$' && next == '$') {
            cur += '$';
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush();
            // A second rule (`a: b` lines after the first) ends the list.
            if ((c == '\n' || c == '\r') && next != '\0' && next != ' ' && next != '\t' && next != '\n' && next != '\r') break;
        } else {
            cur += c;
        }
    }
    flush();
    return out;
}

std::expected<Program, std::string> compile(const std::filesystem::path& source, const Options& options) {
    if (options.formats.empty()) return std::unexpected(std::string("no shader format asked for"));
    std::filesystem::path slangc = options.slangc;
    if (slangc.empty()) {
        auto found = find_slangc();
        if (!found) return std::unexpected(found.error());
        slangc = *found;
    }
    const bool own_scratch = options.scratch.empty();
    Run run{options, slangc, source, own_scratch ? scratch_dir() : options.scratch, {}, 0, {}};
    std::error_code ec;
    std::filesystem::create_directories(run.scratch, ec);
    struct Cleanup {
        bool                  own;
        std::filesystem::path dir;
        ~Cleanup() {
            std::error_code e;
            if (own) std::filesystem::remove_all(dir, e);
        }
    } cleanup{own_scratch, run.scratch};

    const std::string file_name = os::fs::utf8(source.filename());
    auto read_json = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        return Json::parse(in, nullptr, false);
    };

    // The entry points: the caller's, or every one in the file, found with
    // the module's resources by a SPIR-V compile (its set/binding numbers
    // are the convention's).
    struct Wanted {
        std::string name;
        rhi::Stage  stage;
        Json        reflection;  // the entry's, when its own run reports it
    };
    std::vector<Wanted> wanted;
    Json module_reflection;
    if (options.entries.empty()) {
        const auto reflection_file = run.file(".json");
        std::vector<std::string> args = target_args(rhi::ShaderFormat::spirv);
        args.insert(args.end(), {"-reflection-json", os::fs::utf8(reflection_file)});
        if (auto r = run.slangc_run(std::move(args), run.file(".spv")); !r) return std::unexpected(r.error());
        module_reflection = read_json(reflection_file);
        if (module_reflection.is_discarded()) return std::unexpected(std::string("slangc wrote reflection that is not JSON"));
        for (const Json& e : module_reflection.value("entryPoints", Json::array())) {
            const std::string ename = e.value("name", "");
            const auto stage = stage_of(e.value("stage", ""));
            if (!stage)
                return std::unexpected(std::format("{}: the entry point `{}` is a {} shader; rhi runs vertex, fragment and compute shaders",
                                                   file_name, ename, e.value("stage", "?")));
            wanted.push_back({ename, *stage, e});
        }
    } else {
        for (const EntryName& e : options.entries) wanted.push_back({e.name, e.stage, {}});
    }
    for (const Wanted& w : wanted)
        if (w.name == "main")
            return std::unexpected(std::format("{}: an entry point named `main` (Metal reserves it); name it for what it does", file_name));

    // Every remaining run at once: each entry's SPIR-V (with its reflection,
    // when discovery did not run), each entry's other formats, and HLSL's
    // registers (what DXBC, DXIL and MSL bind by), which must say what the
    // Vulkan bindings say.
    const bool registers = options.formats.has(rhi::ShaderFormat::dxbc) || options.formats.has(rhi::ShaderFormat::dxil) ||
                           options.formats.has(rhi::ShaderFormat::msl) || options.formats.has(rhi::ShaderFormat::metallib);
    struct Job {
        std::size_t           entry;  // npos: HLSL's registers
        rhi::ShaderFormat     format;
        std::filesystem::path out, reflection;
        std::future<std::expected<void, std::string>> done;
    };
    constexpr std::size_t registers_job = std::numeric_limits<std::size_t>::max();
    std::vector<Job> jobs;
    auto start = [&](std::size_t entry, rhi::ShaderFormat format, std::vector<std::string> args, std::filesystem::path out,
                     std::filesystem::path reflection) {
        if (!reflection.empty()) args.insert(args.end(), {"-reflection-json", os::fs::utf8(reflection)});
        auto done = std::async(std::launch::async, [&run, args = std::move(args), out]() mutable { return run.slangc_run(std::move(args), out); });
        jobs.push_back({entry, format, std::move(out), std::move(reflection), std::move(done)});
    };
    for (std::size_t i = 0; i < wanted.size(); ++i)
        for (std::size_t f = 0; f < rhi::shader_format_count; ++f) {
            const auto format = static_cast<rhi::ShaderFormat>(f);
            // SPIR-V always: what an entry point uses is read from it.
            if (format != rhi::ShaderFormat::spirv && !options.formats.has(format)) continue;
            std::vector<std::string> args{"-entry", wanted[i].name, "-stage", std::string(name(wanted[i].stage))};
            const auto target = target_args(format);
            args.insert(args.end(), target.begin(), target.end());
            const bool reflect = format == rhi::ShaderFormat::spirv && options.entries.size() > 0;
            start(i, format, std::move(args),
                  run.file(format == rhi::ShaderFormat::spirv ? ".spv" : format == rhi::ShaderFormat::msl ? ".metal" : ".bin"),
                  reflect ? run.file(".json") : std::filesystem::path{});
        }
    if (registers) start(registers_job, rhi::ShaderFormat::spirv, {"-target", "hlsl"}, {}, run.file(".json"));
    std::string failure;
    for (Job& j : jobs)
        if (auto r = j.done.get(); !r && failure.empty())
            failure = j.entry == registers_job ? r.error()
                                               : std::format("{} ({} for {}): {}", file_name, wanted[j.entry].name, name(j.format), r.error());
    if (!failure.empty()) return std::unexpected(failure);

    // The module's resources: from discovery, or from the first entry's run
    // (Slang reports every global of the module for each entry).
    if (module_reflection.is_null()) {
        for (const Job& j : jobs)
            if (j.entry == 0 && !j.reflection.empty()) module_reflection = read_json(j.reflection);
        if (module_reflection.is_discarded() || module_reflection.is_null()) return std::unexpected(std::string("slangc wrote reflection that is not JSON"));
        for (Wanted& w : wanted)
            for (const Job& j : jobs)
                if (j.entry != registers_job && &wanted[j.entry] == &w && !j.reflection.empty()) {
                    const Json r = read_json(j.reflection);
                    for (const Json& e : r.value("entryPoints", Json::array()))
                        if (e.value("name", "") == w.name) w.reflection = e;
                }
    }
    auto params = parameters(module_reflection);
    if (!params) return std::unexpected(std::format("{}: {}", file_name, params.error()));
    if (registers) {
        const Json hlsl = read_json(std::ranges::find(jobs, registers_job, &Job::entry)->reflection);
        for (const Json& p : hlsl.value("parameters", Json::array())) {
            const std::string pname = p.value("name", "");
            auto it = params->find(pname);
            if (it == params->end()) continue;
            std::vector<Json> bindings;
            if (p.contains("binding")) bindings.push_back(p["binding"]);
            for (const Json& b : p.value("bindings", Json::array())) bindings.push_back(b);
            for (const Json& b : bindings) {
                const Binding h{b.value("space", 0u), b.value("index", 0u)};
                if (h.space != it->second.binding.space || h.index != it->second.binding.index) {
                    const std::string k = b.value("kind", "");
                    const char reg = k == "samplerState" ? 's' : k == "unorderedAccess" ? 'u' : k == "constantBuffer" ? 'b' : 't';
                    return std::unexpected(std::format("{}: `{}` is at set {}, binding {} for Vulkan but at register({}{}, space{}) for D3D "
                                                       "and Metal; write both: {}",
                                                       file_name, pname, it->second.binding.space, it->second.binding.index, reg, h.index,
                                                       h.space, annotation(it->second.binding, it->second.kind)));
                }
            }
        }
    }

    Program program;
    program.source = source;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        Entry entry;
        entry.name = wanted[i].name;
        entry.stage = wanted[i].stage;
        std::optional<std::vector<std::byte>> spirv;
        for (const Job& j : jobs)
            if (j.entry == i && j.format == rhi::ShaderFormat::spirv) {
                auto bytes = read_bytes(j.out);
                if (!bytes) return std::unexpected(bytes.error());
                spirv = std::move(*bytes);
            }
        // What the entry point uses, from its own SPIR-V.
        const auto reached = bindings_in(*spirv);
        std::vector<Parameter> used;
        for (const auto& [pname, p] : *params)
            if (reached.contains({p.binding.space, p.binding.index})) used.push_back(p);
        auto counts = check_entry(entry.name, entry.stage, used, source);
        if (!counts) return std::unexpected(counts.error());
        entry.counts = *counts;
        if (entry.stage == rhi::Stage::compute) {
            const Json size = wanted[i].reflection.value("threadGroupSize", Json::array());
            for (std::size_t k = 0; k < 3 && k < size.size(); ++k) entry.threads[k] = size[k].get<std::uint32_t>();
        }
        for (std::size_t f = 0; f < rhi::shader_format_count; ++f) {
            const auto format = static_cast<rhi::ShaderFormat>(f);
            if (!options.formats.has(format)) continue;
            if (format == rhi::ShaderFormat::spirv) {
                entry.code.push_back({format, *spirv});
                continue;
            }
            const auto job = std::ranges::find_if(jobs, [&](const Job& j) { return j.entry == i && j.format == format; });
            auto bytes = read_bytes(job->out);
            if (!bytes) return std::unexpected(bytes.error());
            if (format == rhi::ShaderFormat::msl) {
                // Metal compiles source with fast math unless the source says
                // otherwise (SDL_GPU passes no compile options): IEEE results
                // and no contraction, as SPIR-V and the CPU reference have.
                // Compilers before MSL 3.2 ignore the pragmas.
                constexpr std::string_view precise = "#pragma METAL fp math_mode(safe)\n#pragma METAL fp contract(off)\n";
                const auto head = std::as_bytes(std::span(precise));
                bytes->insert(bytes->begin(), head.begin(), head.end());
            }
            entry.code.push_back({format, std::move(*bytes)});
        }
        program.entries.push_back(std::move(entry));
    }
    if (program.entries.empty())
        return std::unexpected(std::format("{}: no entry point (mark one with [shader(\"vertex\")], [shader(\"fragment\")] or [shader(\"compute\")])",
                                           os::fs::utf8(source.filename())));
    for (const std::string& d : run.dependencies) program.dependencies.push_back(os::fs::from_utf8(d));
    return program;
}

rhi::ShaderDesc desc(const Entry& e, std::vector<rhi::ShaderCode>& codes, std::string_view label) {
    codes.clear();
    for (const Entry::Code& c : e.code) codes.push_back({c.format, c.bytes});
    rhi::ShaderDesc d;
    d.stage = e.stage;
    d.entry = e.name;
    d.code = codes;
    d.samplers = e.counts.samplers;
    d.storage_textures = e.counts.storage_textures;
    d.storage_buffers = e.counts.storage_buffers;
    d.uniform_buffers = e.counts.uniform_buffers;
    d.rw_storage_textures = e.counts.rw_storage_textures;
    d.rw_storage_buffers = e.counts.rw_storage_buffers;
    d.threads = e.threads;
    d.label = label;
    return d;
}

rhi::ShaderFormats formats_for(std::string_view target_os, std::string_view host_os) noexcept {
    if (target_os == "macos" || target_os == "ios" || target_os == "tvos" || target_os == "visionos")
        return rhi::formats({rhi::ShaderFormat::msl});
    rhi::ShaderFormats f = rhi::formats({rhi::ShaderFormat::spirv});
    if (target_os == "windows" && host_os == "windows") f.add(rhi::ShaderFormat::dxbc);
    return f;
}

}  // namespace omui::slang
