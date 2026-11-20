// The `omui` command line's operations (D-150, W15 step 2; RC3.9): the subject
// `omui` (omui.cli's Project) behind the tool kernel's entries — `omui build
// --release` is omui.build, and `omui mcp` serves them as omui_* tools in the
// project the server runs in. A command that writes something to keep — a project, a
// package, a film, git references — is irreversible: the person is asked first.
// `dev`, `run`, `connect` and `mcp` hold a window or the terminal: they are
// `terminal` — a person's command line runs them, an agent's entries do not
// list them. `agent` and `page` are the protocol's catalogs (omui.agent). ops-gen makes the table the
// command line serves them by (src/catalog/cli.cppm) and the listings
// (src/catalog/omui-cli.ops.json, .md).
import { defineOps } from "mlibs/define";

export default defineOps({
  product: "omui-cli",
  module: "omui.cli",
  namespace: "omui::cli::ops",
  doc: "The project's commands: create, build, test, check, describe, package; dependencies, tools and the design's history.",
  generated: "src/catalog",
  operations: [
    {
      name: "omui.doctor",
      effect: "read",
      summary: "Which tools are there and which are missing, and the project's health: the general rules, the workspace's and omui's profiles (an app, an extension), each finding with its fix",
      params: [{ name: "dir", type: "path", optional: true, doc: "the project (default: here)" }],
      result: "json",
      cost: "bounded",
      keywords: ["health", "rules", "profile", "conventions"],
    },
    { name: "omui.templates", effect: "read", summary: "The templates a project is made from, and what each makes", result: "json", cost: "instant" },
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
    { name: "omui.build", effect: "reversible", summary: "Builds the project (mcpp build): its outputs under target/, nothing else changed", params: [{ name: "release", type: "bool", doc: "the release profile (no designer linked)", optional: true }, { name: "web", type: "bool", doc: "the page on the Web instead (D-236): the program run natively writes its first screen as static HTML (target/web/<program>.static.html), then mcpp pack --target wasm32-emscripten --release --pkg-format dist-toy packs it with the page", optional: true }, { name: "lang", type: "string", doc: "with web: the language of the first screen and the page (en, zh-CN)", optional: true }, { name: "rest", type: "json", doc: "words passed on to mcpp build as they are (after --)", optional: true }], result: "json", cost: "heavy", undo: "none needed: target/ is the build's own" },
    { name: "omui.test", effect: "reversible", summary: "Runs the project's tests (mtest): which passed, which failed and where", params: [{ name: "rest", type: "json", doc: "words passed on to mcpp test as they are (after --: which cases)", optional: true }], result: "json", cost: "heavy", undo: "none needed: tests change nothing they keep" },
    {
      name: "omui.dev",
      effect: "reversible",
      terminal: true,
      summary: "Runs the application in development mode: it is its own designer, a person and an agent design it live (D-149); an application in C++ runs in the development host, rebuilt as its sources change (W16)",
      params: [
        { name: "headless", type: "bool", doc: "no window: an agent designs through the protocol (the same operations, the same pixels)", optional: true },
        { name: "rest", type: "json", doc: "words passed on to the application as they are (after --)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none needed: it ends when stopped",
    },
    {
      name: "omui.run",
      effect: "reversible",
      terminal: true,
      summary: "Runs the application as it ships (mcpp run); after --, its own switches: --headless, --png FILE, --serve …",
      params: [
        { name: "release", type: "bool", doc: "an optimised build", optional: true },
        { name: "rest", type: "json", doc: "words passed on to the application as they are (after --)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none needed: it ends when stopped",
    },
    {
      name: "omui.connect",
      effect: "reversible",
      terminal: true,
      summary: "Starts the bridge a page in your browser pairs with, so an agent operates it with `omui page …` (it runs until stopped)",
      params: [
        { name: "code", type: "string", doc: "the pairing code the page shows (in the prompt it gave you)" },
        { name: "port", type: "integer", doc: "the port on 127.0.0.1 (default: the first free of 47110–47113)", optional: true },
        { name: "name", type: "string", doc: "who you are, as the page shows it (Claude Code, Codex …)", optional: true },
        { name: "origin", type: "string", doc: "serve only a page of this origin (https://example.org)", optional: true },
        { name: "dir", type: "path", doc: "where the bridge says where it is (default: the per-user directory)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none needed: it ends when stopped",
    },
    {
      name: "omui.mcp",
      effect: "reversible",
      terminal: true,
      summary: "An MCP server on standard input and output: agents operate running applications with ui_* tools, and the project's commands",
      params: [
        { name: "app", type: "string", doc: "the application a call names none of (default: the one running)", optional: true },
        { name: "actor", type: "string", doc: "who acts (default: the client's name, agent:claude-code)", optional: true },
        { name: "way", type: "string", doc: "semantic (default); a11y-tree or screenshot: what a browser agent's accessibility tree or computer use offers, on the same application (D-142)", optional: true },
        { name: "tools", type: "string", doc: "lean (default): the few tools most tasks use, and ui_more finding the rest by intent; all: every tool on every turn", optional: true },
        { name: "ops", type: "json", doc: "an application's verbs as tools, from its <product>.ops.json listing (ops-gen); given again for more", optional: true },
        { name: "dir", type: "path", doc: "where the running applications' endpoints are (default: the per-user directory)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "none needed: it ends when its input does",
    },
    { name: "omui.replay", effect: "reversible", summary: "Plays a recording (the protocol's `record`, development mode) in a fresh instance, headless, and checks it ends as it did: the semantic tree's hash and the pixels' (W12)", params: [{ name: "file", type: "path", doc: "the recording (JSON)" }], result: "json", cost: "heavy", undo: "none needed: a replay keeps nothing" },
    {
      name: "omui.check",
      effect: "read",
      summary: "Type-checks the project's TypeScript against the API it registers (tsc), or view documents: each problem at its line",
      params: [
        { name: "files", type: "json", doc: "*.view.xdef.json documents to check instead of the project", optional: true },
        { name: "portable", type: "bool", doc: "also what only this implementation offers (D-157)", optional: true },
      ],
      result: "json",
      cost: "heavy",
    },
    { name: "omui.describe", effect: "read", summary: "What writing for the project takes: every builder, native module, theme token and verb, as JSON", params: [{ name: "llms", type: "path", doc: "also write it as llms.txt, for a model's context", optional: true }], result: "json", cost: "heavy" },
    {
      name: "omui.fmt",
      effect: "reversible",
      summary: "View documents written in the canonical format; with check, only which are not",
      params: [
        { name: "files", type: "json", doc: "the *.view.xdef.json files" },
        { name: "check", type: "bool", doc: "change nothing: each file not in the canonical format said, exit 1", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "the files as they were (version control)",
    },
    { name: "omui.schema", effect: "reversible", summary: "The JSON Schema of view documents over the official components (written to out when given)", params: [{ name: "out", type: "path", doc: "write it to this file", optional: true }], result: "json", cost: "bounded", undo: "delete the file written" },
    { name: "omui.deps", effect: "read", summary: "The project's dependencies as its mcpp.toml declares them", result: "json", cost: "instant" },
    { name: "omui.tools", effect: "read", summary: "The tools the project names for xlings, and whether each is installed", result: "json", cost: "bounded" },
    { name: "omui.tools.install", effect: "irreversible", summary: "Installs the tools the project names (xlings install)", result: "json", cost: "heavy", undo: "none: xlings remove what it installed" },
    { name: "omui.config", effect: "read", summary: "The project's omui.config.ts over the defaults: how far agents may go, what the recorder keeps, whether the designer is linked", result: "json", cost: "bounded" },
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
      params: [{ name: "dir", type: "path", doc: "the application's directory (here when absent)", optional: true }],
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
