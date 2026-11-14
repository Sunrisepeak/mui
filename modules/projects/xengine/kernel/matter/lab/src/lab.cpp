// The lab itself: sources made into a world, the world replaced when they change, its picture, its scene's
// edits witnessed as they are made.
module xengine.matter.lab;

import std;
import openas;
import nlohmann.json;
import mlibs.agent;
import mlibs.control;
import mlibs.geom;
import mlibs.hash;
import mlibs.image;
import mlibs.json;
import xengine.matter.json;
import xengine.matter.appearance;
import xengine.cellular.matter.agent;
import xengine.cellular.present.matter;
import xrender.frame;
import :text;

namespace xengine::matter::lab {

namespace {

namespace subject = cellular::matter_agent;
using cellular::Tile;
using cellular::agent::EditKind;

inline constexpr mlibs::geom::Color backdrop = mlibs::geom::Color::hex(0x0d1117);

// Where a problem of a composed snapshot is, in the sources: "/matter/materials/wood/ignition" →
// "materials/wood.json /ignition".
[[nodiscard]] std::string where_of(std::string_view pointer) {
    const auto under = [&](std::string_view prefix) -> std::optional<std::string_view> {
        if (pointer == prefix) return std::string_view{};
        if (pointer.starts_with(prefix) && pointer[prefix.size()] == '/') return pointer.substr(prefix.size());
        return std::nullopt;
    };
    const auto row = [](std::string_view rest, std::string_view inside) {
        const std::size_t end = rest.find('/', 1);
        const std::string_view name = rest.substr(1, end == std::string_view::npos ? end : end - 1);
        return std::format("{} {}{}", material_path(name), inside, end == std::string_view::npos ? std::string_view{} : rest.substr(end));
    };
    if (const auto rest = under("/matter/materials"); rest && !rest->empty()) return row(*rest, "");
    if (const auto rest = under("/appearance/materials"); rest && !rest->empty()) return row(*rest, "/look");
    if (const auto rest = under("/matter/laws")) return std::format("laws.json {}", *rest);
    if (const auto rest = under("/matter/channels")) return std::format("channels.json {}", *rest);
    if (const auto rest = under("/matter/columns")) return std::format("columns.json {}", *rest);
    if (const auto rest = under("/appearance")) return std::format("look.json {}", *rest);
    return std::string(pointer);
}

[[nodiscard]] std::string problems_text(std::span<const mlibs::json::Problem> problems) {
    std::string out;
    for (std::size_t index = 0; index < problems.size() && index < 5; ++index) {
        const mlibs::json::Problem& problem = problems[index];
        if (!out.empty()) out += "; ";
        out += std::format("{}: {}", where_of(problem.at), problem.message);
        if (!problem.hint.empty()) out += std::format(" — {}", problem.hint);
    }
    if (problems.size() > 5) out += std::format("; and {} more", problems.size() - 5);
    return out;
}

[[nodiscard]] std::optional<std::int64_t> integer_of(const Json& value) {
    if (value.is_number_unsigned()) {
        const std::uint64_t wide = value.get<std::uint64_t>();
        if (wide > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
        return static_cast<std::int64_t>(wide);
    }
    if (value.is_number_integer()) return value.get<std::int64_t>();
    return std::nullopt;
}

[[nodiscard]] std::optional<Tile> tile_of(const Json& value) {
    if (!value.is_array() || value.size() != 4) return std::nullopt;
    std::array<std::uint32_t, 4> sides{};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto side = integer_of(value[index]);
        if (!side || *side < 0 || *side > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
        sides[index] = static_cast<std::uint32_t>(*side);
    }
    return Tile{sides[0], sides[1], sides[2], sides[3]};
}

// One edit of a scene made on a world, by the names it says: a material placed, or a channel set, added to
// or taken from in its unit (the material's own channel: set to a material by name).
std::expected<void, std::string> perform(subject::World& world, const Json& edit) {
    if (!edit.is_object()) return std::unexpected("an edit is {step, place | channel …, region, by}");
    const Program& program = world.matter->program();
    const auto region = edit.contains("region") ? tile_of(edit["region"]) : std::nullopt;
    if (!region) return std::unexpected("`region` is [x, y, width, height]");
    openas::Actor actor = cellular::agent::Field::owner();
    if (const auto by = edit.find("by"); by != edit.end() && by->is_string())
        if (auto said = openas::parse(by->get<std::string>())) actor = std::move(*said);
    const auto material_named = [&](const Json& name) -> std::expected<MaterialId, std::string> {
        if (!name.is_string()) return std::unexpected("a material is said by its name");
        const auto material = program.material(name.get<std::string>());
        if (!material) return std::unexpected(std::format("`{}` is no material of the definition", name.get<std::string>()));
        return *material;
    };
    const auto refused = [&] { return std::unexpected(std::format("the field ({}×{}) does not take an edit of [{},{},{},{}]", world.field->state().shape.width, world.field->state().shape.height, region->x, region->y, region->width, region->height)); };
    if (const auto place = edit.find("place"); place != edit.end()) {
        const auto material = material_named(*place);
        if (!material) return std::unexpected(material.error());
        if (!world.matter->place(*material, *region, actor)) return refused();
        return {};
    }
    const auto named = edit.find("channel");
    if (named == edit.end() || !named->is_string()) return std::unexpected("an edit places a material (`place`) or changes a channel (`channel` with `set`, `add` or `take`)");
    const auto channel = program.channel(named->get<std::string>());
    if (!channel) return std::unexpected(std::format("`{}` is no channel of the definition", named->get<std::string>()));
    for (const EditKind kind : {EditKind::set, EditKind::add, EditKind::take}) {
        const auto amount = edit.find(std::string(cellular::agent::edit_kind_names[static_cast<std::size_t>(kind)]));
        if (amount == edit.end()) continue;
        if (*channel == 0) {
            // The material's own channel: what a cell is, the rest of it as it was.
            const auto material = kind == EditKind::set ? material_named(*amount) : std::unexpected(std::string("the material's channel is set, to a material"));
            if (!material) return std::unexpected(material.error());
            if (!world.field->edit(cellular::agent::Edit::place(0, *material, *region), actor)) return refused();
            return {};
        }
        if (!amount->is_number()) return std::unexpected(std::format("`{}` is a number in the channel's unit", cellular::agent::edit_kind_names[static_cast<std::size_t>(kind)]));
        const double stored = std::round(amount->get<double>() * static_cast<double>(std::uint64_t{1} << program.channels()[*channel].fraction));
        if (!(stored >= 0) || stored > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return std::unexpected("the amount is outside what the channel holds");
        if (!world.matter->apply(*channel, kind, static_cast<std::uint32_t>(stored), *region, actor)) return refused();
        return {};
    }
    return std::unexpected("a channel is changed with `set`, `add` or `take`");
}

// How many cells of a world are each material, by name.
[[nodiscard]] std::map<std::string, std::size_t> census(const subject::World& world) {
    const auto& state = world.field->state();
    const auto materials = world.matter->program().materials();
    const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height;
    std::vector<std::size_t> counts(materials.size() + 1);
    for (std::size_t cell = 0; cell < area; ++cell) ++counts[std::min<std::size_t>(state.cells[cell], materials.size())];
    std::map<std::string, std::size_t> out;
    for (std::size_t material = 0; material < materials.size(); ++material)
        if (counts[material]) out.emplace(materials[material].name, counts[material]);
    return out;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& file, std::string_view text) {
    std::error_code ignored;
    if (std::filesystem::exists(file, ignored) && read_file(file) == text) return;
    std::ofstream(file, std::ios::binary | std::ios::trunc).write(text.data(), static_cast<std::streamsize>(text.size()));
}

// The parts of the design a change of sources touches, as claims name them: a source a part, the laws by name
// (`laws` itself when their order changes or one has no name).
[[nodiscard]] std::vector<std::string> parts_of(const Texts& before, const Texts& after, std::span<const std::string> changed) {
    std::vector<std::string> parts;
    for (const std::string& path : changed) {
        if (path != "laws.json") {
            parts.push_back(part_of(path));
            continue;
        }
        const auto laws_of = [](const Texts& texts) {
            std::vector<std::pair<std::string, std::string>> laws;  // name, text
            const auto found = texts.find("laws.json");
            const Json list = found == texts.end() ? Json::array() : parsed(found->second).value_or(Json::array());
            if (list.is_array())
                for (const Json& law : list) {
                    const auto name = law.find("name");
                    laws.emplace_back(law.is_object() && name != law.end() && name->is_string() ? name->get<std::string>() : std::string(), inline_text(law));
                }
            return laws;
        };
        const auto was = laws_of(before), is = laws_of(after);
        bool list_itself = false;
        std::vector<std::string> kept_was, kept_is;
        const auto among = [](const auto& laws, const std::string& name) { return std::ranges::find(laws, name, &std::pair<std::string, std::string>::first); };
        for (const auto& [name, text] : was) {
            if (name.empty()) { list_itself = true; continue; }
            const auto still = among(is, name);
            if (still == is.end() || still->second != text) parts.push_back(std::format("laws/{}", name));
            if (still != is.end()) kept_was.push_back(name);
        }
        for (const auto& [name, text] : is) {
            if (name.empty()) { list_itself = true; continue; }
            if (among(was, name) == was.end()) parts.push_back(std::format("laws/{}", name));
            else kept_is.push_back(name);
        }
        if (list_itself || kept_was != kept_is) parts.emplace_back("laws");
    }
    return parts;
}

[[nodiscard]] std::string world_text(const subject::World& world, std::string_view filled) {
    const auto& state = world.field->state();
    return std::format("{{\"shape\":[{},{}],\"filled\":{},\"steps\":{},\"frame\":{},\"materials\":{},\"laws\":{},\"hash\":\"{:016x}\"}}", state.shape.width, state.shape.height,
                       json_text(filled), state.steps, world.field->frame(), world.matter->program().materials().size(), world.matter->program().laws().size(),
                       world.field->state_hash());
}

}  // namespace

// What a change did to the world, the scene played under both: cells whose material is another, and the
// materials there are more or fewer of — [then, now].
std::string Lab::difference(const cellular::matter_agent::World& then, const cellular::matter_agent::World& now) {
    const auto& before = then.field->state();
    const auto& after = now.field->state();
    std::string out;
    if (before.shape.width == after.shape.width && before.shape.height == after.shape.height) {
        const auto were = then.matter->program().materials();
        const auto are = now.matter->program().materials();
        const std::size_t area = static_cast<std::size_t>(after.shape.width) * after.shape.height;
        std::size_t differ = 0;
        for (std::size_t cell = 0; cell < area; ++cell) {
            const std::uint32_t was = before.cells[cell], is = after.cells[cell];
            differ += was >= were.size() || is >= are.size() || were[was].name != are[is].name;
        }
        out += std::format("\"differ\":{},", differ);
    }
    out += "\"materials\":{";
    const auto were = census(then), are = census(now);
    std::set<std::string> names;
    for (const auto& [name, count] : were) names.insert(name);
    for (const auto& [name, count] : are) names.insert(name);
    bool first = true;
    for (const std::string& name : names) {
        const std::size_t was = were.contains(name) ? were.at(name) : 0, is = are.contains(name) ? are.at(name) : 0;
        if (was == is) continue;
        out += std::format("{}{}:[{},{}]", first ? "" : ",", json_text(name), was, is);
        first = false;
    }
    return out + "}";
}

// ── the picture ─────────────────────────────────────────────────────────────

struct Lab::Screen {
    xrender::ImageStore                     images;
    std::optional<cellular::FieldPresenter> presenter;
    xrender::DisplayList                    list;
    xrender::raster::Surface                surface;
    xrender::CpuFrame                       frame;

    // The cells through the renderer's frame entry into `target`: one layer, the field's image and — where
    // the look gives light — its glow.
    std::expected<void, std::string> paint(std::span<const std::uint32_t> cells, xrender::raster::Surface& target, xrender::CpuFrame& into, int scale) {
        if (auto shown = presenter->present(cells); !shown) return std::unexpected(shown.error());
        const mlibs::geom::Rect whole{0, 0, static_cast<float>(target.width()), static_cast<float>(target.height())};
        list.clear();
        list.begin_node(1);
        presenter->draw(list, whole);
        if (presenter->glows()) presenter->draw_glow(list, whole, 2.0f * static_cast<float>(scale), 0.9f);
        list.end_node();
        const xrender::FrameLayer layer{.name = "field", .list = &list, .resources = {&images, nullptr}, .object_ids = true};
        (void)into.render({&layer, 1}, target, {.background = backdrop});
        return {};
    }
};

// ── `field` and `matter` to a session ─────────────────────────────────────────────

class Lab::Stand final : public mlibs::agent::Subject {
public:
    enum class Of : std::uint8_t { field, matter };
    Stand(Lab& lab, Of of) noexcept : lab_(lab), of_(of) {}

    [[nodiscard]] std::string_view name() const noexcept override { return target().name(); }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return target().interface_name(); }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return target().capabilities(); }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return target().operations(); }
    [[nodiscard]] std::string_view listing() const noexcept override { return target().listing(); }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return target().guide(); }
    [[nodiscard]] std::vector<mlibs::agent::Term> terms() const override { return target().terms(); }
    [[nodiscard]] std::uint64_t state_hash() const override { return target().state_hash(); }
    void poll() override { target().poll(); }
    // What changes the field changes the scene: held while another has claimed it.
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override {
        const openas::Actor* const before = std::exchange(lab_.acting_, &request.actor);
        const auto entries = target().operations();
        const auto entry = std::ranges::find(entries, operation, [](const mlibs::control::CatalogEntry& listed) { return listed.operation.name; });
        static const std::array<std::string, 1> scene{"scene"};
        const auto claim = entry != entries.end() && entry->operation.effect != openas::Effect::read ? lab_.held(scene) : std::nullopt;
        if (claim) answer.fail("conflict", lab_.held_text(*claim));
        else target().operate(operation, request, answer);
        lab_.settle();
        lab_.acting_ = before;
    }

private:
    [[nodiscard]] mlibs::agent::Subject& target() const noexcept {
        if (of_ == Of::field) return *lab_.world_.field;
        return *lab_.world_.matter;
    }
    Lab& lab_;
    Of   of_;
};

// ── made, opened ────────────────────────────────────────────────────────────

Lab::Lab(std::filesystem::path directory, Options options)
    : directory_(std::move(directory)), options_(std::move(options)), history_(directory_ / ".lab"),
      operations_(history_, *this, {.list = "lab.history", .commit = "lab.commit", .edit = "lab.edit"}),
      field_stand_(std::make_unique<Stand>(*this, Stand::Of::field)), matter_stand_(std::make_unique<Stand>(*this, Stand::Of::matter)) {
    options_.scale = std::clamp(options_.scale, 1, 16);
    operations_.by_change = true;  // a material a source: a commit says the ones it changed
}

Lab::~Lab() {
    if (world_.field) world_.field->unwatch(this);
}

std::expected<std::unique_ptr<Lab>, std::string> Lab::create(std::filesystem::path directory, const Begin& begin, Options options) {
    std::error_code ignored;
    if (std::filesystem::exists(directory / "scene.json", ignored) || std::filesystem::exists(directory / ".lab", ignored))
        return std::unexpected(std::format("a lab is in {} already: open it", utf8(directory)));
    auto texts = sources_of(begin);
    if (!texts) return std::unexpected(texts.error());
    std::unique_ptr<Lab> lab(new Lab(std::move(directory), std::move(options)));
    auto built = lab->build(*texts, 0);
    if (!built) return std::unexpected(built.error());
    lab->install(std::move(*built), std::move(*texts));
    auto commit = lab->history_.commit(lab->texts(), "a lab begun", "program:matter-lab", lab->state());
    if (!commit) return std::unexpected(commit.error());
    lab->picture_head();
    return lab;
}

std::expected<std::unique_ptr<Lab>, std::string> Lab::open(std::filesystem::path directory, Options options) {
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(directory / "scene.json", ignored))
        return std::unexpected(std::format("no lab is in {} (it has no scene.json): matter-lab new makes one", utf8(directory)));
    Texts texts;
    const auto take = [&](const std::string& path) -> std::expected<void, std::string> {
        auto kept = normalized(path, read_file(directory / path));
        if (!kept) return std::unexpected(std::format("{} {}", path, kept.error()));
        texts.emplace(path, std::move(*kept));
        return {};
    };
    for (const std::string_view path : {std::string_view{"scene.json"}, std::string_view{"laws.json"}, std::string_view{"look.json"}, std::string_view{"channels.json"}, std::string_view{"columns.json"}})
        if (std::filesystem::is_regular_file(directory / path, ignored))
            if (auto taken = take(std::string(path)); !taken) return std::unexpected(taken.error());
    if (std::filesystem::is_directory(directory / "materials", ignored)) {
        std::vector<std::string> rows;
        for (const auto& entry : std::filesystem::directory_iterator(directory / "materials", ignored))
            if (entry.is_regular_file(ignored) && entry.path().extension() == ".json") rows.push_back(std::format("materials/{}", utf8(entry.path().filename())));
        std::ranges::sort(rows);
        for (const std::string& path : rows)
            if (auto taken = take(path); !taken) return std::unexpected(taken.error());
    }
    std::unique_ptr<Lab> lab(new Lab(std::move(directory), std::move(options)));
    auto built = lab->build(texts, 0);
    if (!built) return std::unexpected(built.error());
    lab->install(std::move(*built), std::move(texts));
    if (!lab->history_.resolve("HEAD")) {
        auto commit = lab->history_.commit(lab->texts(), "a lab opened", "program:matter-lab", lab->state());
        if (!commit) return std::unexpected(commit.error());
    }
    lab->picture_head();
    return lab;
}

std::vector<mlibs::agent::Subject*> Lab::subjects() { return {this, field_stand_.get(), matter_stand_.get(), &picture_}; }

std::unique_ptr<mlibs::agent::Session> Lab::session() {
    return std::make_unique<mlibs::agent::SubjectSession>(subjects(), mlibs::agent::SessionOptions{.actor = {openas::Kind::agent, std::format("session-{}", ++sessions_), {}}});
}

// ── sources into a world ──────────────────────────────────────────────────────

std::expected<Lab::Built, std::string> Lab::build(const Texts& texts, std::int64_t position) const {
    auto program = prepare_document(definition_of(texts), "matter");
    if (!program) return std::unexpected(problems_text(program.error()));
    auto look = look_document(*program, appearance_of(texts), "look");
    if (!look) return std::unexpected(problems_text(look.error()));

    const auto text = texts.find("scene.json");
    const auto scene = text == texts.end() ? std::nullopt : parsed(text->second);
    if (!scene || !scene->is_object()) return std::unexpected("scene.json: a lab has a scene — {shape: [width, height], filled: a material, edits: […]}");
    const auto size = scene->find("shape");
    const auto width = size != scene->end() && size->is_array() && size->size() == 2 ? integer_of((*size)[0]) : std::nullopt;
    const auto height = width ? integer_of((*size)[1]) : std::nullopt;
    if (!width || !height || *width < 1 || *height < 1 || static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height) > options_.most_cells)
        return std::unexpected(std::format("scene.json /shape: [width, height] in cells, at most {} of them", options_.most_cells));
    const auto filled = scene->find("filled");
    if (filled == scene->end() || !filled->is_string()) return std::unexpected("scene.json /filled: the material every cell is at first");
    const auto edits = scene->find("edits");
    if (edits != scene->end() && !edits->is_array()) return std::unexpected("scene.json /edits: a list of edits");

    Built built{.look = std::move(*look), .shape = {static_cast<std::uint32_t>(*width), static_cast<std::uint32_t>(*height)}, .filled = filled->get<std::string>()};
    auto world = subject::World::make(std::move(*program), built.shape, built.filled, {.field = {.boundary = cellular::nothing_beyond, .keep = options_.keep}});
    if (!world) return std::unexpected(std::format("scene.json: {}", world.error()));
    built.world = std::move(*world);
    cellular::agent::Field& field = *built.world.field;
    std::int64_t last = 0;
    if (edits != scene->end()) {
        std::size_t index = 0;
        for (const Json& edit : *edits) {
            const auto step = edit.is_object() && edit.contains("step") ? integer_of(edit["step"]) : std::nullopt;
            if (!step || *step < last || *step > options_.most_steps)
                return std::unexpected(std::format("scene.json /edits/{}: `step` is the step it is made at — not before the edit before it, at most {}", index, options_.most_steps));
            while (field.state().steps < *step) field.step();
            if (auto done = perform(built.world, edit); !done) return std::unexpected(std::format("scene.json /edits/{}: {}", index, done.error()));
            built.entries.push_back({field.frame(), *step, inline_text(edit)});
            last = *step;
            ++index;
        }
    }
    const std::int64_t target = std::min(std::max(position, last), options_.most_steps);
    while (field.state().steps < target) field.step();
    return built;
}

void Lab::install(Built built, Texts texts) {
    if (world_.field) world_.field->unwatch(this);
    world_ = std::move(built.world);
    look_ = std::move(built.look);
    shape_ = built.shape;
    filled_ = std::move(built.filled);
    entries_ = std::move(built.entries);
    texts_ = std::move(texts);
    texts_["scene.json"] = scene_text();
    scene_changed_ = false;
    world_.field->watch(this);

    // The picture of this world: its own image and surface (the shape may be another).
    auto screen = std::make_unique<Screen>();
    auto presenter = cellular::FieldPresenter::make(screen->images, shape_, static_cast<std::uint32_t>(world_.matter->program().channels().size()), cellular::lower(look_), {16, 16});
    if (presenter) screen->presenter.emplace(std::move(*presenter));
    screen->surface = xrender::raster::Surface(static_cast<int>(shape_.width) * options_.scale, static_cast<int>(shape_.height) * options_.scale);
    screen_ = std::move(screen);
    drawn_ = 0;
    picture_.show(screen_->frame, screen_->surface, [this] { draw(); });
    write();
}

void Lab::write() const {
    std::error_code ignored;
    std::filesystem::create_directories(directory_ / "materials", ignored);
    for (const auto& [path, text] : texts_) write_file(directory_ / path, text);
    for (const auto& entry : std::filesystem::directory_iterator(directory_ / "materials", ignored))
        if (entry.is_regular_file(ignored) && entry.path().extension() == ".json" && !texts_.contains(std::format("materials/{}", utf8(entry.path().filename()))))
            std::filesystem::remove(entry.path(), ignored);
    for (const std::string_view table : {std::string_view{"channels.json"}, std::string_view{"columns.json"}})
        if (!texts_.contains(std::string(table))) std::filesystem::remove(directory_ / table, ignored);
    write_file(directory_ / "world.matter.omui.json", definition_of(texts_));
    write_file(directory_ / "world.appearance.omui.json", appearance_of(texts_));
}

std::string Lab::scene_text() const {
    Json scene = Json::object();
    scene["shape"] = Json::array({shape_.width, shape_.height});
    scene["filled"] = filled_;
    Json edits = Json::array();
    for (const Entry& entry : entries_) edits.push_back(parsed(entry.text).value_or(Json::object()));
    scene["edits"] = std::move(edits);
    return rows_text(scene, 2);
}

void Lab::settle() {
    if (!scene_changed_) return;
    texts_["scene.json"] = scene_text();
    scene_changed_ = false;
    write_file(directory_ / "scene.json", texts_["scene.json"]);
}

void Lab::run(std::int64_t steps) {
    for (std::int64_t played = 0; played < steps; ++played) world_.field->step();
}

// ── as sources ──────────────────────────────────────────────────────────────

Texts Lab::texts() {
    if (scene_changed_) texts_["scene.json"] = scene_text();
    return texts_;
}

std::string Lab::state() { return std::format("{:016x}", state_hash()); }

std::uint64_t Lab::state_hash() const {
    mlibs::hash::Fnv1a hash;
    for (const auto& [path, text] : texts_)
        if (path != "scene.json") hash.str(path).str(text);
    hash.str(scene_text()).u64(world_.field->state_hash());
    return hash.value() ? hash.value() : 1;
}

std::expected<std::string, Lab::Refusal> Lab::change_to(const Texts& given, std::optional<std::int64_t> position) {
    Texts next;
    for (const auto& [path, text] : given) {
        if (text.empty()) continue;  // a source no longer there
        auto kept = normalized(path, text);
        if (!kept) return std::unexpected(Refusal{openas::error_code::invalid_argument, std::format("{} {}", path, kept.error())});
        next.emplace(path, std::move(*kept));
    }
    const Texts before = texts();
    std::vector<std::string> changed;
    for (const auto& [path, text] : next)
        if (const auto was = before.find(path); was == before.end() || was->second != text) changed.push_back(path);
    for (const auto& [path, text] : before)
        if (!next.contains(path)) changed.push_back(path);
    if (changed.empty() && !position) return std::string("{\"changed\":[]}");
    const std::vector<std::string> parts = parts_of(before, next, changed);
    if (const auto claim = held(parts)) return std::unexpected(Refusal{"conflict", held_text(*claim)});
    auto built = build(next, position.value_or(steps()));
    if (!built) return std::unexpected(Refusal{openas::error_code::invalid_argument, built.error()});

    std::string report = "{\"changed\":[";
    for (std::size_t index = 0; index < changed.size(); ++index) report += std::format("{}{}", index ? "," : "", json_text(changed[index]));
    report += std::format("],{},", difference(world_, built->world));
    install(std::move(*built), std::move(next));
    return report + std::format("\"world\":{}}}", world_text(world_, filled_));
}

std::expected<std::string, std::string> Lab::apply(const Texts& texts, Reason) {
    auto changed = change_to(texts);
    if (changed) return std::move(*changed);
    refused_ = changed.error().code;
    return std::unexpected(std::move(changed.error().text));
}

// ── claims ────────────────────────────────────────────────────────────────

double Lab::now() const {
    if (options_.clock) return options_.clock();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::optional<mlibs::agent::Claim> Lab::held(std::span<const std::string> parts) const {
    if (!acting_ || acting_->kind == openas::Kind::person || claims_.empty()) return std::nullopt;
    const std::string actor = openas::format(*acting_);
    const double at = now();
    for (const std::string& part : parts)
        if (auto claim = claims_.over(part, actor, at)) return claim;
    return std::nullopt;
}

std::string Lab::held_text(const mlibs::agent::Claim& claim) const {
    return std::format("`{}` is held by {}{} for {:.0f} s more: wait, ask it, or work on another part (lab.claims says who holds what)", claim.part, claim.actor,
                       claim.intent.empty() ? std::string() : std::format(" — \"{}\" —", claim.intent), std::max(0.0, claim.until - now()));
}

// ── the picture ─────────────────────────────────────────────────────────────

void Lab::draw() {
    if (!screen_ || !screen_->presenter) return;
    const std::uint64_t hash = world_.field->state_hash() | 1;
    if (drawn_ == hash) return;
    if (screen_->paint(world_.field->state().cells, screen_->surface, screen_->frame, options_.scale)) drawn_ = hash;
}

std::expected<std::vector<std::uint8_t>, std::string> Lab::png(int scale) {
    if (!screen_ || !screen_->presenter) return std::unexpected("the look is not one the lattice shades");
    const int cell = scale <= 0 ? options_.scale : scale;
    if (cell < 1 || cell > 16) return std::unexpected("`scale` is 1 … 16 pixels a cell");
    const auto encoded = [](const xrender::raster::Surface& picture) {
        return mlibs::image::encode_png({reinterpret_cast<const std::uint8_t*>(picture.pixels().data()), picture.width(), picture.height(), picture.width() * 4});
    };
    if (cell == options_.scale) {
        draw();
        return encoded(screen_->surface);
    }
    xrender::raster::Surface target(static_cast<int>(shape_.width) * cell, static_cast<int>(shape_.height) * cell);
    xrender::CpuFrame frame;
    const auto painted = screen_->paint(world_.field->state().cells, target, frame, cell);
    drawn_ = 0;  // the list is this picture's: the lab's own is drawn again when next asked for
    if (!painted) return std::unexpected(painted.error());
    return encoded(target);
}

void Lab::picture_head() {
    const auto head = history_.resolve("HEAD");
    if (!head || history_.thumbnail(*head)) return;
    const auto bytes = png();
    if (!bytes) return;
    if (const auto file = history_.keep_picture(*bytes)) (void)history_.set_thumbnail(*head, *file);
}

// ── the scene's edits, as the field makes them ──────────────────────────────────────

void Lab::edited(const cellular::agent::FieldState& now, const cellular::agent::Edited& edit) {
    const Program& program = world_.matter->program();
    const auto channels = program.channels();
    const auto materials = program.materials();
    const auto material_name = [&](std::uint32_t material) { return material < materials.size() ? materials[material].name : std::string("?"); };
    const auto entry = [&](Json said) {
        Json line = Json::object();
        line["step"] = now.steps;
        for (auto member = said.begin(); member != said.end(); ++member) line[member.key()] = member.value();
        line["region"] = Json::array({edit.edit.region.x, edit.edit.region.y, edit.edit.region.width, edit.edit.region.height});
        line["by"] = openas::format(edit.actor);
        entries_.push_back({edit.frame, now.steps, inline_text(line)});
    };
    // Every channel set, the material first: a material placed. Else each channel it writes, in its unit.
    bool whole = edit.edit.kind == EditKind::set && edit.edit.count == channels.size();
    for (std::uint32_t written = 0; whole && written < edit.edit.count; ++written) whole = edit.edit.channels[written] == written;
    if (whole) {
        entry(Json{{"place", material_name(edit.edit.values[0])}});
    } else {
        for (std::uint32_t written = 0; written < edit.edit.count; ++written) {
            const std::uint32_t channel = edit.edit.channels[written], value = edit.edit.values[written];
            if (channel >= channels.size()) continue;
            Json said = Json::object();
            said["channel"] = channels[channel].name;
            const std::string kind(cellular::agent::edit_kind_names[static_cast<std::size_t>(edit.edit.kind)]);
            const std::uint64_t one = std::uint64_t{1} << channels[channel].fraction;
            if (channel == 0) said[kind] = material_name(value);
            else if (value % one == 0) said[kind] = value / one;
            else said[kind] = static_cast<double>(value) / static_cast<double>(one);
            entry(std::move(said));
        }
    }
    scene_changed_ = true;
}

void Lab::rewound(std::int64_t frame) {
    if (std::erase_if(entries_, [frame](const Entry& entry) { return entry.frame > frame; })) scene_changed_ = true;
}

}  // namespace xengine::matter::lab
