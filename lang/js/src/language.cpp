// JavaScript and TypeScript as languages of the player: one script, one
// QuickJS context on a thread of its own.
module openmui.lang.js;

import std;
import mcpplibs.os;
import openmui.abi;
import openmui.lang;
import openmui.player;

namespace omui::lang::js {

namespace {

class JsEngine final : public player::Engine {
public:
    explicit JsEngine(abi::OmuiRuntime rt) : host_(rt), session_(host_), script_(session_) {}

    std::future<player::Status> run(std::string code, std::string name) override { return status(script_.run(std::move(code), name)); }
    std::future<player::Status> reload(std::string code, std::string name) override { return status(script_.reload(std::move(code), name)); }
    [[nodiscard]] bool idle() const override { return session_.idle(); }  // async natives in flight count

private:
    std::future<player::Status> status(std::future<Result<void>> inner) {
        auto done = std::make_shared<std::promise<player::Status>>();
        auto out = done->get_future();
        auto shared = std::make_shared<std::future<Result<void>>>(std::move(inner));
        host_.post([done, shared] {
            Result<void> r = shared->get();
            done->set_value(r ? player::Status{} : player::Status(std::unexpected(r.error().text())));
        });
        return out;
    }

    Host    host_;
    Session session_;
    Script  script_;
};

std::unique_ptr<player::Engine> make(abi::OmuiRuntime rt) { return std::make_unique<JsEngine>(rt); }

}  // namespace

std::expected<std::string, std::string> compile_typescript(const std::filesystem::path& file) {
    const auto chosen = mcpplibs::os::env::get("OMUI_ESBUILD");
    const auto esbuild = chosen && !chosen->empty() ? std::optional(mcpplibs::os::fs::from_utf8(*chosen))
                                                    : mcpplibs::os::env::find_executable("esbuild");
    if (!esbuild) return std::unexpected(std::string("TypeScript needs esbuild (xlings install esbuild, or OMUI_ESBUILD=/path/to/esbuild)"));
    mcpplibs::os::process::spawn_options o;
    o.program = *esbuild;
    o.args = {mcpplibs::os::fs::utf8(file), "--bundle", "--format=esm", "--platform=neutral", "--external:openmui", "--external:openmui:native/*", "--log-level=error", "--charset=utf8"};
    auto r = mcpplibs::os::process::run(std::move(o));
    if (!r) return std::unexpected(std::format("esbuild did not run: {}", r.error().message()));
    if (!r->status.success()) return std::unexpected(r->err.empty() ? std::format("esbuild failed ({})", r->status.code) : r->err);
    return std::move(r->out);
}

std::expected<std::string, std::string> compile_typescript_source(std::string_view source, std::string_view name) {
    const auto chosen = mcpplibs::os::env::get("OMUI_ESBUILD");
    const auto esbuild = chosen && !chosen->empty() ? std::optional(mcpplibs::os::fs::from_utf8(*chosen))
                                                    : mcpplibs::os::env::find_executable("esbuild");
    if (!esbuild) return std::unexpected(std::string("TypeScript needs esbuild (xlings install esbuild, or OMUI_ESBUILD=/path/to/esbuild)"));
    mcpplibs::os::process::spawn_options o;
    o.program = *esbuild;
    o.args = {"--loader=ts", "--format=esm", "--platform=neutral", std::format("--sourcefile={}", name), "--log-level=error", "--charset=utf8"};
    o.in = mcpplibs::os::process::stdio::pipe;
    auto r = mcpplibs::os::process::run(std::move(o), source);
    if (!r) return std::unexpected(std::format("esbuild did not run: {}", r.error().message()));
    if (!r->status.success()) return std::unexpected(r->err.empty() ? std::format("esbuild failed ({})", r->status.code) : r->err);
    return std::move(r->out);
}

player::Language javascript() { return {"js", {".js", ".mjs"}, make, {}}; }

player::Language typescript() { return {"ts", {".ts", ".tsx"}, make, compile_typescript}; }

}  // namespace omui::lang::js
