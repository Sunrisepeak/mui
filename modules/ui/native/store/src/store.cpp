// The store in use, and the functions the contract declares over it.
module openmui.native.store;

import std;

namespace omui {

namespace {

MemoryStore& memory() {
    static MemoryStore m;
    return m;
}

Store* current = nullptr;

}  // namespace

bool valid_store_key(std::string_view key) noexcept { return !key.empty() && key.size() <= store_key_bytes; }

std::optional<std::string> MemoryStore::get(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

std::expected<void, std::string> MemoryStore::set(std::string_view key, std::string_view value) {
    if (!valid_store_key(key)) return std::unexpected(std::format("a key is 1 to {} bytes, not {}", store_key_bytes, key.size()));
    values_.insert_or_assign(std::string(key), std::string(value));
    return {};
}

bool MemoryStore::remove(std::string_view key) {
    const auto it = values_.find(key);
    if (it == values_.end()) return false;
    values_.erase(it);
    return true;
}

std::vector<std::string> MemoryStore::keys(std::string_view prefix) const {
    std::vector<std::string> out;
    for (auto it = values_.lower_bound(prefix); it != values_.end() && it->first.starts_with(prefix); ++it) out.push_back(it->first);
    return out;
}

Store& store() { return current ? *current : memory(); }

void use_store(Store* s) { current = s; }

}  // namespace omui

namespace omui::natives::store {

std::expected<std::optional<std::string>, std::string> get(const std::string& key) {
    if (!valid_store_key(key)) return std::unexpected(std::format("a key is 1 to {} bytes, not {}", store_key_bytes, key.size()));
    return omui::store().get(key);
}

std::expected<void, std::string> set(const std::string& key, const std::string& value) { return omui::store().set(key, value); }

std::expected<bool, std::string> remove(const std::string& key) { return omui::store().remove(key); }

std::expected<std::vector<std::string>, std::string> keys(const std::optional<std::string>& prefix) {
    return omui::store().keys(prefix.value_or(std::string{}));
}

}  // namespace omui::natives::store
