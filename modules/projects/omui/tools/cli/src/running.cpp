// A running application's operations behind the tool kernel (RC3.9): `omui agent <op> <app> …` and `omui page <op> …`
// are the protocol's catalogs (omui.agent: ui.*, and the verbs design.*, generate.*, task.*) through
// mlibs.agent's command line — each operation's parameters its catalog's, in its order, with no table of their own.
// A ui.* operation goes out as the protocol's request ({"op":"snapshot",…}); a verb as `invoke`. The application
// decides what it admits; its records come back as they are.
module omui.cli;

import std;
import mlibs.base64;
import mcpplibs.os;
import omui.core;
import omui.agent;

namespace omui::cli {

namespace {

// Who a command line acts as when it says no one.
constexpr std::string_view default_actor = "agent:omui-cli";

class Running final : public mlibs::agent::Subject {
public:
    Running(std::string_view name, std::span<const mlibs::control::CatalogEntry> operations, mlibs::control::Guide guide, std::string_view listing,
            const Transport& send)
        : name_(name), operations_(operations), guide_(guide), listing_(listing), send_(send) {}
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "openmui.tree@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return operations_; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return guide_; }
    [[nodiscard]] std::string_view listing() const noexcept override { return listing_; }
    void operate(std::string_view op, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    std::string_view                              name_;
    std::span<const mlibs::control::CatalogEntry> operations_;
    mlibs::control::Guide                         guide_;
    std::string_view                              listing_;
    const Transport&                              send_;
};

void Running::operate(std::string_view op, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    auto asked = parse_json(request.document->root().dump());
    if (!asked || !asked->map()) return answer.fail("invalid-argument", "a request is a JSON object");
    std::erase_if(*asked->map(), [](const auto& member) { return member.first == "op" || member.first == "re"; });
    const auto entry = std::ranges::find(operations_, op, [](const auto& e) { return e.operation.name; });
    if (!asked->get("actor") && entry != operations_.end() && entry->operation.effect != openas::Effect::read) asked->set("actor", std::string(default_actor));
    Value wire;
    if (op.starts_with("ui.")) {
        wire.set("op", std::string(op.substr(3)));
        for (auto& [key, value] : *asked->map()) wire.set(key, std::move(value));
    } else {  // a verb: what the request itself carries stays beside it, the rest are its arguments
        wire.set("op", "invoke").set("verb", std::string(op));
        Value args(Value::Map{});
        for (auto& [key, value] : *asked->map()) {
            if (key == "actor" || key == "answer" || key == "dry_run") wire.set(key, std::move(value));
            else args.set(key, std::move(value));
        }
        wire.set("args", std::move(args));
    }
    // Waiting is the application's (ui.wait's timeout); the answer may take that long, and a little more.
    std::chrono::milliseconds limit = std::chrono::seconds(30);
    if (const Value* timeout = wire.get("timeout")) limit = std::chrono::milliseconds(static_cast<std::int64_t>(timeout->as_double() * 1000) + 5000);
    bool ended = false;
    for (const Value& record : send_(wire, limit)) {
        const std::string_view kind = record.get("kind") ? record.get("kind")->as_string() : std::string_view{};
        if (kind == "data") answer.data(record.get("data") ? record.get("data")->json() : std::string("null"));
        else if (kind == "notice") answer.notice(record.get("text") ? record.get("text")->as_string() : std::string_view{});
        else if (kind == "error")
            answer.error(record.get("code") ? record.get("code")->as_string() : "failed", record.get("text") ? record.get("text")->as_string() : std::string_view{},
                         record.get("next") ? record.get("next")->json() : std::string());
        else if (kind == "result") {
            answer.result(static_cast<int>(record.get("exitCode") ? record.get("exitCode")->as_int(1) : 1));
            ended = true;
        }
    }
    if (!ended) answer.fail("unavailable", "the application gave no answer");
}

// `--out FILE`: what an answer carries to keep — a picture (base64), its vectors (svg), a trace (chrome) — written
// there, the record saying where; the command line's own, as a person keeps a file.
std::string kept(std::string_view line, const std::string& file, std::ostream& err, bool& failed) {
    auto record = parse_json(line);
    if (!record || !record->get("kind") || record->get("kind")->as_string() != "data" || !record->get("data") || !record->get("data")->map()) return std::string(line);
    Value data = *record->get("data");
    for (const std::string_view key : {"base64", "svg", "chrome"}) {
        const Value* held = data.get(key);
        if (!held) continue;
        std::string bytes(held->as_string());
        if (key == "base64") {
            auto decoded = mlibs::base64::decode(bytes);
            if (!decoded) {
                err << decoded.error() << '\n';
                failed = true;
                return std::string(line);
            }
            bytes.assign(reinterpret_cast<const char*>(decoded->data()), decoded->size());
        }
        if (auto written = mcpplibs::os::fs::write_atomic(file, bytes); !written) {
            err << written.error().message() << '\n';
            failed = true;
            return std::string(line);
        }
        std::erase_if(*data.map(), [&](const auto& member) { return member.first == key; });
        data.set("file", file);
        std::erase_if(*record->map(), [](const auto& member) { return member.first == "data"; });
        return record->set("data", std::move(data)).json();
    }
    return std::string(line);
}

}  // namespace

int print_records(std::vector<Value> records, std::ostream& out) {
    int code = 1;
    for (Value& record : records) {
        if (record.map()) std::erase_if(*record.map(), [](const auto& member) { return member.first == "re"; });
        if (record.get("kind") && record.get("kind")->as_string() == "result") code = static_cast<int>(record.get("exitCode") ? record.get("exitCode")->as_int(1) : 1);
        out << record.json() << '\n';
    }
    return code;
}

std::expected<Value, std::string> raw_request(std::span<const std::string> words) {
    if (words.empty()) return std::unexpected(R"(call needs a request: '{"op":"snapshot","depth":1}')");
    auto request = parse_json(words.front());
    if (!request) return std::unexpected(std::format("the request: {}", request.error().text()));
    if (!request->map()) return std::unexpected("a request is a JSON object");
    return std::move(*request);
}

int operate_running(std::vector<std::string> words, const Transport& send, std::ostream& out, std::ostream& err) {
    std::optional<std::string> out_file;
    if (const auto at = std::ranges::find(words, std::string("--out")); at != words.end() && at + 1 != words.end()) {
        out_file = *(at + 1);
        words.erase(at, at + 2);
    }
    Running ui("ui", agent::catalog, agent::guide, agent::listing, send);
    Running design("design", agent::design::catalog, agent::design::guide, agent::design::listing, send);
    Running generate("generate", agent::generate::catalog, agent::generate::guide, agent::generate::listing, send);
    Running task("task", agent::tasks::catalog, agent::tasks::guide, agent::tasks::listing, send);
    mlibs::agent::SubjectSession session({&ui, &design, &generate, &task});
    // `describe` and `find` are the protocol's here (ui.describe, ui.find), as every name the ui catalog has.
    if (!words.empty() && std::ranges::any_of(agent::catalog, [&](const auto& e) { return e.operation.name == "ui." + words.front(); })) words.front() = "ui." + words.front();
    const std::vector<std::string_view> viewed(words.begin(), words.end());
    if (!out_file) return mlibs::agent::command_line(session, viewed, out, err, {.product = "ui"});
    std::ostringstream answered;
    const int code = mlibs::agent::command_line(session, viewed, answered, err, {.product = "ui"});
    bool failed = false;
    for (const auto line : std::views::split(answered.str(), '\n'))
        if (!line.empty()) out << kept(std::string_view(line.begin(), line.end()), *out_file, err, failed) << '\n';
    return failed ? 1 : code;
}

}  // namespace omui::cli
