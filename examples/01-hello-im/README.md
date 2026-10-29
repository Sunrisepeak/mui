# 01 — hello, immediate mode

The shortest path to an openmui window: an immediate-mode function declares
the interface, and `omui::run` shows it.

```sh
mcpp run                                  # a window
mcpp run -- --headless --png hello.png    # no display: a picture
mcpp run -- --serve                       # headless, for agents: omui agent ls
```

`src/hello.cppm` is the whole application; `tests/hello_test.cpp` drives it as
an agent would (find the field by role, type, activate the button) and checks
that renaming the button kept its id (spec S-9).

The application is also a library (`openmui-example-hello-im`). openmui's site runs it live in one of its places, beside its own source: `build.mcpp` embeds `src/*.cppm` as `hello::hello_source`.
