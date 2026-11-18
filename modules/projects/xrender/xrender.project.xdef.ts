// Who xrender is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "omui/define";

export default defineProject({
    name: "xrender",
    display: "xrender",
    summary: "A CPU/GPU same-source, Web-first renderer whose pixels agents can review.",
    codeName: true,
    packages: "xrender",
    programs: ["xrender"],
    tier: "foundation",
    dependsOn: ["mlibs"],
});
