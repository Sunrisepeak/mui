# mlibs.apart

Data a program carries apart: `mark(kind, length)` is written before a region of a program's data the program never reads itself, and `regions(data)` finds every such region again. A generator writes the mark into the program (mlibs.shader, before each WGSL and ESSL text); a packer finds the regions in the linked program, takes them out and writes them beside it, one file a kind (omui-gen's `omui.gen.apart`); the page gives a region back to whoever reads it (the device that compiles that kind of shader). A program nobody packed reads its regions where they are.

The one mark: the generator and the packer share it, and neither knows what the other does with a kind. Domain-free and small, so it is a tool here rather than a library (the [Web-program size design](../../../../../.agents/docs/2026-11-27-web-program-size-design.md) §7).
