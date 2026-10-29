module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.abi;
import openmui.lang;
import openmui.lang.js;
import openmui.lang.lua;
import openmui.lang.python.embedded;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

using clock_type = std::chrono::steady_clock;

constexpr double poll = 1.0 / 240;  // how often a script's run is looked at

// The gift scripts the game carries.
std::vector<GiftScript> shipped() {
    auto one = [](std::string gift, std::string language, std::string file, std::string_view text) {
        return GiftScript{std::move(gift), std::move(language), std::move(file), std::string(text), std::string(text)};
    };
    return {one("wide", "ts", "wide.ts", files::wide_ts), one("pierce", "ts", "pierce.ts", files::pierce_ts), one("slow", "lua", "slow.lua", files::slow_lua),
            one("multi", "py", "multi.py", files::multi_py), one("magnet", "lua", "magnet.lua", files::magnet_lua)};
}

// A rule a script may answer: its name, where it goes, its bounds.
struct RuleField {
    std::string_view name;
    std::int32_t Rules::* member;
    std::int32_t lo, hi;
};
constexpr std::int32_t s = steps_per_second;
constexpr std::array<RuleField, 8> fields{{{"wide_steps", &Rules::wide_steps, 1 * s, 30 * s},
                                       {"wide_half", &Rules::wide_half, 80, 300},
                                       {"multi_balls", &Rules::multi_balls, 1, 4},
                                       {"slow_steps", &Rules::slow_steps, 1 * s, 20 * s},
                                       {"slow_percent", &Rules::slow_percent, 25, 90},
                                       {"pierce_steps", &Rules::pierce_steps, 1 * s, 20 * s},
                                       {"drop_one_in", &Rules::drop_one_in, 2, 20},
                                       {"magnet_catches", &Rules::magnet_catches, 1, 8}}};

// A verb's answer as rules over `into`: every field known, a whole number, within its bounds — or why not.
std::expected<Rules, std::string> read_rules(const Value& answer, Rules into, std::string_view gift) {
    const auto* map = answer.map();
    if (!map) return std::unexpected(std::format("gift.{} answered no table of rules", gift));
    for (const auto& [key, value] : *map) {
        const auto f = std::ranges::find(fields, std::string_view(key), &RuleField::name);
        if (f == fields.end()) return std::unexpected(std::format("gift.{}: no rule is called `{}`", gift, key));
        if (!value.is_number()) return std::unexpected(std::format("gift.{}: `{}` is not a number", gift, key));
        const double v = value.as_double();
        if (!std::isfinite(v) || v != std::floor(v)) return std::unexpected(std::format("gift.{}: `{}` is {}, not a whole number", gift, key, v));
        if (v < f->lo || v > f->hi) return std::unexpected(std::format("gift.{}: `{}` is {}, outside {} … {}", gift, key, v, f->lo, f->hi));
        into.*(f->member) = static_cast<std::int32_t>(v);
    }
    return into;
}

}  // namespace

struct Workshop::Impl {
    // The C ABI's handle over the runtime, let go last (after the scripts, the session and the host).
    struct Attached {
        abi::OmuiRuntime handle;
        explicit Attached(Runtime& rt) : handle(abi::attach(rt, "org.openmui.breakout")) {}
        Attached(const Attached&) = delete;
        Attached& operator=(const Attached&) = delete;
        ~Attached() { abi::detach(handle); }
    };
    Runtime&                                       runtime;
    Attached                                       attached;
    lang::Host                                     host;
    lang::Session                                  session;
    std::vector<std::unique_ptr<lang::js::Script>>  js;   // per script: one of these two
    std::vector<std::unique_ptr<lang::lua::Script>> lua;
    std::vector<std::unique_ptr<lang::python::embedded::Script>> py;
    std::vector<std::future<lang::Result<void>>>   loading;
    std::string                                    errors;  // raised in a verb

    explicit Impl(Runtime& rt) : runtime(rt), attached(rt), host(attached.handle), session(host) {}

    // Runs a script's text, the first time or again in place.
    std::expected<std::future<lang::Result<void>>, std::string> run(std::size_t i, const GiftScript& g, std::string_view text, bool again) {
        if (g.language == "ts") {
            auto code = lang::js::compile_typescript_source(text, g.file);
            if (!code) return std::unexpected(code.error());
            return again ? js[i]->reload(std::move(*code), g.file) : js[i]->run(std::move(*code), g.file);
        }
        if (g.language == "py") return again ? py[i]->reload(std::string(text), g.file) : py[i]->run(std::string(text), g.file);
        return again ? lua[i]->reload(std::string(text), "=" + g.file) : lua[i]->run(std::string(text), "=" + g.file);
    }

    std::expected<Rules, std::string> ask(const GiftScript& g, Rules base) {
        Invocation call;
        call.actor = {"program", "workshop", {}};
        const Envelope e = runtime.verbs().invoke("gift." + g.gift, call);
        if (e.exit_code() != std::optional<int>(0) || !e.data_value()) {
            for (const auto& r : e.records())
                if (const Value* m = r.body.get("text")) return std::unexpected(std::format("gift.{}: {}", g.gift, m->as_string()));
            return std::unexpected(std::format("gift.{} did not answer", g.gift));
        }
        return read_rules(*e.data_value(), base, g.gift);
    }
};

Workshop::Workshop(Runtime& runtime) : impl_(std::make_unique<Impl>(runtime)), scripts_(shipped()) {
    for (std::size_t i = 0; i < scripts_.size(); ++i) {
        const GiftScript& g = scripts_[i];
        impl_->js.push_back(g.language == "ts" ? std::make_unique<lang::js::Script>(impl_->session) : nullptr);
        impl_->lua.push_back(g.language == "lua" ? std::make_unique<lang::lua::Script>(impl_->session) : nullptr);
        impl_->py.push_back(g.language == "py" ? std::make_unique<lang::python::embedded::Script>(impl_->session) : nullptr);
        if (impl_->js[i]) impl_->js[i]->on_error([this](std::string_view m) { impl_->errors = std::string(m); });
        if (impl_->lua[i]) impl_->lua[i]->on_error([this](std::string_view m) { impl_->errors = std::string(m); });
        if (impl_->py[i]) impl_->py[i]->on_error([this](std::string_view m) { impl_->errors = std::string(m); });
        auto run = impl_->run(i, g, g.shipped, false);
        if (run) impl_->loading.push_back(std::move(*run));
        else warn("breakout: {} did not load: {}", g.file, run.error());
    }
}

Workshop::~Workshop() = default;

std::expected<Rules, std::string> Workshop::rules(Rules base) {
    for (auto& f : impl_->loading)
        if (f.valid()) {
            if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::unexpected(std::string("the scripts are still loading"));
            if (auto r = f.get(); !r) return std::unexpected(r.error().text());
        }
    for (const GiftScript& g : scripts_) {
        auto r = impl_->ask(g, base);
        if (!r) return r;
        base = *r;
    }
    return base;
}

struct Workshop::Pending {
    std::size_t                        script = 0;
    std::string                        source;
    Rules                              base;
    std::function<void(Applied)>       done;
    std::future<lang::Result<void>>    run;
    clock_type::time_point             began;
    [[nodiscard]] double elapsed() const { return std::chrono::duration<double, std::milli>(clock_type::now() - began).count(); }
};

void Workshop::apply(std::size_t script, std::string source, Rules base, std::function<void(Applied)> done) {
    auto p = std::make_shared<Pending>();
    p->began = clock_type::now();
    if (script >= scripts_.size()) {
        done({std::unexpected(std::string("no such script")), 0});
        return;
    }
    auto run = impl_->run(script, scripts_[script], source, true);
    if (!run) {
        done({std::unexpected(run.error()), p->elapsed()});
        return;
    }
    p->script = script;
    p->source = std::move(source);
    p->base = base;
    p->done = std::move(done);
    p->run = std::move(*run);
    look(std::move(p));
}

void Workshop::look(std::shared_ptr<Pending> p) {
    // On the runtime's thread until it ran (the script thread meanwhile asks the runtime for what it needs).
    if (p->run.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        impl_->runtime.after(seconds(poll), [self = shared_from_this(), p] { self->look(p); });
        return;
    }
    if (auto ran = p->run.get(); !ran) {
        p->done({std::unexpected(ran.error().text()), p->elapsed()});
        return;
    }
    auto rules = impl_->ask(scripts_[p->script], p->base);
    if (rules) scripts_[p->script].source = p->source;
    p->done({std::move(rules), p->elapsed()});
}

std::optional<double> Workshop::tuned(std::string_view source) {
    for (auto line : source | std::views::split('\n')) {
        const std::string_view l(line.begin(), line.end());
        if (l.find("← tune") == std::string_view::npos) continue;
        const auto eq = l.find('=');
        if (eq == std::string_view::npos) return std::nullopt;
        std::string_view rest = l.substr(eq + 1);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        double v = 0;
        const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), v);
        if (ec != std::errc{}) return std::nullopt;
        return v;
    }
    return std::nullopt;
}

std::string Workshop::retune(std::string_view source, double value) {
    std::string out;
    bool first = true;
    for (auto line : source | std::views::split('\n')) {
        std::string l(line.begin(), line.end());
        if (const auto eq = l.find('='); l.find("← tune") != std::string::npos && eq != std::string::npos) {
            const auto from = l.find_first_not_of(' ', eq + 1);
            auto to = from;
            while (to < l.size() && (std::isdigit(static_cast<unsigned char>(l[to])) || l[to] == '.' || l[to] == '-')) ++to;
            if (from != std::string::npos) l.replace(from, to - from, std::format("{}", value));
        }
        if (!first) out += '\n';
        out += l;
        first = false;
    }
    return out;
}

}  // namespace omui::breakout
