## From nothing to an interface

Install the build tool through xlings, build the `omui` command from the repository (there is no release yet), then create an application and run it:

```sh
xlings install mcpp
git clone --recursive https://github.com/Sunrisepeak/mui && cd mui
mcpp build -p omui.cli
omui new hello && cd hello
omui dev
```

The program below is the same first interface, running beside its C++ source. Type a name and press its button.

omui is the implementation; [openmui](https://github.com/openlifa/openmui) is the open UI specification. The packages and modules are `omui.*`; what belongs to the specification keeps its name (`import openmui` in scripts, the C ABI, the protocol).

Continue with the [core ideas](features.md) or the [practical guide](best-practices.md).
