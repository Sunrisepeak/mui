// The wall's documents: a folder's Markdown, or the benchmark's synthetic set.
module wall;

import std;
import openmui;

namespace wall {

namespace {

// A deterministic generator: the dataset is the same on every machine.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull) {}
    std::uint64_t next() {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return s;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
    template <class T, std::size_t N>
    std::string_view pick(const std::array<T, N>& a) {
        return a[below(N)];
    }
};

constexpr std::array<std::string_view, 48> words{
    "canvas",  "window",   "layout",  "frame",   "texture", "shader",   "camera",  "signal",  "binding", "agent",   "action",  "semantic",
    "render",  "raster",   "glyph",   "font",    "spring",  "timeline", "module",  "package", "theme",   "token",   "effect",  "layer",
    "budget",  "cache",    "thread",  "pool",    "queue",   "stream",  "parser",   "block",   "inline",  "table",   "image",   "link",
    "heading", "paragraph", "list",   "quote",   "code",    "fence",   "zoom",     "pan",     "tier",    "mip",     "upload",  "promote"};
constexpr std::array<std::string_view, 12> nouns{"Atlas", "Compass", "Harbor", "Lantern", "Meadow", "Orbit", "Prism", "Quarry", "Summit", "Tide", "Vertex", "Willow"};
constexpr std::array<std::string_view, 10> adjectives{"Quiet", "Bright", "Swift", "Gentle", "Hidden", "Silver", "Northern", "Patient", "Open", "Steady"};
constexpr std::array<std::string_view, 12> chinese{
    "无限画布把上千个窗口摊开在同一个平面上。", "远处的窗口只是色块，靠近以后才显示文字。", "纹理在后台线程上按屏幕需要的分辨率光栅化。",
    "缩放停下来以后，画面在三百毫秒内重新变清晰。", "语义树让代理和测试看到和人一样的内容。", "每一帧只处理屏幕上看得见的节点。",
    "动画是时间的纯函数，可以任意跳转。", "流式输出时只重新解析文档的尾部。", "工作区可以拖动，窗口跟着一起移动。",
    "小地图显示整面墙和当前的视野。", "同一个元素既是纹理也是实时组件。", "主题切换以后，所有纹理在新主题下重画。"};

std::string sentence(Rng& r, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        std::string w(r.pick(words));
        if (i == 0) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        out += (i ? " " : "") + w;
    }
    return out + ".";
}

std::string paragraph(Rng& r) {
    std::string p;
    const std::size_t n = 3 + r.below(4);
    for (std::size_t i = 0; i < n; ++i) p += (i ? " " : "") + sentence(r, 6 + r.below(9));
    return p;
}

std::string page(Rng& r, std::size_t kind, const std::string& title) {
    std::string md = std::format("# {}\n\n", title);
    switch (kind) {
        case 0:  // a README
            md += paragraph(r) + "\n\n## Install\n\n```sh\nmcpp add " + std::string(r.pick(words)) + "\n```\n\n## Use\n\n";
            for (int i = 0; i < 5; ++i) md += "- " + sentence(r, 4 + r.below(6)) + "\n";
            md += "\nSee [the guide](https://openmui.dev/" + std::string(r.pick(words)) + ") and `" + std::string(r.pick(words)) + "()`.\n";
            break;
        case 1:  // a long page
            for (int i = 0; i < 4; ++i) {
                md += std::format("## {} {}\n\n", r.pick(adjectives), r.pick(nouns));
                md += paragraph(r) + "\n\n" + paragraph(r) + "\n\n";
            }
            break;
        case 2:  // code
            md += paragraph(r) + "\n\n";
            for (int b = 0; b < 3; ++b) {
                md += "```cpp\n";
                for (int i = 0; i < 9; ++i)
                    md += std::format("auto {}_{} = {}({}, {});\n", r.pick(words), i, r.pick(words), r.below(1000), r.pick(words));
                md += "```\n\n";
            }
            break;
        case 3:  // Chinese
            for (int i = 0; i < 4; ++i) {
                md += std::format("## 第{}节\n\n", i + 1);
                for (int k = 0; k < 4; ++k) md += std::string(r.pick(chinese));
                md += "\n\n";
            }
            break;
        default:  // a table
            md += paragraph(r) + "\n\n| Name | Frames | Cost (ms) | Notes |\n|---|---:|---:|---|\n";
            for (int i = 0; i < 10; ++i)
                md += std::format("| {} | {} | {}.{} | {} |\n", r.pick(words), 60 + r.below(1000), r.below(9), r.below(10), sentence(r, 3));
            md += "\n> " + sentence(r, 10) + "\n";
            break;
    }
    return md;
}

std::string title_of(std::string_view md, std::string_view fallback) {
    std::size_t at = 0;
    while (at < md.size()) {
        const std::size_t end = std::min(md.find('\n', at), md.size());
        std::string_view line = md.substr(at, end - at);
        if (line.starts_with('#')) {
            line.remove_prefix(line.find_first_not_of('#') == std::string_view::npos ? line.size() : line.find_first_not_of('#'));
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.remove_suffix(1);
            if (!line.empty()) return std::string(line);
        }
        at = end + 1;
    }
    return std::string(fallback);
}

}  // namespace

std::string key_of(std::string_view path) {
    std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
    for (unsigned char c : path) h = (h ^ c) * 0x100000001b3ull;
    return std::format("w{:016x}", h);
}

double window_height(const Document& d) noexcept {
    std::size_t lines = 0, run = 0;
    for (char c : d.source) {
        if (c == '\n') {
            lines += 1 + run / 90;
            run = 0;
        } else {
            ++run;
        }
    }
    lines += run / 90;
    return std::clamp(std::round((180 + static_cast<double>(lines) * 15) / 8) * 8, 300.0, 760.0);
}

std::expected<std::vector<Document>, std::string> read_folder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return std::unexpected(std::format("`{}` is not a folder", omui::utf8(folder)));
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const auto& e = *it;
        // Folders a build or a tool keeps are not documents to read.
        const std::string name = omui::utf8(e.path().filename());
        if (e.is_directory(ec) && ((name.starts_with('.') && name != ".agents") || name == "target" || name == "node_modules"))
            it.disable_recursion_pending();
        else if (e.is_regular_file(ec) && e.path().extension() == ".md")
            files.push_back(e.path());
    }
    std::ranges::sort(files);
    std::vector<Document> out;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        Document d;
        d.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        d.path = omui::generic_utf8(std::filesystem::relative(f, folder, ec));
        const auto parent = std::filesystem::path(d.path).parent_path();
        d.group = parent.empty() ? "." : omui::generic_utf8(parent);
        d.title = title_of(d.source, omui::utf8(f.stem()));
        d.key = key_of(d.path);
        out.push_back(std::move(d));
    }
    if (out.empty()) return std::unexpected(std::format("no Markdown files (*.md) under `{}`", omui::utf8(folder)));
    return out;
}

std::vector<Document> synthetic(std::size_t count, std::uint64_t seed, std::size_t groups) {
    if (groups == 0) groups = std::max<std::size_t>(1, (count + 24) / 25);
    Rng r(seed);
    std::vector<Document> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Document d;
        const std::size_t g = i % groups;
        d.group = std::format("{}-{}", nouns[g % nouns.size()], g / nouns.size() + 1);
        d.title = std::format("{} {} {}", r.pick(adjectives), r.pick(nouns), i + 1);
        d.path = std::format("{}/{:04}.md", d.group, i);
        d.source = page(r, i % 5, d.title);
        d.key = key_of(d.path);
        out.push_back(std::move(d));
    }
    // By workspace, then by path: the order the wall shows them in.
    std::ranges::sort(out, {}, &Document::path);
    return out;
}

}  // namespace wall
