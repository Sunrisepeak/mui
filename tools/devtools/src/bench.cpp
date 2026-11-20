module omui.devtools.bench;

import std;
import mcpplibs.os;
import omui.devtools.lead;
import omui.devtools.run;

namespace omui::devtools::bench {

namespace {

namespace fs = std::filesystem;

}  // namespace

std::string corpus(const fs::path& root, std::size_t size) {
    std::string text;
    for (const std::string_view top : {"docs"}) {
        std::vector<fs::path> files;
        std::error_code error;
        for (auto it = fs::recursive_directory_iterator(root / top, error); it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            if (it->is_regular_file(error) && it->path().extension() == ".md") files.push_back(it->path());
        }
        std::ranges::sort(files);
        for (const auto& file : files) {
            if (!text.empty()) text += "\n\n";
            text += mcpplibs::os::fs::read(file).value_or("");
        }
    }
    if (text.empty()) return text;
    std::string out = text;
    while (out.size() < size) out += "\n\n" + text;
    return out;
}

std::expected<int, std::string> competitors(const fs::path& root, const Options& options) {
    const fs::path work = root / "target" / "bench-competitors";
    const fs::path here = root / "tests" / "bench" / "competitors";
    const std::string work_text = mcpplibs::os::fs::utf8(work);
    std::error_code error;
    fs::create_directories(work, error);
    for (const auto& file : fs::directory_iterator(here / "js", error)) fs::copy_file(file.path(), work / file.path().filename(), fs::copy_options::overwrite_existing, error);
    if (error) return std::unexpected(std::format("the competitors' scripts cannot be copied into {}: {}", work_text, error.message()));

    // Their side, at the pinned versions: node's packages, and the Lottie players in a Python environment of their own.
    const auto step = [&](std::string program, std::vector<std::string> args, const fs::path& cwd = {}) -> std::expected<void, std::string> {
        auto done = run::output(root, std::move(program), std::move(args), {.cwd = cwd});
        if (!done) return std::unexpected(done.error());
        return {};
    };
    if (auto done = step("npm", {"install", "--silent", "--no-audit", "--no-fund"}, work); !done) return std::unexpected(done.error());
    const fs::path python = work / "py" / "bin" / "python";
    if (!fs::exists(python))
        if (auto done = step("python3", {"-m", "venv", mcpplibs::os::fs::utf8(work / "py")}); !done) return std::unexpected(done.error());
    if (auto done = step(mcpplibs::os::fs::utf8(work / "py" / "bin" / "pip"), {"install", "--quiet", "-r", mcpplibs::os::fs::utf8(here / "py" / "requirements.txt")}); !done)
        return std::unexpected(done.error());

    // Our side, release, natively and for the Web (within the machine's share, AGENTS §5): the products as mcpp reports them.
    std::map<std::string, std::string> program;
    for (const std::vector<std::string>& target : {std::vector<std::string>{}, std::vector<std::string>{"--target", "wasm32-emscripten"}}) {
        std::vector<std::string> args{"-p", "omui.bench.competitors", "--profile", "release"};
        args.insert(args.end(), target.begin(), target.end());
        const auto built = run::build(root, std::move(args));
        if (!built) return std::unexpected(built.error());
        for (const auto& artifact : *built)
            if (artifact.kind == "bin") program[mcpplibs::os::fs::utf8(artifact.path.filename())] = mcpplibs::os::fs::utf8(artifact.path);
    }
    for (const std::string_view name : {"omui-bench-motion", "omui-bench-vector", "omui-bench-markdown", "omui-bench-lottie", "omui-bench-motion.js", "omui-bench-markdown.js"})
        if (!program.contains(std::string(name))) return std::unexpected(std::format("the build reported no {}", name));
    const fs::path corpus_path = work / "markdown.corpus.md";
    if (!(std::ofstream(corpus_path, std::ios::binary) << corpus(root))) return std::unexpected("the markdown corpus cannot be written");
    const std::string corpus_text = mcpplibs::os::fs::utf8(corpus_path), vector_dir = mcpplibs::os::fs::utf8(work / "vector"),
                      lottie_dir = mcpplibs::os::fs::utf8(work / "lottie"), lottie_players = mcpplibs::os::fs::utf8(here / "py" / "lottie.py"),
                      python_text = mcpplibs::os::fs::utf8(python);
    const auto node = run::output(root, "node", {"--version"});
    std::cout << std::format("node {}, {} threads\n", node.value_or("?"), std::thread::hardware_concurrency()) << std::flush;

    if (options.rounds <= 0) {
        const auto said = [&](std::string_view label, std::string program_name, std::vector<std::string> args, const fs::path& cwd = {}) {
            const auto out = run::output(root, std::move(program_name), std::move(args), {.cwd = cwd});
            std::cout << std::format("{}{}\n", label, out ? *out : "failed: " + out.error()) << std::flush;
            return static_cast<bool>(out);
        };
        bool all = true;
        for (int pass = 0; pass < 3; ++pass) {
            all &= said("motion, native: ", program["omui-bench-motion"], {});
            all &= said("motion, wasm:   ", "node", {program["omui-bench-motion.js"]});
            all &= said("motion, gsap:   ", "node", {"gsap-1000.mjs"}, work);
        }
        // The vector scenes: written by our side, drawn by all three, compared. The Lottie animation: written by our
        // side, played by hanim, ThorVG and rlottie, frame 60 compared.
        for (auto&& [name, args, cwd] : std::vector<std::tuple<std::string, std::vector<std::string>, fs::path>>{
                 {program["omui-bench-vector"], {"--out", vector_dir}, {}},
                 {"node", {"vector.mjs", "vector"}, work},
                 {program["omui-bench-lottie"], {"--out", lottie_dir}, {}},
                 {python_text, {"-I", lottie_players, lottie_dir}, {}},
                 {program["omui-bench-lottie"], {"--compare", lottie_dir}, {}},
             }) {
            auto done = run::program(root, name, args, {.cwd = cwd, .shown = true});
            all &= done && done->status == 0;
        }
        all &= said("markdown, native: ", program["omui-bench-markdown"], {corpus_text});
        all &= said("markdown, wasm:   ", "node", {program["omui-bench-markdown.js"], corpus_text});
        auto done = run::program(root, "node", {"markdown.mjs", mcpplibs::os::fs::utf8(corpus_path.filename())}, {.cwd = work, .shown = true});
        all &= done && done->status == 0;
        return all ? 0 : 1;
    }

    // The ledger: each round, every side once, alternating; each JSON line a sample.
    const fs::path samples_path = work / "samples.jsonl";
    fs::remove(samples_path, error);
    struct Sample {
        std::string series, series_field, key, program;
        std::vector<std::string> args;
    };
    const std::vector<Sample> sides{
        {"hanim-native", "", "drive-1000", program["omui-bench-motion"], {"--json"}},
        {"hanim-wasm", "", "drive-1000", "node", {program["omui-bench-motion.js"], "--json"}},
        {"gsap", "", "drive-1000", "node", {mcpplibs::os::fs::utf8(work / "gsap-1000.mjs"), "--json"}},
        // xrender first: it writes the scenes and its pictures the others are compared with.
        {"xrender", "", "", program["omui-bench-vector"], {"--out", vector_dir, "--runs", "5"}},
        {"skia", "", "", "node", {mcpplibs::os::fs::utf8(work / "vector.mjs"), vector_dir, "--runs", "5"}},
        {"hanim", "", "lottie-200", program["omui-bench-lottie"], {"--out", lottie_dir, "--runs", "3"}},
        {"lottie", "player", "lottie-200", python_text, {"-I", lottie_players, lottie_dir, "--runs", "3"}},
        {"mlibs.markdown-wasm", "", "markdown", "node", {program["omui-bench-markdown.js"], corpus_text, "--runs", "5", "--json"}},
        {"markdown-competitors", "name", "markdown", "node", {mcpplibs::os::fs::utf8(work / "markdown.mjs"), corpus_text, "--runs", "5"}},
    };
    for (int round = 1; round <= options.rounds; ++round) {
        std::cout << std::format("round {} of {}\n", round, options.rounds) << std::flush;
        for (const auto& side : sides) {
            auto done = run::program(root, side.program, side.args);
            if (!done) return std::unexpected(done.error());
            if (done->status != 0) return std::unexpected(std::format("{} (round {}) exited {}", side.series, round, done->status));
            const auto records = lead::samples(done->out, side.series, side.series_field, side.key, round);
            if (records.empty()) return std::unexpected(std::format("{} printed no JSON line", side.series));
            if (!lead::append(samples_path, records)) return std::unexpected("the samples cannot be written");
        }
    }
    std::vector<std::string> skipped;
    const auto judged = lead::verdicts(mcpplibs::os::fs::read(root / lead::metrics_path).value_or(""), mcpplibs::os::fs::read(samples_path).value_or(""),
                                       mcpplibs::os::fs::read(root / lead::ledger_path).value_or(""), lead::context_here(root), skipped);
    if (!judged) return std::unexpected(judged.error());
    for (const auto& id : skipped) std::cerr << std::format("{}: no paired samples\n", id);
    std::vector<std::string> records;
    for (const auto& verdict : *judged) {
        std::cout << verdict.said << '\n';
        records.push_back(verdict.record);
    }
    if (options.dry_run) return 0;
    if (!lead::append(root / lead::ledger_path, records)) return std::unexpected("the ledger cannot be written");
    const auto written = lead::write_pages(root);
    if (!written) return std::unexpected(written.error());
    for (const auto& path : *written) std::cout << std::format("wrote {}\n", path);
    return 0;
}

}  // namespace omui::devtools::bench
