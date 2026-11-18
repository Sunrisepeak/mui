// Who meng is (D-208): read by the lint (the projects' order) and the rename command. Its parts sit at
// different heights: the simulation kernels below hanim, the media kernels above it, the game layer above omui.
import { defineProject } from "omui/define";

const kernels = ["mlibs", "xrender", "meng.sim", "hanim", "meng.media"];
const simulation = ["dynamics", "cellular", "matter", "particles", "field"];

export default defineProject({
    name: "meng",
    display: "meng",
    summary: "An agent-native game engine system in which people and agents develop and play as equals.",
    packages: "meng",
    programs: ["matter-lab", "omui-music"],
    tier: "engine",
    dependsOn: [],
    parts: [
        ...simulation.map((kernel) => ({ path: `kernel/${kernel}`, name: "meng.sim", dependsOn: ["mlibs", "xrender"] })),
        ...["audio", "video"].map((kernel) => ({ path: `kernel/${kernel}`, name: "meng.media", dependsOn: ["mlibs", "xrender", "meng.sim", "hanim"] })),
        // Above the interface; in it only the bridge knows omui (D-162).
        { path: "game", name: "meng.game", dependsOn: [...kernels, "spec", "omui", "gongfu.core"], through: { omui: "game/ui" } },
    ],
});
