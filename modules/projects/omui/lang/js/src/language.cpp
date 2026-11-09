// JavaScript and TypeScript as languages of the player: one script, one
// QuickJS context on a thread of its own.
module omui.lang.js;

import std;
import omui.abi;
import omui.lang;
import omui.lang.ts;
import omui.player;

namespace omui::lang::js {

namespace {

class JsEngine final : public player::Engine {
public:
    explicit JsEngine(abi::OmuiRuntime rt) : host_(rt), session_(host_), script_(session_) {}

    std::future<player::Status> run(std::string code, std::string name) override { return status(script_.run(std::move(code), name)); }
    std::future<player::Status> reload(std::string code, std::string name) override { return status(script_.reload(std::move(code), name)); }
    void module_changed(const std::filesystem::path& file, std::optional<std::string> text) override { script_.module_changed(file, std::move(text)); }
    [[nodiscard]] std::vector<std::filesystem::path> modules() const override { return script_.modules(); }
    player::Status reload_now(std::string code, std::string name) override {
        Result<void> done = script_.reload_now(std::move(code), std::move(name));
        return done ? player::Status{} : player::Status(std::unexpected(done.error().text()));
    }
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

player::Language javascript() { return {"js", {".js", ".mjs"}, make, {}, {}}; }

player::Language typescript() {
    return {"ts", {".ts", ".mts"}, make, [](const std::filesystem::path& file) { return ts::compile(file); },
            [](std::string_view text, const std::filesystem::path& file) { return ts::compile_source(text, std::string(reinterpret_cast<const char*>(file.generic_u8string().c_str()))); }};
}

}  // namespace omui::lang::js
