// openmui.testing:target — the Web row (wasm32-emscripten, D-84): built
// without threads, as a page served without cross-origin isolation (GitHub
// Pages, B站 Toy) has no SharedArrayBuffer to give them.
export module openmui.testing:target;

export namespace omui::testing {

// Whether the program can start threads (std::thread, std::jthread).
inline constexpr bool threads = false;

}  // namespace omui::testing
