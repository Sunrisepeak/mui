// A behaviour carried in the package (`-` in its stem becomes `_`): what it
// evaluates to, not its meaning, is what the fixture checks.

import { defineBehavior } from "omui/define";

export default defineBehavior({
  "pan-zoom": { on: "canvas" },
});
