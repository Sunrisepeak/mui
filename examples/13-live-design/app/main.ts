// Subscription settings: the plan, how it is paid, whether it renews.
import { signal, select, column, row, text, button, toggle, radio, box, slot, mount } from "openmui";

const plan = signal("pro");
const yearly = signal(false);
const renew = signal(false);

mount("main", column({ padding: 20, gap: 12 }, [
  text({ id: "title", text: "Your subscription", style: "title", role: "heading", level: 1 }),
  row({ id: "plans", gap: 10 }, [
    radio({ id: "basic", value: "basic", group: plan, label: "Basic · $4" }),
    radio({ id: "pro", value: "pro", group: plan, label: "Pro · $9" }),
    radio({ id: "team", value: "team", group: plan, label: "Team · $19" }),
  ]),
  toggle({ id: "yearly", checked: yearly, label: "Pay yearly: two months free" }),
  toggle({ id: "renew", checked: renew, label: "Renew automatically" }),
  box({ id: "details", height: select(renew, 72, 0), background: "#e0f2fe", radius: 10, animate: {"height":{"spring":{"stiffness":260,"damping":26}}} }),
  slot("extras"),
  button({ id: "save", text: "Save", variant: "primary" }),
]));
