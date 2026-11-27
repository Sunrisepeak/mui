// omui-devtools ci plan (D-254): the repository's lane catalogue against its workflows, a selection
// read left to right, a commit's `CI:` trailer, and the plan each event follows.
import std;
import mcpplibs.mtest;
import omui.devtools.lanes;

using namespace mcpplibs::mtest;
namespace lanes = omui::devtools::lanes;

namespace {

std::filesystem::path repository() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path().parent_path();
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

constexpr std::string_view small = R"({
  "lanes": [
    { "name": "linux", "rows": [{ "os": "ubuntu-24.04", "toolchain": "llvm@23.1.3", "profile": "ci" }], "minutes": 130, "what": "Linux" },
    { "name": "macos", "rows": [{ "os": "macos-15", "toolchain": "llvm@23.1.3", "profile": "ci" }], "minutes": 161, "what": "macOS" },
    { "name": "gcc", "rows": [{ "os": "ubuntu-24.04", "toolchain": "gcc@16.1.0", "profile": "dev" },
                              { "os": "ubuntu-24.04", "toolchain": "gcc@16.1.0", "profile": "release" }], "minutes": 300, "what": "GCC" },
    { "name": "web", "job": "web", "minutes": 71, "what": "Web" },
    { "name": "android", "job": "android", "minutes": 15, "what": "Android" },
    { "name": "pages", "workflow": "pages.yml", "minutes": 50, "what": "the site" }
  ],
  "groups": [ { "name": "all", "lanes": ["linux", "macos", "web", "android"], "what": "a node's" } ],
  "events": { "pull_request": "", "workflow_dispatch": "all", "push": "all gcc", "schedule": "all gcc" }
})";

lanes::Catalogue catalogue() { return *lanes::read(small); }

std::vector<std::string> selected(std::string_view selection) {
    auto chosen = lanes::select(catalogue(), selection);
    return chosen ? *chosen : std::vector<std::string>{"error: " + chosen.error()};
}

}  // namespace

static UTest cases{"devtools.lanes", [] {

    "the repository's catalogue reads, and every lane is wired in its workflow"_test = [] {
        const auto root = repository();
        const auto read_catalogue = lanes::read(read(root / ".github/ci.lanes.xdef.json"));
        expect(read_catalogue.has_value()) << (read_catalogue ? std::string() : read_catalogue.error());
        if (!read_catalogue) return;
        const std::string ci = read(root / ".github/workflows/ci.yml");
        expect(ci.contains("-- ci plan --dispatch")) << "ci.yml plans its run with omui-devtools ci plan";
        expect(ci.contains("fromJSON(needs.lint.outputs.matrix)")) << "the matrix is the plan's";
        for (const auto& lane : read_catalogue->lanes) {
            if (!lane.job.empty()) {
                expect(ci.contains(std::format("\n  {}:\n", lane.job))) << lane.job << ": a job of ci.yml";
                expect(ci.contains(std::format("needs.lint.outputs.{} == 'true'", lane.name))) << lane.name << ": its job runs when the plan says";
                expect(ci.contains(std::format("      {}: ${{{{ steps.plan.outputs.{} }}}}", lane.name, lane.name)))
                    << lane.name << ": the lint job passes the plan's output on";
            }
            if (!lane.workflow.empty()) {
                const std::string workflow = read(root / ".github/workflows" / lane.workflow);
                expect(workflow.contains("workflow_dispatch:")) << lane.workflow << ": can be requested";
            }
            expect(lane.minutes > 0 && !lane.what.empty()) << lane.name << ": says what it checks and what it costs";
        }
        const auto node = lanes::plan(*read_catalogue, "workflow_dispatch", "", "");
        expect(node && node->rows.size() == 4u && node->workflows.empty()) << "a node's run: four LLVM rows, no workflow";
    };

    "a selection is read left to right: lanes and groups add, -name takes away, none empties"_test = [] {
        expect(selected("macos linux") == std::vector<std::string>{"linux", "macos"}) << "in the catalogue's order";
        expect(selected("all -android,-web") == std::vector<std::string>{"linux", "macos"});
        expect(selected("all gcc -linux") == std::vector<std::string>{"macos", "gcc", "web", "android"});
        expect(selected("all none pages") == std::vector<std::string>{"pages"});
        expect(selected("-all macos") == std::vector<std::string>{"macos"}) << "taking away what is not there is nothing";
        expect(selected("").empty());
        const auto unknown = lanes::select(catalogue(), "linux mac");
        expect(!unknown && unknown.error().contains("`mac`") && unknown.error().contains("macos") && unknown.error().contains("all"))
            << "an unknown name lists the names there are";
    };

    "a commit's CI: trailer is a line of its last paragraph"_test = [] {
        expect(lanes::trailer("Site: tabs\n\nThe body.\n\nCI: macos windows\n") == std::optional<std::string>("macos windows"));
        expect(lanes::trailer("Site: tabs\n\nCI: pages") == std::optional<std::string>("pages")) << "a body that is only the trailer";
        expect(!lanes::trailer("CI: macos")) << "a subject is not a trailer";
        expect(!lanes::trailer("Subject\n\nCI: the matrix runs on demand now.\n\nThe last paragraph.\n")) << "prose in another paragraph";
        expect(lanes::trailer("Subject\n\nBody.\n\nCI: linux\nCI: none\n") == std::optional<std::string>("none")) << "the last one says";
    };

    "a pull request's push runs the lint and requests what its commit names"_test = [] {
        const auto bare = lanes::plan(catalogue(), "pull_request", "", "Subject\n\nBody.\n");
        expect(bare && bare->lanes.empty() && bare->rows.empty() && bare->requested.empty() && bare->workflows.empty());
        const auto named = lanes::plan(catalogue(), "pull_request", "", "Subject\n\nBody.\n\nCI: macos web pages\n");
        expect(named && named->rows.empty() && named->jobs.empty()) << "nothing of ci.yml in the pull request's run";
        expect(named && named->requested == std::vector<std::string>{"macos", "web"}) << "requested as a run of their own";
        expect(named && named->workflows == std::vector<std::string>{"pages.yml"});
        expect(named && named->minutes == 161 + 71 + 50);
        const auto wrong = lanes::plan(catalogue(), "pull_request", "", "Subject\n\nCI: mac\n");
        expect(!wrong && wrong.error().contains("trailer")) << "a wrong name fails the plan, and says where it came from";
    };

    "a run by hand, main and the schedule test their lanes themselves"_test = [] {
        const auto by_hand = lanes::plan(catalogue(), "workflow_dispatch", "all -android pages", "");
        expect(by_hand && by_hand->rows.size() == 2u && by_hand->jobs == std::vector<std::string>{"web"} && by_hand->requested.empty());
        expect(by_hand && by_hand->workflows == std::vector<std::string>{"pages.yml"});
        const auto main = lanes::plan(catalogue(), "push", "", "Subject\n\nCI: none\n");
        expect(main && main->rows.size() == 4u) << "main ignores a trailer: two LLVM rows and GCC's two";
        const auto defaulted = lanes::plan(catalogue(), "workflow_dispatch", "", "");
        expect(defaulted && defaulted->source == "workflow_dispatch's default" && defaulted->jobs.size() == 2u);
        const auto request = lanes::plan(catalogue(), "request", "macos pages", "");
        expect(request && request->requested == std::vector<std::string>{"macos"} && request->rows.empty()) << "the command line requests";
        expect(!lanes::plan(catalogue(), "request", "", "")) << "a request names its lanes";
    };

    "the step's outputs: the matrix, whether it has rows, each job's lane"_test = [] {
        const auto planned = lanes::plan(catalogue(), "workflow_dispatch", "macos web", "");
        expect(planned.has_value());
        if (!planned) return;
        const std::string text = lanes::outputs(catalogue(), *planned);
        expect(text.contains("matrix=[{\"os\":\"macos-15\",\"profile\":\"ci\",\"toolchain\":\"llvm@23.1.3\"}]\n")) << text;
        expect(text.contains("rows=true\n") && text.contains("web=true\n") && text.contains("android=false\n")) << text;
        expect(lanes::describe(catalogue(), *planned).contains("About 232 runner minutes")) << lanes::describe(catalogue(), *planned);
    };

    "a catalogue that cannot be followed is refused"_test = [] {
        expect(!lanes::read(R"({"lanes":[{"name":"x"}]})")) << "neither rows, a job nor a workflow";
        expect(!lanes::read(R"({"lanes":[{"name":"x","job":"x"},{"name":"x","job":"y"}]})")) << "a name twice";
        expect(!lanes::read(R"({"lanes":[{"name":"x","job":"x"}],"groups":[{"name":"g","lanes":["y"]}]})")) << "a group naming no lane";
        expect(!lanes::read(R"({"lanes":[{"name":"x","job":"x"}],"events":{"push":"y"}})")) << "an event's default naming no lane";
    };
}};
