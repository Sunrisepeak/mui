import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

// Explicit ids are checked at compile time; an invalid literal does not compile
// (see spec/checks/invalid-id).
constexpr spec::IdLiteral delete_id = "notes/delete";
static_assert(delete_id.text == "notes/delete");

int main(int argc, char** argv) {
    Suite s{"spec.id"};

    s.test("segments and paths", [] {
        check(spec::valid_segment("inc"));
        check(spec::valid_segment("column:0"));
        check(spec::valid_explicit_segment("notes.delete-1_x"));
        check(!spec::valid_explicit_segment("column:0"), "':' is reserved for structural ids");
        check(!spec::valid_segment(""));
        check(!spec::valid_segment(".."));
        check(!spec::valid_segment("a b"));
        check(!spec::valid_segment("删除"), "S-9: no display text in ids");
        check(!spec::valid_segment(std::string(129, 'a')));
        check(spec::valid_path("app/column:0/inc"));
        check(!spec::valid_path("app//inc"));
        check(!spec::valid_path("/app"));
    });

    s.test("structural ids (S-8)", [] {
        check_eq(spec::structural_segment("text", 3), std::string("text:3"));
        check(spec::is_structural("column:0") && !spec::is_structural("inc"));
    });

    s.test("paths", [] {
        auto p = spec::Path::parse("app/column:0/inc");
        check(p.has_value());
        check_eq(p->last(), std::string_view("inc"));
        check_eq(p->parent().str(), std::string("app/column:0"));
        check_eq(p->depth(), std::size_t{3});
        check(p->within(*spec::Path::parse("app")));
        check(!p->within(*spec::Path::parse("ap")));
        check_eq(spec::Path{}.child("a").child("b").str(), std::string("a/b"));
        check(!spec::Path::parse("bad path").has_value());
    });

    s.test("glob matching", [] {
        check(spec::match_path("notes/*", "notes/delete"));
        check(!spec::match_path("notes/*", "notes/list/item:0"));
        check(spec::match_path("**/delete", "delete"));
        check(spec::match_path("**/delete", "a/b/delete"));
        check(spec::match_path("notes/**", "notes/list/item:0"));
        check(spec::match_path("*/item:?", "list/item:7"));
        check(!spec::match_path("*/item:?", "list/item:17"));
        check(spec::match_text("删除*", "删除笔记"));
        check(spec::match_text("*TODO*", "a TODO b"));
        check(!spec::match_text("a*b", "a"));
        check(spec::match_text(std::string(40, '*') + "x", std::string(10000, 'a') + "x"));
    });

    return s.run(argc, argv);
}
