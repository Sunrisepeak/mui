module omui.inspector;
import std;
import omui;
import hanim.studio;
import mcpplibs.os;

namespace omui::inspector::motion {

namespace studio = hanim::studio;
namespace os = mcpplibs::os;

namespace {

std::string problems_text(const studio::Problems& problems) { return studio::to_text(problems); }

// A JSON pointer's tokens ("/program/items/2" → program, items, 2), `~1` and `~0` undone.
std::vector<std::string> tokens(std::string_view pointer) {
    std::vector<std::string> out;
    if (pointer.empty()) return out;
    std::size_t at = pointer.front() == '/' ? 1 : 0;
    while (at <= pointer.size()) {
        const std::size_t end = std::min(pointer.find('/', at), pointer.size());
        std::string token;
        for (std::size_t index = at; index < end; ++index) {
            if (pointer[index] == '~' && index + 1 < end) {
                token += pointer[index + 1] == '1' ? '/' : '~';
                ++index;
            } else {
                token += pointer[index];
            }
        }
        out.push_back(std::move(token));
        at = end + 1;
    }
    return out;
}

const Value* at_pointer(const Value& root, std::string_view pointer) {
    const Value* here = &root;
    for (const std::string& token : tokens(pointer)) {
        if (const Value::List* list = here->list()) {
            std::size_t index = 0;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), index);
            if (parsed.ec != std::errc{} || index >= list->size()) return nullptr;
            here = &(*list)[index];
        } else if (const Value* next = here->get(token)) {
            here = next;
        } else {
            return nullptr;
        }
    }
    return here;
}

// What a person tunes in a step: its numbers, words and switches, one level into
// a pair or an object (by: [x, y], spring: {stiffness, damping}); not what
// names the step's kind, its objects or its children.
void knobs_of(const Value& step, const std::string& where, Value::List& out, int depth = 0) {
    const auto add = [&](std::string key, std::string pointer, const Value& value) {
        out.push_back(Value{}.set("key", std::move(key)).set("where", std::move(pointer)).set("value", value));
    };
    if (const Value::Map* map = step.map()) {
        for (const auto& [key, value] : *map) {
            if (depth == 0 && (key == "do" || key == "object" || key == "objects" || key == "items" || key == "item")) continue;
            const std::string pointer = where + "/" + key;
            if (value.is_number() || value.type() == Value::Type::text || value.type() == Value::Type::boolean) add(key, pointer, value);
            else if (depth == 0 && (value.list() || value.map())) {
                Value::List inner;
                knobs_of(value, pointer, inner, depth + 1);
                for (Value& knob : inner) {
                    knob.set("key", std::format("{}.{}", key, knob.get("key")->as_string()));
                    out.push_back(std::move(knob));
                }
            }
        }
    } else if (const Value::List* list = step.list()) {
        for (std::size_t index = 0; index < list->size(); ++index)
            if ((*list)[index].is_number()) add(std::to_string(index), std::format("{}/{}", where, index), (*list)[index]);
    }
}

}  // namespace

struct Session::Impl {
    std::string text;
    std::filesystem::path file;
    std::optional<studio::Studio> opened;
    std::vector<std::string> undo, redo;  // the document's text before each set (after, for redo)

    // The document in `next` read and opened, or its problems (what was open stays).
    std::expected<void, std::string> load(std::string next) {
        auto document = studio::read(next);
        if (!document) return std::unexpected(problems_text(document.error()));
        auto made = studio::Studio::open(*document);
        if (!made) return std::unexpected(problems_text(made.error()));
        opened.emplace(std::move(*made));
        text = std::move(next);
        return {};
    }
};

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;

double Session::duration() const {
    if (!impl_->opened) return 0;
    const double length = impl_->opened->duration().count();
    return std::isfinite(length) ? length : 10.0;  // a loop without a count: ten seconds of it
}

Size Session::stage() const {
    if (!impl_->opened) return {0, 0};
    return {static_cast<float>(impl_->opened->document().width), static_cast<float>(impl_->opened->document().height)};
}

namespace {
// The stage fitted into `dest`: its scale and where its corner lands.
struct Fit {
    float scale, x, y;
};
Fit fit(Size stage, Rect dest) {
    if (stage.width <= 0 || stage.height <= 0) return {1, dest.x, dest.y};
    const float scale = std::min(dest.width / stage.width, dest.height / stage.height);
    return {scale, dest.x + (dest.width - stage.width * scale) / 2, dest.y + (dest.height - stage.height * scale) / 2};
}
}  // namespace

void Session::draw(xrender::DisplayList& list, Rect dest) {
    if (!impl_->opened) return;
    const auto drawn = impl_->opened->display(hanim::seconds(time.peek()));
    if (!drawn) return;
    const Fit placed = fit(stage(), dest);
    list.push_clip(dest);
    list.push_transform(Affine::translate(placed.x, placed.y) * Affine::scale(placed.scale, placed.scale));
    list.append(**drawn);
    list.pop_transform();
    list.pop_clip();
}

std::string Session::object_at(Point at, Rect dest) {
    if (!impl_->opened) return {};
    const auto states = impl_->opened->state(hanim::seconds(time.peek()));
    if (!states) return {};
    const Fit placed = fit(stage(), dest);
    const double x = (at.x - placed.x) / placed.scale, y = (at.y - placed.y) / placed.scale;
    // The last drawn is on top.
    for (auto it = states->rbegin(); it != states->rend(); ++it)
        if (it->drawn && x >= it->low.x && x <= it->high.x && y >= it->low.y && y <= it->high.y) return it->id;
    return {};
}

VerbResult Session::perform(std::string_view operation, const Value& args) {
    Impl& d = *impl_;
    const auto done = [&](Value result) -> VerbResult {
        changed.set(changed.peek() + 1);
        if (runtime) runtime->invalidate_paint(runtime->root());
        return result;
    };
    const auto text_of = [&](std::string_view key) { return args.get(key) ? std::string(args.get(key)->as_string()) : std::string(); };
    if (operation == "snapshot") {
        return Value{}
            .set("file", d.file.empty() ? Value{} : Value(generic_utf8(d.file)))
            .set("open", d.opened.has_value())
            .set("duration", duration())
            .set("time", time.peek())
            .set("selected", selected.peek())
            .set("undo", static_cast<std::int64_t>(d.undo.size()))
            .set("redo", static_cast<std::int64_t>(d.redo.size()));
    }
    if (operation == "document") {
        if (!d.opened) return std::unexpected(std::string("no document is open"));
        return parse_json(d.text).value_or(Value{});
    }
    if (operation == "open") {
        std::string next = text_of("text");
        std::filesystem::path file;
        if (next.empty()) {
            if (text_of("file").empty()) return std::unexpected(std::string("open needs `file` or `text`"));
            file = os::fs::from_utf8(text_of("file"));
            auto read = os::fs::read(file);
            if (!read) return std::unexpected(std::format("cannot read {}: {}", text_of("file"), read.error().message()));
            next = std::move(*read);
        }
        if (auto loaded = d.load(std::move(next)); !loaded) {
            problem.set(loaded.error());
            return std::unexpected(loaded.error());
        }
        d.file = std::move(file);
        d.undo.clear();
        d.redo.clear();
        time.set(0.0);
        selected.set({});
        problem.set({});
        return done(Value{}.set("duration", duration()));
    }
    if (!d.opened) return std::unexpected(std::string("no document is open: open {file} first"));
    if (operation == "seek") {
        const Value* at = args.get("time");
        if (!at || !at->is_number() || !(at->as_double() >= 0)) return std::unexpected(std::string("seek needs `time`, 0 or more seconds"));
        time.set(std::min(at->as_double(), duration()));
        return done(Value{}.set("time", time.peek()));
    }
    if (operation == "at") {
        const double moment = args.get("time") && args.get("time")->is_number() ? args.get("time")->as_double() : time.peek();
        const auto document = parse_json(d.text).value_or(Value{});
        Value::List steps;
        for (const auto& governing : d.opened->at(hanim::seconds(moment), text_of("object"))) {
            Value::List objects;
            for (const std::string& object : governing.objects) objects.emplace_back(object);
            const Value* step = at_pointer(document, governing.where);
            steps.push_back(Value{}
                                .set("where", governing.where)
                                .set("do", step && step->get("do") ? Value(std::string(step->get("do")->as_string())) : Value{})
                                .set("start", governing.start)
                                .set("end", governing.end)
                                .set("progress", governing.progress)
                                .set("objects", Value(std::move(objects))));
        }
        return Value{}.set("time", moment).set("steps", Value(std::move(steps)));
    }
    if (operation == "knobs") {
        const std::string where = text_of("where").empty() ? selected.peek() : text_of("where");
        const auto document = parse_json(d.text).value_or(Value{});
        const Value* step = at_pointer(document, where);
        if (!step || !step->map()) return std::unexpected(std::format("no step at `{}`", where));
        Value::List knobs;
        knobs_of(*step, where, knobs);
        return Value{}.set("where", where).set("do", step->get("do") ? Value(*step->get("do")) : Value{}).set("knobs", Value(std::move(knobs)));
    }
    if (operation == "set") {
        const std::string where = text_of("where");
        const Value* value = args.get("value");
        if (where.empty() || !value) return std::unexpected(std::string("set needs `where` (a JSON pointer) and `value`"));
        const std::vector<std::string> values{value->json()};
        const auto variants = studio::vary(d.text, where, values);
        if (!variants) return std::unexpected(problems_text(variants.error()));
        const studio::Variant& changed_one = (*variants)[1];
        if (!changed_one.document) return std::unexpected(problems_text(changed_one.document.error()));
        std::string before = d.text;
        if (auto loaded = d.load(changed_one.text); !loaded) return std::unexpected(loaded.error());
        d.undo.push_back(std::move(before));
        d.redo.clear();
        time.set(std::min(time.peek(), duration()));
        problem.set({});
        return done(Value{}.set("where", where).set("value", *value).set("duration", duration()));
    }
    if (operation == "undo" || operation == "redo") {
        auto& from = operation == "undo" ? d.undo : d.redo;
        auto& to = operation == "undo" ? d.redo : d.undo;
        if (from.empty()) return std::unexpected(std::format("nothing to {}", operation));
        std::string current = d.text;
        if (auto loaded = d.load(std::move(from.back())); !loaded) return std::unexpected(loaded.error());
        from.pop_back();
        to.push_back(std::move(current));
        time.set(std::min(time.peek(), duration()));
        return done(Value{}.set("duration", duration()));
    }
    if (operation == "save") {
        const std::filesystem::path file = text_of("file").empty() ? d.file : os::fs::from_utf8(text_of("file"));
        if (file.empty()) return std::unexpected(std::string("save needs `file` (the document was not opened from one)"));
        if (auto saved = os::fs::write_atomic(file, d.text); !saved)
            return std::unexpected(std::format("cannot write {}: {}", generic_utf8(file), saved.error().message()));
        return Value{}.set("file", generic_utf8(file));
    }
    return std::unexpected(std::format("no operation `{}`", operation));
}

}  // namespace omui::inspector::motion
