## From nothing to an interface

Install the build tool through xlings, create an application, then run it:

```sh
xlings install mcpp
mcpp new hello --template openmui
cd hello
mcpp run
```

The program below is the same first interface, running beside its C++ source. Type a name and press its button.

omui is the implementation; [openmui](https://github.com/openlifa/openmui) is the open UI specification. Existing package and module names remain `openmui.*`.

Continue with the [core ideas](features.md) or the [practical guide](best-practices.md).
