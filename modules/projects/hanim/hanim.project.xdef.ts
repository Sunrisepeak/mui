// Who hanim is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "omui/define";

export default defineProject({
    name: "hanim",
    display: "hanim",
    summary: "A composable, agent-native animation engine: one set of capabilities for UI animation, video and game animation.",
    packages: "hanim",
    programs: ["hanim", "hanim-timeline"],
    tier: "foundation",
    dependsOn: ["mlibs", "xrender", "meng.sim"],
});
