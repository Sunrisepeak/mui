// openmui.ui.window — the framework's own window frame (D-75).
//
//     import openmui.ui.window;
//     omui::window_frame({.title = "Notes"}, {content…});
//     omui::window_frame({.title = "Notes", .controls = "start", .look = "traffic"}, {content…});
//
// In a framework-drawn window it draws the shadow, the rounded edge, the title
// bar and its buttons, and tells the host what its parts are (its title bar
// moves the window, its edges resize it); in a native window it is only its
// content, so a program that has one runs the same either way. The window's
// operations are the window.* verbs (openmui.view:window). The interfaces are
// contract/*.contract.omui.toml; register the components by name with
// register_window_components() for view documents and scripts.
export module openmui.ui.window;

export import openmui.core;
export import openmui.view;
export import :window_frame_contract;
export import :caption_button_contract;
export import :frame;
