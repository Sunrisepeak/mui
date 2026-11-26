// Who mrender is (D-208): read by the lint (the projects' order) and the rename command.
import { defineProject } from "mlibs/define";

export default defineProject({
    name: "mrender",
    display: "mrender",
    summary: "A CPU/GPU same-source, Web-first renderer whose pixels agents can review.",
    packages: "mrender",
    programs: ["mrender"],
    tier: "foundation",
    dependsOn: ["mlibs"],
});
