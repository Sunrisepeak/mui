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
    /** Whether the designer is linked (D-149): in development by default; a release leaves it out at compile time unless the application is a design tool that ships it. */
    designer?: ByMode<boolean>;
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

// ── sound and music (xengine.audio) ─────────────────────────────────────

/** A number, or [from, to] over the layer's length. */
export type Glide = number | [number, number];
/** One source of a sound: an oscillator, noise or a recording, shaped. */
export interface SoundLayer {
    wave?: "sine" | "triangle" | "square" | "saw" | "noise" | "recording";
    /** Seconds after the sound starts. */
    at?: number;
    /** Seconds. */
    length?: number;
    /** Hz. */
    pitch?: Glide;
    envelope?: { attack?: number; hold?: number; decay?: number; sustain?: number; release?: number };
    filter?: { kind?: "lowpass" | "highpass" | "bandpass"; cutoff?: Glide; q?: number };
    gain?: number;
    pan?: number;
    drive?: number;
    seed?: number;
    /** A recording's layer: its name, and its speed (1: as recorded). */
    recording?: string;
    speed?: Glide;
    doc?: string;
    name?: string;
}
export interface Sound {
    gain?: number;
    bus?: string;
    layers: SoundLayer[];
    doc?: string;
}
/** `export default defineSound({ hit: { layers: [{ wave: "noise", length: 0.1 }] } })`. */
export declare function defineSound(sounds: { [name: string]: Sound }): Defined<"sound">;

export interface Adsr {
    attack?: number;
    decay?: number;
    sustain?: number;
    release?: number;
}
/** How an instrument sounds: a built-in patch shaded (`base`), or an engine's own. */
export interface Patch {
    base?: string;
    engine?: "analog" | "fm" | "pluck" | "drum";
    gain?: number;
    glide?: number;
    drive?: number;
    amp?: Adsr;
    osc?: { wave?: "sine" | "triangle" | "square" | "saw" | "noise"; unison?: number; detune?: number; spread?: number; width?: number; pwm?: number; pwm_rate?: number; sub?: number; noise?: number };
    fm?: { ratio?: number; index?: number; sustain?: number; decay?: number; feedback?: number };
    pluck?: { brightness?: number; decay?: number; pick?: number };
    drum?: { tone?: number; pitch?: number; from?: number; sweep?: number; decay?: number; modes?: number; noise?: number; noise_decay?: number; metal?: number; click?: number; bursts?: number };
    filter?: Adsr & { kind?: "none" | "lowpass" | "highpass" | "bandpass"; cutoff?: number; q?: number; envelope?: number; keytrack?: number };
    vibrato?: { depth?: number; rate?: number; delay?: number };
    doc?: string;
}
export interface Instrument {
    patch: string;
    bus?: string;
    gain?: number;
    pan?: number;
    legato?: number;
    humanize?: number;
    transpose?: number;
    voices?: number;
    /** A level for each bus it sends to. */
    sends?: { [bus: string]: number };
    /** Open (`x`) and shut (`.`) sixteenths. */
    gate?: string;
    choke?: string;
    doc?: string;
}
export type MusicEffect =
    | { kind: "lowpass" | "highpass" | "bandpass"; cutoff?: number; q?: number; wet?: number; dry?: number }
    | { kind: "echo"; time?: number; beats?: number; feedback?: number; pingpong?: boolean; wet?: number; dry?: number }
    | { kind: "reverb"; size?: number; damping?: number; width?: number; predelay?: number; wet?: number; dry?: number }
    | { kind: "chorus"; delay?: number; depth?: number; rate?: number; voices?: number; wet?: number; dry?: number };
export interface Bus {
    to?: string;
    gain?: number;
    effects?: MusicEffect[];
    /** Ducked under another bus (a kick under the bass). */
    duck?: { key: string; depth?: number; threshold?: number; attack?: number; release?: number };
    doc?: string;
}
/** Notes as text ("D5 . F5 G5 | A5:2"), or a lane of steps an instrument ("x...x..."). */
export interface Pattern {
    instrument?: string;
    notes?: string;
    lanes?: { [instrument: string]: string };
    /** A note: "1/16", "1/12" (triplets) … */
    step?: string;
    swing?: number;
    doc?: string;
}
export interface MusicLayer {
    pattern: string;
    instrument?: string;
    /** It plays while the intensity is in [from, to). */
    intensity?: [number, number];
    at?: number;
    times?: number;
    transpose?: number;
    gain?: number;
}
export interface Section {
    bars: number;
    layers?: MusicLayer[];
    doc?: string;
}
export interface Music {
    doc?: string;
    name?: string;
    /** Beats a minute. */
    tempo?: number;
    /** [beats in a bar, the beat's note]. */
    meter?: [number, number];
    gain?: number;
    patches?: { [name: string]: Patch };
    instruments?: { [name: string]: Instrument };
    buses?: { [name: string]: Bus };
    patterns?: { [name: string]: Pattern };
    sections?: { [name: string]: Section };
    arrangement: { intro?: string[]; loop: string[]; outro?: string[] };
}
/** `export default defineMusic({ name: "study", tempo: 128, instruments: {…}, …, arrangement: { loop: ["a"] } })`. */
export declare function defineMusic(piece: Music): Defined<"music">;

// ── timelines and subtitles (xengine.timeline) ──────────────────────────

/** A time: "90f" (frames), "1.5s", "250ms", "123fl" (flicks), or a number of seconds. */
export type Time = string | number;
/** A key of a property: [time, value], or [time, value, ease] ("out_cubic", "cubic-bezier(…)", "hold"). */
export type Key = [Time, number] | [Time, number, string];
export interface Clip {
    name?: string;
    /** What it shows or plays: a file, "shot:<name>", "line:<id>" … */
    source?: string;
    at?: Time;
    length: Time;
    /** Where in its source it begins. */
    in?: Time;
    speed?: number;
    level?: number;
    fade?: { in?: Time; out?: Time; ease?: string };
    properties?: { name: string; keys: Key[] }[];
    doc?: string;
}
export interface Track {
    name?: string;
    kind?: "video" | "audio" | "animation" | "events";
    muted?: boolean;
    clips?: Clip[];
    doc?: string;
}
export interface Timeline {
    doc?: string;
    name?: string;
    /** Frames a second, or "30000/1001". */
    rate?: number | string;
    sample_rate?: number;
    /** [width, height]. */
    size?: [number, number];
    markers?: { at: Time; name: string }[];
    tracks?: Track[];
}
/** `export default defineTimeline({ name: "promo", rate: 60, tracks: [{ name: "picture", kind: "video", clips: […] }] })`. */
export declare function defineTimeline(timeline: Timeline): Defined<"timeline">;
/** A film's lines, each in both languages. */
export declare function defineSubtitles(subtitles: { doc?: string; lines: { [id: string]: { zh: string; en: string } } }): Defined<"subtitles">;

// ── videos (xengine.video) ───────────────────────────────────────────────

/** A video: its size, rate and length, where its frames come from, its sound track. */
export interface Video {
    doc?: string;
    width?: number;
    height?: number;
    /** Frames a second, or "30000/1001". */
    fps?: number | string;
    seconds?: number;
    frames?: number;
    from?: { card: "sync" } | { png: string } | { view: string };
    audio?: {
        rate?: number;
        /** A piece (`theme.music.omui.ts`), relative to the description. */
        music?: string;
        /** One number, or from a time on. */
        intensity?: number | { at: number; value: number }[];
        sounds?: string;
        recordings?: { [name: string]: string };
        /** A sound on a frame's first sample, or at a time (seconds): one of `frame` and `at`. */
        cues?: { sound: string; frame?: number; at?: number; gain?: number; pan?: number; pitch?: number }[];
        /** The sync card's. */
        beeps?: boolean;
        doc?: string;
    };
}
/** `export default defineVideo({ width: 1280, height: 720, fps: 30, seconds: 3, from: { card: "sync" } })`. */
export declare function defineVideo(video: Video): Defined<"video">;

// ── a program's own effects (xrender.effect) and canvas behaviours ───────

/** A parameter: its range and default (animatable like any property). */
export interface EffectParam {
    name: string;
    default?: number;
    min?: number;
    max?: number;
    doc?: string;
}
/** A pixel effect: its Slang shader (`<name>.slang`, entry `<name>_fs` unless named) and its parameters. */
export interface Effect {
    name: string;
    doc?: string;
    shader?: string;
    entry?: string;
    /** How far it reaches outside its layer: logical pixels, or a parameter's name. */
    reach?: number | string;
    params?: EffectParam[];
}
/** `export default defineEffect({ name: "scanlines", reach: 0, params: [{ name: "strength", default: 0.5 }] })`. */
export declare function defineEffect(effect: Effect): Defined<"effect">;
/** A canvas behaviour: a state machine over a canvas's events, its states, actions and what each does. */
export declare function defineBehavior(behaviors: { [name: string]: { on?: string; initial?: string; doc?: string; [field: string]: unknown } }): Defined<"behavior">;

// ── an application's languages (mlibs.i18n) ──────────────────────────────

/** A catalog's messages (MessageFormat 2), by key; an object's keys are dotted (`inbox.count`). */
export interface Messages {
    [key: string]: string | Messages;
}
/** `export default defineMessages({ title: "Inbox", inbox: { count: "{$count :number} new" } })`, a file a language (`app.zh-Hans.messages.omui.ts`). */
export declare function defineMessages(messages: Messages): Defined<"messages">;

// ── motion recipes ───────────────────────────────────────────────────────

export declare function defineMotion(motion: { [field: string]: unknown }): Defined<"motion">;
)ts";
}

}  // namespace omui::lang::ts
