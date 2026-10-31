// A tour of the wall through the semantic API — the same in tour.lua and
// tour.py (the tests check they record the same and leave the same wall).
// Each step's result is recorded with the verb `tour.record`.
import { act, invoke } from "openmui";

const wall = "main/wall";
const record = (step: string, value: unknown) => invoke("tour.record", { step, value: value ?? null });
// A call with nothing to say answers true: recorded as nothing, as Python's None.
const data = (v: unknown) => (v === true ? null : v);
const refusal = (f: () => unknown) => {
  try {
    f();
    return "done";
  } catch (e) {
    return String((e as Error).message).split(":")[0];
  }
};

// Find a document by name, anywhere on the wall; how many workspaces.
const using = act({ node: wall, action: "find", args: { query: { name: "using" } } }) as string[];
record("using", using);
record("workspaces", (act({ node: wall, action: "find", args: { query: { kind: "workspace" } } }) as string[]).length);
// Fly to it (it goes live), move it, choose it.
record("focus", data(act({ node: wall, action: "focus_node", args: { key: using[0] } })));
record("move", data(act({ node: `${wall}/${using[0]}`, action: "move_to", args: { x: 4000, y: 3000 } })));
record("select", data(act({ node: `${wall}/${using[0]}`, action: "select" })));
// What is near where it went, nearest first.
record("near", act({ node: wall, action: "find", args: { query: { near: [4000, 3000], radius: 900 } } }));
// Two windows written into, then not.
record("stream", invoke("wall.stream", { windows: 2, rate: 50 }));
record("stop", data(invoke("wall.stop", {})));
// What the wall refuses: a window has no `delete` here.
record("refused", refusal(() => act({ node: `${wall}/${using[0]}`, action: "delete", answer: "yes" })));
// Back to the whole wall.
record("overview", data(act({ node: wall, action: "camera_preset", args: { name: "overview" } })));
