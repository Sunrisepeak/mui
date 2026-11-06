// openmui.host.web.toy — Bilibili Toy's SDK for a program in a Toy page
// (D-84 §6.5). Thin and optional: a program that never calls it runs the
// same in any page; off a Toy page every call is refused (openas `refused`).
//
//     omui::toy::Store save;                     // cloud storage, at checkpoints
//     omui::toy::attach(web, save);              // flushed when the page hides
//     save.set("best", "1200");
//     omui::toy::submit_score(1200, 1, [](auto r) { … });  // at settlement only
//
// Answers come back on the page's thread, between frames; attach() wakes the
// host so a change they make is drawn.
//
// Toy's API (toy.d.ts of the publishing guide, SDK 0.3.7): isSupport,
// get/setCloudStorage (keys [A-Za-z0-9_-]{1,128} not starting `__`, values
// at most 1024 bytes, 128 pairs a Toy), submitScore ({board 1–3, score}),
// getRankList ({board, period, limit ≤ 100}), getMyRank. Toy has no
// lifecycle of its own: the page's visibility is it (Web::on_visibility).
export module openmui.host.web.toy;

import std;
export import openmui.core;
export import openmui.host.web;

export namespace omui::toy {

// Toy's error, in openas's words (design §6.5): invalid_param is
// `invalid-argument`, unsupported (or no SDK) `refused`, the rate limit
// 307044 `not ready` (exit code 3: the same request may succeed later —
// tried again with backoff before it is reported), anything else `failed`.
struct Error {
    std::string  type;      // Toy's: invalid_param, unsupported, http_error, timeout, …
    std::int64_t code = 0;  // Toy's numeric code (307044 the rate limit)
    std::string  message;

    [[nodiscard]] std::string_view openas_code() const noexcept;
    [[nodiscard]] int exit_code() const noexcept;
};

template <class T>
using Done = std::function<void(std::expected<T, Error>)>;

// Whether the page has Toy's SDK (window.toy).
[[nodiscard]] bool available() noexcept;

// isSupport: whether the container offers an ability ("submitScore", "toy.share").
void supports(std::string_view ability, Done<bool> done);

// ── cloud storage ──
//
// Written to memory at once and to Toy at a checkpoint — flush(), or the
// page hidden when attach()ed — as one call for every key changed: Toy's
// quota is shared by every player of a Toy, so never a call a frame.
class Store {
public:
    static constexpr std::size_t max_keys = 128, max_key_bytes = 128, max_value_bytes = 1024;

    // The limits are checked here, not when Toy refuses the flush.
    std::expected<void, Error> set(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] bool dirty() const noexcept { return !changed_.empty(); }
    // What Toy has for these keys (all of them when empty), into memory;
    // what was set since and not flushed stays as set.
    void load(std::vector<std::string> keys, Done<void> done = {});
    void flush(Done<void> done = {});

private:
    std::map<std::string, std::string, std::less<>> values_;
    std::set<std::string, std::less<>>              changed_;
};

[[nodiscard]] bool valid_key(std::string_view key) noexcept;

// ── leaderboards ──

struct Rank {
    std::int64_t rank = 0, score = 0;
    std::string  nickname, avatar;
};

// At settlement only (a round's end, not a frame's): Toy keeps the best;
// the answer is the score it keeps. Boards 1–3; scores −16777216 … 16777215.
void submit_score(std::int64_t score, int board, Done<std::int64_t> done = {});
// period: "all", "month", "week" or "day"; limit at most 100.
void rank_list(int board, std::string_view period, int limit, Done<std::vector<Rank>> done);
// The player's own rank: none when not ranked.
void my_rank(int board, std::string_view period, Done<std::optional<Rank>> done);

// The page's lifecycle for a Toy: when it hides, the store is flushed; an
// answer from Toy wakes the host so what it changed is drawn.
void attach(Web& web, Store& store);

// ── the call underneath (any method of window.toy, JSON in and out) ──
void call(std::string_view method, const Value& args, Done<Value> done);

}  // namespace omui::toy
