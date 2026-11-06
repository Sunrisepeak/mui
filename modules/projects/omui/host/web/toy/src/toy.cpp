module openmui.host.web.toy;

import std;
import openmui.core;
import openmui.host.web;
import openas;
import :sdk;

namespace omui::toy {

namespace {

Web* host = nullptr;  // attach()'s: woken when an answer arrives

Error error_of(std::string_view json) {
    Error e;
    if (auto v = parse_json(json)) {
        if (const Value* t = v->get("type")) e.type = std::string(t->as_string());
        if (const Value* c = v->get("code")) e.code = c->as_int();
        if (const Value* m = v->get("message")) e.message = std::string(m->as_string());
    }
    if (e.type.empty()) e.type = "unknown";
    return e;
}

Error invalid(std::string message) { return {"invalid_param", 0, std::move(message)}; }

}  // namespace

std::string_view Error::openas_code() const noexcept {
    if (type == "invalid_param") return openas::error_code::invalid_argument;
    if (type == "unsupported") return openas::error_code::refused;
    if (code == 307044) return openas::error_code::failed;  // with exit code 3: not ready
    if (type == "timeout") return openas::error_code::timeout;
    return openas::error_code::failed;
}

int Error::exit_code() const noexcept { return code == 307044 ? openas::exit_code::not_ready : openas::exit_code::failed; }

bool available() noexcept { return sdk::available(); }

void call(std::string_view method, const Value& args, Done<Value> done) {
    sdk::call(method, args.is_null() ? std::string() : args.json(), [done = std::move(done)](bool ok, std::string_view json) {
        if (done) {
            if (!ok) done(std::unexpected(error_of(json)));
            else done(parse_json(json).value_or(Value{}));
        }
        if (host) host->wake();
    });
}

void supports(std::string_view ability, Done<bool> done) {
    call("isSupport", Value(std::string(ability)), [done = std::move(done)](std::expected<Value, Error> r) {
        if (!done) return;
        if (!r) done(std::unexpected(r.error()));
        else done(r->as_bool());
    });
}

// ── cloud storage ──

bool valid_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > Store::max_key_bytes || key.starts_with("__")) return false;
    return std::ranges::all_of(key, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::expected<void, Error> Store::set(std::string_view key, std::string_view value) {
    if (!valid_key(key)) return std::unexpected(invalid(std::format("`{}`: a key is [A-Za-z0-9_-]{{1,128}}, not starting `__`", key)));
    if (value.size() > max_value_bytes)
        return std::unexpected(invalid(std::format("`{}`: {} bytes; a value is at most {}", key, value.size(), max_value_bytes)));
    if (!values_.contains(key) && values_.size() >= max_keys) return std::unexpected(invalid(std::format("a Toy keeps at most {} keys", max_keys)));
    auto [it, fresh] = values_.try_emplace(std::string(key), value);
    if (!fresh && it->second == value) return {};
    it->second = std::string(value);
    changed_.insert(std::string(key));
    return {};
}

std::optional<std::string> Store::get(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void Store::load(std::vector<std::string> keys, Done<void> done) {
    Value args;
    if (!keys.empty()) {
        Value::List list;
        for (std::string& k : keys) list.emplace_back(std::move(k));
        args = Value(Value::List{Value(std::move(list))});
    }
    call("getCloudStorage", args, [this, done = std::move(done)](std::expected<Value, Error> r) {
        if (r && r->map())
            for (const auto& [k, v] : *r->map())
                if (!changed_.contains(k)) values_[k] = std::string(v.as_string());
        if (!done) return;
        if (!r) done(std::unexpected(r.error()));
        else done({});
    });
}

void Store::flush(Done<void> done) {
    if (changed_.empty()) {
        if (done) done({});
        return;
    }
    Value items;
    for (const std::string& k : changed_) items.set(k, Value(values_[k]));
    std::set<std::string, std::less<>> sent = std::move(changed_);
    changed_.clear();
    call("setCloudStorage", items, [this, sent = std::move(sent), done = std::move(done)](std::expected<Value, Error> r) mutable {
        // Not written: changed still (unless set again since, which is newer anyway).
        if (!r) changed_.merge(sent);
        if (!done) return;
        if (!r) done(std::unexpected(r.error()));
        else done({});
    });
}

// ── leaderboards ──

void submit_score(std::int64_t score, int board, Done<std::int64_t> done) {
    if (score < -16777216 || score > 16777215 || board < 1 || board > 3) {
        if (done) done(std::unexpected(invalid("a score is −16777216 … 16777215 on board 1, 2 or 3")));
        return;
    }
    call("submitScore", Value{}.set("board", board).set("score", score), [done = std::move(done)](std::expected<Value, Error> r) {
        if (!done) return;
        if (!r) done(std::unexpected(r.error()));
        else done(r->get("score") ? r->get("score")->as_int() : std::int64_t{0});
    });
}

namespace {

Rank rank_of(const Value& v) {
    Rank r;
    if (const Value* x = v.get("rank")) r.rank = x->as_int();
    if (const Value* x = v.get("score")) r.score = x->as_int();
    if (const Value* x = v.get("nickname")) r.nickname = std::string(x->as_string());
    if (const Value* x = v.get("avatar")) r.avatar = std::string(x->as_string());
    return r;
}

}  // namespace

void rank_list(int board, std::string_view period, int limit, Done<std::vector<Rank>> done) {
    call("getRankList", Value{}.set("board", board).set("period", period).set("limit", std::clamp(limit, 1, 100)),
         [done = std::move(done)](std::expected<Value, Error> r) {
             if (!done) return;
             if (!r) return done(std::unexpected(r.error()));
             std::vector<Rank> out;
             if (r->list())
                 for (const Value& v : *r->list()) out.push_back(rank_of(v));
             done(std::move(out));
         });
}

void my_rank(int board, std::string_view period, Done<std::optional<Rank>> done) {
    call("getMyRank", Value{}.set("board", board).set("period", period), [done = std::move(done)](std::expected<Value, Error> r) {
        if (!done) return;
        if (!r) return done(std::unexpected(r.error()));
        if (!r->get("ranked") || !r->get("ranked")->as_bool()) return done(std::optional<Rank>{});
        done(std::optional<Rank>(rank_of(*r)));
    });
}

void attach(Web& web, Store& store) {
    host = &web;
    web.on_visibility([&store](bool hidden) {
        if (hidden) store.flush();  // a checkpoint: the page may not come back
    });
}

}  // namespace omui::toy
