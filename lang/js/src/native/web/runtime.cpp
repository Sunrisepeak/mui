// JavaScript on the Web: not yet (D-84). QuickJS-ng's module wrap
// (mcpplibs.mwrap.quickjs_ng) takes the struct representation of JSValue
// that 64-bit targets have; wasm32 NaN-boxes it. Until the wrap supports it
// — or the page's own JavaScript engine runs the scripts — a Script answers
// every run with OMUI_ERROR_EXTENSION_NOT_PRESENT, and the program goes on.
module openmui.lang.js;

import std;
import openmui.abi;
import openmui.lang;

namespace omui::lang::js {

struct Script::Impl {
    std::function<void(std::string_view)> errors = [](std::string_view m) { std::println(std::cerr, "openmui js: {}", m); };
};

namespace {

std::future<Result<void>> not_here(Script::Impl& s) {
    const std::string message = "JavaScript is not on the Web yet (QuickJS-ng's wrap supports 64-bit targets)";
    s.errors(message);
    std::promise<Result<void>> done;
    done.set_value(std::unexpected(Error{abi::OMUI_ERROR_EXTENSION_NOT_PRESENT, message}));
    return done.get_future();
}

}  // namespace

Script::Script(Session&) : impl_(std::make_unique<Impl>()) {}
Script::~Script() = default;
void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }
std::future<Result<void>> Script::run(std::string, std::string) { return not_here(*impl_); }
std::future<Result<void>> Script::reload(std::string, std::string) { return not_here(*impl_); }

}  // namespace omui::lang::js
