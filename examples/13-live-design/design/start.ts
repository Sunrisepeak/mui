// subscription: a button that counts. Save this file while the window is
// open and it shows at once; the count stays.
// docs: counter
import { signal, fmt, column, text, button, mount } from "openmui";

const clicks = signal(0);

export const App = () =>
  column({ padding: 24, gap: 12 }, [
    // The page's heading too (role and level): its document's h1 on the Web, a reader's first heading.
    text({ text: "Hello from subscription", style: "title", role: "heading", level: 1 }),
    text({ id: "count", text: fmt("Clicked {} times", clicks), style: "heading" }),
    button({ id: "click", text: "Click me", variant: "primary", onActivated: () => clicks.update((n: number) => n + 1) }),
  ]);

mount("main", App());
// docs: end
