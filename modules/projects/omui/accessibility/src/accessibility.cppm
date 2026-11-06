// openmui.accessibility — the semantic tree served to the platform's
// assistive technology (D-21; spec §3: one tree for pixels, agents, tests and
// accessibility).
//
//     accessibility::Mirror mirror(runtime, {.name = "Notes"});
//     auto adapter = accessibility::Adapter::attach(mirror, hwnd_or_nswindow);
//     // each turn of the loop:
//     mirror.serve();          // what a screen reader asked, as the person's input
//     runtime.frame();
//     (*adapter)->update();    // what changed, told to the platform
export module openmui.accessibility;

export import :map;
export import :platform;
export import :adapter;
