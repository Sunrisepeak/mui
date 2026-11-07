// openmui:agent_guide — what an agent needs to operate an application
// (agent connect §5.2), written from the application itself: agent.md, and
// its machine-readable twin (/.well-known/openmui-agent.json). How to link
// (the pairing code, the bridge — `omui connect`, or one written from the
// protocol here), the tools, the application's own verbs and the places
// its navigation offers, its self-check, and the rules. Every openmui
// application writes them with `--agent-guide DIR` (beside its Web page).
export module omui:agent_guide;

import std;
import omui.core;
import omui.view;
import :app;

export namespace omui {

struct AgentGuide {
    std::string markdown;  // agent.md
    std::string json;      // .well-known/openmui-agent.json
};

// Where prebuilt `omui` comes from: its releases, each file with its SHA-256
// in SHA256SUMS and its build attested (GitHub's build provenance).
inline constexpr std::string_view omui_releases = "https://github.com/Sunrisepeak/mui/releases/latest";
inline constexpr std::string_view omui_repository = "Sunrisepeak/mui";

// The application as it is mounted (its verbs; its tree's navigation, as `snapshot`).
[[nodiscard]] AgentGuide agent_guide(const AppSpec& app, Runtime& runtime, const Value& snapshot);

// Writes agent.md and .well-known/openmui-agent.json into `dir`, and links
// the page there (index.html, when there is one) to them:
// <link rel="openmui-agent" href="agent.md">.
std::expected<void, std::string> write_agent_guide(const AgentGuide& guide, const std::filesystem::path& dir);

}  // namespace omui
