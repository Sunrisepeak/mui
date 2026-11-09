// The module "openmui/define" (omui.lang.ts:data), the same everywhere.
module omui.lang.ts;

import std;

namespace omui::lang::ts {

std::string define_module_source(bool spec) {
    std::string out = std::format("// {}/define: what a data module's default export is (omui.lang.ts:data, D-157).\n"
                                  "const define = (kind) => (value) => Object.freeze({{ kind, value }});\n",
                                  spec ? "openmui" : "omui");
    for (const auto kind : spec ? std::span<const std::string_view>(spec_kinds) : std::span<const std::string_view>(omui_kinds)) {
        std::string name(kind);
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        out += std::format("export const define{} = define(\"{}\");\n", name, kind);
    }
    return out;
}

std::string_view omui_define_types() {
    return R"ts(// omui/define — omui's own data formats written as TypeScript data modules
// (D-139, D-157): an application's configuration, an operation catalog.
// A module's default export is one define call, evaluated to the JSON omui reads.

/** What a define call makes: the document of one kind, evaluated to JSON. */
export interface Defined<Kind extends string> {
    readonly kind: Kind;
}

// ── an application's configuration (omui.config.ts) ──────────────────────

/** How far agents may go (D-155): what the build says, a developer's full control, or asking the person before what cannot be undone. */
export type AgentMode = "automatic" | "development" | "dual-user";
/** What the interaction recorder keeps (D-148). */
export type RecordLevel = "off" | "errors" | "actions" | "interactions" | "full";
/** A setting in development (`omui dev`) and in a release (`omui run`). */
export interface ByMode<T> {
    development?: T;
    release?: T;
}
export interface Config {
    agent?: ByMode<AgentMode>;
    record?: ByMode<RecordLevel>;
}
/** `export default defineConfig({ agent: { release: "dual-user" } })`; a switch given at launch wins. */
export declare function defineConfig(config: Config): Defined<"config">;

// ── an operation catalog (`omui-gen ops`: CLI, MCP and the reference) ────

/** How much an operation changes (openas §2.4). */
export type Effect = "read" | "reversible" | "irreversible";
/** How long it takes: at once, within a frame, a bounded time, or long enough to show progress. */
export type Cost = "instant" | "frame" | "bounded" | "heavy";
export type ParamType = "bool" | "integer" | "number" | "seconds" | "string" | "json" | "path" | (string & {});

export interface Param {
    name: string;
    type: ParamType;
    doc?: string;
    optional?: boolean;
}
export interface Operation {
    /** Dotted: `fighter.bake`. */
    name: string;
    effect: Effect;
    summary: string;
    params?: Param[];
    /** What it answers: `json`, a type, or a description. */
    result?: string;
    cost?: Cost;
    /** How to take it back, for what changes. */
    undo?: string;
    // ── less often ──
    /** Only a person may run it. */
    person?: boolean;
    cancellable?: boolean;
    /** It runs another product's operation. */
    delegated?: boolean;
    /** The runtime's undo takes it back. */
    undoable?: boolean;
}
/** A level an agent plays at (games): how it sees and how fast it may act. */
export interface Tier {
    name: string;
    doc?: string;
    view?: "drawn" | "full";
    reaction_ms?: number;
    actions_per_second?: number;
    /** The operations open at this tier. */
    operations?: string[];
}
export interface Catalog {
    product: string;
    doc: string;
    operations: Operation[];
    /** Where the listings go (`media/ops`). */
    generated?: string;
    /** A program's module and namespace, for the catalog's C++. */
    module?: string;
    namespace?: string;
    /** The partition holding it (`catalog` when absent): a module with two catalogs names the second. */
    partition?: string;
    tiers?: Tier[];
}
export declare function defineOps(catalog: Catalog): Defined<"ops">;

// ── looks and motion (until their formats join the spec) ────────────────

/** A theme, a style pack, motion recipes: as their TOML files have them, as JSON. */
export declare function defineTheme(theme: { [field: string]: unknown }): Defined<"theme">;
export declare function defineStyle(style: { [field: string]: unknown }): Defined<"style">;
export declare function defineMotion(motion: { [field: string]: unknown }): Defined<"motion">;
)ts";
}

}  // namespace omui::lang::ts
