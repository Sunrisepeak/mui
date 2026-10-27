module openmui.cts;

import std;
import nlohmann.json;
import mcpplibs.os;

namespace omui::cts {

namespace os = mcpplibs::os;

namespace {

std::optional<std::string> read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string show(const Json& v) {
    std::string s = v.dump();
    return s.size() > 400 ? s.substr(0, 400) + "…" : s;
}

const Json* recorded(const Json& record) {
    for (const char* k : {"act", "invoke", "get", "rows", "native", "theme", "themes", "error"})
        if (record.contains(k)) return &record[k];
    return nullptr;
}

}  // namespace

std::filesystem::path directory() {
    // tests/cts/src/cts.cpp → the workspace → spec/conformance/S.
    const std::filesystem::path here = os::fs::from_utf8(std::source_location::current().file_name());
    return here.parent_path().parent_path().parent_path().parent_path() / "spec" / "conformance" / "S";
}

std::expected<std::vector<Scenario>, std::string> scenarios() {
    std::vector<Scenario> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(directory(), ec)) {
        const std::string name = os::fs::utf8(e.path().filename());
        constexpr std::string_view suffix = ".scenario.omui.json";
        if (!name.ends_with(suffix)) continue;
        auto text = read(e.path());
        if (!text) return std::unexpected(std::format("cannot read {}", os::fs::utf8(e.path())));
        Json data = Json::parse(*text, nullptr, false);
        if (data.is_discarded() || !data.contains("scenario")) return std::unexpected(std::format("{} is not a scenario", name));
        out.push_back({name.substr(0, name.size() - suffix.size()), e.path(), std::move(data)});
    }
    if (ec) return std::unexpected(std::format("cannot list {}: {}", os::fs::utf8(directory()), ec.message()));
    std::ranges::sort(out, {}, &Scenario::name);
    return out;
}

Json run(const Scenario& scenario, const std::function<Json(std::string_view, const Json&)>& invoke, const std::function<void()>& settle) {
    Json trace = Json::array();
    const Json loaded = invoke("cts.load", scenario.data);
    settle();
    if (loaded.is_object() && loaded.contains("runner-error")) {
        trace.push_back({{"step", -1}, {"runner-error", loaded["runner-error"]}});
        return trace;
    }
    const std::size_t steps = scenario.data["scenario"]["steps"].size();
    for (std::size_t i = 0; i < steps; ++i) {
        Json r = invoke("cts.step", Json{{"step", i}});
        settle();
        if (!r.is_null()) trace.push_back(std::move(r));
    }
    return trace;
}

Json canonical(const Json& v) {
    if (v.is_object()) {
        if (v.empty()) return Json::array();
        Json out = Json::object();
        for (auto it = v.begin(); it != v.end(); ++it)
            if (it.key() != "bounds") out[it.key()] = canonical(it.value());
        return out;
    }
    if (v.is_array()) {
        Json out = Json::array();
        for (const Json& x : v) out.push_back(canonical(x));
        return out;
    }
    return v;
}

std::vector<std::string> check(const Scenario& scenario, const Json& raw) {
    std::vector<std::string> out;
    const Json trace = canonical(raw);
    // The steps' own expectations.
    std::map<std::int64_t, const Json*> by_step;
    for (const Json& r : trace)
        if (r.contains("step")) by_step[r["step"].get<std::int64_t>()] = &r;
    const Json& steps = scenario.data["scenario"]["steps"];
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Json& st = steps[i];
        if (!st.contains("expect")) continue;
        auto it = by_step.find(static_cast<std::int64_t>(i));
        const Json* r = it == by_step.end() ? nullptr : it->second;
        if (st.contains("tree")) {
            std::map<std::string, const Json*> nodes;
            if (r && r->contains("tree"))
                for (const Json& n : (*r)["tree"]) nodes[n.value("id", "")] = &n;
            for (auto e = st["expect"].begin(); e != st["expect"].end(); ++e)
                for (auto f = e.value().begin(); f != e.value().end(); ++f) {
                    auto n = nodes.find(e.key());
                    const Json got = n != nodes.end() && n->second->contains(f.key()) ? (*n->second)[f.key()] : Json(nullptr);
                    if (got != f.value())
                        out.push_back(std::format("{}: step {}: {}.{} is {}, not {}", scenario.name, i, e.key(), f.key(), show(got), show(f.value())));
                }
        } else {
            const Json* got = r ? recorded(*r) : nullptr;
            if (!got || *got != st["expect"])
                out.push_back(std::format("{}: step {}: {}, not {}", scenario.name, i, got ? show(*got) : "nothing", show(st["expect"])));
        }
    }
    // The pinned trace.
    std::filesystem::path pinned = scenario.file.parent_path() / (scenario.name + ".trace.omui.json");
    if (os::env::get("OMUI_CTS_WRITE") == "1") {
        std::ofstream f(pinned, std::ios::binary);
        f << Json{{"omui", "0.1"}, {"trace", trace}}.dump(1, ' ', false) << "\n";
        if (!f) out.push_back(std::format("cannot write {}", os::fs::utf8(pinned)));
        return out;
    }
    auto text = read(pinned);
    const Json file = text ? Json::parse(*text, nullptr, false) : Json();
    if (!text || file.is_discarded() || !file.contains("trace")) {
        out.push_back(std::format("{}: no pinned trace ({}; OMUI_CTS_WRITE=1 writes it)", scenario.name, os::fs::utf8(pinned)));
        return out;
    }
    const Json want = canonical(file["trace"]);
    for (std::size_t i = 0; i < std::max(trace.size(), want.size()); ++i) {
        const Json got = i < trace.size() ? trace[i] : Json(nullptr);
        const Json pin = i < want.size() ? want[i] : Json(nullptr);
        if (got == pin) continue;
        const Json& at = pin.is_object() ? pin : got;
        const std::string step = at.is_object() && at.contains("step") ? at["step"].dump() : std::string("?");
        out.push_back(std::format("{}: record {} (step {}) differs from the pinned trace:\n  got  {}\n  want {}", scenario.name, i, step,
                                  show(got), show(pin)));
        break;
    }
    return out;
}

}  // namespace omui::cts
