// The browser step (moved from tools/ci/browser.sh and shaders.mjs): what only a real page can check, in headless
// Chromium where one is found (omui.devtools.browser runs each program).
module omui.devtools.check;

import std;
import mcpplibs.os;
import nlohmann.json;
import tomlplusplus;
import omui.devtools.browser;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

namespace {

// The newest of each shader partition the Web build wrote that holds WGSL or ESSL, of the workspace's current
// members only (namespace migrations leave old generated trees in incremental caches).
std::vector<fs::path> shader_partitions(const fs::path& root) {
    std::set<std::string> owners;
    try {
        const toml::table workspace = toml::parse(mcpplibs::os::fs::read(root / "mcpp.toml").value_or(""));
        if (const auto* members = workspace["workspace"]["members"].as_array())
            for (const auto& member : *members) {
                const auto path = member.value<std::string>();
                if (!path) continue;
                const toml::table manifest = toml::parse(mcpplibs::os::fs::read(root / *path / "mcpp.toml").value_or(""));
                const std::string name = manifest["package"]["name"].value_or(std::string{});
                if (name.empty()) continue;
                const std::string space = manifest["package"]["namespace"].value_or(std::string{});
                owners.insert(space.empty() ? name : space + "." + name);
            }
    } catch (const toml::parse_error&) {
        return {};
    }
    std::map<std::pair<std::string, std::string>, std::pair<fs::path, fs::file_time_type>> newest;
    std::error_code error;
    for (const auto& build : fs::directory_iterator(root / "target/.build-mcpp/out", error))
        for (const auto& owner : fs::directory_iterator(build.path(), error)) {
            const std::string package = mcpplibs::os::fs::utf8(owner.path().filename());
            if (!owners.contains(package)) continue;
            for (const auto& file : fs::directory_iterator(owner.path() / "mlibs" / "shader", error)) {
                const std::string name = mcpplibs::os::fs::utf8(file.path().filename());
                if (!name.ends_with("_shaders.cppm")) continue;
                const std::string text = mcpplibs::os::fs::read(file.path()).value_or("");
                if (!text.contains("_wgsl{") && !text.contains("_essl{")) continue;
                const auto written = fs::last_write_time(file.path(), error);
                auto& kept = newest[{package, name}];
                if (kept.first.empty() || written > kept.second) kept = {file.path(), written};
            }
        }
    std::vector<fs::path> out;
    for (const auto& [_, kept] : newest) out.push_back(kept.first);
    std::ranges::sort(out);
    return out;
}

}  // namespace

std::expected<std::string, std::string> shader_check_script(std::span<const fs::path> partitions) {
    nlohmann::json wgsl = nlohmann::json::object(), essl = nlohmann::json::object();
    for (const auto& path : partitions) {
        const std::string text = mcpplibs::os::fs::read(path).value_or("");
        const std::string file = mcpplibs::os::fs::utf8(path.stem());
        // Each entry's stage: `inline constexpr …::rhi::ShaderDesc NAME{` then `.stage = …::rhi::Stage::STAGE`.
        std::map<std::string, std::string> stages;
        for (std::size_t at = text.find("::rhi::ShaderDesc "); at != std::string::npos; at = text.find("::rhi::ShaderDesc ", at + 1)) {
            const std::size_t name_start = at + std::string_view("::rhi::ShaderDesc ").size(), brace = text.find('{', name_start);
            const std::size_t stage = text.find("::rhi::Stage::", brace);
            if (brace == std::string::npos || stage == std::string::npos || stage - brace > 80) continue;
            std::size_t stage_end = stage + std::string_view("::rhi::Stage::").size();
            const std::size_t stage_start = stage_end;
            while (stage_end < text.size() && (std::isalnum(static_cast<unsigned char>(text[stage_end])) || text[stage_end] == '_')) ++stage_end;
            stages[text.substr(name_start, brace - name_start)] = text.substr(stage_start, stage_end - stage_start);
        }
        // Each `std::array<std::byte, N> NAME_wgsl{0x…, …};` (or _essl): its bytes as text.
        for (std::size_t at = text.find("std::array<std::byte, "); at != std::string::npos; at = text.find("std::array<std::byte, ", at + 1)) {
            const std::size_t name_start = text.find("> ", at), brace = text.find('{', at), close = text.find("};", at);
            if (name_start == std::string::npos || brace == std::string::npos || close == std::string::npos || brace > close) continue;
            const std::string declared = text.substr(name_start + 2, brace - name_start - 2);
            const bool is_wgsl = declared.ends_with("_wgsl"), is_essl = declared.ends_with("_essl");
            if (!is_wgsl && !is_essl) continue;
            const std::string entry = declared.substr(0, declared.size() - 5);
            std::string code;
            for (std::size_t byte = text.find("0x", brace); byte != std::string::npos && byte < close; byte = text.find("0x", byte + 4)) {
                unsigned value = 0;
                std::from_chars(text.data() + byte + 2, text.data() + byte + 4, value, 16);
                code += static_cast<char>(value);
            }
            const std::string key = file + ":" + entry;
            if (is_wgsl) wgsl[key] = code;
            else essl[key] = {{"file", file}, {"stage", stages.contains(entry) ? stages[entry] : ""}, {"code", code}};
        }
    }
    if (wgsl.empty() && essl.empty()) return std::unexpected("no WGSL or ESSL in those partitions (a build for wasm32-emscripten writes them)");
    const auto dumped = [](const nlohmann::json& value) { return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); };
    return std::format(R"(// Written by omui-devtools check browser: {} WGSL and {} ESSL entry points.
var wgsl = {};
var essl = {};
(async function () {{
  var failed = 0, n = 0;
  var fail = function (name, why) {{ ++failed; Module.print('  FAIL  ' + name + ': ' + why); }};
  var adapter = navigator.gpu && await navigator.gpu.requestAdapter();
  if (!adapter) fail('WebGPU', 'no adapter');
  else {{
    var device = await adapter.requestDevice();
    for (var name in wgsl) {{
      ++n;
      var info = await device.createShaderModule({{ code: wgsl[name] }}).getCompilationInfo();
      var errors = info.messages.filter(function (m) {{ return m.type === 'error'; }});
      if (errors.length) fail(name, errors.map(function (e) {{ return e.lineNum + ':' + e.linePos + ' ' + e.message; }}).join('; '));
    }}
  }}
  var gl = document.createElement('canvas').getContext('webgl2');
  if (!gl) fail('WebGL2', 'no context');
  else {{
    var compiled = {{}};
    for (var key in essl) {{
      ++n;
      var e = essl[key];
      var s = gl.createShader(e.stage === 'vertex' ? gl.VERTEX_SHADER : gl.FRAGMENT_SHADER);
      gl.shaderSource(s, e.code);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) fail(key, gl.getShaderInfoLog(s));
      else compiled[key] = s;
    }}
    // A file's vertex entries with its fragment entries: their varyings meet by name.
    for (var v in compiled) {{
      if (essl[v].stage !== 'vertex') continue;
      for (var f in compiled) {{
        if (essl[f].stage !== 'fragment' || essl[f].file !== essl[v].file) continue;
        ++n;
        var p = gl.createProgram();
        gl.attachShader(p, compiled[v]);
        gl.attachShader(p, compiled[f]);
        gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS)) fail(v + ' + ' + f, gl.getProgramInfoLog(p));
        gl.deleteProgram(p);
      }}
    }}
  }}
  Module.print('shaders: ' + (n - failed) + ' passed, ' + failed + ' failed');
  Module.onExit(failed ? 1 : 0);
}})();
)",
                       wgsl.size(), essl.size(), dumped(wgsl), dumped(essl));
}

int browser_checks(const fs::path& root) {
    namespace page = omui::devtools::browser;
    const auto chrome = page::chromium();
    if (!chrome) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_BROWSER") == "1") return failed("no headless Chromium for the browser's checks");
        std::cout << "::notice::no headless Chromium (OMUI_CHROME): the browser's checks (WebGPU's and WebGL2's conformance, the shaders) were not run\n";
        return 0;
    }
    std::cout << "browser: " << run::output(root, mcpplibs::os::fs::utf8(*chrome), {"--version"}).value_or(mcpplibs::os::fs::utf8(*chrome)) << '\n' << std::flush;
    const auto in_page = [&](const fs::path& program, std::vector<std::string> arguments, std::chrono::seconds timeout = std::chrono::seconds(300),
                             std::vector<std::string> flags = {}, std::vector<std::pair<fs::path, std::string>> preloads = {}) {
        page::Options options{.chrome = *chrome, .timeout = timeout, .flags = std::move(flags), .preloads = std::move(preloads), .program = program,
                              .arguments = std::move(arguments)};
        const auto status = page::run(options);
        if (!status) std::cout << "::error::" << status.error() << '\n';
        return status && *status == 0;
    };
    // The test programs of these packages built for the page; each found by its file name.
    const auto built = [&](std::vector<std::string> packages, std::string_view what) -> std::optional<std::map<std::string, fs::path>> {
        std::vector<std::string> args{"--target", "wasm32-emscripten", "--profile", "wasm-browser"};
        for (auto& package : packages) args.insert(args.end(), {"-p", std::move(package)});
        const auto programs = run::tests(root, std::move(args));
        if (!programs) {
            failed(std::format("{} did not build for the browser: {}", what, programs.error()));
            return std::nullopt;
        }
        std::map<std::string, fs::path> by_file;
        for (const auto& [_, program] : *programs) by_file[mcpplibs::os::fs::utf8(program.filename())] = program;
        return by_file;
    };
    const auto program_of = [&](const std::map<std::string, fs::path>& programs, std::string_view name) -> std::optional<fs::path> {
        const auto found = programs.find(std::format("{}.js", name));
        if (found == programs.end()) {
            failed(std::format("{}.js was not built", name));
            return std::nullopt;
        }
        return found->second;
    };

    // 1. rhi.webgpu and rhi.webgl on the page's WebGPU and WebGL2 (the mlibs.rhi conformance suite among them).
    const auto backends = built({"xrender.rhi.webgpu", "xrender.rhi.webgl"}, "the page backends' tests");
    if (!backends) return 1;
    for (const std::string backend : {"webgpu", "webgl"}) {
        const auto program = program_of(*backends, backend + "_test");
        if (!program || !in_page(*program, {"--require-" + backend})) return failed(std::format("rhi.{} failed in the browser", backend));
    }
    // 1b. The suites of the backends' consumers, run by their owners (D-161): hanim.rhi's shape batches and the
    // cellular kernel's CPU/GPU equality, on the page's WebGPU.
    const auto owners = built({"hanim.rhi", "xengine.cellular.rhi.cts"}, "the owners' WebGPU suites");
    if (!owners) return 1;
    for (const std::string_view suite : {"shape_webgpu_test", "cellular_webgpu_test"}) {
        const auto program = program_of(*owners, suite);
        if (!program || !in_page(*program, {"--require-webgpu"})) return failed(std::format("{} failed in the browser", suite));
    }
    // 1c. The mesh pass's contract, run by its owner on the page's WebGPU and its WebGL2: every case on both.
    const auto mesh = built({"xrender.mesh.gpu.cts"}, "the mesh pass's suite");
    if (!mesh) return 1;
    for (const std::string backend : {"webgpu", "webgl"}) {
        const auto program = program_of(*mesh, "mesh_" + backend + "_test");
        if (!program || !in_page(*program, {"--require-" + backend}, std::chrono::seconds(600)))
            return failed(std::format("the mesh pass's suite failed on the page's {}", backend));
    }
    // 1d. Quay's frame on the page's WebGPU (RC2.1's program): a few frames, that it runs — SwiftShader's times are not
    // performance; the p99 is measured on a real GPU (omui-devtools browser --headed under omui-devtools gpu --real).
    const auto quay = built({"omui.showcase.quay"}, "Quay's web frame");
    if (!quay) return 1;
    if (const auto program = program_of(*quay, "web_frame_test"); !program || !in_page(*program, {"--require-webgpu", "--frames", "3"}, std::chrono::seconds(600)))
        return failed("Quay's frame failed on the page's WebGPU");

    // 2. host.web drawing on the page's WebGPU, then its WebGL2 (a canvas keeps the first kind of context it gives: one
    // run each). Headless Chromium's SwiftShader differs by build and machine: some lose a WebGPU device that presents
    // to a canvas unless their Vulkan is SwiftShader's too, others offer no adapter with those switches. WebGPU's run
    // takes the default switches, else the Vulkan ones; a browser that can do neither has its frames compared offscreen.
    // The fonts as dist-toy's page gives them, so the page draws its text.
    const auto web = built({"omui.host.web"}, "host.web's tests");
    if (!web) return 1;
    const auto drawing = program_of(*web, "drawing_test");
    if (!drawing) return 1;
    const std::vector<std::pair<fs::path, std::string>> fonts{{fs::absolute(root / "modules/content/text/fonts"), "/fonts"}};
    const std::vector<std::string> vulkan{"--enable-features=Vulkan", "--use-vulkan=swiftshader", "--use-angle=vulkan"};
    if (!in_page(*drawing, {"--webgpu"}, std::chrono::seconds(300), {}, fonts) && !in_page(*drawing, {"--webgpu"}, std::chrono::seconds(300), vulkan, fonts)) {
        std::cout << "::notice::this browser's WebGPU cannot present to a canvas headless: host.web's WebGPU frames checked offscreen\n";
        if (!in_page(*drawing, {"--webgpu", "--offscreen"}, std::chrono::seconds(300), {}, fonts)) return failed("host.web did not draw on WebGPU in the browser");
    }
    if (!in_page(*drawing, {"--webgl"}, std::chrono::seconds(300), {}, fonts)) return failed("host.web did not draw on WebGL2 in the browser");

    // 3. Every shader the Web build wrote for the page, compiled by the browser's own compilers (WebGPU's WGSL, WebGL2's
    // ESSL, each file's vertex and fragment entries linked): what Slang writes and mlibs.shader rewrites, checked by
    // the compilers that will run it.
    const auto partitions = shader_partitions(root);
    if (partitions.empty()) return failed("no WGSL or ESSL in the build's shader partitions (check web builds them)");
    const auto script = shader_check_script(partitions);
    if (!script) return failed(script.error());
    const fs::path check = fs::temp_directory_path() / std::format("omui-shaders-check-{}.js", std::random_device{}());
    std::ofstream(check, std::ios::binary) << *script;
    std::cout << std::format("browser: the shaders of {} partitions\n", partitions.size()) << std::flush;
    const bool shaders = in_page(check, {});
    std::error_code error;
    fs::remove(check, error);
    if (!shaders) return failed("the browser refused shaders the build wrote");
    std::cout << "browser: ok\n";
    return 0;
}

int web(const fs::path& root, std::string_view profile) {
    // Every member whose manifest says it runs on the Web (`[package] platforms` lists "emscripten"; lint `platforms`
    // holds that each depends only on members that say it too), host.web and the site — the program browsers run, its
    // pins and reference run checked where it runs: built for wasm32-emscripten and tested under node.
    std::vector<std::string> args{"--target", "wasm32-emscripten", "--profile", std::string(profile)};
    std::vector<std::string> names;
    try {
        const toml::table workspace = toml::parse(mcpplibs::os::fs::read(root / "mcpp.toml").value_or(""));
        if (const auto* members = workspace["workspace"]["members"].as_array())
            for (const auto& member : *members) {
                const auto path = member.value<std::string>();
                if (!path) continue;
                const toml::table manifest = toml::parse(mcpplibs::os::fs::read(root / *path / "mcpp.toml").value_or(""));
                const auto* platforms = manifest["package"]["platforms"].as_array();
                if (!platforms || !std::ranges::any_of(*platforms, [](const toml::node& item) { return item.value_or(std::string{}) == "emscripten"; })) continue;
                const std::string space = manifest["package"]["namespace"].value_or(std::string{}), name = manifest["package"]["name"].value_or(std::string{});
                names.push_back(space.empty() ? name : space + "." + name);
            }
    } catch (const toml::parse_error& failure) {
        return failed(std::format("a manifest does not read: {}", failure.description()));
    }
    if (names.empty()) return failed("no member says it runs on the Web");
    std::cout << std::format("web: {} members:", names.size());
    for (const auto& name : names) std::cout << ' ' << name;
    std::cout << '\n' << std::flush;
    for (const auto& name : names) args.insert(args.end(), {"-p", name});
    args.insert(args.end(), {"-p", "omui.host.web", "-p", "omui.site"});
    std::vector<std::string> test{"test"};
    test.insert(test.end(), args.begin(), args.end());
    if (const auto tested = run::program(root, "mcpp", test, {.shown = true, .variables = {{"MCPP_JOBS", std::to_string(run::share_of_machine())}}});
        !tested || tested->status != 0)
        return failed("the Web row's tests failed");
    // The same selection again, built and not run: the programs' paths, nothing compiled.
    const auto programs = run::tests(root, args);
    if (!programs) return failed(programs.error());
    std::optional<fs::path> page_test;
    for (const auto& [_, program] : *programs)
        if (program.filename() == "page_test.js") page_test = program;
    if (!page_test) return failed("host.web's page_test was not built");

    // The page halves under node's test runner with a fake DOM, WebGPU and WebGL2: host.web's (alone, and with the
    // program: page_test in a fake page) and the two backends'.
    std::string node = "node";
    if (!mcpplibs::os::env::find_executable("node")) {
        std::vector<fs::path> installed;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(fs::path(mcpplibs::os::env::get("HOME").value_or("")) / ".xlings/data/xpkgs/xim-x-node", error))
            if (fs::exists(entry.path() / "bin/node")) installed.push_back(entry.path() / "bin/node");
        if (installed.empty()) return failed("no node for the page's tests");
        node = mcpplibs::os::fs::utf8(*std::ranges::max_element(installed));
    }
    const auto page = run::program(root, node, {"--test", "modules/projects/omui/host/web/tests/js/page.test.mjs", "modules/projects/omui/host/web/tests/js/wasm.test.mjs"},
                                   {.shown = true, .variables = {{"OMUI_PAGE_TEST", mcpplibs::os::fs::utf8(*page_test)}}});
    if (!page || page->status != 0) return failed("the page's half of host.web failed its tests");
    const auto backends = run::program(root, node, {"--test", "modules/projects/xrender/rhi/webgpu/tests/js/webgpu.test.mjs", "modules/projects/xrender/rhi/webgl/tests/js/webgl.test.mjs"},
                                       {.shown = true});
    if (!backends || backends->status != 0) return failed("the page's halves of rhi.webgpu and rhi.webgl failed their tests");

    // What three dimensions weigh on a page, held to its budget; then what only a real page can check.
    if (size(root, {.package = "omui.size.yard", .json = root / "target/mesh-web-size.json", .budget = "tests/size/yard/size.budget.xdef.json"}) != 0)
        return failed("the yard's bytes are over their budget");
    if (browser_checks(root) != 0) return failed("the browser's checks failed");
    std::cout << "web: ok\n";
    return 0;
}

}  // namespace omui::devtools::check
