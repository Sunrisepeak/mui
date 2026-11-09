// The `omui` command line's operations (D-150, W15 step 2): what an Agent may
// run through MCP (`omui mcp` serves them as omui_* tools) in the project the
// server runs in. A command that writes something to keep — a project, a
// package, a film, git references — is irreversible: the person is asked first.
// `dev` and `run` are left out (they hold a window or the terminal), so are
// `mcp` and `agent` (an agent is already in). omui-gen ops makes the table the
// command line serves them by (src/catalog/cli.cppm) and the listings
// (src/catalog/omui-cli.ops.json, .md).
import { defineOps } from "omui/define";

export default defineOps({
  product: "omui-cli",
  module: "omui.cli",
  namespace: "omui::cli::ops",
  doc: "The project's commands: create, build, test, check, describe, package; dependencies, tools and the design's history.",
  generated: "src/catalog",
  operations: [
    { name: "omui.doctor", effect: "read", summary: "Which tools are there and which are missing (mcpp, xlings, tsc, esbuild), and what each is for", result: "json", cost: "bounded" },
    {
      name: "omui.new",
      effect: "irreversible",
      summary: "A new project from a template (app-ts by default): a directory of files, written beside the one the server runs in",
      params: [
        { name: "name", type: "string", doc: "the project's name and directory" },
        { name: "template", type: "string", doc: "app-ts, app-cpp, component, native or theme", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none: delete the directory it made",
    },
    { name: "omui.build", effect: "reversible", summary: "Builds the project (mcpp build): its outputs under target/, nothing else changed", params: [{ name: "release", type: "bool", doc: "the release profile (no designer linked)", optional: true }], result: "json", cost: "heavy", undo: "none needed: target/ is the build's own" },
    { name: "omui.test", effect: "reversible", summary: "Runs the project's tests (mtest): which passed, which failed and where", result: "json", cost: "heavy", undo: "none needed: tests change nothing they keep" },
    {
      name: "omui.check",
      effect: "read",
      summary: "Type-checks the project's TypeScript against the API it registers (tsc), or view documents: each problem at its line",
      params: [{ name: "portable", type: "bool", doc: "also what only this implementation offers (D-157)", optional: true }],
      result: "json",
      cost: "heavy",
    },
    { name: "omui.describe", effect: "read", summary: "What writing for the project takes: every builder, native module, theme token and verb, as JSON", result: "json", cost: "heavy" },
    { name: "omui.deps", effect: "read", summary: "The project's dependencies as its mcpp.toml declares them", result: "json", cost: "instant" },
    { name: "omui.tools", effect: "read", summary: "The tools the project names for xlings, and whether each is installed", result: "json", cost: "bounded" },
    { name: "omui.config", effect: "read", summary: "The project's omui.config.ts over the defaults: how far agents may go, what the recorder keeps", result: "json", cost: "bounded" },
    {
      name: "omui.add",
      effect: "reversible",
      summary: "Adds a dependency to mcpp.toml (mcpp add)",
      params: [{ name: "package", type: "string", doc: "a package of mcpp-index, as name or name@version" }],
      result: "json",
      cost: "bounded",
      undo: "omui.remove",
    },
    {
      name: "omui.remove",
      effect: "reversible",
      summary: "Removes a dependency from mcpp.toml (mcpp remove)",
      params: [{ name: "package", type: "string", doc: "a dependency the project has" }],
      result: "json",
      cost: "bounded",
      undo: "omui.add",
    },
    {
      name: "omui.package",
      effect: "irreversible",
      summary: "Packages the application for people to install (mcpp pack): an artifact written to keep and hand out",
      params: [{ name: "format", type: "string", doc: "a format mcpp pack makes (dist-toy: a Bilibili Toy page)", optional: true }],
      result: "json",
      cost: "heavy",
      undo: "none: delete the package it wrote",
    },
    {
      name: "omui.design.sync",
      effect: "irreversible",
      summary: "Carries Live Design's history into git: each design commit a commit on design/<branch>, tags as design/<tag> (the work tree untouched)",
      result: "json",
      cost: "bounded",
      undo: "none: delete the design/* references",
    },
    {
      name: "omui.design.film",
      effect: "irreversible",
      summary: "The design's history as a film (WebM): each step as it was presented, captioned",
      params: [
        { name: "out", type: "path", doc: "the film (design.webm)", optional: true },
        { name: "branch", type: "string", doc: "whose commits", optional: true },
        { name: "fps", type: "integer", doc: "frames a second (30)", optional: true },
        { name: "seconds", type: "number", doc: "each step's time on screen (1.5)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none: delete the film",
    },
  ],
});
