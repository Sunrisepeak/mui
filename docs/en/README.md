# omui documentation

For people who make things with it: application developers, game developers, people who put agents to work. How the engines are designed inside is elsewhere (the [design documents](../../.agents/docs/README.md)).

## What you can make with it today

| You want | You use | Maturity | Start at |
|---|---|---|---|
| Interfaces and tools for the desktop and the Web | omui | usable | [Start](guide/start.md) → [Interfaces](guide/ui.md) |
| An agent that understands and operates your application | omui's agent surface, `omui agent`, MCP | usable | [Agents](guide/agent.md) |
| Interface motion; animation and video made by code | hanim | usable | [Motion](guide/motion.md) |
| A 2D world of matter that burns, flows and sets | xengine's matter and lattice | usable (2D) | [A world of matter](guide/world.md) |
| A 2D game that runs with no window and that an agent can play | xengine's game runtime | early | [Games](guide/game.md) |
| 3D rendering, 3D physics, skeletal animation | — | not yet | — |

**Usable**: tested, with examples, its interface mostly settled. **Early**: things can be made; the interface will change. **Not yet**: planned, not begun. The whole project is in development and has no release.

## Three ideas to know first

1. **An interface is a semantic tree.** The pixels on screen, what a screen reader says, what an agent reads and acts on, what a test asserts — all come from one tree. You write it once and have all four.
2. **A person and an agent are users alike.** What a person can press, an agent can do; what an agent may do is yours and the user's to decide, and what cannot be undone asks a person.
3. **The same input, the same result.** Animation is a function of time and simulation runs in fixed steps, so any frame can be made again, replayed, or branched from to try something.

## Read on

- [Examples](../../examples): twelve small, real tools, each showing one thing.
- [Works](../../showcase): the document wall, the style gallery, the widget breakout, Kiln — each with how it was made.
- [Features](features.md), [measured against others](lead.md), [benchmarks](benchmarks/README.md).
- [Best practices](best-practices.md): rules that making the works taught.
