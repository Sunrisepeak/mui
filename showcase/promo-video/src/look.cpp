// The film's look: its palette and style packs, code coloured by what it
// is, the drawing paper, labels shaped once.
module openmui.showcase.promo_video;

import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;

namespace omui::promo {

Color hex(std::uint32_t rgb, float alpha) { return Color::hex(rgb, alpha); }

void add_styles() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (auto r = themes::add_styles(); !r)
            for (const auto& d : r.error()) warn("promo: {}", d.text());
        for (const auto& [name, text] : files::style_files)
            if (auto r = theme::library().add_styles(text, name); !r)
                for (const auto& d : r.error()) warn("promo: {}", d.text());
    });
}

Theme blueprint() {
    add_styles();
    auto t = theme::library().compose_style(look_style);
    if (!t) fatal(std::format("promo: the blueprint style does not compose: {}", t.error()));
    return *t;
}

// ── code ────────────────────────────────────────────────────────────────

namespace {

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::span<const std::string_view> keywords(std::string_view language) {
    static constexpr std::array<std::string_view, 16> ts{"import", "from", "const", "let", "export", "return", "true", "false",
                                                         "new", "function", "if", "else", "for", "of", "await", "async"};
    static constexpr std::array<std::string_view, 22> cpp{"return", "auto", "const", "namespace", "import", "module", "export", "static_cast",
                                                          "if", "else", "for", "true", "false", "struct", "class", "using", "int", "double",
                                                          "bool", "void", "std", "inline"};
    static constexpr std::array<std::string_view, 16> py{"import", "as", "def", "with", "if", "else", "elif", "return", "lambda", "None",
                                                         "True", "False", "raise", "for", "in", "not"};
    static constexpr std::array<std::string_view, 13> lua{"local", "function", "return", "end", "if", "then", "else", "nil", "true", "false",
                                                          "for", "do", "assert"};
    if (language == "ts") return ts;
    if (language == "cpp") return cpp;
    if (language == "py") return py;
    return lua;
}

std::string_view comment_of(std::string_view language) {
    if (language == "py") return "#";
    if (language == "lua") return "--";
    return "//";
}

}  // namespace

std::vector<std::vector<Span>> highlight(std::string_view code, std::string_view language) {
    std::vector<std::vector<Span>> lines;
    const auto kw = keywords(language);
    const std::string_view comment = comment_of(language);
    std::size_t at = 0;
    while (at <= code.size()) {
        const std::size_t end = std::min(code.find('\n', at), code.size());
        const std::string_view line = code.substr(at, end - at);
        std::vector<Span> spans;
        // Spaces go with what follows them (a text's trailing space is not laid out); those ending a line go.
        std::string space;
        auto push = [&](std::string_view text, Token kind) {
            if (text.empty()) return;
            if (text.find_first_not_of(" \t") == std::string_view::npos) {
                space += text;
                return;
            }
            if (!spans.empty() && spans.back().kind == kind) spans.back().text += space + std::string(text);
            else spans.push_back({space + std::string(text), kind});
            space.clear();
        };
        std::size_t i = 0;
        while (i < line.size()) {
            const char c = line[i];
            if (line.substr(i).starts_with(comment)) {
                push(line.substr(i), Token::comment);
                break;
            }
            if (c == '"' || c == '\'') {
                std::size_t j = i + 1;
                while (j < line.size() && line[j] != c) j += line[j] == '\\' ? 2 : 1;
                j = std::min(j + 1, line.size());
                push(line.substr(i, j - i), Token::string);
                i = j;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t j = i;
                while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
                push(line.substr(i, j - i), Token::number);
                i = j;
                continue;
            }
            if (word_char(c)) {
                std::size_t j = i;
                while (j < line.size() && word_char(line[j])) ++j;
                const std::string_view w = line.substr(i, j - i);
                std::size_t k = j;
                while (k < line.size() && line[k] == ' ') ++k;
                Token kind = Token::plain;
                if (std::ranges::find(kw, w) != kw.end()) kind = Token::keyword;
                else if (k < line.size() && line[k] == '(') kind = Token::call;
                else if (std::isupper(static_cast<unsigned char>(w.front()))) kind = Token::type;
                push(w, kind);
                i = j;
                continue;
            }
            if (c == ' ' || c == '\t') {
                std::size_t j = i;
                while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
                push(line.substr(i, j - i), Token::plain);
                i = j;
                continue;
            }
            // One character of punctuation, or a multi-byte one (−) whole.
            std::size_t j = i + 1;
            while (j < line.size() && (static_cast<unsigned char>(line[j]) & 0xC0) == 0x80) ++j;
            push(line.substr(i, j - i), std::ispunct(static_cast<unsigned char>(c)) ? Token::punct : Token::plain);
            i = j;
        }
        lines.push_back(std::move(spans));
        if (end == code.size()) break;
        at = end + 1;
    }
    return lines;
}

Color token_color(Token t) {
    switch (t) {
        case Token::keyword: return hex(0xc4b5fd);
        case Token::string: return hex(0x86efac);
        case Token::number: return hex(0xfbbf24);
        case Token::comment: return hex(ink::muted, 0.7f);
        case Token::punct: return hex(0x7dd3fc, 0.85f);
        case Token::call: return hex(ink::cyan);
        case Token::type: return hex(0xf9a8d4);
        case Token::plain: break;
    }
    return hex(ink::ice);
}

std::string shown_code(std::string_view code, std::string_view language) {
    const std::string_view comment = comment_of(language);
    std::vector<std::string_view> lines;
    std::size_t at = 0;
    while (at < code.size()) {
        const std::size_t end = std::min(code.find('\n', at), code.size());
        lines.push_back(code.substr(at, end - at));
        at = end + 1;
    }
    std::size_t first = 0;
    bool in_doc = false;  // a Python docstring at the head
    for (; first < lines.size(); ++first) {
        std::string_view l = lines[first];
        while (!l.empty() && l.front() == ' ') l.remove_prefix(1);
        if (language == "py" && l.starts_with("\"\"\"")) {
            in_doc = !(l.size() > 3 && l.ends_with("\"\"\"")) && !in_doc;
            continue;
        }
        if (in_doc) {
            if (l.ends_with("\"\"\"")) in_doc = false;
            continue;
        }
        if (!l.empty() && !l.starts_with(comment) && !l.starts_with("module ") && !l.starts_with("import std") && !l.starts_with("import openmui;") &&
            !l.starts_with("namespace "))
            break;
    }
    std::size_t last = lines.size();
    // Trailing blank lines, and a C++ file's closing namespace, go.
    while (last > first && (lines[last - 1].find_first_not_of(" \t\r") == std::string_view::npos || lines[last - 1].starts_with("}  // namespace"))) --last;
    std::string out;
    for (std::size_t i = first; i < last; ++i) {
        out += lines[i];
        if (i + 1 < last) out += '\n';
    }
    return out;
}

Element code_block(std::string id, std::string_view code, std::string_view language, float size) {
    const auto lines = highlight(code, language);
    std::vector<Element> rows;
    // The numbers stay put while the code is typed: a motion of their own (at rest) keeps them out of the block's.
    GlyphMotion still;
    still.from = {};
    still.to = {};
    still.transition = Transition::tween(hanim::seconds(0.001), hanim::ease::linear);
    still.stagger = hanim::seconds(0);
    for (std::size_t n = 0; n < lines.size(); ++n) {
        std::vector<Element> spans;
        spans.push_back(animate_glyphs(text({.text = std::format("{:>2}", n + 1), .style = "code", .color = Fill(hex(ink::line)), .wrap = false,
                                             .size = size, .width = Dim(size * 2.2f)}),
                                       still));
        for (const Span& s : lines[n]) {
            // Spaces as no-break spaces: a span's own leading or trailing space is laid out, not trimmed.
            std::string t;
            for (const char c : s.text) t += c == ' ' ? std::string("\u00a0") : std::string(1, c);
            spans.push_back(text({.text = std::move(t), .style = "code", .color = Fill(token_color(s.kind)), .wrap = false, .size = size}));
        }
        rows.push_back(row({.id = std::format("l{}", n + 1), .height = Dim(std::round(size * 1.55f)), .gap = 0, .align = "center"}, std::move(spans)));
    }
    return column({.id = std::move(id), .gap = 0}, std::move(rows));
}

// ── drawing ─────────────────────────────────────────────────────────────

void paint_paper(render::DisplayList& list, Size size, Point offset, float scale, float grid) {
    list.fill_rect({0, 0, size.width, size.height}, hex(ink::paper));
    if (grid > 0 && scale > 0.05f) {
        auto lines = [&](float step, float alpha) {
            const float s = step * scale;
            if (s < 6) return;
            const Color c = hex(ink::cyan, alpha * grid * std::clamp((s - 6) / 18, 0.0f, 1.0f));
            const float ox = std::fmod(std::fmod(offset.x, s) + s, s), oy = std::fmod(std::fmod(offset.y, s) + s, s);
            for (float x = ox; x < size.width; x += s) list.fill_rect({std::floor(x), 0, 1, size.height}, c);
            for (float y = oy; y < size.height; y += s) list.fill_rect({0, std::floor(y), size.width, 1}, c);
        };
        lines(24, 0.045f);
        lines(120, 0.09f);
    }
    // Darker towards the corners: the light is on the middle of the sheet.
    const std::array<render::GradientStop, 3> stops{{{0, hex(ink::paper_deep, 0)}, {0.62f, hex(ink::paper_deep, 0.18f)}, {1, hex(ink::paper_deep, 0.72f)}}};
    render::Paint p;
    p.kind = render::Paint::Kind::gradient;
    p.gradient.kind = render::GradientKind::radial;
    p.gradient.start = p.gradient.end = {size.width / 2, size.height / 2};
    p.gradient.radius = static_cast<float>(hanim::dm::sqrt(static_cast<double>(size.width) * size.width + static_cast<double>(size.height) * size.height)) / 2;
    p.gradient.stops = stops;
    list.fill_rect({0, 0, size.width, size.height}, p);
}

void paint_ticks(render::DisplayList& list, Rect r, Color c, float tick, float width, float hairline) {
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
    const std::array<std::array<float, 4>, 8> bars{{{x0 - width, y0 - width, tick, width},
                                                    {x0 - width, y0 - width, width, tick},
                                                    {x1 - tick + width, y0 - width, tick, width},
                                                    {x1, y0 - width, width, tick},
                                                    {x0 - width, y1, tick, width},
                                                    {x0 - width, y1 - tick + width, width, tick},
                                                    {x1 - tick + width, y1, tick, width},
                                                    {x1, y1 - tick + width, width, tick}}};
    for (const auto& b : bars) list.fill_rect({b[0], b[1], b[2], b[3]}, c);
    if (hairline > 0) list.stroke_round_rect(r, CornerRadii::all(0), 1, render::Paint::solid(c.with_alpha(c.a * hairline)));
}

void paint_line(render::DisplayList& list, Point a, Point b, Color c, float width) {
    render::Path path;
    path.move_to(a);
    path.line_to(b);
    render::StrokeStyle st;
    st.width = width;
    st.cap = render::LineCap::round;
    list.stroke_path(path, st, render::Paint::solid(c));
}

const typeset::Paragraph& Labels::get(typeset::FontCollection& fonts, std::string_view text, float size, Color color, float weight,
                                      std::string_view family, float width) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mixin = [&](std::string_view s) {
        for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
        h = (h ^ 0xff) * 0x100000001b3ull;
    };
    mixin(text);
    mixin(family);
    for (float f : {size, color.r, color.g, color.b, color.a, weight, width}) mixin(std::string_view(reinterpret_cast<const char*>(&f), sizeof f));
    auto it = cache_.find(h);
    if (it == cache_.end()) {
        if (cache_.size() > 2048) cache_.clear();
        typeset::ParagraphStyle ps;
        ps.style.families = {std::string(family)};
        ps.style.size = size;
        ps.style.weight = weight;
        ps.style.color = color;
        ps.wrap = width > 0;
        typeset::Paragraph p(fonts, std::string(text), std::move(ps));
        p.layout(width > 0 ? width : std::numeric_limits<float>::infinity());
        it = cache_.emplace(h, std::move(p)).first;
    }
    return it->second;
}

// ── time ────────────────────────────────────────────────────────────────

double ramp(double t, double from, double to) noexcept {
    if (to == from) return t >= to ? 1 : 0;
    return std::clamp((t - from) / (to - from), 0.0, 1.0);  // falling when `to` is below `from`
}

double smooth(double t, double from, double to) noexcept {
    const double u = ramp(t, from, to);
    return u * u * (3 - 2 * u);
}

double mix(double a, double b, double u) noexcept { return a + (b - a) * u; }

}  // namespace omui::promo
