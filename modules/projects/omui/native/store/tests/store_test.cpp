// The Store plugin and its script face: the memory store where no host gives
// one, a host's store put in use (and every call reaching it), the contract's
// functions by position and by name.
import std;
import nlohmann.json;
import openmui.native;
import openmui.native.store;
import openmui.testing;

using namespace omui::testing;
namespace native = omui::native;
using native::Json;

namespace {

// A host's store, as the Web's localStorage is one: what it was asked, kept.
class Recording final : public omui::Store {
public:
    std::vector<std::string> calls;
    std::map<std::string, std::string, std::less<>> kept;
    std::size_t room = 64;  // bytes, all values together

    [[nodiscard]] std::string_view kind() const noexcept override { return "recording"; }
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override {
        const auto it = kept.find(key);
        return it == kept.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    std::expected<void, std::string> set(std::string_view key, std::string_view value) override {
        calls.push_back(std::format("set {}", key));
        std::size_t used = value.size();
        for (const auto& [k, v] : kept)
            if (k != key) used += v.size();
        if (used > room) return std::unexpected("the storage is full");
        kept.insert_or_assign(std::string(key), std::string(value));
        return {};
    }
    bool remove(std::string_view key) override {
        calls.push_back(std::format("remove {}", key));
        return kept.erase(std::string(key)) > 0;
    }
    [[nodiscard]] std::vector<std::string> keys(std::string_view prefix) const override {
        std::vector<std::string> out;
        for (const auto& [k, v] : kept)
            if (k.starts_with(prefix)) out.push_back(k);
        return out;
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"native.store"};

    s.test("no host's store: memory, keys checked, in byte order", [] {
        omui::Store& m = omui::store();
        check_eq(m.kind(), std::string_view("memory"));
        check(m.set("b", "2").has_value() && m.set("a", "1").has_value() && m.set("ab", "3").has_value());
        check(m.get("a") == std::optional<std::string>("1") && !m.get("c"));
        check(m.keys() == std::vector<std::string>{"a", "ab", "b"}, std::format("{}", m.keys()));
        check(m.keys("a") == std::vector<std::string>{"a", "ab"});
        check(!m.set("", "x").has_value() && !m.set(std::string(257, 'k'), "x").has_value(), "a key is 1 to 256 bytes");
        check(m.set(std::string(256, 'k'), "x").has_value());
        check(m.remove("ab") && !m.remove("ab"), "removed once");
    });

    s.test("define() registers the contract's module, once", [] {
        auto d = omui::natives::store::define();
        check(d.has_value(), d ? "" : d.error());
        const native::Module* m = native::find("store");
        check(m != nullptr && m->functions.size() == 4, "get, set, remove, keys");
        if (m) check(m->functions[1].name == "set" && m->functions[1].effect == "reversible" && m->functions[0].effect == "read");
        check(!omui::natives::store::define().has_value(), "a second definition is refused");
    });

    s.test("a host's store in use: scripts reach it; its refusal is theirs", [] {
        Recording host;
        omui::use_store(&host);
        check(&omui::store() == &host);
        check(native::call("store", "set", Json::array({"volume", "0.8"})).has_value());
        check(native::call("store", "set", Json{{"key", "lang"}, {"value", "zh"}}).has_value(), "by name");
        auto v = native::call("store", "get", Json::array({"volume"}));
        check(v.has_value() && *v == Json("0.8"), v ? v->dump() : v.error().message);
        auto none = native::call("store", "get", Json::array({"missing"}));
        check(none.has_value() && none->is_null());
        auto keys = native::call("store", "keys", Json::array());
        check(keys.has_value() && *keys == Json::array({"lang", "volume"}), keys ? keys->dump() : "");
        auto some = native::call("store", "keys", Json::array({"vol"}));
        check(some.has_value() && *some == Json::array({"volume"}));
        auto full = native::call("store", "set", Json::array({"big", std::string(100, 'x')}));
        check(!full && full.error().code == "failed" && full.error().message.ends_with("the storage is full"), full ? "" : full.error().message);
        auto gone = native::call("store", "remove", Json::array({"lang"}));
        check(gone.has_value() && *gone == Json(true));
        auto bad = native::call("store", "set", Json::array({"k", 3}));
        check(!bad && bad.error().code == "invalid-argument", "the contract's types");
        auto empty = native::call("store", "get", Json::array({""}));
        check(!empty && empty.error().code == "failed", "an empty key is not one");
        check(host.calls == std::vector<std::string>{"set volume", "set lang", "set big", "remove lang"}, std::format("{}", host.calls));
        omui::use_store(nullptr);
        check(omui::store().kind() == "memory", "back to memory");
        check(!omui::store().get("volume"), "the host's values stay the host's");
    });

    return s.run(argc, argv);
}
