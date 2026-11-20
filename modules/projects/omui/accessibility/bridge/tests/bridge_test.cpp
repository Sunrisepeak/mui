// The bridge installed: AccessKit's service where it has a library, the
// update it would be told; elsewhere nothing installed.
import std;
import omui.core;
import omui.view;
import omui.accessibility;
import omui.accessibility.bridge;
import mlibs.text;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace a11y = omui::accessibility;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

}  // namespace

static UTest cases{"accessibility.bridge", [] {
    "the update AccessKit would be told"_test = [] {
        a11y::bridge::install();
        RuntimeOptions o;
        o.fonts = &fonts();
        o.size = {320, 200};
        Runtime rt(o);
        Signal<int> count{0};
        rt.mount("main", column({.padding = 8, .gap = 4},
                                {text({.id = "count", .text = derive([count] { return std::format("{} notes", count.get()); })}),
                                 box({.id = "add", .width = 60, .height = 24, .label = "Add note",
                                      .on = {{"press", [count](const Value&) { count.set(count.peek() + 1); }}}})}));
        rt.frame({std::chrono::seconds(0), {}});
        a11y::Mirror mirror(rt, {.name = "Notes"});
        (void)mirror.refresh();
        const std::string text = a11y::bridge::debug(mirror, {.scale = 2});
        if (!a11y::available()) {  // AccessKit has no library here
            expect(text.empty() && a11y::service() == "none");
            return;
        }
        expect(text.find("role: Window") != std::string::npos && text.find("\"Notes\"") != std::string::npos) << text;
        expect(text.find("label: \"Add note\"") != std::string::npos && text.find("Click") != std::string::npos) << text;
        expect(text.find("value: \"0 notes\"") != std::string::npos) << text;
        expect(text.find("toolkit_name: Some(\"openmui\")") != std::string::npos) << text;
        expect(!a11y::service().empty() && a11y::service() != "none");
    };
}};
