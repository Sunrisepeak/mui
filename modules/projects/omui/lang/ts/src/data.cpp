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

// ── looks (omui.theme; until their formats join the spec) ───────────────

/** "#rgb", "#rrggbb", "#rrggbbaa", or an exact alpha as CSS writes it: "#0f172a / 0.08". */
export type Colour = string;
/** A text style, field by field: a layer over a theme keeps the fields it does not name. */
export interface TextStyle {
    size?: number;
    weight?: number;
    line_height?: number;
    letter_spacing?: number;
    /** First preferred. */
    families?: string[];
}
export interface ShadowLayer {
    x?: number;
    y?: number;
    blur?: number;
    spread?: number;
    color?: Colour;
}
/** The key light's shadow, and under it a wide, faint ambient one. */
export interface Shadow extends ShadowLayer {
    /** 0 … 1: how much of the colour of what casts it the shadow takes. */
    tint?: number;
    ambient?: ShadowLayer;
}
/** Another token's name ("motion.instant"), a tween (milliseconds) or a spring. */
export type MotionToken =
    | string
    | { duration?: number; easing?: string; delay?: number }
    | { stiffness?: number; damping?: number; mass?: number; delay?: number };
/** The design tokens a layer sets (a theme's whole set, or what it changes). */
export interface Tokens {
    /** Corners' continuous curvature: 0 circular, about 0.6 iOS's. */
    corner_smoothing?: number;
    colors?: { [name: string]: Colour };
    text?: { [name: string]: TextStyle };
    spacing?: { [name: string]: number };
    radius?: { [name: string]: number };
    shadows?: { [name: string]: Shadow };
    motion?: { [token: string]: MotionToken };
}
/** A theme (a whole set, or the changes to the one it `extends`), or an overlay over any theme. */
export interface Theme extends Tokens {
    doc?: string;
    extends?: string;
    dark?: boolean;
    /** A layer over whichever theme it is composed with: it extends nothing and says nothing of dark. */
    overlay?: boolean;
    /** Controls' metrics, scaled: 0.5 … 2 (compact: 0.8). */
    density?: number;
}
/** `export default defineTheme({ light: {…}, dark: { extends: "light", dark: true, … } })`. */
export declare function defineTheme(themes: { [name: string]: Theme }): Defined<"theme">;

/** An effect on the window, a component type or `role:<role>`: its parameters by name, a colour by the theme's name. */
export interface EffectRule {
    effect: string;
    /** Seconds. */
    duration?: number;
    delay?: number;
    ease?: string;
    transition?: number;
    usage?: "appearance" | "feedback";
    /** A parameter's endpoints, played over `duration`. */
    animate?: { [param: string]: [number, number] };
    reverse?: boolean;
    [param: string]: string | number | boolean | object | undefined;
}
/** A motion for a slot: a recipe's name, candidates in order, or a recipe with its fields. */
export type SlotMotion = string | string[] | { [field: string]: unknown };
export type Slot = "update" | "enter" | "exit" | "state" | "reorder" | "replace";
/** A style pack (S2): a theme to start from, the tokens it changes, its effects and motions; the semantic tree the same under every one. */
export interface Style extends Tokens {
    doc?: string;
    /** "dark", or a theme and overlays: "light+compact". */
    theme: string;
    /** Seconds a switch to it is seen as a cross-fade (0.35). */
    crossfade?: number;
    effects?: { [selector: string]: EffectRule };
    /** A slot's motion for every node the rules move, or a type's or a role's slots. */
    motions?: { [slotOrTarget: string]: SlotMotion | { [slot in Slot]?: SlotMotion } };
}
/** `export default defineStyle({ neon: { theme: "dark", effects: { button: { effect: "neon", radius: 12 } } } })`. */
export declare function defineStyle(styles: { [name: string]: Style }): Defined<"style">;

// ── motion recipes ───────────────────────────────────────────────────────

export declare function defineMotion(motion: { [field: string]: unknown }): Defined<"motion">;
)ts";
}

}  // namespace omui::lang::ts
