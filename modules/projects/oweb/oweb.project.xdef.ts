// Who oweb is (D-208, D-275): read by the lint (the projects' order) and the rename command.
import { defineProject } from "mlibs/define";

export default defineProject({
    name: "oweb",
    display: "oweb",
    summary: "A site framework on omui: one semantic tree published as a site that people, search engines and agents each read in their own way.",
    packages: "oweb",
    programs: ["oweb"],
    // A framework on omui, beside the game layer: neither knows the other (plan 0.1.0 §4.4).
    tier: "framework",
    dependsOn: ["mlibs", "mrender", "meng.sim", "hanim", "meng.media", "spec", "omui"],
});
