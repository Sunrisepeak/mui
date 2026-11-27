# mlibs.xdef.tool

Declared data's tool kernel (D-209, stage 0 of D-205): the kinds' registry each owner keeps in its manifest, a data module's snapshot in its one canonical form (`{"$format":"<kind>@<major>","<kind>":value}`), the checker, and the migration from the earlier suffix. The container's rules are written in [mlibs.xdef](../README.md). `xdef` is its program:

```sh
xdef kinds                                  # kind@major, its owner, its define module
xdef snapshot rise.motion.xdef.ts --write   # the module's canonical snapshot, beside it
xdef check .                                # the container's rules over a tree
xdef migrate . --write                      # the earlier suffix's files renamed, the texts that name them rewritten
```

It evaluates through mlibs.ts with the define modules the registry synthesises: each registered module's kinds, each `define<Kind>` writing its `<kind>@<major>`.
