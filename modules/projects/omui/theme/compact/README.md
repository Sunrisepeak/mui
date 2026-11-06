# openmui.theme.compact

The compact density as an overlay ([`compact.theme.omui.toml`](compact.theme.omui.toml)), for tools and dense lists. It sets smaller spacing, text and radii, and a control density of 0.8, so controls are about a fifth smaller in whole pixels. It goes over whichever theme it is composed with:

```lua
omui.theme("dark", "compact")
```

```cpp
import openmui.theme.compact;
(void)omui::themes::add_compact();
```
