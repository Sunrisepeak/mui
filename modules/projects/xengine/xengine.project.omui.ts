// Who xengine is (D-208): read by the lint (the projects' order) and the rename command. Its parts sit at
// different heights: the simulation kernels below hanim, the media kernels above it, the game layer above omui.
import { defineProject } from "omui/define";

const kernels = ["mlibs", "xrender", "xengine.sim", "hanim", "xengine.media"];
const simulation = ["dynamics", "cellular", "matter", "particles", "field"];

export default defineProject({
    name: "xengine",
    display: "xengine",
    summary: "An agent-native game engine system in which people and agents develop and play as equals.",
    codeName: true,
    packages: "xengine",
    programs: ["matter-lab", "omui-music"],
    dependsOn: [],
    parts: [
        ...simulation.map((kernel) => ({ path: `kernel/${kernel}`, name: "xengine.sim", dependsOn: ["mlibs", "xrender"] })),
        ...["audio", "video"].map((kernel) => ({ path: `kernel/${kernel}`, name: "xengine.media", dependsOn: ["mlibs", "xrender", "xengine.sim", "hanim"] })),
        // Above the interface; in it only the bridge knows omui (D-162).
        { path: "game", name: "xengine.game", dependsOn: [...kernels, "spec", "omui", "gongfu.core"], through: { omui: "game/ui" } },
    ],
});
