// openmui.shader:session — Slang's compiler in this process: the library
// beside the slangc mcpp provisioned (libslang-compiler), loaded once, its
// sessions kept, so a compile no longer starts a program and loads Slang's
// core module (most of a slangc run: 130 of 160 ms here, three times that on
// a CI Mac) — a shader edited while the program runs is back in tens of
// milliseconds. A compile takes slangc's own arguments and gives what slangc
// would write: the code, the reflection JSON, the files it read, its
// diagnostics in slangc's words. Where the library is not there (or
// OMUI_SLANGC_PROCESS is set) slangc runs, as before.
module openmui.shader:session;

import std;

namespace omui::shader {

struct InProcessOutput {
    std::vector<std::byte>   code;          // what -o would hold
    std::string              reflection;    // -reflection-json's text, when asked for
    std::vector<std::string> dependencies;  // the files the compile read, UTF-8
};

class InProcess {
public:
    // The compiler beside `slangc` (its ../lib, or its own directory on
    // Windows), loaded on first use; nullptr when there is none.
    [[nodiscard]] static InProcess* beside(const std::filesystem::path& slangc);

    // slangc's arguments (the source first; no -o, -depfile or
    // -reflection-json: what they would write is returned). The error is
    // slangc's diagnostics.
    [[nodiscard]] std::expected<InProcessOutput, std::string> run(std::span<const std::string> args, bool code, bool reflection);

    // Whether a failure is the environment's — a piece of Slang that did not
    // load here (a downstream compiler) — not the shader's: then slangc,
    // which finds its pieces beside it, runs instead from here on.
    [[nodiscard]] static bool environment_failure(std::string_view diagnostics) noexcept;
    void give_up() noexcept;
    [[nodiscard]] bool usable() const noexcept;

    InProcess(const InProcess&) = delete;
    InProcess& operator=(const InProcess&) = delete;
    ~InProcess();

    struct Impl;
    explicit InProcess(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::shader
