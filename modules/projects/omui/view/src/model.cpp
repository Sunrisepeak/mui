module openmui.view;

import std;
import openmui.core;

namespace omui {

Model::Model(std::string name) : name_(std::move(name)) {}

std::optional<std::size_t> Model::find(std::string_view key) const {
    auto it = index_.find(std::string(key));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::expected<void, std::string> Model::check(std::span<const Row> rows, std::size_t skip_from, std::size_t skip_count) const {
    std::unordered_set<std::string_view> seen;
    for (const Row& r : rows) {
        if (!spec::valid_explicit_segment(r.key))
            return std::unexpected(std::format("model `{}`: `{}` is not a key (1-128 of [A-Za-z0-9_.-]: keys are ids, spec S-8)", name_, r.key));
        if (!seen.insert(r.key).second) return std::unexpected(std::format("model `{}`: the key `{}` twice in a batch", name_, r.key));
        if (auto at = find(r.key); at && !(*at >= skip_from && *at < skip_from + skip_count))
            return std::unexpected(std::format("model `{}`: the key `{}` is taken", name_, r.key));
    }
    return {};
}

void Model::reindex() {
    index_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) index_.emplace(rows_[i].key, i);
}

void Model::changed() { revision_.set(revision_.peek() + 1); }

std::expected<void, std::string> Model::push(std::vector<Row> rows) { return insert(rows_.size(), std::move(rows)); }

std::expected<void, std::string> Model::insert(std::size_t at, std::vector<Row> rows) {
    if (at > rows_.size()) return std::unexpected(std::format("model `{}`: insert at {} past its {} rows", name_, at, rows_.size()));
    if (auto ok = check(rows, 0, 0); !ok) return ok;
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    reindex();
    changed();
    return {};
}

std::expected<void, std::string> Model::patch(std::size_t from, std::vector<Row> rows) {
    if (from + rows.size() > rows_.size())
        return std::unexpected(std::format("model `{}`: patch of rows {}..{} past its {} rows", name_, from, from + rows.size(), rows_.size()));
    if (auto ok = check(rows, from, rows.size()); !ok) return ok;
    for (std::size_t i = 0; i < rows.size(); ++i) rows_[from + i] = std::move(rows[i]);
    reindex();
    changed();
    return {};
}

void Model::remove(std::size_t from, std::size_t count) {
    from = std::min(from, rows_.size());
    count = std::min(count, rows_.size() - from);
    if (count == 0) return;
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(from), rows_.begin() + static_cast<std::ptrdiff_t>(from + count));
    reindex();
    changed();
}

void Model::clear() {
    if (rows_.empty()) return;
    rows_.clear();
    index_.clear();
    changed();
}

Value Model::to_value(std::size_t from, std::size_t count) const {
    Value::List out;
    for (std::size_t i = from; i < rows_.size() && i - from < count; ++i) {
        Value v;
        v.set("key", rows_[i].key);
        if (const Value::Map* m = rows_[i].fields.map())
            for (const auto& [k, x] : *m) v.set(k, x);
        out.push_back(std::move(v));
    }
    return Value(std::move(out));
}

std::string Model::label_of(const Row& row) {
    for (std::string_view k : {"label", "title", "text", "name"})
        if (const Value* v = row.fields.get(k); v && v->type() == Value::Type::text) return std::string(v->as_string());
    if (const Value::Map* m = row.fields.map())
        for (const auto& [k, v] : *m)
            if (v.type() == Value::Type::text) return std::string(v.as_string());
    return row.key;
}

}  // namespace omui
