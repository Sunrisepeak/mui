// openmui.native.store — a program's own small storage (D-84 §6.2): the
// plugin a host gives, and its face for scripts.
//
//     omui::store().set("theme", "dark");          // C++
//     auto t = omui::store().get("theme");
//
//     import { store } from "openmui:native/store"; // TypeScript (and Python, Lua)
//     store.set("theme", "dark");
//
// `Store` is the plugin interface: text values by key, small (settings,
// progress, a draft), read and written on the program's thread. A host puts
// its own in place with use_store() — the Web host the page's localStorage —
// and where none does, the program keeps them in memory (MemoryStore). The
// script interface is the contract's (native/store.native.omui.toml),
// generated into :store_native; its functions reach whichever store is in use.
export module openmui.native.store;

import std;
export import :store_native;

export namespace omui {

// Keys are 1 to 256 bytes; anything else is refused.
inline constexpr std::size_t store_key_bytes = 256;
[[nodiscard]] bool valid_store_key(std::string_view key) noexcept;

class Store {
public:
    virtual ~Store() = default;

    // What the host calls it in a diagnostic: "memory", "localStorage".
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) const = 0;
    // Refused (why) when the key is not one or the storage is full.
    virtual std::expected<void, std::string> set(std::string_view key, std::string_view value) = 0;
    // Whether the key was there.
    virtual bool remove(std::string_view key) = 0;
    // In byte order.
    [[nodiscard]] virtual std::vector<std::string> keys(std::string_view prefix = {}) const = 0;
};

// The store where the host keeps none: the program's memory, gone when it ends.
class MemoryStore final : public Store {
public:
    [[nodiscard]] std::string_view kind() const noexcept override { return "memory"; }
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override;
    std::expected<void, std::string> set(std::string_view key, std::string_view value) override;
    bool remove(std::string_view key) override;
    [[nodiscard]] std::vector<std::string> keys(std::string_view prefix = {}) const override;

private:
    std::map<std::string, std::string, std::less<>> values_;
};

// The store in use: the host's, or the program's MemoryStore.
[[nodiscard]] Store& store();
// Puts a host's store in use (nullptr: back to memory). The host keeps it
// alive while it is in use.
void use_store(Store* store);

}  // namespace omui
