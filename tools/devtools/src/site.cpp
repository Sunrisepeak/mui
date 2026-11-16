module omui.devtools.site;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.run;

namespace omui::devtools::site {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using cdp::json;

namespace {

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool write_file(const fs::path& path, std::string_view bytes) {
    std::error_code error;
    if (path.has_parent_path())
        fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    return static_cast<bool>(out);
}

int failed(std::string_view message) {
    std::cout << "::error::" << message << '\n' << std::flush;
    return 1;
}

double mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / 1048576.0;
}

std::string last_lines(std::string_view text, std::size_t count) {
    std::vector<std::string_view> lines;
    for (auto part : std::views::split(text, '\n'))
        lines.emplace_back(part.begin(), part.end());
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    std::string out;
    for (std::size_t index = lines.size() > count ? lines.size() - count : 0; index < lines.size();
         ++index)
        out += std::string(lines[index]) + "\n";
    return out;
}

// `mcpp ARGS…` at the root within the machine's share, its output shown.
bool mcpp_shown(const fs::path& root, std::vector<std::string> args) {
    const auto done =
        run::program(root, "mcpp", std::move(args),
                     {.shown = true,
                      .variables = {{"MCPP_JOBS", std::to_string(run::share_of_machine())}}});
    return done && done->status == 0;
}

// A page script's String(value), as console.log joins its arguments.
std::string shown(const json& argument) {
    const json& value = cdp::at(argument, "value");
    if (!value.is_null()) {
        if (value.is_string())
            return value.get<std::string>();
        if (value.is_number())
            return cdp::js_number(value.get<double>());
        if (value.is_boolean())
            return value.get<bool>() ? "true" : "false";
        if (value.is_object())
            return "[object Object]";
        return value.dump();
    }
    return cdp::text(cdp::at(argument, "description"));
}

}  // namespace

std::optional<fs::path> newest(std::vector<fs::path> paths) {
    if (paths.empty())
        return std::nullopt;
    // Version order: runs of digits compared as numbers, the rest as text.
    const auto parts = [](const std::string& text) {
        std::vector<std::pair<bool, std::string>> out;
        for (std::size_t at = 0; at < text.size();) {
            const bool digits = std::isdigit(static_cast<unsigned char>(text[at])) != 0;
            std::size_t end = at;
            while (end < text.size() &&
                   (std::isdigit(static_cast<unsigned char>(text[end])) != 0) == digits)
                ++end;
            out.emplace_back(digits, text.substr(at, end - at));
            at = end;
        }
        return out;
    };
    const auto less = [&](const fs::path& left, const fs::path& right) {
        const auto a = parts(os::fs::utf8(left)), b = parts(os::fs::utf8(right));
        for (std::size_t index = 0; index < std::min(a.size(), b.size()); ++index) {
            if (a[index] == b[index])
                continue;
            if (a[index].first && b[index].first) {
                const std::string x = a[index].second.substr(
                    std::min(a[index].second.find_first_not_of('0'), a[index].second.size()));
                const std::string y = b[index].second.substr(
                    std::min(b[index].second.find_first_not_of('0'), b[index].second.size()));
                if (x.size() != y.size())
                    return x.size() < y.size();
                if (x != y)
                    return x < y;
                continue;
            }
            return a[index].second < b[index].second;
        }
        return a.size() < b.size();
    };
    return *std::ranges::max_element(paths, less);
}

std::expected<std::string, std::string>
with_decoded_size(std::string_view page, std::string_view wasm, std::uint64_t size) {
    const std::string key = cdp::q(wasm) + ":";
    std::size_t found = std::string_view::npos, count = 0;
    for (std::size_t at = page.find(key); at != std::string_view::npos;
         at = page.find(key, at + 1)) {
        std::size_t digits = at + key.size();
        while (digits < page.size() && std::isdigit(static_cast<unsigned char>(page[digits])))
            ++digits;
        if (digits == at + key.size())
            continue;
        if (count++ == 0)
            found = at;
    }
    if (count != 1)
        return std::unexpected(
            std::format("site-split: the page states no decoded size for {}", wasm));
    std::size_t digits = found + key.size();
    while (digits < page.size() && std::isdigit(static_cast<unsigned char>(page[digits])))
        ++digits;
    return std::string(page.substr(0, found)) + key + std::to_string(size) +
           std::string(page.substr(digits));
}

int verify(const fs::path& out) {
    const auto page = read_file(out / "index.html");
    if (!page)
        return failed(std::format("the site has no {}", os::fs::utf8(out / "index.html")));
    const bool require = os::env::get("OMUI_REQUIRE_BROWSER") == "1";
    const std::vector<std::string> need{require ? "var omuiDeferred = {" : "var omuiDeferred",
                                        "<main id=\"omui-static\"",
                                        "<section id=\"/home\"",
                                        "<section id=\"/docs/features\"",
                                        "aria-label=\"One interface. Every perspective.\"",
                                        "window.omuiHasPresented",
                                        "@font-face",
                                        "<canvas id=\"canvas\"",
                                        "<link rel=\"openmui-agent\" href=\"agent.md\">",
                                        "id=\"omui-first-frames\""};
    std::vector<std::string> missing;
    for (const auto& wanted : need)
        if (!page->contains(wanted))
            missing.push_back(wanted);
    if (!missing.empty())
        return failed(std::format("the page lacks {}", cdp::stringify(json(missing))));
    for (const fs::path file :
         {fs::path("agent.md"), fs::path(".well-known") / "openmui-agent.json",
          fs::path("first-frames.assets.omui.json")})
        if (!fs::is_regular_file(out / file))
            return failed(std::format("the site has no {}", os::fs::utf8(file)));
    if (page->contains("toy-sdk"))
        return failed("the site loads Toy's SDK");
    std::vector<std::pair<std::uint64_t, fs::path>> files;
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(out, error))
        if (entry.is_regular_file(error))
            files.emplace_back(entry.file_size(error), entry.path());
    std::uint64_t total = 0;
    for (const auto& [size, _] : files)
        total += size;
    std::cout << std::format("site: index.html {} bytes; {} files, {:.2f} MiB\n", page->size(),
                             files.size(), mib(total));
    std::ranges::stable_sort(files, std::greater<>{}, [](const auto& file) { return file.first; });
    for (std::size_t index = 0; index < std::min<std::size_t>(4, files.size()); ++index)
        std::cout << std::format("  {:>8.2f} MiB  {}\n", mib(files[index].first),
                                 os::fs::utf8(fs::relative(files[index].second, out, error)));
    return 0;
}

int build(const fs::path& root) {
    const fs::path out = root / "target/site";
    std::error_code error;
    fs::create_directories(root / "target", error);
    const auto packed =
        run::program(root, "mcpp",
                     {"pack", "-p", "omui.site", "--target", "wasm32-emscripten", "--profile",
                      "site-web", "--pkg-format", "dist-toy", "--message-format", "json"},
                     {.variables = {{"MCPP_JOBS", std::to_string(run::share_of_machine())}}});
    if (packed)
        write_file(root / "target/site.pack.json", packed->out);
    if (!packed || packed->status != 0)
        return failed("omui.site did not pack for the Web");
    // The envelope: data.artifacts, the dist-toy one's path (its index.html).
    fs::path index;
    const auto envelope = json::parse(packed->out, nullptr, false);
    for (const auto& artifact : cdp::dig(envelope, "data", "artifacts"))
        if (cdp::text(cdp::at(artifact, "format")) == "dist-toy") {
            index = os::fs::from_utf8(cdp::text(cdp::at(artifact, "path")));
            break;
        }
    if (index.empty())
        return failed("the pack's envelope (target/site.pack.json) names no dist-toy artifact");
    fs::remove_all(out, error);
    fs::copy(index.parent_path(), out, fs::copy_options::recursive, error);
    if (error)
        return failed(std::format("the package did not copy to target/site: {}", error.message()));
    const std::string site = os::fs::utf8(out);
    // The actual native program draws each opening: the pictures and geometry the page shows while
    // it loads, by their contents' hash, with no local paths in them; hidden pages have no image
    // src to prefetch.
    if (!mcpp_shown(root, {"run", "omui-site", "-p", "omui.site", "--", "--openings", site}))
        return failed("the site's openings were not exported");
    if (!mcpp_shown(root, {"run", "omui-site", "-p", "omui.site", "--", "--prerender",
                           site + "/index.html", "--opening-assets",
                           site + "/first-frames.assets.omui.json"}))
        return failed("the site's places were not pre-rendered");
    // What an agent needs to operate it (agent connect §5.2), as every openmui application writes
    // it.
    if (!mcpp_shown(root, {"run", "omui-site", "-p", "omui.site", "--", "--agent-guide", site}))
        return failed("the site's agent guide was not written");
    // The program cut in two by what its first screen runs (D5).
    if (const int status = split(root, out); status != 0)
        return status;
    return verify(out);
}

int split(const fs::path& root, const fs::path& given) {
    const fs::path site = given.is_absolute() ? given : root / given;
    const fs::path work = root / "target/site-split";
    const bool require = os::env::get("OMUI_REQUIRE_BROWSER") == "1";
    const auto give_up = [&](std::string_view why) {
        if (require)
            return failed(std::format("site-split: {}", why));
        std::cout << "::notice::site-split: " << why << "; the program stays whole\n";
        return 0;
    };
    std::vector<fs::path> splitters;
    const fs::path home = os::fs::from_utf8(os::env::get("HOME").value_or(""));
    std::error_code error;
    for (const fs::path packages :
         {home / ".mcpp/registry/data/xpkgs/xim-x-emsdk", home / ".xlings/data/xpkgs/xim-x-emsdk"})
        for (const auto& entry : fs::directory_iterator(packages, error))
            if (fs::exists(entry.path() / "bin/wasm-split"))
                splitters.push_back(entry.path() / "bin/wasm-split");
    const auto splitter = newest(splitters);
    if (!splitter)
        return give_up("no wasm-split (Binaryen, from the emsdk mcpp installs)");
    if (!browser::chromium())
        return give_up("no headless Chromium to profile the first screen in");
    const std::string wasm_split = os::fs::utf8(*splitter);
    // What Emscripten's wasm32 output uses: wasm-split must read and write the same features (and
    // none newer, as compact imports, that browsers reject).
    const std::vector<std::string> features{"--enable-bulk-memory",
                                            "--enable-bulk-memory-opt",
                                            "--enable-nontrapping-float-to-int",
                                            "--enable-sign-ext",
                                            "--enable-mutable-globals",
                                            "--enable-multivalue",
                                            "--enable-reference-types",
                                            "--enable-call-indirect-overlong"};
    const auto splitting = [&](std::vector<std::string> args, std::string_view log,
                               std::string_view what) {
        std::vector<std::string> all = features;
        all.insert(all.end(), args.begin(), args.end());
        const auto done = run::program(root, wasm_split, all, {.merged = true});
        write_file(work / log, done ? done->out : done.error());
        if (done && done->status == 0)
            return true;
        std::cout << last_lines(done ? done->out : done.error(), 5);
        failed(std::format("site-split: wasm-split could not {}", what));
        return false;
    };

    fs::remove_all(work, error);
    fs::create_directories(work, error);
    const auto page = read_file(site / "index.html");
    if (!page)
        return failed(
            std::format("site-split: {} does not exist", os::fs::utf8(site / "index.html")));
    static const std::regex named(R"re(var omuiWasm = "([^"]+)";)re");
    std::smatch found;
    if (!std::regex_search(*page, found, named))
        return failed(std::format("site-split: {} names no program (var omuiWasm)",
                                  os::fs::utf8(site / "index.html")));
    const std::string wasm = found[1].str();
    if (!page->contains("var omuiDeferred = null;"))
        return failed(
            std::format("site-split: {} is split already, or its loader predates the split",
                        os::fs::utf8(site / "index.html")));
    const std::string whole = os::fs::utf8(work / "whole.wasm"),
                      instrumented = os::fs::utf8(work / "instrumented.wasm");
    fs::copy_file(site / wasm, work / "whole.wasm", fs::copy_options::overwrite_existing, error);
    if (!splitting({"--instrument", whole, "-o", instrumented}, "instrument.log",
                   "instrument the program"))
        return 1;

    fs::copy(site, work / "page", fs::copy_options::recursive, error);
    fs::copy_file(work / "instrumented.wasm", work / "page" / wasm,
                  fs::copy_options::overwrite_existing, error);
    const auto edited = with_decoded_size(read_file(work / "page/index.html").value_or(""), wasm,
                                          fs::file_size(work / "instrumented.wasm", error));
    if (!edited) {
        std::cerr << edited.error() << '\n';
        return 1;
    }
    write_file(work / "page/index.html", *edited);
    // The first screen in each language the page speaks (the Chinese home draws with its own face,
    // through code the English one never runs), merged.
    if (profile({.site = work / "page", .out = work / "en.profile"}) != 0)
        return 1;
    if (profile({.site = work / "page", .out = work / "zh.profile", .route = "/zh"}) != 0)
        return 1;
    if (!splitting({"--merge-profiles", os::fs::utf8(work / "en.profile"),
                    os::fs::utf8(work / "zh.profile"), "-o",
                    os::fs::utf8(work / "first-screen.profile")},
                   "merge.log", "merge the profiles"))
        return 1;
    if (!splitting({"--split", "--profile=" + os::fs::utf8(work / "first-screen.profile"),
                    "--export-prefix=%", "--placeholder-namespace=placeholder", whole, "-o1",
                    os::fs::utf8(work / "first.wasm"), "-o2", os::fs::utf8(work / "deferred.wasm")},
                   "split.log", "split the program"))
        return 1;
    if (!mcpp_shown(root, {"run", "omui-gen", "-p", "omui.gen", "--", "dist-toy-split",
                           os::fs::utf8(site), "--primary", os::fs::utf8(work / "first.wasm"),
                           "--deferred", os::fs::utf8(work / "deferred.wasm"), "--lazy", ""}))
        return 1;
    for (const std::string name : {"whole.wasm", "first.wasm", "deferred.wasm"}) {
        const std::string data = read_file(work / name).value_or("");
        std::cout << std::format("site-split: {:<14} {:6.2f} MiB, gzip {:5.2f} MiB\n", name,
                                 mib(data.size()), mib(cdp::gzip(data, 9).size()));
    }
    fs::remove_all(work / "page", error);
    return 0;
}

int profile(const Profile& asked) {
    const auto chrome = browser::chromium();
    if (!chrome) {
        std::cerr << "usage: omui-devtools site profile DIR PROFILE [--route R] [--idle MS] (no "
                     "headless Chromium: OMUI_CHROME)\n";
        return 2;
    }
    auto server = cdp::Server::start(cdp::pages(asked.site));
    if (!server) {
        std::cerr << "site-profile: " << server.error() << '\n';
        return 1;
    }
    auto launched = cdp::Browser::launch(*chrome, {.width = 1280, .height = 800});
    if (!launched) {
        std::cerr << "site-profile: " << launched.error() << '\n';
        return 1;
    }
    auto& page = **launched;
    std::vector<std::string> errors;
    page.on([&](const std::string& method, const json& params) {
        if (method != "Runtime.exceptionThrown")
            return;
        const json& details = cdp::at(params, "exceptionDetails");
        const json& description = cdp::dig(details, "exception", "description");
        errors.push_back(description.is_null() ? cdp::text(cdp::at(details, "text"))
                                               : cdp::text(description));
    });
    const auto fail = [&](std::string message) {
        std::cerr << message << '\n';
        page.close();
        return 1;
    };
    // The instance that carries the profile: the loader's own instantiation.
    if (!page.call("Page.addScriptToEvaluateOnNewDocument", {{"source", R"((() => {
  const keep = (result) => { const instance = result.instance || result; if (instance.exports && instance.exports.__write_profile) window.omuiProfiled = instance; return result; };
  const instantiate = WebAssembly.instantiate, streaming = WebAssembly.instantiateStreaming;
  WebAssembly.instantiate = function (...a) { return instantiate.apply(this, a).then(keep); };
  if (streaming) WebAssembly.instantiateStreaming = function (...a) { return streaming.apply(this, a).then(keep); };
})();)"}}))
        return fail("site-profile: the page did not answer");
    if (const auto navigated =
            page.call("Page.navigate",
                      {{"url", (*server)->url() + "index.html" +
                                   (asked.route.empty() ? "" : "#" + asked.route)}});
        !navigated)
        return fail("site-profile: " + navigated.error());
    bool presented = false;
    for (int attempt = 0; attempt < 600 && !presented; ++attempt) {
        const auto seen = page.value("!!(window.omuiProfiled && window.omuiHasPresented)");
        if (!seen)
            return fail("site-profile: " + seen.error());
        presented = cdp::truthy(*seen);
        if (!presented)
            page.pump(std::chrono::milliseconds(100));
    }
    if (!presented)
        return fail(
            std::format("site-profile: the instrumented page never presented its first frame{}",
                        errors.empty() ? "" : ": " + errors.front()));
    const auto mouse = [&](std::string type, double x, double y, json extra = json::object()) {
        json params = {{"type", std::move(type)}, {"x", x}, {"y", y}};
        params.update(extra);
        return page.call("Input.dispatchMouseEvent", std::move(params)).has_value();
    };
    bool answering = true;
    // The pointer across the first screen, slowly: hover states, Mui turning to it.
    for (int step = 0; step <= 24 && answering; ++step) {
        answering = mouse("mouseMoved", 80 + step * 46, 120 + (step % 6) * 100);
        page.pump(std::chrono::milliseconds(60));
    }
    page.pump(std::chrono::milliseconds(1200));
    // Down the page to its end, a wheel's notch at a time, and back up.
    for (int notch = 0; notch < 40 && answering; ++notch) {
        answering = mouse("mouseWheel", 640, 400, {{"deltaX", 0}, {"deltaY", 240}});
        page.pump(std::chrono::milliseconds(80));
    }
    page.pump(std::chrono::milliseconds(1000));
    for (int notch = 0; notch < 40 && answering; ++notch) {
        answering = mouse("mouseWheel", 640, 400, {{"deltaX", 0}, {"deltaY", -240}});
        page.pump(std::chrono::milliseconds(50));
    }
    page.pump(std::chrono::milliseconds(asked.idle_ms));
    const auto encoded = page.value(R"((() => {
  const exports = window.omuiProfiled.exports;
  const memory = Object.values(exports).find((v) => v instanceof WebAssembly.Memory);
  const pages = 64, at = memory.grow(pages) * 65536;
  const length = exports.__write_profile(at, pages * 65536);
  const bytes = new Uint8Array(memory.buffer, at, length);
  let text = ''; for (const byte of bytes) text += String.fromCharCode(byte);
  return btoa(text);
})())");
    page.close();
    (*server)->close();
    if (!encoded) {
        std::cerr << "site-profile: " << encoded.error() << '\n';
        return 1;
    }
    if (!cdp::truthy(*encoded)) {
        std::cerr << "site-profile: no profile written\n";
        return 1;
    }
    const std::string bytes = cdp::from_base64(cdp::text(*encoded));
    write_file(asked.out, bytes);
    std::cout << std::format("site-profile: {} bytes{}{}\n", bytes.size(),
                             asked.route.empty() ? "" : " (route " + asked.route + ")",
                             errors.empty() ? ""
                                            : std::format("; {} page exception(s): {}",
                                                          errors.size(), errors.front()));
    return errors.empty() ? 0 : 1;
}

int drive(const Drive& asked) {
    const auto chrome = browser::chromium();
    if (!chrome) {
        std::cerr << "usage: omui-devtools site drive URL OUT.png [SECONDS] [WIDTH] [HEIGHT] "
                     "[SCRIPT] (no headless Chromium: OMUI_CHROME)\n";
        return 2;
    }
    // --flag instead of the default --disable-gpu: a page on this machine's GPU, e.g.
    // --flag=--enable-unsafe-webgpu
    // --flag=--enable-features=Vulkan --flag=--use-angle=vulkan --flag=--ignore-gpu-blocklist.
    const std::vector<std::string> flags =
        asked.flags.empty() ? std::vector<std::string>{"--disable-gpu"} : asked.flags;
    auto launched =
        cdp::Browser::launch(*chrome,
                             {.width = asked.width, .height = asked.height, .flags = flags});
    if (!launched) {
        std::cerr << "omui-devtools site drive: " << launched.error() << '\n';
        return 1;
    }
    auto& page = **launched;
    std::vector<std::string> logs;
    page.on([&](const std::string& method, const json& params) {
        if (method == "Runtime.consoleAPICalled") {
            std::string line = cdp::text(cdp::at(params, "type")) + " ";
            bool first = true;
            for (const auto& argument : cdp::at(params, "args")) {
                line += (first ? "" : " ") + shown(argument);
                first = false;
            }
            logs.push_back(std::move(line));
        }
        if (method == "Runtime.exceptionThrown")
            logs.push_back("EXC " +
                           cdp::stringify(cdp::at(params, "exceptionDetails")).substr(0, 300));
    });
    static_cast<void>(page.call("Runtime.enable"));
    static_cast<void>(page.call("Emulation.setDeviceMetricsOverride", {{"width", asked.width},
                                                                       {"height", asked.height},
                                                                       {"deviceScaleFactor", 1},
                                                                       {"mobile", false}}));
    static_cast<void>(page.call("Page.navigate", {{"url", asked.url}}));
    page.pump(std::chrono::milliseconds(static_cast<long long>(asked.seconds * 1000)));
    // --click X,Y: a person's click there first (trusted, as DevTools gives it): what lets a page
    // start its sound.
    if (asked.click) {
        for (const std::string type : {"mousePressed", "mouseReleased"})
            static_cast<void>(page.call("Input.dispatchMouseEvent", {{"type", type},
                                                                     {"x", asked.click->first},
                                                                     {"y", asked.click->second},
                                                                     {"button", "left"},
                                                                     {"clickCount", 1}}));
        page.pump(std::chrono::milliseconds(1500));
    }
    if (!asked.script.empty()) {
        const auto answer = page.call("Runtime.evaluate", {{"expression", asked.script},
                                                           {"awaitPromise", true},
                                                           {"returnByValue", true}});
        if (answer) {
            const json& value = cdp::dig(*answer, "result", "result", "value");
            logs.push_back("EVAL " +
                           cdp::stringify(value.is_null() ? cdp::at(*answer, "result") : value)
                               .substr(0, 2000));
        }
        page.pump(std::chrono::milliseconds(3000));
    }
    const auto shot = page.call("Page.captureScreenshot", {{"format", "png"}});
    if (shot)
        cdp::write_base64(asked.out, cdp::text(cdp::dig(*shot, "result", "data")));
    for (std::size_t index = 0; index < logs.size(); ++index)
        std::cout << (index ? "\n" : "") << logs[index];
    std::cout << '\n';
    page.close();
    return 0;
}

}  // namespace omui::devtools::site
