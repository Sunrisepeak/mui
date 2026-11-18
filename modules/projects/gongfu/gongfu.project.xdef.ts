// Who gongfu is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "omui/define";

export default defineProject({
    name: "gongfu",
    display: "gongfu",
    summary: "A deterministic, rollback-ready fighting-game framework whose characters are data.",
    packages: "gongfu",
    programs: ["gongfu-studio"],
    tier: "engine",
    dependsOn: ["mlibs", "xrender", "xengine.sim", "hanim", "xengine.media", "gongfu.core"],
    parts: [
        // Its core alone (the standard library; mlibs for its tests): a fixture the game layer's tests may take.
        { path: ".", name: "gongfu.core", exact: true, dependsOn: ["mlibs"] },
    ],
});
