// The site step (moved from tools/ci/site-browser.sh, D-206): the built site (target/site, from
// `omui-devtools site build`) in a real browser, served as Pages serves it — its delivery against
// its budget (site/delivery.budget.xdef.json: a cold visit over the budget's connection, then the
// same without the HTTP cache), what assistive technology reads, its structure and handoff,
// first-frame fonts, the component catalog, the release and reaction workspaces, motion and the
// reference pages, the first views, the opening assets and loading, shared font delivery. Each
// report under target/, a line for each of its checks; the step's status 1 when one failed.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.browser;
import omui.devtools.cdp;
import :pages;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::dig, cdp::number, cdp::text;

void write_report(const fs::path& path, const json& report) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream(path, std::ios::binary) << cdp::pretty(report) << '\n';
}

// Python's str() of a JSON value, as the script's summaries printed it.
std::string said(const json& value) {
    if (value.is_null())
        return "None";
    if (value.is_boolean())
        return value.get<bool>() ? "True" : "False";
    if (value.is_number_float()) {
        const double number = value.get<double>();
        return number == std::floor(number) && std::abs(number) < 1e16
                   ? std::format("{:.1f}", number)
                   : std::format("{}", number);
    }
    if (value.is_string())
        return value.get<std::string>();
    return value.dump();
}

std::string fixed(const json& value, int digits) {
    if (!value.is_number())
        return said(value);
    return std::format("{:.{}f}", value.get<double>(), digits);
}

void print_budgets(const json& report) {
    for (const auto& budget : at(report, "budgets"))
        std::cout << std::format("  {} {}: {} (budget {})\n",
                                 cdp::truthy(at(budget, "pass")) ? "ok  " : "OVER",
                                 text(at(budget, "metric")), said(at(budget, "value")),
                                 said(at(budget, "max")));
}

void print_checks(std::string_view heading, const json& report) {
    std::cout << heading << '\n';
    for (const auto& check : at(report, "checks"))
        std::cout << std::format("  {} {}\n", cdp::truthy(at(check, "ok")) ? "ok  " : "FAIL",
                                 text(at(check, "what")));
}

}  // namespace

const std::vector<std::string>& site_check_names() {
    static const std::vector<std::string> names{"perf",      "a11y",         "structure",
                                                "fonts",     "catalog",      "workspace",
                                                "reactions", "motion",       "presentation",
                                                "loading",   "font-sharing", "transition"};
    return names;
}

int site(const fs::path& root, const fs::path& built, std::vector<std::string> names) {
    const auto chrome = browser::chromium();
    if (!chrome)
        return failed("no headless Chromium for the site's delivery");
    const fs::path site_dir =
        built.empty() ? root / "target/site" : (built.is_absolute() ? built : root / built);
    if (!fs::is_regular_file(site_dir / "index.html"))
        return failed(std::format("no {} (omui-devtools site build first)",
                                  mcpplibs::os::fs::utf8(site_dir / "index.html")));
    // Every check but transition (its own early-motion coverage study) unless named.
    if (names.empty())
        for (const auto& name : site_check_names())
            if (name != "transition")
                names.push_back(name);
    for (const auto& name : names)
        if (!std::ranges::contains(site_check_names(), name)) {
            std::cerr << std::format("omui-devtools check site: no check {} (", name);
            for (const auto& known : site_check_names())
                std::cerr << known << (known == site_check_names().back() ? ")\n" : " ");
            return 2;
        }
    const pages::Context context{.root = root, .site = site_dir, .chrome = *chrome};
    const fs::path target = root / "target";
    int status = 0;
    for (const auto& name : names) {
        if (name == "perf") {
            const fs::path budget = root / "site/delivery.budget.xdef.json";
            const auto budgets = json::parse(mcpplibs::os::fs::read(budget).value_or("{}"), nullptr, false);
            const std::string network = text(at(budgets, "network"));
            const json cached =
                pages::perf(context,
                            {.network = network,
                             .budget = budget,
                             .request_log = target / "site-perf-requests.report.xdef.json"});
            write_report(target / "site-perf.json", cached);
            const json& frames = at(cached, "frames");
            std::cout << std::format("site delivery: {} MB, first frame {} ms, ready {} ms; frames "
                                     "p50 {} p95 {} max {} ms, {} long\n",
                                     at(dig(cached, "transfer"), "total").is_number()
                                         ? std::format("{:.2f}",
                                                       number(dig(cached, "transfer", "total")) /
                                                           1e6)
                                         : "None",
                                     fixed(at(cached, "first_frame_ms"), 0),
                                     fixed(at(cached, "ready_ms"), 0), fixed(at(frames, "p50"), 1),
                                     fixed(at(frames, "p95"), 1), fixed(at(frames, "max"), 1),
                                     said(at(frames, "long")));
            print_budgets(cached);
            if (!cdp::truthy(at(cached, "ok"))) {
                std::cout
                    << "::error::the site's delivery is over its budget (target/site-perf.json)\n";
                status = 1;
            }
            const json uncached =
                pages::perf(context, {.network = network,
                                      .budget = budget,
                                      .cache = false,
                                      .request_log =
                                          target / "site-perf-no-cache-requests.report.xdef.json"});
            write_report(target / "site-perf-no-cache.report.xdef.json", uncached);
            std::cout
                << std::format("site delivery without HTTP cache: {} bytes, first frame {} ms ({})\n",
                               said(dig(uncached, "transfer", "total")),
                               fixed(at(uncached, "first_frame_ms"), 0), text(at(uncached, "browser")));
            print_budgets(uncached);
            if (!cdp::truthy(at(uncached, "ok"))) {
                std::cout << "::error::delivery without HTTP cache is over the same budget\n";
                status = 1;
            }
            continue;
        }
        struct Step {
            std::string_view name;
            json (*run)(const pages::Context&);
            std::string_view report, heading, error;
        };
        static const std::array<Step, 11> steps{{
            {"a11y", pages::a11y, "site-a11y.json", "",
             "the browser's accessibility tree or the keyboard's way in is wrong "
             "(target/site-a11y.json)"},
            {"structure", pages::structure, "site-structure.report.xdef.json",
             "site structure and handoff:",
             "site routing or static handoff failed (target/site-structure.report.xdef.json)"},
            {"fonts", pages::fonts, "site-fonts.report.xdef.json",
             "site first-frame font coverage:",
             "first-frame glyph coverage or font handoff failed "
             "(target/site-fonts.report.xdef.json)"},
            {"catalog", pages::catalog, "site-catalog.report.xdef.json", "site component catalog:",
             "component catalog browser checks failed (target/site-catalog.report.xdef.json)"},
            {"workspace", pages::workspace, "site-workspace.report.xdef.json",
             "site release workspace:",
             "release workspace checks failed (target/site-workspace.report.xdef.json)"},
            {"reactions", pages::reactions, "site-reactions.report.xdef.json",
             "site reaction workspace:",
             "reaction workspace checks failed (target/site-reactions.report.xdef.json)"},
            {"motion", pages::motion, "site-motion.report.xdef.json",
             "site motion, the reference pages and Agent native:",
             "motion and creative browser checks failed (target/site-motion.report.xdef.json)"},
            {"presentation", pages::presentation, "site-presentation.report.xdef.json",
             "site first views, mobile comparison and Agent handoff:",
             "site progressive presentation checks failed"},
            {"loading", pages::loading, "site-loading.report.xdef.json",
             "site opening assets and loading:",
             "opening pixels, progress or retry failed (target/site-loading.report.xdef.json)"},
            {"font-sharing", pages::font_sharing, "site-font-sharing.report.xdef.json",
             "site shared font delivery:",
             "static and runtime fonts fetched twice or failed shared ownership/retry "
             "(target/site-font-sharing.report.xdef.json)"},
            {"transition", pages::transition, "site-transition.report.xdef.json",
             "site transitions: early painted input on moving primaries:",
             "transition browser checks failed (target/site-transition.report.xdef.json)"},
        }};
        const auto step = std::ranges::find(steps, std::string_view(name), &Step::name);
        const json report = step->run(context);
        write_report(target / step->report, report);
        if (name == "a11y")
            print_checks(std::format("site accessibility ({} nodes in the browser's tree):",
                                     said(dig(report, "tree", "nodes"))),
                         report);
        else
            print_checks(step->heading, report);
        if (!cdp::truthy(at(report, "ok"))) {
            std::cout << "::error::" << step->error << '\n';
            status = 1;
        }
        std::cout << std::flush;
    }
    return status;
}

}  // namespace omui::devtools::check
