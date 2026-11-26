// Who omui is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "mlibs/define";

export default defineProject({
    name: "omui",
    display: "omui",
    summary: "A UI framework in which one semantic tree drives pixels, strong motion, accessibility, agents and tests.",
    packages: "omui",
    programs: ["omui", "omui-gen", "omui-mcp", "omui-devhost"],
    stateDir: ".omui",
    metadata: "package.metadata.openmui",
    tier: "framework",
    dependsOn: ["mlibs", "mrender", "meng.sim", "hanim", "meng.media", "spec"],
});
