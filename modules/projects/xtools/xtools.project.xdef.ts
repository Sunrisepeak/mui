// Who xtools is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "omui/define";

export default defineProject({
    name: "xtools",
    display: "xtools",
    summary: "The toolchain whole: one command and one MCP server over every project's toolkit.",
    codeName: true,
    packages: "xtools",
    programs: ["xtools", "xtools-inspector"],
    // A composition of every project's toolkit; nothing depends on it (plan §4.5).
    tier: "engine",
    dependsOn: ["mlibs", "xrender", "meng.sim", "hanim", "meng.media", "spec", "omui", "meng.game", "gongfu.core", "gongfu"],
});
