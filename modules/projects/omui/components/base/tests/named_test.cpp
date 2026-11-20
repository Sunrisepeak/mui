// The base components registered by name (B7b): an application's derived registry names the components its
// scripts create, and only those are registered — a name that is no base component (a layout's, a function's)
// registers nothing. A program of its own: the registry is the process's, and the other tests register them all.
import std;
import mcpplibs.mtest;
import omui.ui.base;

using namespace mcpplibs::mtest;

static UTest cases{"ui.base named", [] {
    "only the named base components are registered; other names register none"_test = [] {
        omui::register_base_named<"button", "segmented", "text_box", "column", "signal">();
        expect(omui::find_type("button") != nullptr && omui::find_type("segmented") != nullptr && omui::find_type("text_box") != nullptr);
        for (const std::string_view absent : {"slider", "timeline", "path", "data_grid", "code_editor"})
            expect(omui::find_type(absent) == nullptr) << absent;
        std::vector<std::string> base;
        for (const omui::ComponentType* type : omui::registered_types())
            if (type && std::ranges::contains(std::array{"button", "segmented", "text_box"}, type->name)) base.emplace_back(type->name);
        expect(eq(base.size(), 3uz));
    };

    "every base component has the name it is registered by, and they are 66"_test = [] {
        omui::register_base_components();
        expect(omui::find_type("timeline") != nullptr && omui::find_type("file_drop") != nullptr);
        std::size_t count = 0;
        [&]<class... Component>(omui::ComponentList<Component...>) { count = sizeof...(Component); }(omui::BaseComponents{});
        expect(eq(count, 66uz));
    };
}};
