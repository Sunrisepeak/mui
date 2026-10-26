// openmui.view:model — batch data (component-binding §6.1): a named list of
// rows, each a key and its fields. Lists render the rows they show of one;
// scripts and agents push to it; a change bumps its revision, a signal, so
// whatever reads it follows. Keys are ids (spec S-8): a list's items are
// addressed `list/key`, materialised or not (S-7).
export module openmui.view:model;

import std;
import openmui.core;

export namespace omui {

struct Row {
    std::string key;
    Value       fields;  // a map: {"title": "Buy milk", "done": false}
};

class Model {
public:
    explicit Model(std::string name);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    // Reading the revision makes a binding, region or watch follow the model.
    [[nodiscard]] std::uint64_t revision() const { return revision_.get(); }
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] const Row& at(std::size_t i) const { return rows_.at(i); }
    [[nodiscard]] std::span<const Row> rows() const noexcept { return rows_; }
    [[nodiscard]] std::optional<std::size_t> find(std::string_view key) const;

    // Batches, checked whole: keys are explicit ids, unique in the model.
    std::expected<void, std::string> push(std::vector<Row> rows);
    std::expected<void, std::string> insert(std::size_t at, std::vector<Row> rows);
    // Replaces rows [from, from + rows.size()) (a row may change its key).
    std::expected<void, std::string> patch(std::size_t from, std::vector<Row> rows);
    void remove(std::size_t from, std::size_t count);
    void clear();

    // The rows as data (scripts, agents): [{"key": …, …fields}].
    [[nodiscard]] Value to_value(std::size_t from = 0, std::size_t count = std::numeric_limits<std::size_t>::max()) const;
    // A row's text when nothing else says how to show it: its label, title,
    // text or name field, else its first text field, else its key.
    [[nodiscard]] static std::string label_of(const Row& row);

private:
    std::expected<void, std::string> check(std::span<const Row> rows, std::size_t skip_from, std::size_t skip_count) const;
    void reindex();
    void changed();

    std::string                                  name_;
    std::vector<Row>                             rows_;
    std::unordered_map<std::string, std::size_t> index_;
    Signal<std::uint64_t>                        revision_{0};
};

}  // namespace omui
