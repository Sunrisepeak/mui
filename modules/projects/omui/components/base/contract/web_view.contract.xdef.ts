// A web page in the interface (D-141: an external surface): drawn by the
// system's own web view, which the host places over this node, the framework
// drawing everything around it. What the page shows is the page's, not the
// semantic tree's: the tree holds its address, its title and whether it is
// loading, and an agent goes back, forward, reloads or follows an address.
// Where the host has no web view (headless, a host without one), a card that
// opens the page in the system's browser.
import { defineContract } from "openmui/define";

export default defineContract({
  web_view: {
    role: "document",
    spec: "0.1",
    doc: "A web page drawn by the system's own web view, placed over this node; its address, title and loading said in the tree.",
    props: {
      url: { type: "string", default: "", bindable: true, doc: "The page's address (http, https); it follows the page as the page navigates." },
      label: { type: "string", default: "", doc: "Its name while the page has no title." },
    },
    events: {
      navigated: { args: { url: "string" } },
      loaded: { args: { title: "string" } },
    },
    actions: {
      navigate: { effect: "external", answer: "yes", args: { url: "string" }, doc: "Show the page at this address (http, https)." },
      back: { effect: "external", answer: "yes", doc: "The page before." },
      forward: { effect: "external", answer: "yes", doc: "The page after." },
      reload: { effect: "external", answer: "yes", doc: "Load the page again." },
      open_external: { effect: "external", answer: "yes", doc: "Open the page in the system's browser." },
    },
    keys: { Enter: "open_external" },
    parts: { card: {}, address: {}, focus_ring: {} },
  },
});
