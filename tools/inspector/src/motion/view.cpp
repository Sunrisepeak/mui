module omui.inspector;
import std;
import omui;

namespace omui::inspector::motion {

namespace {

// Through the runtime, as the person's own (journaled, an Agent's view of what changed).
std::optional<Value> invoke(const std::shared_ptr<Session>& session, std::string_view operation, Value args = {}) {
    if (!session->runtime) return std::nullopt;
    const auto result = session->runtime->invoke("motion." + std::string(operation), {.args = std::move(args)});
    if (result.exit_code() != 0) {
        for (const auto& record : result.records())
            if (record.kind == spec::RecordKind::error && record.body.get("text")) session->problem.set(std::string(record.body.get("text")->as_string()));
        return std::nullopt;
    }
    return result.data_value() ? std::optional<Value>(*result.data_value()) : std::optional<Value>(Value{});
}

// A step's number as a control: its step a small fraction of its size, as a person nudges it.
// A whole number (a count) steps by one.
Element number_knob(const std::shared_ptr<Session>& session, std::string id, std::string key, std::string where, double value, bool whole) {
    const double magnitude = std::abs(value);
    const double step = whole ? std::max(1.0, magnitude >= 100 ? 10.0 : 1.0) : magnitude >= 100 ? 10 : magnitude >= 10 ? 1 : magnitude >= 1 ? 0.1 : 0.05;
    return number_input({.id = std::move(id), .digits = step < 0.1 ? 2 : step < 1 ? 1 : 0, .label = key, .step = step, .value = value,
                         .on_committed = [session, where = std::move(where)](double next) {
                             (void)invoke(session, "set", Value{}.set("where", where).set("value", next));
                         }});
}

}  // namespace

void add_verbs(AppSpec& app, const std::shared_ptr<Session>& session) {
    // The verbs are the catalog's motion.* (inspector.ops.omui.ts).
    for (const auto& entry : catalog) {
        if (!entry.operation.name.starts_with("motion.")) continue;
        const auto operation = entry.operation.name.substr(std::string_view("motion.").size());
        // Writing over the file asks the person first: "save" is their answer.
        app.verbs.push_back({verb_of(entry, operation == "save" ? "save" : ""),
                             [session, operation](const Value& values) { return session->perform(operation, values); }});
    }
}

Element view(const std::shared_ptr<Session>& session, float width) {
    auto pointed = signal(std::string());  // the object last pointed at on the stage
    auto stage = painter({.id = "stage",
                          .width = Dim(mlibs::layout::pct(100)),
                          .label = "The stage at the moment shown: point at an object for the steps that move it then",
                          .paint =
                              [session, pointed](PaintContext& ctx) {
                                  (void)session->changed.get();
                                  (void)session->time.get();
                                  ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, ctx.theme.color("surface_variant"));
                                  session->draw(ctx.list, {0, 0, ctx.size.width, ctx.size.height});
                              },
                          .on_event =
                              [session, pointed](EventContext& ctx, const InputEvent& event) {
                                  const auto* pointer = std::get_if<PointerEvent>(&event);
                                  if (!pointer || pointer->kind != PointerKind::down || pointer->button != MouseButton::left) return false;
                                  const Rect bounds = ctx.services.bounds(ctx.node);
                                  const std::string object = session->object_at(ctx.local, {0, 0, bounds.width, bounds.height});
                                  pointed.set(object);
                                  if (const auto steps = invoke(session, "at", Value{}.set("object", object)))
                                      if (const Value* list = steps->get("steps"); list && list->list() && !list->list()->empty())
                                          session->selected.set(std::string(list->list()->front().get("where")->as_string()));
                                  ctx.repaint();
                                  return true;
                              }});
    const Size size = session->stage();
    if (size.width > 0) stage.prop("aspect_ratio", Source(Value(static_cast<double>(size.width / size.height))));
    stage.prop("max_height", Source(Value(420.0)));

    auto length = computed([session] {
        (void)session->changed.get();
        return session->duration();
    });
    auto scrubber = row({.id = "transport", .width = Dim(width), .gap = 10, .align = "center"},
                        {slider({.id = "time", .label = "Time", .max = length, .step = 0.01, .value = session->time, .grow = 1}),
                         text({.id = "at", .text = computed([session] { return std::format("{:.2f} s", session->time.get()); }), .style = "caption"})});

    // The steps that govern the moment (of the object pointed at), innermost first: choosing one tunes it.
    auto steps = dynamic([session, pointed] {
        (void)session->changed.get();
        (void)session->time.get();
        const std::string chosen = session->selected.get();
        const auto found = session->perform("at", Value{}.set("object", pointed.get()));
        std::vector<Element> buttons;
        if (found)
            if (const Value* list = found->get("steps"); list && list->list())
                for (std::size_t index = 0; index < list->list()->size(); ++index) {
                    const Value& step = (*list->list())[index];
                    const std::string where(step.get("where")->as_string());
                    buttons.push_back(button({.id = std::format("step-{}", index),
                                              .text = std::format("{} · {}", step.get("do")->as_string("step"), where),
                                              .variant = where == chosen ? ButtonVariant::primary : ButtonVariant::secondary,
                                              .on_activated = [session, where] { session->selected.set(where); }}));
                }
        if (buttons.empty())
            buttons.push_back(text({.id = "none", .text = "Nothing moves here at this moment", .style = "caption", .color = "text_muted"}));
        return std::vector<Element>{row({.id = "steps", .gap = 8, .wrap = "wrap"}, std::move(buttons))};
    });

    // The chosen step's numbers, words and switches: each change one step of history.
    auto knobs = dynamic([session] {
        (void)session->changed.get();
        const std::string where = session->selected.get();
        if (where.empty()) return std::vector<Element>{};
        const auto found = session->perform("knobs", Value{}.set("where", where));
        if (!found) return std::vector<Element>{};
        std::vector<Element> controls;
        const Value::List& list = *found->get("knobs")->list();
        for (std::size_t index = 0; index < list.size(); ++index) {
            const Value& knob = list[index];
            const std::string key(knob.get("key")->as_string()), pointer(knob.get("where")->as_string());
            const Value& value = *knob.get("value");
            const std::string id = std::format("knob-{}", index);
            Element control;
            if (value.is_number()) {
                control = number_knob(session, id, key, pointer, value.as_double(), value.type() == Value::Type::integer);
            } else if (value.type() == Value::Type::boolean) {
                control = toggle({.id = id, .checked = value.as_bool(), .label = key, .on_changed = [session, pointer](bool on) {
                                               (void)invoke(session, "set", Value{}.set("where", pointer).set("value", on));
                                           }});
            } else {
                control = text_box({.id = id, .label = key, .text = std::string(value.as_string()), .on_submitted = [session, pointer](std::string next) {
                                                 (void)invoke(session, "set", Value{}.set("where", pointer).set("value", std::move(next)));
                                             }});
            }
            // Its name over it, for the eye (the control carries it for a reader too).
            controls.push_back(column({.id = std::format("field-{}", index), .gap = 4},
                                      {text({.id = "name", .text = key, .style = "caption", .color = "text_muted"}), std::move(control)}));
        }
        return std::vector<Element>{column({.id = "knobs", .gap = 8},
                                           {text({.id = "tuning", .text = std::format("Tuning {}", where), .style = "label"}),
                                            row({.id = "controls", .gap = 12, .wrap = "wrap"}, std::move(controls))})};
    });

    auto history = dynamic([session] {
        (void)session->changed.get();
        const auto state = session->perform("snapshot");
        const bool can_undo = state && state->get("undo")->as_int() > 0, can_redo = state && state->get("redo")->as_int() > 0;
        return std::vector<Element>{row({.id = "history", .gap = 8},
                                        {button({.id = "undo", .disabled = !can_undo, .text = "Undo", .on_activated = [session] { (void)invoke(session, "undo"); }}),
                                         button({.id = "redo", .disabled = !can_redo, .text = "Redo", .on_activated = [session] { (void)invoke(session, "redo"); }}),
                                         button({.id = "save", .text = "Save", .variant = ButtonVariant::ghost,
                                                 .on_activated = [session] { (void)invoke(session, "save"); }})})};
    });

    return column({.id = "motion", .width = Dim(mlibs::layout::pct(100)), .max_width = Dim(width), .gap = 14},
                  {text({.id = "eyebrow", .text = "MOTION WORKBENCH", .style = "label", .color = "accent"}),
                   text({.id = "guide",
                         .text = "Scrub to a moment, point at what looks wrong, and tune the step that moves it. An Agent does the same "
                                 "through the motion.* verbs; every change is a step you can undo.",
                         .style = "body",
                         .max_width = Dim(width)}),
                   std::move(stage), std::move(scrubber), std::move(steps), std::move(knobs), std::move(history),
                   text({.id = "problem", .text = session->problem, .style = "caption", .max_width = Dim(width)})});
}

AppSpec app(const std::shared_ptr<Session>& session, std::filesystem::path file) {
    AppSpec spec;
    spec.id = "org.openmui.inspector.motion";
    spec.title = "omui · Motion workbench";
    spec.size = {960, 860};
    spec.description = "Look at an animation, point at a moment and an object, tune the step that moves it; every operation is an Agent's too.";
    spec.setup = [session, file = std::move(file)](Runtime& runtime) {
        session->runtime = &runtime;
        register_base_components();
        if (!file.empty()) (void)session->perform("open", Value{}.set("file", generic_utf8(file)));
    };
    spec.view = [session] {
        return scroll({.id = "workspace", .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(20)},
                      {view(session, 920)});
    };
    add_verbs(spec, session);
    return spec;
}

}  // namespace omui::inspector::motion
