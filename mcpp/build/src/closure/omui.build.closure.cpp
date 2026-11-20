module omui.build.closure;

import std;
import mcpp;
import omui.build.plan;

using namespace mlibs::build;

namespace omui::build {

bool closure(closure_options opt) {
    const std::string graph = mcpp::graph_file();
    if (graph.empty()) return true;  // a dependency's build: the application derives its own
    const std::string gen = generator();
    if (gen.empty()) {
        std::println(std::cerr, "omui.build.closure: closure() needs omui-gen built for the build");
        return false;
    }
    const std::filesystem::path package(mcpp::manifest_dir());
    std::vector<std::string> files;
    for (const std::string& named : opt.files) {
        const std::filesystem::path path = package / std::filesystem::path(named);
        std::error_code error;
        if (std::filesystem::is_directory(path, error)) {
            std::vector<std::string> found;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path, error))
                if (const std::string file = utf8(entry.path()); entry.is_regular_file() && (file.ends_with(".ts") || file.ends_with(".js")))
                    found.push_back(file);
            std::ranges::sort(found);
            files.insert(files.end(), found.begin(), found.end());
        } else if (std::filesystem::is_regular_file(path, error)) {
            files.push_back(utf8(path));
        } else {
            std::println(std::cerr, "omui.build.closure: {} names nothing here", named);
            return false;
        }
    }
    const std::string unit = opt.out_dir + "/omui/closure.cpp", report = opt.out_dir + "/closure.json";
    edge e{.id = "closure", .description = "the components the application's scripts take by name (B7b)"};
    e.command = {gen, "closure"};
    e.command.insert(e.command.end(), files.begin(), files.end());
    e.command.insert(e.command.end(), {"--out", unit, "--report", report, "--graph", graph});
    e.inputs = files;
    e.inputs.insert(e.inputs.end(), {gen, graph});
    if (std::error_code error; std::filesystem::is_regular_file(package / opt.config, error)) {
        const std::string config = utf8(package / opt.config);
        e.command.insert(e.command.end(), {"--config", config});
        e.inputs.push_back(config);
    }
    e.outputs = {unit};
    e.imports = {"omui.ui.base"};
    mcpp::rerun_if_changed(graph.c_str());
    return submit(std::array{e});
}

}  // namespace omui::build
