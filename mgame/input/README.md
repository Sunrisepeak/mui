# mgame.input

Devices to a game's actions, sampled once a frame.

```cpp
import mgame.input;

mgame::Bindings b;
b.key(code_left, mgame::Axis{0, -1});       // axis 0 toward −1
b.key(code_space, mgame::Button{0});        // button 0
b.pointer_x(mgame::Axis{1, 1});             // axis 1: the pointer, across
mgame::Sampler in(b);
in.key(code, down);                         // as the host delivers them
const mgame::Actions a = in.sample();       // once a frame: plain data
```

- **Actions:** held, pressed and released bits for up to 32 buttons; four axes in 16-bit steps; the pointer within the game's area. It is plain data, so recordings replay and hash exactly.
- **No tap lost:** a press and a release between two samples still count as a press. A key's repeat changes nothing.
- **Axes:** arrows held both ways cancel. A stick past its dead zone wins over keys. The pointer can be an axis, as a paddle follows it.
- **Recordings as text:** one frame a line, readable and writable by people and agents (`to_text`, `from_text`).

The standard library alone; `mgame.ui` feeds it from openmui's events.
