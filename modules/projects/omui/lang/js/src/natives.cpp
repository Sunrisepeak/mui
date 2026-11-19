// `__omui`'s table (natives.cppm): the session's side of every native, whichever engine runs the script.
module omui.lang.js;

import std;
import nlohmann.json;
import omui.abi;
import omui.lang;
import omui.lang.ts;
import :natives;
import :prelude;

namespace omui::lang::js {

namespace {

Json parsed(std::string_view text) { return Json::parse(text, nullptr, false); }

abi::OmuiEffect effect_of(std::string_view effect) {
    if (effect == "read") return abi::OMUI_EFFECT_READ;
    if (effect == "irreversible") return abi::OMUI_EFFECT_IRREVERSIBLE;
    if (effect == "external") return abi::OMUI_EFFECT_EXTERNAL;
    return abi::OMUI_EFFECT_REVERSIBLE;
}

Result<std::string> records(const Result<std::vector<Json>>& answered) {
    if (!answered) return std::unexpected(answered.error());
    return Json(*answered).dump();
}

// Development mode: where each element was built (built_at) moved from where the engine placed its call — the last
// position its arguments recorded, inside them and often in a nested call, which then has the same — to the call as
// written (its callee's first character): outward from that position to the nearest call of its builder, past as many
// as the elements below it with that position and that builder (column > row > column, all ending in one text).
struct Placed {
    std::string                        reported;
    std::map<std::string, std::size_t> chain;  // builders below with the same position, counted
};

Placed place_built_at(const std::function<std::optional<std::string>(std::string_view)>& text_of, Element& element,
                      std::map<std::string, std::optional<ts::CallIndex>>& sources) {
    Placed placed{element.built_at, {}};
    const auto below = [&](std::vector<Element>& list) {
        for (Element& child : list) {
            Placed under = place_built_at(text_of, child, sources);
            if (placed.reported.empty() && element.region) placed.reported = under.reported;  // a region has the position of what is in it
            if (!under.reported.empty() && under.reported == placed.reported)
                for (const auto& [builder, count] : under.chain) placed.chain[builder] += count;
        }
    };
    below(element.children);
    if (element.region) {
        below(element.region->then);
        below(element.region->otherwise);
    }
    if (element.built_at.empty() || element.type.empty()) return placed;
    const std::string_view reported = element.built_at;
    const auto column_at = reported.rfind(':');
    const auto line_at = column_at == std::string_view::npos || column_at == 0 ? std::string_view::npos : reported.rfind(':', column_at - 1);
    if (line_at == std::string_view::npos) return placed;
    const std::string file(reported.substr(0, line_at));
    std::size_t line = 0, column = 0;
    (void)std::from_chars(reported.data() + line_at + 1, reported.data() + column_at, line);
    (void)std::from_chars(reported.data() + column_at + 1, reported.data() + reported.size(), column);
    auto [entry, fresh] = sources.try_emplace(file);
    if (fresh)
        if (const auto text = text_of ? text_of(file) : std::nullopt)
            if (auto index = ts::CallIndex::of(*text)) entry->second = std::move(*index);
    const std::string builder = ts::camel_case(element.type);
    if (entry->second)
        if (const auto at = entry->second->enclosing(line, column, builder, placed.chain[builder]))
            element.built_at = std::format("{}:{}:{}", file, at->first, at->second);
    ++placed.chain[builder];
    return placed;
}

}  // namespace

Result<std::int64_t> Natives::signal_create(std::string_view initial_text, std::string_view key_text) {
    const Json initial = parsed(initial_text);
    const std::string key(key_text);
    abi::OmuiSignal handle = 0;
    if (!key.empty()) {
        if (auto it = keyed_.find(key); it != keyed_.end()) handle = it->second;
    } else if (next_signal_ < ordered_.size()) {
        handle = ordered_[next_signal_];  // a reload: the same signal, its value kept…
        // …unless the edit made it another kind of thing (a count become a plan's name): it starts again from its new
        // first value. Numbers stay numbers; a first value of null keeps whatever was there.
        const Json kept = session_.get(handle);
        if (!initial.is_null() && !initial.is_discarded() && kept.type() != initial.type() && !(kept.is_number() && initial.is_number()))
            session_.set(handle, initial);
    }
    if (!handle) {
        auto made = session_.signal(initial);
        if (!made) return std::unexpected(made.error());
        handle = *made;
        if (!key.empty()) keyed_.emplace(key, handle);
        else ordered_.push_back(handle);
    }
    if (key.empty()) ++next_signal_;
    return static_cast<std::int64_t>(handle);
}

std::string Natives::signal_get(std::int64_t signal) { return session_.get(static_cast<abi::OmuiSignal>(signal)).dump(); }

void Natives::signal_set(std::int64_t signal, std::string_view value) { session_.set(static_cast<abi::OmuiSignal>(signal), parsed(value)); }

void Natives::signal_watch(std::int64_t signal, int function) {
    const std::uint64_t token = session_.watch(static_cast<abi::OmuiSignal>(signal), [this, alive = std::weak_ptr<bool>(alive), function](const Json& value) {
        if (auto live = alive.lock(); live && *live) (void)engine_.call(function, value.dump(-1, ' ', false, Json::error_handler_t::replace), false);
    });
    watches_.emplace_back(token, function);
}

std::string Natives::catalog() {
    std::string out;
    session_.host().ui([&] {
        (void)abi::omuiCatalog(session_.host().runtime(), [](void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); },
                               &out);
    });
    return out;
}

Result<void> Natives::mount(std::string_view slot_text, std::string_view tree, std::vector<int> handlers) {
    const std::string slot(slot_text);
    auto root = element_from_json(parsed(tree), [&](std::size_t index) -> std::optional<Handler> {
        if (index >= handlers.size()) return std::nullopt;
        return [this, alive = std::weak_ptr<bool>(alive), function = handlers[index]](const Json& event) {
            if (auto live = alive.lock(); live && *live) (void)engine_.call(function, event.dump(), true);
        };
    });
    if (root && keep_built_at) {
        std::map<std::string, std::optional<ts::CallIndex>> sources;
        (void)place_built_at(text_of, *root, sources);
    }
    Result<void> mounted = root ? session_.mount(slot, *root) : Result<void>(std::unexpected(root.error()));
    if (!mounted) {
        for (const int function : handlers) engine_.release(function);
        return mounted;
    }
    // The view's old handlers go; the new ones stay while it is mounted.
    for (const int function : slot_functions_[slot]) engine_.release(function);
    slot_functions_[slot] = std::move(handlers);
    return {};
}

Result<void> Natives::unmount(std::string_view slot_text) {
    const std::string slot(slot_text);
    auto unmounted = session_.unmount(slot);
    for (const int function : slot_functions_[slot]) engine_.release(function);
    slot_functions_.erase(slot);
    return unmounted;
}

Result<std::string> Natives::document(std::string_view tree) {
    // A document only shown: its handlers are placeholders.
    auto root = element_from_json(parsed(tree), [](std::size_t) -> std::optional<Handler> { return [](const Json&) {}; });
    if (!root) return std::unexpected(root.error());
    return session_.document(*root).dump();
}

Result<void> Natives::verb(std::string_view name, std::string_view options_text, int function) {
    const Json options = parsed(options_text);
    Session::VerbSpec spec{std::string(name), options.value("summary", ""), effect_of(options.value("effect", "reversible")), options.value("answer", ""),
                           options.value("person", false)};
    auto made = session_.verb(std::move(spec), [this, alive = std::weak_ptr<bool>(alive), function](const Json& arguments) -> Result<Json> {
        if (auto live = alive.lock(); !live || !*live) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the script is gone"});
        auto out = engine_.call(function, arguments.dump(), false);
        if (!out) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the verb threw"});
        Json value = parsed(*out);
        return value.is_discarded() ? Json(nullptr) : value;
    });
    if (!made) engine_.release(function);
    return made;
}

Result<std::string> Natives::invoke(std::string_view name, std::string_view arguments) { return records(session_.invoke(std::string(name), parsed(arguments))); }
Result<std::string> Natives::act(std::string_view request) { return records(session_.act(parsed(request))); }
Result<std::string> Natives::snapshot(std::string_view request) { return records(session_.snapshot(parsed(request))); }

Result<std::int64_t> Natives::find(std::string_view path) {
    auto node = session_.find(std::string(path));
    if (!node) return std::unexpected(node.error());
    return static_cast<std::int64_t>(*node);
}

Result<std::string> Natives::get(std::int64_t node, std::string_view property) {
    auto value = session_.get(static_cast<abi::OmuiNode>(node), std::string(property));
    if (!value) return std::unexpected(value.error());
    return value->dump();
}

Result<void> Natives::set(std::int64_t node, std::string_view properties) {
    const Json props = parsed(properties);
    std::vector<std::pair<std::string, Json>> list;
    for (auto it = props.begin(); it != props.end(); ++it) list.emplace_back(it.key(), it.value());
    return session_.set(static_cast<abi::OmuiNode>(node), list);
}

Result<std::string> Natives::rows(std::string_view model) {
    auto rows = session_.rows(std::string(model));
    if (!rows) return std::unexpected(rows.error());
    return rows->dump();
}

Result<void> Natives::splice(std::string_view model, std::int64_t at, std::int64_t removed, std::string_view rows) {
    return session_.splice(std::string(model), static_cast<std::size_t>(std::max<std::int64_t>(0, at)), static_cast<std::size_t>(std::max<std::int64_t>(0, removed)),
                           parsed(rows));
}

// The runtime's theme (omui.theme): a JSON list of layers.
Result<void> Natives::theme(std::string_view layers_text) {
    const Json layers = parsed(layers_text);
    std::vector<std::string> list;
    if (layers.is_array())
        for (const Json& layer : layers)
            if (layer.is_string()) list.push_back(layer.get<std::string>());
    return session_.theme(list);
}

std::string Natives::themes() { return session_.themes().dump(); }

// A native module's function (component-binding §8): its result as JSON text.
Result<std::string> Natives::native_call(std::string_view module, std::string_view function, std::string_view arguments) {
    auto result = session_.native(std::string(module), std::string(function), parsed(arguments));
    if (!result) return std::unexpected(result.error());
    return result->dump();
}

void Natives::native_async(std::string_view module, std::string_view function, std::string_view arguments, std::function<void(Result<std::string>)> settled) {
    session_.native_async(std::string(module), std::string(function), parsed(arguments),
                          [alive = std::weak_ptr<bool>(alive), settled = std::move(settled)](Result<Json> result) {
                              if (auto live = alive.lock(); !live || !*live) return;
                              if (result) settled(result->dump());
                              else settled(std::unexpected(result.error()));
                          });
}

void Natives::reload() {
    next_signal_ = 0;  // the module's signals are the last run's, in order
    for (const auto& [token, function] : watches_) {  // the last run's watches go: this run makes its own again
        session_.unwatch(token);
        engine_.release(function);
    }
    watches_.clear();
}

std::string openmui_module(Session& session) {
    std::string catalog;
    session.host().ui([&] {
        (void)abi::omuiCatalog(session.host().runtime(), [](void* u, const char* d, std::size_t k) { static_cast<std::string*>(u)->append(d, k); },
                               &catalog);
    });
    std::string source(prelude);
    const Json c = Json::parse(catalog, nullptr, false);
    if (c.is_object() && c.contains("components"))
        for (auto it = c["components"].begin(); it != c["components"].end(); ++it) {
            const std::string name = ts::camel_case(it.key());
            // The prelude's own names win (its select is the component's builder too).
            if (source.find(std::format("export const {} ", name)) != std::string::npos ||
                source.find(std::format("export function {}(", name)) != std::string::npos)
                continue;
            source += std::format("export const {} = (props, children) => element(\"{}\", props, children);\n", name, it.key());
        }
    return source;
}

std::optional<std::string> native_module(Session& session, std::string_view module) {
    const Json catalog = session.natives();
    if (!catalog.contains("modules") || !catalog["modules"].contains(std::string(module))) return std::nullopt;
    const Json& m = catalog["modules"][std::string(module)];
    std::string out = "import { __native } from \"openmui\";\n";
    std::string members;
    for (auto it = m["functions"].begin(); it != m["functions"].end(); ++it) {
        const std::string fn = ts::camel_case(it.key());
        const bool async = it.value().value("async", false);
        out += std::format("export const {} = (...args) => __native({}, {}, {}, args);\n", fn, Json(std::string(module)).dump(), Json(it.key()).dump(),
                           async ? "true" : "false");
        members += (members.empty() ? "" : ", ") + fn;
        if (async) {
            // And waiting for it, where a promise cannot be awaited (a verb).
            out += std::format("export const {}Sync = (...args) => __native({}, {}, false, args);\n", fn, Json(std::string(module)).dump(),
                               Json(it.key()).dump());
            members += std::format(", {}Sync", fn);
        }
    }
    out += std::format("export const {0} = {{ {1} }};\nexport default {0};\n", module, members);
    return out;
}

}  // namespace omui::lang::js
