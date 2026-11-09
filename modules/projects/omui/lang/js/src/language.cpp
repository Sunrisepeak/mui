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


// The `openmui` module as a script writes it (D-145: what an agent reads before writing an edit), then a builder
// for every component the application has registered, from its contract.
constexpr std::string_view module_api = R"(import { … } from "openmui"
  signal(initial, key?) → Signal: .get() .set(value) .update(fn); used in props it is live: text: fmt("{} items", count)
  fmt(pattern, …args), select(cond, a, b), call(name, …args); eq ne lt le gt ge, and or not, add sub mul div mod neg — expressions the runtime evaluates (also as methods: count.add(1), done.not())
  when(cond, element or [elements], otherwise?), each(model, (row) => element, where?), slot(name) — regions
  mount(slot, element) — what a slot shows: mount("main", column({ … }, [ … ]))
  verb(name, { effect: "read" | "reversible" | "irreversible", summary }, (args) => result), invoke(name, args)
  model(name) → .push(rows) .splice(at, remove, rows) .rows() .clear(); tr(key, args); theme(…layers); log(…)
Builders: name(props, children?) — props in camelCase; give anything a person or an agent operates an explicit `id`; handlers are on<Event>.)";

std::string describe_typescript(std::string_view catalog_json) {
    std::string out(module_api);
    const Json catalog = Json::parse(catalog_json, nullptr, false);
    if (!catalog.is_object() || !catalog.contains("components")) return out;
    for (auto it = catalog["components"].begin(); it != catalog["components"].end(); ++it) {
        const Json& c = it.value();
        std::string props;
        if (c.contains("props"))
            for (auto p = c["props"].begin(); p != c["props"].end(); ++p) {
                std::string type = p.value().value("type", "any");
                if (type == "bool") type = "boolean";
                else if (type == "i64" || type == "i32" || type == "u32" || type == "f64" || type == "f32") type = "number";
                if (p.value().contains("values")) {
                    type.clear();
                    for (const auto& v : p.value()["values"]) type += (type.empty() ? "" : " | ") + std::format("\"{}\"", v.get<std::string>());
                }
                props += std::format("{}{}?: {}", props.empty() ? "" : "; ", ts::camel_case(p.key()), type);
            }
        if (c.contains("events"))
            for (auto e = c["events"].begin(); e != c["events"].end(); ++e) {
                std::string event = ts::camel_case(e.key());
                if (!event.empty()) event[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(event[0])));
                props += std::format("{}on{}?: (args) => void", props.empty() ? "" : "; ", event);
            }
        std::string actions;
        if (c.contains("actions"))
            for (auto a = c["actions"].begin(); a != c["actions"].end(); ++a) actions += std::format("{}{}", actions.empty() ? "" : ", ", a.key());
        out += std::format("\n  {}({{ id?: string; {} }}) — {}{}", ts::camel_case(it.key()), props, c.value("role", std::string("generic")),
                           actions.empty() ? std::string() : std::format("; actions: {}", actions));
    }
    return out;
}

}  // namespace

player::Language javascript() { return {"js", {".js", ".mjs"}, make, {}, {}, describe_typescript}; }

player::Language typescript() {
    return {"ts", {".ts", ".mts"}, make, [](const std::filesystem::path& file) { return ts::compile(file); },
            [](std::string_view text, const std::filesystem::path& file) { return ts::compile_source(text, std::string(reinterpret_cast<const char*>(file.generic_u8string().c_str()))); },
            describe_typescript, declare_typescript};
}

}  // namespace omui::lang::js
