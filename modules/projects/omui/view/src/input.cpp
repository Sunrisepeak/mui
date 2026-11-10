// Input: hit testing, bubbling, pointer capture, hover, press-to-activate,
// focus and Tab order, and the key bindings of contracts.
module omui.view;

import std;
import omui.core;
import :impl;

namespace omui {

namespace {

// The word around a byte offset (a double click's): a run of letters, digits
// and underscores, or one character of another script (a CJK character is a
// word of its own); a space or a sign, just itself.
std::pair<std::size_t, std::size_t> word_at(std::string_view text, std::size_t offset) {
    if (text.empty()) return {0, 0};
    offset = std::min(offset, text.size() - 1);
    while (offset > 0 && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) --offset;  // to a character's start
    const auto letter = [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    const unsigned char at = static_cast<unsigned char>(text[offset]);
    if (letter(at)) {
        std::size_t begin = offset, end = offset;
        while (begin > 0 && letter(static_cast<unsigned char>(text[begin - 1]))) --begin;
        while (end < text.size() && letter(static_cast<unsigned char>(text[end]))) ++end;
        return {begin, end};
    }
    std::size_t end = offset + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) ++end;
    return {offset, end};
}

}  // namespace

namespace {

std::string_view handler_name(const InputEvent& e) {
    return std::visit(
        [](const auto& x) -> std::string_view {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<T, PointerEvent>) {
                switch (x.kind) {
                    case PointerKind::down:   return "pointer_down";
                    case PointerKind::up:     return "pointer_up";
                    case PointerKind::move:   return "pointer_move";
                    case PointerKind::enter:  return "pointer_enter";
                    case PointerKind::leave:  return "pointer_leave";
                    case PointerKind::cancel: return "pointer_cancel";
                }
                return "pointer_move";
            } else if constexpr (std::same_as<T, WheelEvent>) return "wheel";
            else if constexpr (std::same_as<T, KeyEvent>) return x.kind == KeyKind::down ? "key_down" : "key_up";
            else if constexpr (std::same_as<T, TextEvent>) return "text_input";
            else if constexpr (std::same_as<T, PreeditEvent>) return "preedit";
            else if constexpr (std::same_as<T, FocusEvent>) return x.gained ? "focus" : "blur";
            else if constexpr (std::same_as<T, GamepadEvent>) {
                switch (x.kind) {
                    case GamepadKind::connected:    return "gamepad_connect";
                    case GamepadKind::disconnected: return "gamepad_disconnect";
                    case GamepadKind::down:         return "gamepad_down";
                    case GamepadKind::up:           return "gamepad_up";
                    case GamepadKind::axis:         return "gamepad_axis";
                }
                return "gamepad_axis";
            } else if constexpr (std::same_as<T, FileDropEvent>) {
                return x.kind == FileDropEvent::Kind::over ? "file_over" : x.kind == FileDropEvent::Kind::drop ? "file_drop" : "file_leave";
            } else return {};
        },
        e);
}

bool is_pointer_kind(const InputEvent& e, PointerKind k) {
    const auto* p = std::get_if<PointerEvent>(&e);
    return p && p->kind == k;
}

}  // namespace

std::span<const std::string_view> runtime_events() noexcept {
    // handler_name's events, the root's close request, and the activation handlers.
    static constexpr std::array<std::string_view, 25> events{
        "activate", "blur", "close_request", "context", "file_drop", "file_leave", "file_over", "focus", "gamepad_axis", "gamepad_connect",
        "gamepad_disconnect", "gamepad_down", "gamepad_up", "key_down", "key_up", "pointer_cancel", "pointer_down", "pointer_enter",
        "pointer_leave", "pointer_move", "pointer_up", "preedit", "press", "text_input", "wheel"};
    return events;
}

KeyTarget parse_key_target(std::string_view text) {
    KeyTarget t;
    auto open = text.find('(');
    t.name = std::string(text.substr(0, open));
    if (open == std::string_view::npos) return t;
    std::string_view args = text.substr(open + 1);
    if (auto close = args.rfind(')'); close != std::string_view::npos) args = args.substr(0, close);
    while (!args.empty()) {
        auto comma = args.find(',');
        std::string_view a = args.substr(0, comma);
        while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
        while (!a.empty() && a.back() == ' ') a.remove_suffix(1);
        if (!a.empty()) t.args.emplace_back(a);
        if (comma == std::string_view::npos) break;
        args.remove_prefix(comma + 1);
    }
    return t;
}

void Runtime::Impl::input_frame(const FrameInput& input) {
    const auto previous_actor = input_actor;
    const auto previous_call = input_call;
    const InputAttribution* active_range = nullptr;
    std::size_t next_range = 0;
    // Explicit ranges supersede the legacy whole-frame attribution. No
    // range preserves set_input_actor for low-level compatibility.
    if (!input.attribution.empty()) self.set_input_actor(std::nullopt);
    for (std::size_t index = 0; index < input.events.size(); ++index) {
        while (next_range < input.attribution.size() && input.attribution[next_range].end <= index) ++next_range;
        const auto* range = next_range < input.attribution.size() && input.attribution[next_range].begin <= index ?
            &input.attribution[next_range] : nullptr;
        if (!input.attribution.empty() && range != active_range) {
            if (range) self.set_input_actor(range->actor, range->session);
            else self.set_input_actor(std::nullopt);
            active_range = range;
        }
        Invocation given, copy;
        const Invocation& call = attributed(given, copy, "input");
        const auto& event = input.events[index];
        cause = ++causes;  // the event's chain: what it hits, misses and activates is recorded with it
        record_input(event);
        const auto* pointer = std::get_if<PointerEvent>(&event);
        const bool cancel = pointer && pointer->kind == PointerKind::cancel;
        if (!cancel && clearance("input", Effect::reversible, false, call) == openas::Decision::refuse) {
            record(RecordLevel::errors, "refused", {}, "refused: input from this actor is above what agents may do here (the cap)");
            (void)above_cap("input event", call);
            journal_add("app", "input", call, spec::exit_code::failure);
            continue;
        }
        // The scope includes batch's reactive flush. Restoring it per event
        // prevents a later person's event from borrowing agent authority.
        InvocationScope active(invocation_context, call);
        batch([&] { dispatch(event); });
    }
    input_actor = previous_actor;
    input_call = previous_call;
    cause = 0;
}

std::optional<double> Runtime::Impl::person_operating(NodeId id) const {
    if (!alive(id)) return std::nullopt;
    if (held && person_holds == id) return 0.5;  // until they let go: asked again soon
    const double since = clock.now().count() - person_typed_at;
    if (person_typed == id && since < 1.0) return 1.0 - since;
    return std::nullopt;
}

void Runtime::Impl::dispatch(const InputEvent& e) {
    // Input an agent gives (set_input_actor) is its own, not the person's (S-14).
    const bool person = !input_actor;
    std::visit(
        [&](const auto& x) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<T, PointerEvent>) {
                pointer_event(x);
                person_holds = held && person ? (alive(capture) ? capture : pressed) : NodeId{};
            } else if constexpr (std::same_as<T, WheelEvent>) wheel_event(x);
            else if constexpr (std::same_as<T, KeyEvent>) {
                // Typing is text going in: an editing key in a text field — not a key that activates or moves.
                if (person && alive(focus)) {
                    const Role role = semantics(focus).role;
                    if (role == Role::text_box || role == Role::search_box) person_typed = focus, person_typed_at = clock.now().count();
                }
                key_event(x);
            } else if constexpr (std::same_as<T, TextEvent> || std::same_as<T, PreeditEvent> || std::same_as<T, GamepadEvent>) {
                if (person && alive(focus) && !std::same_as<T, GamepadEvent>) person_typed = focus, person_typed_at = clock.now().count();
                deliver(alive(focus) ? focus : root, e, pointer, true);  // a gamepad, as the keyboard: to the focus
            }
            else if constexpr (std::same_as<T, FocusEvent>) {
                if (!x.gained) {
                    // The window lost focus: nothing stays pressed or captured.
                    if (alive(pressed)) at(pressed).state.pressed = false;
                    pressed = capture = {};
                    touch(dirty_paint);
                }
            } else if constexpr (std::same_as<T, FileDropEvent>) {
                // To what they are over, bubbling; what they left told so.
                const NodeId over = x.kind == FileDropEvent::Kind::leave ? NodeId{} : self.hit_test(x.position);
                if (alive(file_over) && file_over != over) {
                    FileDropEvent left = x;
                    left.kind = FileDropEvent::Kind::leave;
                    left.files.clear();
                    deliver(file_over, left, x.position, true);
                }
                file_over = x.kind == FileDropEvent::Kind::over ? over : NodeId{};
                if (alive(over)) deliver(over, e, x.position, true);
            } else if constexpr (std::same_as<T, ResizeEvent>) self.resize(x.size, x.scale);
            else if constexpr (std::same_as<T, CloseRequestEvent>) call_handlers(root, "close_request", {});
        },
        e);
}

bool Runtime::Impl::deliver(NodeId target, const InputEvent& e, Point window, bool bubble, NodeId* by) {
    const std::string_view name = handler_name(e);
    bool handled = false;
    for (NodeId cur = target; alive(cur);) {
        Node& n = at(cur);
        if (n.state.disabled && !is_pointer_kind(e, PointerKind::move)) {  // a disabled node swallows input
            if (is_pointer_kind(e, PointerKind::down) || std::holds_alternative<KeyEvent>(e))
                record(RecordLevel::errors, "missed", cur, "disabled: a disabled control takes no input");
            break;
        }
        EventContext ctx{self, cur, local_point(n, window)};
        if (n.type->event) {
            auto use = use_theme(cur);
            if (n.type->event(n.instance, ctx, e)) ctx.handled = true;
        }
        if (alive(cur) && !name.empty() && has_handler(cur, name)) {
            Value args = to_value(e);
            args.set("x", ctx.local.x).set("y", ctx.local.y);
            call_handlers(cur, name, args);
        }
        if (ctx.handled && !handled && by) *by = cur;
        handled = handled || ctx.handled;
        if (!bubble || ctx.handled || ctx.stopped || !alive(cur)) break;
        cur = at(cur).parent;
    }
    return handled;
}

void Runtime::Impl::pointer_event(const PointerEvent& e) {
    pointer = e.position;
    pointer_inside = e.kind != PointerKind::leave;
    // Who follows the pointer is drawn again (gone ones forgotten).
    std::erase_if(pointer_watchers, [&](NodeId watcher) { return !alive(watcher); });
    for (const NodeId watcher : pointer_watchers) self.invalidate_paint(watcher);
    switch (e.kind) {
        case PointerKind::move: {
            if (long_press && e.type == PointerType::touch) {
                const float slop = platform_conventions().touch_slop;
                const float dx = e.position.x - pressed_at.x, dy = e.position.y - pressed_at.y;
                if (dx * dx + dy * dy > slop * slop) self.cancel(std::exchange(long_press, 0));  // a drag, not a press held
            }
            // A selection being dragged reaches the words under the pointer (between them, where it reached).
            if (selection.dragging && held)
                if (const NodeId over = self.hit_test(e.position); alive(over))
                    if (const auto offset = text_offset_at(over, e.position)) select(selection.anchor_node, selection.anchor, over, *offset);
            NodeId target = alive(capture) ? capture : self.hit_test(e.position);
            if (!alive(capture)) set_hover(target);
            deliver(target, e, e.position, true);
            break;
        }
        case PointerKind::down: {
            ++presses;
            held = true;
            const PlatformConventions conventions = platform_conventions();
            // Control and a click is a right click where the platform says so (macOS): it asks, it does not press.
            const bool control_click = e.type == PointerType::mouse && e.button == MouseButton::left && conventions.control_click_context &&
                                       e.mods == Modifiers(Modifier::ctrl);
            const bool primary_press = (e.button == MouseButton::left && !control_click) || e.type != PointerType::mouse;
            NodeId target = self.hit_test(e.position);
            record(RecordLevel::interactions, "hit", target, "the press landed on it", e.position);
            // A press outside an overlay dismisses it (light dismiss), top first;
            // outside a modal one it goes no further.
            bool swallowed = false;
            for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
                const NodeId o = *it;
                if (!alive(o) || !displayed(o)) continue;
                const Node& on = at(o);
                const Placement p = on.type->placement ? on.type->placement(on.instance) : Placement{};
                const bool outside = presentation_epoch != 0 ? !presentation_contains(on, e.position) || !Rect{0, 0, on.frame.width, on.frame.height}.contains(local_point(on, e.position)) : !on.bounds.contains(e.position);
                // A modal overlay answers hit tests on its scrim itself: outside is what counts.
                if (contains(o, target) && !(p.modal && outside)) break;
                if (p.modal) {
                    if (outside && p.light_dismiss) {
                        Invocation call;
                        call.actor = {"person", "user", {}};
                        (void)self.act(o, "dismiss", call);
                    }
                    swallowed = outside;
                    if (outside)
                        record(RecordLevel::errors, "missed", o,
                               p.light_dismiss ? "pressed outside a modal overlay: the press dismissed it and went no further"
                                               : "pressed outside a modal overlay: the press goes no further while it is open");
                    break;
                }
                // A press inside what it opened from keeps it: its parent (a select's field and popup), or the
                // element before it when it is anchored there (a popover's button: the rest of the page dismisses it).
                NodeId keeps = on.parent;
                if (p.anchor == Placement::Anchor::previous && alive(on.parent))
                    for (const NodeId c : at(on.parent).children) {
                        if (c == o) break;
                        if (!at(c).type->overlay) keeps = c;
                    }
                if (p.light_dismiss && !contains(keeps, target)) {
                    Invocation call;
                    call.actor = {"person", "user", {}};
                    (void)self.act(o, "dismiss", call);
                }
            }
            if (swallowed) break;
            target = self.hit_test(e.position);
            set_hover(target);
            NodeId f = focusable_ancestor(target);
            if (f) set_focus(f, false);
            else if (alive(focus)) set_focus({}, false);
            if (primary_press) {
                pressed = activatable_ancestor(target);
                pressed_at = e.position;
                if (alive(pressed)) {
                    at(pressed).state.pressed = true;
                    touch(dirty_paint | dirty_semantics);
                }
            }
            capture = target;  // implicit capture: the drag stays with the node it began on
            const bool took = deliver(target, e, e.position, true);
            if (e.type == PointerType::mouse && ((e.button == MouseButton::right && !conventions.context_on_release) || control_click))
                context_event(target, e.position, "mouse");
            // A touch held still asks for the context menu (the press then activates nothing).
            if (e.type == PointerType::touch) {
                if (long_press) self.cancel(long_press);
                long_press = self.after(seconds(conventions.long_press), [this, target, where = e.position, actor = input_actor] {
                    long_press = 0;
                    if (!held || !alive(target)) return;
                    if (alive(pressed)) {
                        at(pressed).state.pressed = false;
                        touch(dirty_paint | dirty_semantics);
                    }
                    pressed = {};
                    const auto before = std::exchange(input_actor, actor);  // whose touch it was
                    context_event(target, where, "touch");
                    input_actor = before;
                });
            }
            if (primary_press && !alive(pressed) && !took && !(alive(target) && at(target).state.disabled))
                record(RecordLevel::interactions, "missed", target, "nothing here takes a press: no button or other control under the pointer", e.position);
            // Text a person selects, as on any page: a press on words no control
            // or handler took begins a selection (a double press a word, a triple
            // its whole text); a press elsewhere lets it go.
            if (e.type == PointerType::mouse && e.button == MouseButton::left && !took && !alive(f) && !alive(pressed)) {
                if (const auto offset = text_offset_at(target, e.position)) {
                    const Node& n = at(target);
                    const std::string_view text = n.type->text_of ? n.type->text_of(n.instance) : std::string_view();
                    if (e.clicks >= 3) select(target, 0, target, text.size());
                    else if (e.clicks == 2) {
                        const auto [begin, end] = word_at(text, *offset);
                        select(target, begin, target, end);
                    } else select(target, *offset, target, *offset);
                    selection.dragging = e.clicks == 1;
                } else {
                    clear_selection();
                }
            }
            break;
        }
        case PointerKind::up: {
            selection.dragging = false;
            if (long_press) self.cancel(std::exchange(long_press, 0));
            NodeId target = alive(capture) ? capture : self.hit_test(e.position);
            NodeId by{};
            const bool handled = deliver(target, e, e.position, true, &by);
            if (e.type == PointerType::mouse && e.button == MouseButton::right && platform_conventions().context_on_release)
                context_event(target, e.position, "mouse");
            capture = {};
            held = false;
            NodeId p = pressed;
            pressed = {};
            if (alive(p)) {
                at(p).state.pressed = false;
                touch(dirty_paint | dirty_semantics);
                NodeId over = self.hit_test(e.position);
                bool inside = false;
                for (NodeId c = over; alive(c); c = at(c).parent)
                    if (c == p) {
                        inside = true;
                        break;
                    }
                // A release nobody took activates what was pressed. One an
                // ancestor of it took (a canvas that captured the press for a
                // pan, then offered the release to its behaviours as a tap)
                // still does, unless the pointer travelled: then it was that
                // ancestor's drag. The pressed node's own handling (or a
                // node's inside it, or elsewhere) keeps the activation, as before.
                constexpr float slop = 4;  // logical pixels
                const float dx = e.position.x - pressed_at.x, dy = e.position.y - pressed_at.y;
                const bool travelled = dx * dx + dy * dy > slop * slop;  // squared: no libm, the same on every platform
                const bool ancestor_took = handled && alive(by) && by != p && contains(by, p);
                if (inside && (!handled || (ancestor_took && !travelled))) {
                    record(RecordLevel::actions, "activated", p, "the release over what was pressed activates it", e.position);
                    Invocation call;
                    call.actor = {"person", "user", {}};
                    call.answer = own_answer(p, "activate");
                    (void)self.act(p, "activate", call);
                } else if (!inside) {
                    record(RecordLevel::errors, "missed", p,
                           std::format("released outside it (over {}): a press activates what it began on only when released over it",
                                       alive(over) ? self.path(over) : std::string("nothing")),
                           e.position);
                } else if (ancestor_took) {
                    record(RecordLevel::interactions, "missed", p, std::format("{} took it as a drag: the pointer travelled", self.path(by)), e.position);
                } else {
                    record(RecordLevel::interactions, "handled", alive(by) ? by : p, "the release was handled by the control itself", e.position);
                }
            }
            set_hover(self.hit_test(e.position));
            break;
        }
        case PointerKind::cancel:
            if (long_press) self.cancel(std::exchange(long_press, 0));
            if (alive(pressed)) at(pressed).state.pressed = false;
            pressed = capture = {};
            held = false;
            touch(dirty_paint);
            break;
        case PointerKind::leave:
            set_hover({});
            break;
        case PointerKind::enter:
            break;
    }
}

void Runtime::Impl::context_event(NodeId target, Point window, std::string_view source) {
    for (NodeId cur = target; alive(cur); cur = at(cur).parent) {
        if (at(cur).state.disabled) return;
        if (!has_handler(cur, "context")) continue;
        const Point local = local_point(at(cur), window);
        record(RecordLevel::interactions, "context", cur, std::format("asked for its context menu ({})", source), window);
        call_handlers(cur, "context", Value{}.set("x", local.x).set("y", local.y).set("source", std::string(source)));
        return;
    }
}

void Runtime::Impl::wheel_event(const WheelEvent& e) {
    pointer = e.position;
    deliver(self.hit_test(e.position), e, e.position, true);
}

void Runtime::Impl::key_event(const KeyEvent& e) {
    NodeId target = alive(focus) ? focus : root;
    if (e.kind == KeyKind::down && alive(focus) && !focus_visible &&
        (e.key == Key::tab || e.key == Key::up || e.key == Key::down || e.key == Key::left || e.key == Key::right)) {
        // Navigating by keyboard shows where focus is (:focus-visible).
        focus_visible = true;
        at(focus).state.focus_visible = true;
        touch(dirty_paint);
    }
    bool handled = false;
    // Nothing focused: the keys go to what is mounted — each view, the last
    // mounted first, its own handlers (an Esc that closes the page's menu) —
    // then to the root.
    std::vector<NodeId> starts;
    if (target == root && alive(root))
        for (auto c = at(root).children.rbegin(); c != at(root).children.rend(); ++c) starts.push_back(*c);
    starts.push_back(target);
    for (const NodeId start : starts) {
        for (NodeId cur = start; alive(cur) && !handled;) {
            Node& n = at(cur);
            EventContext ctx{self, cur, {}};
            if (!n.state.disabled && n.type->event) {
                auto use = use_theme(cur);
                if (n.type->event(n.instance, ctx, InputEvent(e))) ctx.handled = true;
            }
            if (alive(cur) && has_handler(cur, handler_name(InputEvent(e)))) call_handlers(cur, handler_name(InputEvent(e)), to_value(InputEvent(e)));
            handled = ctx.handled || ctx.stopped;
            if (!handled && e.kind == KeyKind::down && alive(cur) && key_binding(cur, e)) handled = true;
            if (!alive(cur) || (start != target && cur == start)) break;  // a view's own chain ends at the view
            cur = at(cur).parent;
        }
        if (handled) break;
    }
    // The menu key, or Shift+F10: the context menu of what has focus, at its lower left.
    if (!handled && e.kind == KeyKind::down && (e.key == Key::menu || (e.key == Key::f10 && e.mods == Modifiers(Modifier::shift)))) {
        const Rect at_focus = alive(focus) ? self.bounds(focus) : Rect{};
        context_event(target, {at_focus.x + 8, at_focus.bottom()}, "key");
        handled = true;
    }
    if (!handled && e.kind == KeyKind::down && e.key == Key::tab && !e.mods.has(Modifier::ctrl) &&
        !e.mods.has(Modifier::alt) && !e.mods.has(Modifier::meta))
        self.focus_next(e.mods.has(Modifier::shift));
    // Undo and redo of the history around the focus (S-12), when nothing
    // focused took the chord itself (a text field undoes its own typing).
    const bool command = (e.mods.has(Modifier::ctrl) || e.mods.has(Modifier::meta)) && !e.mods.has(Modifier::alt);
    // Copy: the selected text, when nothing focused took the chord (a field copies its own).
    if (!handled && e.kind == KeyKind::down && command && e.key == Key::c && !selected.empty()) self.set_clipboard_text(self.selected_text());
    if (!handled && e.kind == KeyKind::down && command && (e.key == Key::z || e.key == Key::y)) {
        Invocation call;
        call.actor = {"person", "user", {}};
        const bool redo = e.key == Key::y || e.mods.has(Modifier::shift);
        const NodeId scope = scope_of(target);
        if (const ScopeHistory* h = find_history(scope); h && !(redo ? h->redo : h->undo).empty())
            (void)self.act(scope, redo ? "redo" : "undo", call);
    }
}

bool Runtime::Impl::key_binding(NodeId id, const KeyEvent& e) {
    Node& n = at(id);
    if (n.state.disabled) return false;
    const spec::ContractInfo* contract = n.type->contract;
    Invocation call;
    call.actor = {"person", "user", {}};
    if (contract) {
        for (const spec::KeyInfo& k : contract->keys) {
            auto chords = parse_chords(k.chord);
            if (!chords || std::ranges::none_of(*chords, [&](const Chord& c) { return c.matches(e.key, e.mods); })) continue;
            KeyTarget t = parse_key_target(k.target);
            // Positional arguments take the names the action declares; a
            // symbol (end, page, max) is the component's to interpret.
            const spec::ActionSpec* a = contract->action(t.name);
            Value args;
            for (std::size_t i = 0; i < t.args.size(); ++i) {
                std::string key = a && i < a->args.size() ? std::string(a->args[i].name) : std::format("arg{}", i);
                double num = 0;
                const std::string& s = t.args[i];
                auto r = std::from_chars(s.data(), s.data() + s.size(), num);
                if (r.ec == std::errc{} && r.ptr == s.data() + s.size()) args.set(key, num);
                else args.set(key, s);
            }
            call.args = std::move(args);
            // The person's own key is their answer to what reaches outside (a link), as their click is.
            call.answer = own_answer(id, t.name);
            if (a) (void)self.act(id, t.name, call);
            else (void)perform(id, t.name, call, true);  // a command of the component (next, submit)
            return true;
        }
        return false;
    }
    if ((e.key == Key::enter || e.key == Key::space) && e.mods.bits == 0 && activatable_ancestor(id) == id) {
        call.answer = own_answer(id, "activate");
        (void)self.act(id, "activate", call);
        return true;
    }
    return false;
}

void Runtime::Impl::set_hover(NodeId target) {
    std::vector<NodeId> chain;
    for (NodeId c = target; alive(c); c = at(c).parent) chain.push_back(c);
    if (chain == hover_chain) return;
    const std::vector<NodeId> old = std::move(hover_chain);
    hover_chain = chain;
    auto notify = [&](NodeId id, bool enter) {
        if (!alive(id)) return;
        at(id).state.hovered = enter;
        PointerEvent pe;
        pe.kind = enter ? PointerKind::enter : PointerKind::leave;
        pe.position = pointer;
        deliver(id, pe, pointer, false);
    };
    for (NodeId id : old)
        if (std::ranges::find(chain, id) == chain.end()) notify(id, false);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        if (std::ranges::find(old, *it) == old.end()) notify(*it, true);
    touch(dirty_paint);
}

void Runtime::Impl::set_focus(NodeId id, bool keyboard) {
    if (id && !alive(id)) id = {};
    if (id == focus) {
        if (alive(id) && keyboard != focus_visible) {
            focus_visible = keyboard;
            at(id).state.focus_visible = keyboard;
            touch(dirty_paint);
        }
        return;
    }
    const NodeId old = focus;
    focus = id;
    focus_visible = keyboard && alive(id);
    if (alive(old)) {
        Node& o = at(old);
        o.state.focused = o.state.focus_visible = false;
        if (o.type->event) {
            EventContext ctx{self, old, {}};
            auto use = use_theme(old);
            o.type->event(o.instance, ctx, FocusEvent{false});
        }
        call_handlers(old, "blur", {});
        if (text_input_node == old) {
            text_input_node = {};
            text_input.reset();
        }
    }
    if (alive(id)) {
        Node& n = at(id);
        n.state.focused = true;
        n.state.focus_visible = keyboard;
        if (n.type->event) {
            EventContext ctx{self, id, {}};
            auto use = use_theme(id);
            n.type->event(n.instance, ctx, FocusEvent{true});
        }
        call_handlers(id, "focus", {});
        if (keyboard && alive(id)) (void)perform(id, "scroll_into_view", {}, true);
    }
    touch(dirty_paint | dirty_semantics);
}

bool Runtime::Impl::is_focusable(NodeId id) const {
    if (!alive(id)) return false;
    const Node& n = at(id);
    if (n.state.disabled || n.state.removing || !displayed(id)) return false;
    if (n.focusable) return *n.focusable;
    if (n.type->focusable) return true;
    // Whatever can be activated can be reached by keyboard.
    return !n.type->contract && (has_handler(id, "press") || has_handler(id, "activate") || n.props.contains("verb"));
}

std::vector<NodeId> Runtime::Impl::focus_order() const {
    std::vector<NodeId> out;
    auto walk = [&](auto& self, NodeId id) -> void {
        const Node& n = at(id);
        if (!displayed(id) || n.state.removing || n.state.disabled) return;
        if (id != root && is_focusable(id)) out.push_back(id);
        for (NodeId c : n.children) self(self, c);
    };
    // A modal overlay keeps Tab inside it.
    const NodeId modal = topmost_modal();
    walk(walk, modal ? modal : root);
    return out;
}

NodeId Runtime::Impl::focusable_ancestor(NodeId id) const {
    for (NodeId c = id; alive(c); c = at(c).parent)
        if (is_focusable(c)) return c;
    return {};
}

std::string Runtime::Impl::own_answer(NodeId id, std::string_view action) const {
    // An agent's input (set_input_actor) is the agent's: its click answers nothing a person must answer.
    if (!alive(id) || input_actor) return {};
    const spec::SemanticNode n = semantics(id);
    const spec::ActionDesc* a = n.action(action);
    // Only reaching outside (a link, a file): an irreversible action asks a
    // person too, and the application turns needs-answer into its confirmation.
    return a && a->effect == Effect::external ? a->answer : std::string();
}

NodeId Runtime::Impl::activatable_ancestor(NodeId id) const {
    for (NodeId c = id; alive(c); c = at(c).parent) {
        const Node& n = at(c);
        if (n.state.disabled) return {};
        if (n.type->contract && n.type->contract->action("activate")) return c;
        if (has_handler(c, "press") || has_handler(c, "activate") || n.props.contains("verb")) return c;
    }
    return {};
}

void Runtime::focus_next(bool backward) {
    Impl& d = *impl_;
    std::vector<NodeId> order = d.focus_order();
    if (order.empty()) return;
    auto it = std::ranges::find(order, d.focus);
    std::size_t i;
    if (it == order.end()) i = backward ? order.size() - 1 : 0;
    else {
        std::size_t at = static_cast<std::size_t>(it - order.begin());
        i = backward ? (at + order.size() - 1) % order.size() : (at + 1) % order.size();
    }
    d.set_focus(order[i], true);
}

}  // namespace omui
