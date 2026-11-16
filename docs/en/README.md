# omui documentation

For people who make things with it: application developers, game developers, people who put agents to work. How the engines are designed inside is elsewhere (the [design documents](../../.agents/docs/README.md)).

What has been run on which platform — on real hardware, in software, or not yet — is on [Platforms](platforms.md). How far each package is ready — stable, preview or experimental, and what each label promises — is on [Maturity](maturity.md); what ships experimental, with its target and its gap, is on [Experimental](experimental.md). The code in the guides is excerpted from files CI builds and runs (lint `docs-code`).

## What you can make with it today

| You want | You use | Maturity | Start at |
|---|---|---|---|
| Interfaces and tools for the desktop and the Web | omui | stable | [Start](guide/start.md) → [Interfaces](guide/ui.md) |
| An agent that understands and operates your application | omui's agent surface, `omui agent`, MCP | stable | [Agents](guide/agent.md) |
| Interface motion; animation and video made by code | hanim | stable (studio: preview) | [Motion](guide/motion.md) |
| A 2D world of matter that burns, flows and sets | xengine's matter and lattice | preview | [A world of matter](guide/world.md) |
| A 2D game that runs with no window and that an agent can play | xengine's game runtime | preview | [Games](guide/game.md) |
| A 3D world: bodies, a crowd, sound through walls; scenes, meshes, shadows, glTF | xengine's kernels, xrender's scene and mesh | preview | [A world in three dimensions](guide/world3d.md) → [Quay](../../showcase/quay) |

**Stable**: every quality gate, source-compatible within 0.1.x. **Preview**: every quality gate within its stated scope; the interface will change. **Experimental**: builds and holds, short of its own target. Each package's label is on [Maturity](maturity.md). The whole project is in development and has no release yet (2026-11-17).

## Three ideas to know first

1. **An interface is a semantic tree.** The pixels on screen, what a screen reader says, what an agent reads and acts on, what a test asserts — all come from one tree. You write it once and have all four.
2. **A person and an agent are users alike.** What a person can press, an agent can do; what an agent may do is yours and the user's to decide, and what cannot be undone asks a person.
3. **The same input, the same result.** Animation is a function of time and simulation runs in fixed steps, so any frame can be made again, replayed, or branched from to try something.

## Read on

- [Examples](../../examples): twelve small, real tools, each showing one thing.
- [Works](../../showcase): the document wall, the style gallery, the widget breakout, Kiln — each with how it was made.
- [Features](features.md), [measured against others](lead.md), [benchmarks](benchmarks/README.md).
- [Best practices](best-practices.md): rules that making the works taught.
