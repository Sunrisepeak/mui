// The document as renderers, the semantic tree and agents read it.
import std;
import openmui.markdown;
import openmui.testing;

using namespace omui::testing;
namespace md = omui::md;

int main(int argc, char** argv) {
    Suite s{"markdown"};

    s.test("blocks nest as in the source", [] {
        const auto doc = md::parse("# Title\n\n> quoted *text*\n\n1. one\n2. two\n\n---\n");
        const auto& b = doc.root.children;
        check_eq(b.size(), std::size_t{4});
        check(b[0].kind == md::Kind::heading && b[0].level == 1 && b[0].content.text == "Title");
        check(b[1].kind == md::Kind::quote && b[1].children.size() == 1 && b[1].children[0].kind == md::Kind::paragraph);
        check(b[2].kind == md::Kind::ordered_list && b[2].start == 1 && b[2].mark == '.' && b[2].tight && b[2].children.size() == 2);
        check(b[3].kind == md::Kind::rule);
        check_eq(md::name(b[2].kind), std::string_view("ordered_list"));
    });

    s.test("items of tight lists have paragraphs too; the list says it is tight", [] {
        const auto tight = md::parse("- a\n- b\n");
        const auto& item = tight.root.children[0].children[0];
        check(item.children.size() == 1 && item.children[0].kind == md::Kind::paragraph && item.children[0].content.text == "a");
        const auto loose = md::parse("- a\n\n- b\n");
        check(!loose.root.children[0].tight && loose.root.children[0].children[1].children[0].content.text == "b");
    });

    s.test("inline marks keep the nesting; runs flatten it for a text engine", [] {
        const auto doc = md::parse("a *b **c** d* [e `f`](/u \"T\") ![g](/i.png)\n");
        const auto& in = doc.root.children[0].content;
        check_eq(in.text, std::string("a b c d e f g"));
        check_eq(doc.links.size(), std::size_t{2});
        check(doc.links[0].href == "/u" && doc.links[0].title == "T" && doc.links[1].href == "/i.png");
        const auto runs = md::runs(in);
        // a ␣ | b ␣ (em) | c (em strong) | ␣ d (em) | ␣ | e ␣ (link) | f (link code) | ␣ | g (image)
        auto at = [&](std::string_view piece) {
            const auto pos = static_cast<std::uint32_t>(in.text.find(piece));
            for (const auto& r : runs)
                if (r.begin <= pos && pos < r.end) return r;
            return md::Run{};
        };
        check_eq(at("c").spans, static_cast<std::uint16_t>(md::bit(md::Span::emphasis) | md::bit(md::Span::strong)));
        check_eq(at("b").spans, md::bit(md::Span::emphasis));
        check(at("f").spans == (md::bit(md::Span::link) | md::bit(md::Span::code)) && at("f").link == 0);
        check(at("g").spans == md::bit(md::Span::image) && at("g").link == 1);
        check_eq(at("a").spans, std::uint16_t{0});
        // The runs tile the text.
        std::uint32_t end = 0;
        for (const auto& r : runs) {
            check_eq(r.begin, end);
            end = r.end;
        }
        check_eq(end, static_cast<std::uint32_t>(in.text.size()));
    });

    s.test("entities are decoded; breaks are marked", [] {
        const auto doc = md::parse("&copy; &#x4E2D; &amp;\nnext  \nhard\n");
        const auto& in = doc.root.children[0].content;
        check_eq(in.text, std::string("\xC2\xA9 \xE4\xB8\xAD & next\nhard"));
        check_eq(std::ranges::count_if(in.marks, [](const md::Mark& m) { return m.op == md::Op::soft_break; }), std::ptrdiff_t{1});
        check_eq(std::ranges::count_if(in.marks, [](const md::Mark& m) { return m.op == md::Op::hard_break; }), std::ptrdiff_t{1});
    });

    s.test("tasks know where their mark is in the source", [] {
        const std::string src = "- [ ] write\n- [x] test\n";
        const auto doc = md::parse(src);
        const auto& items = doc.root.children[0].children;
        check(items[0].task == md::Task::open && items[1].task == md::Task::done);
        check_eq(src[items[0].task_offset], ' ');
        check_eq(src[items[1].task_offset], 'x');
        check(md::parse(src, md::commonmark).root.children[0].children[0].task == md::Task::none);
    });

    s.test("code blocks: fence, info, language, verbatim text", [] {
        const auto doc = md::parse("```cpp linenos\nint x = 1 < 2;\n```\n\n    indented\n");
        const auto& c = doc.root.children[0];
        check(c.kind == md::Kind::code && c.fence == '`' && c.info == "cpp linenos" && c.language == "cpp");
        check_eq(c.content.text, std::string("int x = 1 < 2;\n"));
        check(doc.root.children[1].fence == 0 && doc.root.children[1].content.text == "indented\n");
    });

    s.test("tables: head, body, alignment", [] {
        const auto doc = md::parse("| a | b |\n|:-|-:|\n| 1 | 2 |\n");
        const auto& t = doc.root.children[0];
        check(t.kind == md::Kind::table && t.children.size() == 2);
        const auto& head = t.children[0].children[0].children;
        check(head[0].header && head[0].align == md::Align::left && head[1].align == md::Align::right);
        check_eq(md::plain_text(t), std::string("a\tb\n1\t2"));
    });

    s.test("headings get unique GitHub-style anchors", [] {
        const auto doc = md::parse("# Hello, World!\n\n## Hello, World!\n\n## hello-world-1\n\n# 中文 标题\n");
        const auto& b = doc.root.children;
        check_eq(b[0].anchor, std::string("hello-world"));
        check_eq(b[1].anchor, std::string("hello-world-1"));
        check_eq(b[2].anchor, std::string("hello-world-1-1"));
        check_eq(b[3].anchor, std::string("中文-标题"));
        check_eq(md::slug("  A_b-C? "), std::string("--a_b-c-"));
    });

    s.test("hashes: equal blocks hash equal, wherever they are; a change changes them", [] {
        const auto a = md::parse("para [x](/u)\n\n# H\n\npara [x](/u)\n");
        check_eq(a.root.children[0].hash, a.root.children[2].hash);
        check(a.root.children[0].hash != a.root.children[1].hash);
        const auto b = md::parse("para [x](/v)\n");
        check(b.root.children[0].hash != a.root.children[0].hash, "the destination counts, not the link's index");
    });

    s.test("plain text reads like the document", [] {
        const auto doc = md::parse("# T\n\nSome *words*.\n\n- one\n- two\n\n```\ncode\n```\n");
        check_eq(md::plain_text(doc.root), std::string("T\nSome words.\none\ntwo\ncode"));
    });

    s.test("extensions by name", [] {
        check(md::extension("gfm") == md::gfm);
        check(md::extension("tables") == md::Extension::tables);
        check(!md::extension("nope"));
        const std::vector<std::string> names{"commonmark", "math", "nope"};
        check(md::extensions(names) == md::Extension::math);
        check(md::has(md::gfm, md::autolinks) && !md::has(md::gfm, md::Extension::math));
        // Raw HTML is text without its extension, markup with it.
        check(md::parse("<div>\nx\n</div>\n").root.children[0].kind == md::Kind::html);
        check(md::parse("<div>\nx\n</div>\n", md::no_html).root.children[0].kind == md::Kind::paragraph);
    });

    s.test("to_html writes what md4c-html writes", [] {
        check_eq(md::to_html(md::parse("# Hi\n\n- [x] *a* & <b>\n")),
                 std::string("<h1>Hi</h1>\n<ul>\n<li class=\"task-list-item\"><input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled "
                             "checked><em>a</em> &amp; <b></li>\n</ul>\n"));
    });

    return s.run(argc, argv);
}
