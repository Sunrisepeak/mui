module openmui.slang;

import std;
import mcpplibs.os;
import :session;

namespace omui::slang {

namespace {

namespace os = mcpplibs::os;

// Slang's C interface (slang-deprecated.h), as the library exports it: the
// handful a compile from slangc's arguments needs. Opaque handles; no header
// (the library is found at run time, and may not be there).
struct SlangSession;
struct SlangCompileRequest;
struct SlangReflection;
using SlangResult = std::int32_t;

// ISlangBlob, as COM lays it out: queryInterface, addRef, release, then
// getBufferPointer and getBufferSize (on x64 and arm64 one calling
// convention serves C and COM alike).
struct Blob {
    struct Table {
        void* query_interface;
        void* add_ref;
        std::uint32_t (*release)(Blob*);
        const void* (*buffer)(Blob*);
        std::size_t (*size)(Blob*);
    };
    const Table* table;
};

struct Api {
    SlangSession* (*create_session)(const char*);
    void (*destroy_session)(SlangSession*);
    SlangCompileRequest* (*create_request)(SlangSession*);
    void (*destroy_request)(SlangCompileRequest*);
    SlangResult (*process_arguments)(SlangCompileRequest*, const char* const*, int);
    SlangResult (*compile)(SlangCompileRequest*);
    const char* (*diagnostics)(SlangCompileRequest*);
    const void* (*entry_code)(SlangCompileRequest*, int, std::size_t*);
    const void* (*request_code)(SlangCompileRequest*, std::size_t*);
    SlangReflection* (*reflection)(SlangCompileRequest*);
    SlangResult (*reflection_json)(SlangReflection*, SlangCompileRequest*, Blob**);
    int (*dependency_count)(SlangCompileRequest*);
    const char* (*dependency)(SlangCompileRequest*, int);
};

// The library's file beside slangc: lib/ under its root, or (Windows) bin/
// with it; the current name, then the one before 2025.
std::vector<std::filesystem::path> candidates(const std::filesystem::path& slangc) {
    const std::filesystem::path bin = slangc.parent_path(), root = bin.parent_path();
    const std::string_view ext = os::library::suffix();
    if constexpr (os::is_windows)
        return {bin / std::format("slang-compiler{}", ext), bin / std::format("slang{}", ext)};
    else
        return {root / "lib" / std::format("libslang-compiler{}", ext), root / "lib" / std::format("libslang{}", ext)};
}

}  // namespace

struct InProcess::Impl {
    os::library::handle              library;
    std::vector<os::library::handle> companions;  // Windows: Slang's own DLLs, loaded from its directory
    std::atomic<bool>                broken{false};  // its pieces did not load here: slangc runs instead
    Api                        api{};
    std::mutex                 lock;
    std::vector<SlangSession*> idle;    // sessions not compiling now

    ~Impl() {
        for (SlangSession* s : idle) api.destroy_session(s);
    }

    // A session of its own for each compile at the same time (a session is
    // not shared across threads); kept for the next.
    SlangSession* take() {
        {
            std::scoped_lock g(lock);
            if (!idle.empty()) {
                SlangSession* s = idle.back();
                idle.pop_back();
                return s;
            }
        }
        return api.create_session(nullptr);  // the core module loaded: outside the lock, so the first compiles load theirs at once
    }
    void give(SlangSession* s) {
        std::scoped_lock g(lock);
        idle.push_back(s);
    }
};

InProcess::InProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
InProcess::~InProcess() = default;

bool InProcess::usable() const noexcept { return !impl_->broken.load(std::memory_order_relaxed); }

bool InProcess::environment_failure(std::string_view diagnostics) noexcept {
    return diagnostics.contains("failed to load downstream compiler") || diagnostics.contains("failed to load dynamic library");
}

void InProcess::give_up() noexcept { impl_->broken.store(true, std::memory_order_relaxed); }

InProcess* InProcess::beside(const std::filesystem::path& slangc) {
    static std::mutex lock;
    static std::map<std::filesystem::path, std::unique_ptr<InProcess>> loaded;  // nullptr: none there
    if (auto off = os::env::get("OMUI_SLANGC_PROCESS"); off && !off->empty() && *off != "0") return nullptr;
    if (!os::library::supported()) return nullptr;
    std::scoped_lock g(lock);
    if (auto it = loaded.find(slangc); it != loaded.end()) return it->second.get();
    std::unique_ptr<InProcess>& slot = loaded[slangc];
    for (const auto& file : candidates(slangc)) {
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) continue;
        auto lib = os::library::open(file);
        if (!lib) continue;
        auto impl = std::make_unique<Impl>();
        Api& a = impl->api;
        a.create_session = lib->function<SlangSession*(const char*)>("spCreateSession");
        a.destroy_session = lib->function<void(SlangSession*)>("spDestroySession");
        a.create_request = lib->function<SlangCompileRequest*(SlangSession*)>("spCreateCompileRequest");
        a.destroy_request = lib->function<void(SlangCompileRequest*)>("spDestroyCompileRequest");
        a.process_arguments = lib->function<SlangResult(SlangCompileRequest*, const char* const*, int)>("spProcessCommandLineArguments");
        a.compile = lib->function<SlangResult(SlangCompileRequest*)>("spCompile");
        a.diagnostics = lib->function<const char*(SlangCompileRequest*)>("spGetDiagnosticOutput");
        a.entry_code = lib->function<const void*(SlangCompileRequest*, int, std::size_t*)>("spGetEntryPointCode");
        a.request_code = lib->function<const void*(SlangCompileRequest*, std::size_t*)>("spGetCompileRequestCode");
        a.reflection = lib->function<SlangReflection*(SlangCompileRequest*)>("spGetReflection");
        a.reflection_json = lib->function<SlangResult(SlangReflection*, SlangCompileRequest*, Blob**)>("spReflection_ToJson");
        a.dependency_count = lib->function<int(SlangCompileRequest*)>("spGetDependencyFileCount");
        a.dependency = lib->function<const char*(SlangCompileRequest*, int)>("spGetDependencyFilePath");
        const bool complete = a.create_session && a.destroy_session && a.create_request && a.destroy_request && a.process_arguments &&
                              a.compile && a.diagnostics && a.entry_code && a.request_code && a.reflection && a.reflection_json &&
                              a.dependency_count && a.dependency;
        if (!complete) continue;
        if constexpr (os::is_windows) {
            // Slang loads its downstream compilers (slang-glslang for SPIR-V,
            // dxcompiler for DXIL) by bare name, which Windows looks for beside
            // the program and on PATH — not beside Slang, as it is for slangc.
            // Loaded here from Slang's directory first, the bare name finds them.
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), ec)) {
                const std::string name = os::fs::utf8(entry.path().filename());
                const bool companion = name.starts_with("slang-glslang") || name.starts_with("dxcompiler") || name.starts_with("dxil");
                if (!companion || entry.path().extension() != ".dll") continue;
                if (auto c = os::library::open(entry.path())) impl->companions.push_back(std::move(*c));
            }
        }
        impl->library = std::move(*lib);
        slot = std::make_unique<InProcess>(std::move(impl));
        break;
    }
    return slot.get();
}

std::expected<InProcessOutput, std::string> InProcess::run(std::span<const std::string> args, bool code, bool reflection) {
    Impl& d = *impl_;
    SlangSession* session = d.take();
    if (!session) return std::unexpected(std::string("Slang's compiler made no session"));
    struct Give {
        Impl&         d;
        SlangSession* s;
        ~Give() { d.give(s); }
    } give{d, session};
    SlangCompileRequest* request = d.api.create_request(session);
    if (!request) return std::unexpected(std::string("Slang's compiler made no compile request"));
    struct Destroy {
        const Api&           api;
        SlangCompileRequest* r;
        ~Destroy() { api.destroy_request(r); }
    } destroy{d.api, request};

    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& a : args) argv.push_back(a.c_str());
    auto said = [&](std::string_view fallback) {
        const char* text = d.api.diagnostics(request);
        std::string out = text ? text : "";
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return out.empty() ? std::string(fallback) : out;
    };
    if (d.api.process_arguments(request, argv.data(), static_cast<int>(argv.size())) < 0) return std::unexpected(said("slangc's arguments were refused"));
    if (d.api.compile(request) < 0) return std::unexpected(said("slangc failed"));

    InProcessOutput out;
    if (code) {
        // What slangc writes with -o: the request's code (a whole program for
        // SPIR-V and Metal); a source target's may be the entry point's only.
        std::size_t size = 0;
        const void* bytes = d.api.request_code(request, &size);
        if (!bytes || size == 0) bytes = d.api.entry_code(request, 0, &size);
        if (!bytes || size == 0) return std::unexpected(said("slangc wrote no code"));
        out.code.resize(size);
        std::memcpy(out.code.data(), bytes, size);
    }
    if (reflection) {
        Blob* blob = nullptr;
        if (d.api.reflection_json(d.api.reflection(request), request, &blob) < 0 || !blob)
            return std::unexpected(said("slangc wrote no reflection"));
        out.reflection.assign(static_cast<const char*>(blob->table->buffer(blob)), blob->table->size(blob));
        blob->table->release(blob);
    }
    const int n = d.api.dependency_count(request);
    for (int i = 0; i < n; ++i)
        if (const char* p = d.api.dependency(request, i)) out.dependencies.emplace_back(p);
    return out;
}

}  // namespace omui::slang
