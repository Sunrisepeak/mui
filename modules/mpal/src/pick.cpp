// Choosing files (mpal:pick): the desktop portal's FileChooser, spoken over
// the session bus. A request's answer is a signal on its own object path,
// which is known before the call (the handle_token), so the match is asked
// first and no answer can come before it is listened for.
module mpal;

import std;
import mcpplibs.os;

namespace mpal {

namespace os = mcpplibs::os;

namespace {

constexpr std::string_view portal = "org.freedesktop.portal.Desktop";
constexpr std::string_view portal_path = "/org/freedesktop/portal/desktop";
constexpr std::string_view chooser = "org.freedesktop.portal.FileChooser";

using dbus::Value;
using dbus::Variant;

Value variant(std::string signature, Value inner) { return Value{Variant{std::move(signature), std::make_shared<Value>(std::move(inner))}}; }
Value entry(std::string key, Value inner) { return Value{std::vector<Value>{Value::of(std::move(key)), std::move(inner)}}; }

// A token no other request of this connection has.
std::string fresh_token() {
    static std::atomic<std::uint32_t> counter{0};
    return std::format("mpal{}_{}", os::random::hex(4).value_or("0"), ++counter);
}

}  // namespace

std::optional<std::filesystem::path> path_of_uri(std::string_view uri) {
    constexpr std::string_view scheme = "file://";
    if (!uri.starts_with(scheme)) return std::nullopt;
    uri.remove_prefix(scheme.size());
    if (const auto slash = uri.find('/'); slash != 0 && slash != std::string_view::npos) uri.remove_prefix(slash);  // file://host/path
    std::string decoded;
    for (std::size_t at = 0; at < uri.size(); ++at) {
        if (uri[at] == '%' && at + 2 < uri.size()) {
            unsigned value = 0;
            if (std::from_chars(uri.data() + at + 1, uri.data() + at + 3, value, 16).ec == std::errc{}) {
                decoded.push_back(static_cast<char>(value));
                at += 2;
                continue;
            }
        }
        decoded.push_back(uri[at]);
    }
    return os::fs::from_utf8(decoded);
}

bool can_pick() {
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            return bus && can_pick_on(*bus);
        }
    return false;
}

bool can_pick_on(dbus::Bus& bus) {
    auto owned = bus.call({.destination = "org.freedesktop.DBus", .path = "/org/freedesktop/DBus", .interface = "org.freedesktop.DBus",
                           .member = "NameHasOwner", .signature = "s", .arguments = {Value::of(std::string(portal))}});
    return owned && !owned->empty() && std::holds_alternative<bool>(owned->front().data) && std::get<bool>(owned->front().data);
}

std::expected<std::vector<std::filesystem::path>, Error> pick(const Pick& asked) {
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            if (!bus) return std::unexpected(bus.error());
            return pick_on(*bus, asked);
        }
    return failure(Error::Code::unsupported, std::format("choosing files on {} is not written yet (mpal README)", os::name(os::host())));
}

std::expected<std::vector<std::filesystem::path>, Error> pick_on(dbus::Bus& bus, const Pick& asked, std::chrono::milliseconds wait) {
    // The request's path: /org/freedesktop/portal/desktop/request/<sender>/<token>, the sender's
    // unique name without its colon and with its dots as underscores.
    const std::string token = fresh_token();
    std::string sender = bus.name();
    if (sender.starts_with(':')) sender.erase(0, 1);
    std::ranges::replace(sender, '.', '_');
    const std::string request = std::format("{}/request/{}/{}", portal_path, sender, token);
    if (auto matched = bus.add_match(std::format("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='{}'", request));
        !matched)
        return std::unexpected(matched.error());

    std::vector<Value> options{entry("handle_token", variant("s", Value::of(token)))};
    if (asked.kind == Pick::Kind::open && asked.many) options.push_back(entry("multiple", variant("b", Value{true})));
    if (asked.kind == Pick::Kind::folder) options.push_back(entry("directory", variant("b", Value{true})));
    if (!asked.filters.empty()) {
        // a(sa(us)): each filter's name and its patterns (0: a glob).
        std::vector<Value> filters;
        for (const Filter& filter : asked.filters) {
            std::vector<Value> patterns;
            if (filter.extensions.empty()) patterns.push_back(Value{std::vector<Value>{Value{std::uint32_t{0}}, Value::of("*")}});
            for (const std::string& extension : filter.extensions)
                patterns.push_back(Value{std::vector<Value>{Value{std::uint32_t{0}}, Value::of(std::format("*.{}", extension))}});
            filters.push_back(Value{std::vector<Value>{Value::of(filter.name), Value{std::move(patterns)}}});
        }
        options.push_back(entry("filters", variant("a(sa(us))", Value{std::move(filters)})));
    }
    if (!asked.start.empty()) {
        // ay: the folder's bytes, nul-terminated.
        std::vector<Value> bytes;
        for (const char c : os::fs::utf8(asked.start)) bytes.push_back(Value{static_cast<std::uint8_t>(c)});
        bytes.push_back(Value{std::uint8_t{0}});
        options.push_back(entry("current_folder", variant("ay", Value{std::move(bytes)})));
    }
    if (asked.kind == Pick::Kind::save && !asked.name.empty()) options.push_back(entry("current_name", variant("s", Value::of(asked.name))));

    // OpenFile(parent_window s, title s, options a{sv}) or SaveFile(…): the request's handle.
    auto handle = bus.call({.destination = std::string(portal),
                            .path = std::string(portal_path),
                            .interface = std::string(chooser),
                            .member = asked.kind == Pick::Kind::save ? "SaveFile" : "OpenFile",
                            .signature = "ssa{sv}",
                            .arguments = {Value::of(asked.parent), Value::of(asked.title), Value{std::move(options)}}});
    if (!handle) {
        if (handle.error().text.contains("ServiceUnknown")) return failure(Error::Code::unavailable, "no desktop portal on the session bus");
        return std::unexpected(handle.error());
    }

    // Response(u response, a{sv} results): 0 chosen, 1 cancelled, 2 ended otherwise.
    const auto until = std::chrono::steady_clock::now() + wait;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        auto signal = bus.next_signal(std::max(left, std::chrono::milliseconds(0)));
        if (!signal) return std::unexpected(signal.error());
        if (!*signal) return failure(Error::Code::failed, "the portal did not answer in time");
        const dbus::Message& heard = **signal;
        if (heard.member != "Response" || heard.path != request || heard.body.size() < 2) continue;
        const auto* response = std::get_if<std::uint32_t>(&heard.body[0].data);
        if (!response) return failure(Error::Code::failed, "a portal response without its code");
        if (*response == 1) return std::vector<std::filesystem::path>{};
        if (*response != 0) return failure(Error::Code::failed, "the portal ended the choice");
        std::vector<std::filesystem::path> chosen;
        if (const auto* results = std::get_if<std::vector<Value>>(&heard.body[1].data))
            for (const Value& result : *results) {
                const auto* pair = std::get_if<std::vector<Value>>(&result.data);
                if (!pair || pair->size() != 2) continue;
                const auto* key = std::get_if<std::string>(&(*pair)[0].data);
                const auto* held = std::get_if<Variant>(&(*pair)[1].data);
                if (!key || *key != "uris" || !held || !held->second) continue;
                if (const auto* uris = std::get_if<std::vector<Value>>(&held->second->data))
                    for (const Value& uri : *uris)
                        if (const auto* text = std::get_if<std::string>(&uri.data))
                            if (auto path = path_of_uri(*text)) chosen.push_back(std::move(*path));
            }
        return chosen;
    }
}

}  // namespace mpal
