// openmui.session — sessions: a header and timestamped entries, as NDJSON.
//
// A session file (`<name>.session.omui.jsonl`) is one JSON object per line:
// the header first ({"format":"openmui.session@0.1", ...}), then entries
// ordered by time ({"t":1.25,"kind":"act","body":{...}}). What an entry
// means is its kind's business (input events, semantic actions, frames,
// notes); this module keeps them in order, finds the ones of a time span by
// binary search, merges sessions, and reads and writes them. Bodies are
// kept as canonical JSON text, so a session written, read and written again
// is byte-identical.
export module openmui.session;

import std;

export namespace omui::session {

inline constexpr std::string_view format_name = "openmui.session@0.1";

struct Header {
    std::string                                      app;      // the application id
    std::string                                      spec;     // the spec version it ran
    double                                           width = 0, height = 0, scale = 1;
    std::string                                      clock = "virtual";  // virtual | render | interactive
    double                                           fps = 0;  // render clocks
    std::uint64_t                                    seed = 0;
    std::vector<std::pair<std::string, std::string>> meta;    // free-form, in order
};

struct Entry {
    double      time = 0;  // seconds on the session's clock
    std::string kind;      // "frame", "act", "input", "note", "check" …
    std::string body;      // a JSON object, canonical
};

class Session {
public:
    Header header;

    // Adds an entry; `body` must be a JSON object (it is made canonical). An
    // entry at the same time as others goes after them.
    std::expected<void, std::string> add(double time, std::string_view kind, std::string_view body_json);

    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] double duration() const noexcept { return entries_.empty() ? 0 : entries_.back().time; }

    // The first entry at or after `time`.
    [[nodiscard]] std::size_t seek(double time) const noexcept;
    // Entries with from <= time < to.
    [[nodiscard]] std::span<const Entry> between(double from, double to) const noexcept;
    // Entries of a kind, in order.
    [[nodiscard]] std::vector<const Entry*> of(std::string_view kind) const;

    // Another session's entries, shifted by `offset` seconds, interleaved by time.
    void merge(const Session& other, double offset = 0);
    // Keeps the entries of [from, to), with times made relative to `from`.
    [[nodiscard]] Session slice(double from, double to) const;

    [[nodiscard]] std::string ndjson() const;
    [[nodiscard]] static std::expected<Session, std::string> parse(std::string_view ndjson);

    std::expected<void, std::string> save(const std::filesystem::path& file) const;
    [[nodiscard]] static std::expected<Session, std::string> load(const std::filesystem::path& file);

private:
    std::vector<Entry> entries_;
};

}  // namespace omui::session
