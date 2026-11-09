// The document as renderers, the semantic tree and agents read it.
import std;
import mlibs.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace md = mlibs::markdown;

UTest cases{"markdown", [] {

    "blocks nest as in the source"_test = [] {
        const auto doc = md::parse("# Title\n\n> quoted *text*\n\n1. one\n2. two\n\n---\n");
        const auto& b = doc.root.children;
        expect(eq(b.size(), std::size_t{4}));
        expect(b[0].kind == md::Kind::heading && b[0].level == 1 && b[0].content.text == "Title");
        expect(b[1].kind == md::Kind::quote && b[1].children.size() == 1 && b[1].children[0].kind == md::Kind::paragraph);
        expect(b[2].kind == md::Kind::ordered_list && b[2].start == 1 && b[2].mark == '.' && b[2].tight && b[2].children.size() == 2);
        expect(b[3].kind == md::Kind::rule);
        expect(eq(md::name(b[2].kind), std::string_view("ordered_list")));
    };

    "items of tight lists have paragraphs too; the list says it is tight"_test = [] {
        const auto tight = md::parse("- a\n- b\n");
        const auto& item = tight.root.children[0].children[0];
        expect(item.children.size() == 1 && item.children[0].kind == md::Kind::paragraph && item.children[0].content.text == "a");
        const auto loose = md::parse("- a\n\n- b\n");
        expect(!loose.root.children[0].tight && loose.root.children[0].children[1].children[0].content.text == "b");
    };

    "inline marks keep the nesting; runs flatten it for a text engine"_test = [] {
        const auto doc = md::parse("a *b **c** d* [e `f`](/u \"T\") ![g](/i.png)\n");
        const auto& in = doc.root.children[0].content;
        expect(eq(in.text, std::string("a b c d e f g")));
        expect(eq(doc.links.size(), std::size_t{2}));
        expect(doc.links[0].href == "/u" && doc.links[0].title == "T" && doc.links[1].href == "/i.png");
        const auto runs = md::runs(in);
        // a ␣ | b ␣ (em) | c (em strong) | ␣ d (em) | ␣ | e ␣ (link) | f (link code) | ␣ | g (image)
        auto at = [&](std::string_view piece) {
            const auto pos = static_cast<std::uint32_t>(in.text.find(piece));
            for (const auto& r : runs)
                if (r.begin <= pos && pos < r.end) return r;
            return md::Run{};
        };
        expect(eq(at("c").spans, static_cast<std::uint16_t>(md::bit(md::Span::emphasis) | md::bit(md::Span::strong))));
        expect(eq(at("b").spans, md::bit(md::Span::emphasis)));
        expect(at("f").spans == (md::bit(md::Span::link) | md::bit(md::Span::code)) && at("f").link == 0);
        expect(at("g").spans == md::bit(md::Span::image) && at("g").link == 1);
        expect(eq(at("a").spans, std::uint16_t{0}));
        // The runs tile the text.
        std::uint32_t end = 0;
        for (const auto& r : runs) {
            expect(eq(r.begin, end));
            end = r.end;
        }
        expect(eq(end, static_cast<std::uint32_t>(in.text.size())));
    };

    "entities are decoded; breaks are marked"_test = [] {
        const auto doc = md::parse("&copy; &#x4E2D; &amp;\nnext  \nhard\n");
        const auto& in = doc.root.children[0].content;
        expect(eq(in.text, std::string("\xC2\xA9 \xE4\xB8\xAD & next\nhard")));
        expect(eq(std::ranges::count_if(in.marks, [](const md::Mark& m) { return m.op == md::Op::soft_break; }), std::ptrdiff_t{1}));
        expect(eq(std::ranges::count_if(in.marks, [](const md::Mark& m) { return m.op == md::Op::hard_break; }), std::ptrdiff_t{1}));
    };

    "tasks know where their mark is in the source"_test = [] {
        const std::string src = "- [ ] write\n- [x] test\n";
        const auto doc = md::parse(src);
        const auto& items = doc.root.children[0].children;
        expect(items[0].task == md::Task::open && items[1].task == md::Task::done);
        expect(eq(src[items[0].task_offset], ' '));
        expect(eq(src[items[1].task_offset], 'x'));
        expect(md::parse(src, md::commonmark).root.children[0].children[0].task == md::Task::none);
    };

    "code blocks: fence, info, language, verbatim text"_test = [] {
        const auto doc = md::parse("```cpp linenos\nint x = 1 < 2;\n```\n\n    indented\n");
        const auto& c = doc.root.children[0];
        expect(c.kind == md::Kind::code && c.fence == '`' && c.info == "cpp linenos" && c.language == "cpp");
        expect(eq(c.content.text, std::string("int x = 1 < 2;\n")));
        expect(doc.root.children[1].fence == 0 && doc.root.children[1].content.text == "indented\n");
    };

    "tables: head, body, alignment"_test = [] {
        const auto doc = md::parse("| a | b |\n|:-|-:|\n| 1 | 2 |\n");
        const auto& t = doc.root.children[0];
        expect(t.kind == md::Kind::table && t.children.size() == 2);
        const auto& head = t.children[0].children[0].children;
        expect(head[0].header && head[0].align == md::Align::left && head[1].align == md::Align::right);
        expect(eq(md::plain_text(t), std::string("a\tb\n1\t2")));
    };

    "headings get unique GitHub-style anchors"_test = [] {
        const auto doc = md::parse("# Hello, World!\n\n## Hello, World!\n\n## hello-world-1\n\n# 中文 标题\n");
        const auto& b = doc.root.children;
        expect(eq(b[0].anchor, std::string("hello-world")));
        expect(eq(b[1].anchor, std::string("hello-world-1")));
        expect(eq(b[2].anchor, std::string("hello-world-1-1")));
        expect(eq(b[3].anchor, std::string("中文-标题")));
        expect(eq(md::slug("  A_b-C? "), std::string("--a_b-c-")));
    };

    "hashes: equal blocks hash equal, wherever they are; a change changes them"_test = [] {
        const auto a = md::parse("para [x](/u)\n\n# H\n\npara [x](/u)\n");
        expect(eq(a.root.children[0].hash, a.root.children[2].hash));
        expect(a.root.children[0].hash != a.root.children[1].hash);
        const auto b = md::parse("para [x](/v)\n");
        expect(b.root.children[0].hash != a.root.children[0].hash) << "the destination counts, not the link's index";
    };

    "plain text reads like the document"_test = [] {
        const auto doc = md::parse("# T\n\nSome *words*.\n\n- one\n- two\n\n```\ncode\n```\n");
        expect(eq(md::plain_text(doc.root), std::string("T\nSome words.\none\ntwo\ncode")));
    };

    "extensions by name"_test = [] {
        expect(md::extension("gfm") == md::gfm);
        expect(md::extension("tables") == md::Extension::tables);
        expect(!md::extension("nope"));
        const std::vector<std::string> names{"commonmark", "math", "nope"};
        expect(md::extensions(names) == md::Extension::math);
        expect(md::has(md::gfm, md::autolinks) && !md::has(md::gfm, md::Extension::math));
        // Raw HTML is text without its extension, markup with it.
        expect(md::parse("<div>\nx\n</div>\n").root.children[0].kind == md::Kind::html);
        expect(md::parse("<div>\nx\n</div>\n", md::no_html).root.children[0].kind == md::Kind::paragraph);
    };

    "to_html writes what md4c-html writes"_test = [] {
        expect(eq(md::to_html(md::parse("# Hi\n\n- [x] *a* & <b>\n")),
                 std::string("<h1>Hi</h1>\n<ul>\n<li class=\"task-list-item\"><input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled "
                             "checked><em>a</em> &amp; <b></li>\n</ul>\n")));
    };

    "to_html from the source in one call: the same bytes as from the parsed document, for every page of the repository's docs and site"_test = [] {
        const auto root = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path().parent_path();
        std::size_t pages = 0, bytes = 0;
        bool same = true;
        std::string first_different;
        for (const auto& folder : {root / "docs", root / "site" / "content"}) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (entry.path().extension() != ".md") continue;
                std::ifstream in(entry.path(), std::ios::binary);
                const std::string text{std::istreambuf_iterator<char>(in), {}};
                const std::string once = md::to_html(text), twice = md::to_html(md::parse(text));
                if (once != twice && first_different.empty()) first_different = entry.path().string();
                same = same && once == twice;
                ++pages, bytes += once.size();
            }
        }
        expect(pages >= 8 && bytes > 100000) << std::format("{} pages, {} bytes", pages, bytes);
        expect(same) << "differs: " + first_different;
        expect(md::parse("# A\n\ntext\n").root.hash != 0) << "parse still hashes its blocks";
    };
}};
