// Python as a language of the player: one script, one PocketPy VM on a thread
// of its own.
module openmui.lang.python.embedded;

import std;
import openmui.abi;
import openmui.lang;
import openmui.player;

namespace omui::lang::python::embedded {

namespace {

class PythonEngine final : public player::Engine {
public:
    explicit PythonEngine(abi::OmuiRuntime rt) : host_(rt), session_(host_), script_(session_) {}

    std::future<player::Status> run(std::string code, std::string name) override { return status(script_.run(std::move(code), std::move(name))); }
    std::future<player::Status> reload(std::string code, std::string name) override {
        return status(script_.reload(std::move(code), std::move(name)));
    }
    [[nodiscard]] bool idle() const override { return session_.idle(); }  // async natives in flight count

private:
    // The run's result, as the player states it: settled on the script thread
    // right after the run (the queue keeps their order).
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

}  // namespace

player::Language language() {
    return {"py", {".py"}, [](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<PythonEngine>(rt); }, {}};
}

}  // namespace omui::lang::python::embedded
