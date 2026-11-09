// The theme preview: choose a theme, a density and the motion, and see every
// common component, the colour tokens and the text styles under them. The
// themes are data — openmui's light, dark, compact and reduced_motion, and
// this application's own brand (themes/brand.theme.omui.ts), which the
// player reloads when it is saved.
import {
  signal, fmt, ne, when, theme, themes, mount,
  column, row, select, toggle, button, textBox, checkbox, slider, progress, box, text,
} from "openmui";

const scheme = signal("light");
const compact = signal(false);
const calm = signal(false);
const current = signal(themes().current);
const problem = signal("");

// The composition the controls say: a theme, then overlays.
function compose(): void {
  const layers: string[] = [scheme.get()];
  if (compact.get()) layers.push("compact");
  if (calm.get()) layers.push("reduced_motion");
  try {
    theme(layers);
    problem.set("");
  } catch (error) {
    problem.set(String((error as Error).message));
  }
  current.set(themes().current);
}

const swatches = ["background", "surface", "surface_variant", "accent", "accent_subtle", "text", "text_muted", "border", "success", "warning", "danger"];
const styles = ["display", "title", "heading", "body", "label", "caption", "code"];

const Controls = () =>
  row({ id: "controls", gap: 16, alignItems: "center" }, [
    select({ id: "scheme", label: "Theme", options: ["light", "dark", "brand"], labels: ["Light", "Dark", "Brand"], value: scheme,
      onChanged: (e: { value: string }) => { scheme.set(e.value); compose(); } }),
    toggle({ id: "compact", label: "Compact", checked: compact, onChanged: (e: { checked: boolean }) => { compact.set(e.checked); compose(); } }),
    toggle({ id: "calm", label: "Reduced motion", checked: calm, onChanged: (e: { checked: boolean }) => { calm.set(e.checked); compose(); } }),
  ]);

const Gallery = () =>
  column({ id: "gallery", gap: "m" }, [
    row({ id: "buttons", gap: "s" }, [
      button({ id: "primary", text: "Save", variant: "primary" }),
      button({ id: "secondary", text: "Cancel" }),
      button({ id: "ghost", text: "More", variant: "ghost" }),
      button({ id: "off", text: "Disabled", disabled: true }),
    ]),
    textBox({ id: "field", label: "Name", placeholder: "Ada Lovelace" }),
    row({ id: "choices", gap: "l", alignItems: "center" }, [
      checkbox({ id: "check", label: "Remember me", checked: true }),
      toggle({ id: "toggle", label: "Notifications", checked: true }),
    ]),
    slider({ id: "slider", label: "Volume", min: 0, max: 100, value: 60 }),
    progress({ id: "progress", label: "Upload", value: 0.4 }),
  ]);

const Swatches = () =>
  row({ id: "swatches", gap: "xs" },
    swatches.map((token) => box({ id: token, background: token, width: 44, height: 28, radius: "small", borderWidth: 1, borderColor: "border", label: token })));

const Type = () =>
  column({ id: "type", gap: "xs" },
    styles.map((style) => text({ id: style, text: `${style[0].toUpperCase()}${style.slice(1)} — the quick brown fox`, style })));

const Preview = () =>
  column({ padding: "xl", gap: "l", background: "background" }, [
    Controls(),
    text({ id: "current", text: fmt("Composed: {}", current), style: "caption" }),
    when(ne(problem, ""), [text({ id: "problem", text: problem })]),
    Gallery(),
    Swatches(),
    Type(),
  ]);

mount("main", Preview());
