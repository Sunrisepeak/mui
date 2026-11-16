// A theme carried in the package: what it evaluates to, not its meaning, is
// what the fixture checks.

import { defineTheme } from "omui/define";

export default defineTheme({
  dusk: { extends: "dark", colors: { accent: "#f59e0b" } },
});
