// The person's preferences (mpal:preferences): the desktop portal's Settings,
// spoken over the session bus. ReadAll answers the namespaces asked for (it
// has no version 1 quirk of Read's, a variant inside a variant); a change is
// the portal's SettingChanged signal, asked for by namespace so that nothing
// else wakes a listener. The standard keys (org.freedesktop.appearance) first;
// where the standard has none yet, the desktop's own one the portal passes on
// (GNOME's enable-animations, under its "Reduce animation").
module mcpplibs.mpal;

import std;
import mcpplibs.os;

namespace mcpplibs::mpal {

namespace os = mcpplibs::os;

namespace {

constexpr std::string_view portal = "org.freedesktop.portal.Desktop";
constexpr std::string_view portal_path = "/org/freedesktop/portal/desktop";
constexpr std::string_view settings = "org.freedesktop.portal.Settings";
constexpr std::string_view appearance = "org.freedesktop.appearance";
constexpr std::string_view gnome_interface = "org.gnome.desktop.interface";

using dbus::Value;
using dbus::Variant;

// Whether this key is one read here; its value taken into `keys` (none: no preference said).
bool take(SettingKeys& keys, std::string_view space, std::string_view key, const Value& value) {
    const auto* number = std::get_if<std::uint32_t>(&value.data);
    const auto* flag = std::get_if<bool>(&value.data);
    if (space == appearance && key == "color-scheme") {
        keys.dark = !number || *number == 0 ? std::nullopt : std::optional<bool>(*number == 1);
        return true;
    }
    if (space == appearance && key == "reduced-motion") {
        keys.reduced_motion = number ? std::optional<bool>(*number == 1) : std::nullopt;
        return true;
    }
    if (space == gnome_interface && key == "enable-animations") {
        keys.enable_animations = flag ? std::optional<bool>(*flag) : std::nullopt;
        return true;
    }
    return false;
}

Preferences preferences_of(const SettingKeys& keys) {
    Preferences out;
    out.dark = keys.dark;
    if (keys.reduced_motion) out.reduce_motion = keys.reduced_motion;
    else if (keys.enable_animations) out.reduce_motion = !*keys.enable_animations;
    return out;
}

// A variant's value (none: not a variant).
const Value* held(const Value& value) {
    const auto* variant = std::get_if<Variant>(&value.data);
    return variant && variant->second ? variant->second.get() : nullptr;
}

// ReadAll(as namespaces) → a{sa{sv}}: each namespace's keys and their values.
std::expected<SettingKeys, Error> read(dbus::Bus& bus) {
    auto answer = bus.call({.destination = std::string(portal),
                            .path = std::string(portal_path),
                            .interface = std::string(settings),
                            .member = "ReadAll",
                            .signature = "as",
                            .arguments = {Value{std::vector<Value>{Value::of(std::string(appearance)), Value::of(std::string(gnome_interface))}}}});
    if (!answer) {
        if (answer.error().text.contains("ServiceUnknown")) return failure(Error::Code::unavailable, "no desktop portal on the session bus");
        return std::unexpected(answer.error());
    }
    SettingKeys keys;
    if (answer->empty()) return keys;
    const auto* spaces = std::get_if<std::vector<Value>>(&answer->front().data);
    if (!spaces) return failure(Error::Code::failed, "the portal's settings answered in another shape");
    for (const Value& space_entry : *spaces) {
        const auto* space_pair = std::get_if<std::vector<Value>>(&space_entry.data);
        if (!space_pair || space_pair->size() != 2) continue;
        const auto* space = std::get_if<std::string>(&(*space_pair)[0].data);
        const auto* entries = std::get_if<std::vector<Value>>(&(*space_pair)[1].data);
        if (!space || !entries) continue;
        for (const Value& entry : *entries) {
            const auto* pair = std::get_if<std::vector<Value>>(&entry.data);
            if (!pair || pair->size() != 2) continue;
            const auto* key = std::get_if<std::string>(&(*pair)[0].data);
            const Value* value = held((*pair)[1]);
            if (key && value) (void)take(keys, *space, *key, *value);
        }
    }
    return keys;
}

}  // namespace

std::expected<Preferences, Error> preferences() {
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            if (!bus) return std::unexpected(bus.error());
            return preferences_on(*bus);
        }
    return failure(Error::Code::unsupported, std::format("the person's preferences on {} are not written yet (mpal README)", os::name(os::host())));
}

std::expected<Preferences, Error> preferences_on(dbus::Bus& bus) {
    auto keys = read(bus);
    if (!keys) return std::unexpected(keys.error());
    return preferences_of(*keys);
}

std::expected<PreferenceWatch, Error> PreferenceWatch::open() {
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            if (!bus) return std::unexpected(bus.error());
            return on(std::move(*bus));
        }
    return failure(Error::Code::unsupported, std::format("the person's preferences on {} are not written yet (mpal README)", os::name(os::host())));
}

std::expected<PreferenceWatch, Error> PreferenceWatch::on(dbus::Bus bus) {
    // SettingChanged(s namespace, s key, v value), for the two namespaces read here only.
    for (const std::string_view space : {appearance, gnome_interface})
        if (auto matched = bus.add_match(std::format("type='signal',interface='{}',member='SettingChanged',path='{}',arg0='{}'", settings, portal_path, space));
            !matched)
            return std::unexpected(matched.error());
    auto keys = read(bus);
    if (!keys) return std::unexpected(keys.error());
    PreferenceWatch watch;
    watch.keys_ = *keys;
    watch.now_ = preferences_of(*keys);
    watch.bus_.emplace(std::move(bus));
    return watch;
}

void PreferenceWatch::stop() {
    stopped_->store(true);
    if (bus_) bus_->shutdown();
}

std::expected<std::optional<Preferences>, Error> PreferenceWatch::next(std::chrono::milliseconds wait) {
    if (stopped_->load() || !bus_) return std::optional<Preferences>{};
    const auto until = std::chrono::steady_clock::now() + wait;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        auto signal = bus_->next_signal(std::max(left, std::chrono::milliseconds(0)));
        if (!signal && stopped_->load()) return std::optional<Preferences>{};  // the conversation ended by `stop`
        if (!signal) return std::unexpected(signal.error());
        if (!*signal) return std::optional<Preferences>{};
        const dbus::Message& heard = **signal;
        if (heard.interface != settings || heard.member != "SettingChanged" || heard.body.size() < 3) continue;
        const auto* space = std::get_if<std::string>(&heard.body[0].data);
        const auto* key = std::get_if<std::string>(&heard.body[1].data);
        const Value* value = held(heard.body[2]);
        if (!space || !key || !value || !take(keys_, *space, *key, *value)) continue;
        const Preferences changed = preferences_of(keys_);
        if (changed == now_) continue;
        now_ = changed;
        return std::optional(now_);
    }
}

}  // namespace mcpplibs::mpal
