// Where one is, as the way down to it: each place a link (the last, the
// current one, not), separated. Each crumb is a node of the semantic tree an
// agent can activate (`<id>/<index>`).
import { defineContract } from "openmui/define";

export default defineContract({
  breadcrumb: {
    role: "navigation",
    spec: "0.1",
    doc: "Where one is, as the way down to it: each place a link, the last the current one.",
    props: {
      items: { type: "list<string>", default: [], bindable: true, doc: "The places, outermost first." },
      label: { type: "string", default: "Breadcrumb", doc: "The accessible name of the navigation." },
      separator: { type: "string", default: "/", doc: "Between two crumbs." },
    },
    events: { navigated: { args: { index: "i64" } } },
    actions: { go: { effect: "reversible", args: { index: "i64" }, doc: "To the place at index (its crumb activated)." } },
    keys: { Left: "previous", Right: "next" },
    commands: { next: { doc: "Move focus to the next crumb." }, previous: { doc: "Move focus to the previous crumb." } },
    parts: { crumb: {}, separator: {}, current: {} },
  },
});
