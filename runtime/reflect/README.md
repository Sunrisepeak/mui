# openmui.reflect

C++26 reflection's convenience layer (D-39). It needs GCC 16 with `-freflection`, and nothing else in openmui depends on it. The core is C++23, and contracts are its alternative.

```cpp
import openmui.reflect;

enum class Quality { low, medium, high };
struct Settings {
    std::string display_name = "Ada";
    [[=omui::reflect::range{0.5, 2, 0.25}]] double zoom = 1;
    [[=omui::reflect::label{"Dark appearance"}]] bool dark = false;
    Quality quality = Quality::medium;
};

auto settings = omui::signal(Settings{});
rt.mount("main", omui::reflect::form(settings));    // main/display_name, main/zoom, main/dark, main/quality
```

- **`form(signal)`** gives each public member a control by its type:
  - a text box for a string;
  - a toggle for a bool;
  - a slider for a number, its range from `[[=range{min, max, step}]]`;
  - a select for an enum, offering its enumerators;
  - a group for a struct, the same again.

  Every control is bound both ways to the signal, through a lens on the struct. The members' names are the semantic ids (S-8), and, as people read them ("display_name" → "Display name"), the labels. `[[=label{"…"}]]` and `[[=hidden{}]]` say otherwise.
- **`to_value` and `from_value`** convert a struct to openmui's values and back, for a model's rows, a verb's arguments or a script's data. Enums travel by name, vectors as lists, optionals as their value or null. What `from_value` does not find keeps its default; what is of another type says where (`` `value.tags[1]` is not a string ``).
- **`fields<T>()`** describes the members as a model's rows declare them.

It is a member of its own because C++26 modules are built apart from C++23 ones: its graph is built again in C++26. CI tests it on the GCC dev row by itself, and the workspace's runs leave it out (`--exclude openmui.reflect`).
