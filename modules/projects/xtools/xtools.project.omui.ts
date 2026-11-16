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
    dependsOn: ["mlibs", "xrender", "xengine.sim", "hanim", "xengine.media", "spec", "omui", "xengine.game", "gongfu.core", "gongfu"],
});
