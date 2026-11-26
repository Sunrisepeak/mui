module omui.host.terminal;

import std;
import openas;
import mlibs.agent;
import mlibs.compose;
import mlibs.control;
import mlibs.hash;
import mlibs.text;
import mcpplibs.os;
import omui.agent;
import :catalog;

namespace omui {

using mlibs::log::warn;

namespace terminal {

namespace {

std::string hex(Color c) {
    const auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}", channel(c.r), channel(c.g), channel(c.b));
}

std::string style_names(std::uint8_t bits) {
    std::string out = "[";
    const auto add = [&](std::uint8_t bit, std::string_view name) {
        if (!(bits & bit)) return;
        if (out.size() > 1) out += ",";
        out += std::format("\"{}\"", name);
    };
    add(mrender::cells::style::bold, "bold");
    add(mrender::cells::style::italic, "italic");
    add(mrender::cells::style::underline, "underline");
    add(mrender::cells::style::strikethrough, "strikethrough");
    return out + "]";
}

}  // namespace

std::span<const mlibs::control::CatalogEntry> Subject::operations() const noexcept { return catalog; }
std::string_view Subject::listing() const noexcept { return terminal::listing; }
mlibs::control::Guide Subject::guide() const noexcept { return terminal::guide; }

std::uint64_t Subject::state_hash() const {
    const mrender::cells::Frame& frame = host_.frame();
    mlibs::hash::Fnv1a hash;
    hash.u64(static_cast<std::uint64_t>(frame.columns())).u64(static_cast<std::uint64_t>(frame.rows()));
    for (int row = 0; row < frame.rows(); ++row)
        for (int column = 0; column < frame.columns(); ++column) {
            const mrender::cells::Cell& cell = frame.at(column, row);
            hash.str(cell.text).str(hex(cell.foreground)).str(hex(cell.background)).byte(cell.style);
        }
    return hash.value();
}

void Subject::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const mrender::cells::Frame& frame = host_.frame();
    const mrender::cells::Grid& grid = host_.options().grid;
    if (operation == "terminal.frame") {
        std::string out = std::format("{{\"size\":[{},{}],\"grid\":[{},{},{}],\"text\":{}", frame.columns(), frame.rows(), grid.column, grid.row,
                                      grid.baseline, mlibs::agent::json_text(frame.text()));
        if (request["cells"].boolean().value_or(false)) {
            out += ",\"cells\":[";
            for (int row = 0; row < frame.rows(); ++row) {
                out += row ? ",[" : "[";
                for (int column = 0; column < frame.columns(); ++column) {
                    const mrender::cells::Cell& cell = frame.at(column, row);
                    out += std::format("{}[{},\"{}\",\"{}\",{},{}]", column ? "," : "", mlibs::agent::json_text(cell.text), hex(cell.foreground),
                                       hex(cell.background), style_names(cell.style), cell.node);
                }
                out += "]";
            }
            out += "]";
        }
        return answer.data(out + "}");
    }
    if (operation == "terminal.at") {
        const auto parts = request["at"].items();
        const auto column = parts.size() == 2 ? parts[0].integer() : std::nullopt;
        const auto row = parts.size() == 2 ? parts[1].integer() : std::nullopt;
        if (!column || !row) return answer.fail(openas::error_code::invalid_argument, "`at` is [column, row], whole cells from 0");
        if (*column < 0 || *row < 0 || *column >= frame.columns() || *row >= frame.rows())
            return answer.fail(openas::error_code::invalid_argument,
                               std::format("[{}, {}] is not within the frame ({} × {} cells)", *column, *row, frame.columns(), frame.rows()));
        const int c = static_cast<int>(*column), r = static_cast<int>(*row);
        const mrender::cells::Cell& cell = frame.at(c, r);
        const Point point = host_.point_of(c, r);
        std::string out = std::format("{{\"at\":[{},{}],\"text\":{},\"foreground\":\"{}\",\"background\":\"{}\",\"style\":{},\"point\":[{},{}]", c, r,
                                      mlibs::agent::json_text(cell.text), hex(cell.foreground), hex(cell.background), style_names(cell.style),
                                      point.x, point.y);
        Runtime& runtime = host_.runtime();
        if (const NodeId node = cell.node ? runtime.node_at_index(cell.node) : NodeId{}; runtime.valid(node))
            out += std::format(",\"node\":{{\"path\":{}}}", mlibs::agent::json_text(runtime.path(node)));
        return answer.data(out + "}");
    }
    answer.fail(openas::error_code::not_found, std::format("{} is not a terminal operation (terminal.frame, terminal.at)", operation));
}

namespace {

int show(TerminalOptions options, const std::function<std::shared_ptr<void>(Runtime&)>& prepare) {
    Terminal host(std::move(options));
    const std::shared_ptr<void> kept = prepare ? prepare(host.runtime()) : nullptr;
    return host.run();
}

}  // namespace

void install() {
    // Made when installed, never at program start: a program that installs none carries none of it.
    static const ShowService service{.run = &show};
    if (mlibs::compose::process().provider(show_slot.info) == "terminal") return;
    if (auto installed = mlibs::compose::process().install(show_slot, service, "terminal"); !installed)
        warn("the terminal: {}", installed.error().message);
}

}  // namespace terminal

struct Terminal::Impl {
    TerminalOptions                     options;
    // Text measured in cells, whatever this process installed for its other text: the grid's own collection.
    mlibs::text::FontCollection         fonts{mlibs::text::cells::shaper()};
    std::unique_ptr<Runtime>            runtime;
    std::unique_ptr<semantic::Tree>     tree;
    terminal::Input                     input;
    mrender::cells::Frame               frame;
    std::uint64_t                       drawn = 0;  // the list hash the frame shows
    bool                                have_frame = false;
    int                                 columns = 80, rows = 24;
    std::int64_t                        index = -1;
    seconds                             last{0};
    agent::Served                       endpoint;
    std::unique_ptr<terminal::Subject>  subject;
    std::atomic<bool>                   quitting{false};

    Size size() const { return {static_cast<float>(columns) * options.grid.column, static_cast<float>(rows) * options.grid.row}; }

    void draw() {
        const mrender::DisplayList& list = runtime->display_list();
        const std::uint64_t hash = list.hash();
        if (have_frame && hash == drawn && frame.columns() == columns && frame.rows() == rows) return;
        mrender::cells::Options how{.grid = options.grid,
                                    .foreground = options.theme.color("text"),
                                    .background = options.theme.color("background", Color::white()),
                                    .font_style = [this](mrender::FontRef font) -> std::uint8_t {
                                        const mlibs::text::FontRef ref{font.id};
                                        std::uint8_t bits = 0;
                                        if (fonts.weight_of(ref) >= 600) bits |= mrender::cells::style::bold;
                                        if (fonts.slant_of(ref) == mlibs::text::Slant::italic) bits |= mrender::cells::style::italic;
                                        return bits;
                                    }};
        frame = mrender::cells::render(list, columns, rows, how);
        drawn = hash;
        have_frame = true;
    }

    // One frame of these events at this time.
    void frame_of(std::vector<InputEvent> events, seconds at) {
        std::optional<agent::AgentInput> given;
        const std::size_t agent_begin = events.size();
        if (endpoint) {
            endpoint.pump();
            given = endpoint.take_input();
            if (given) events.insert(events.end(), std::make_move_iterator(given->events.begin()), std::make_move_iterator(given->events.end()));
        }
        ++index;
        FrameInput in;
        in.time = at;
        in.events = std::move(events);
        std::array<InputAttribution, 1> attribution;
        if (given && in.events.size() > agent_begin) {
            attribution[0] = {agent_begin, in.events.size(), given->actor, given->session};
            in.attribution = attribution;
        }
        const FrameOutput out = runtime->frame(in);
        last = at;
        if (input.interrupted()) quitting = true;
        draw();
        if (out.repainted) runtime->frame_shown({at});
        animating = out.animating;
        next_work = out.next_work;
    }
    bool                   animating = false;
    std::optional<seconds> next_work;
};

Terminal::Terminal(TerminalOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    d.columns = std::max(d.options.columns, 1);
    d.rows = std::max(d.options.rows, 1);
    d.input = terminal::Input({.column = d.options.grid.column, .row = d.options.grid.row});
    // The grid the cell measure lays text out by; installed for the process's other text where nothing else is.
    if (auto installed = mlibs::text::cells::install({.column = d.options.grid.column, .row = d.options.grid.row, .baseline = d.options.grid.baseline});
        !installed && mlibs::compose::process().provider(mlibs::text::shaper_slot.info).empty())
        warn("the cell measure: {}", installed.error().message);
    if (auto loaded = d.fonts.add_defaults(); !loaded) warn("the cell measure's face: {}", loaded.error());
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = &d.fonts;
    ro.clock = hanim::Clock::virtual_time();
    ro.size = d.size();
    ro.scale = 1;
    ro.stages = Stages::paint;
    d.runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime->set_renderer("cells");
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    // Its endpoint is the agent surface's, as a window's is (agent::endpoint_slot); the picture is a subject beside it.
    const agent::EndpointService* surface = mlibs::compose::process().get(agent::endpoint_slot);
    if (surface && d.options.agent) {
        d.subject = std::make_unique<terminal::Subject>(*this);
        d.runtime->add_subject(*d.subject);
        agent::EndpointOptions eo;
        eo.name = d.options.endpoint_name.empty() ? d.options.app : d.options.endpoint_name;
        eo.directory = d.options.endpoint_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.capture = [](Rect) -> std::expected<std::vector<std::uint8_t>, ViewFailure> {
            return std::unexpected(ViewFailure{ViewError::no_pixel_capture});
        };
        eo.protocol.capture_svg = [](Rect) -> std::expected<agent::VectorCapture, ViewFailure> { return std::unexpected(ViewFailure{ViewError::no_vector_capture}); };
        eo.protocol.pixel_owner = [this](Point at) {
            const Impl& held = *impl_;
            const int column = static_cast<int>(at.x / held.options.grid.column), row = static_cast<int>(at.y / held.options.grid.row);
            const std::uint32_t node = held.frame.node_at(column, row);
            return node ? held.runtime->node_at_index(node) : NodeId{};
        };
        eo.protocol.input = d.options.agent_input;
        d.endpoint = agent::Served(*surface, *d.runtime, std::move(eo));
        if (auto started = d.endpoint.start(); !started) {
            warn("the agent endpoint did not start: {}", started.error());
            d.endpoint.reset();
        }
    }
    d.frame = mrender::cells::Frame(d.columns, d.rows, d.options.theme.color("text"), d.options.theme.color("background", Color::white()));
}

Terminal::~Terminal() {
    if (impl_->endpoint) impl_->endpoint.stop();
    impl_->endpoint.reset();
    if (impl_->subject) impl_->runtime->remove_subject(*impl_->subject);
    impl_->tree.reset();
    impl_->runtime.reset();
}

Runtime& Terminal::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Terminal::tree() noexcept { return *impl_->tree; }
const TerminalOptions& Terminal::options() const noexcept { return impl_->options; }
agent::Endpoint* Terminal::endpoint() noexcept { return impl_->endpoint.get(); }
const mrender::cells::Frame& Terminal::frame() const noexcept { return impl_->frame; }
void Terminal::quit() noexcept { impl_->quitting = true; }

NodeId Terminal::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }

const mrender::cells::Frame& Terminal::step(std::string_view input, std::optional<seconds> at) {
    Impl& d = *impl_;
    std::vector<InputEvent> events = d.input.feed(input);
    // The bytes given are all there is for now: an Escape alone is the key.
    std::vector<InputEvent> rest = d.input.flush();
    events.insert(events.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
    d.frame_of(std::move(events), at.value_or(d.index < 0 ? seconds(0) : d.last + d.options.frame_period));
    return d.frame;
}

std::size_t Terminal::settle(seconds limit) {
    const seconds end = impl_->last + limit;
    std::size_t frames = 0;
    while (true) {
        step();
        ++frames;
        const bool soon = impl_->next_work && *impl_->next_work <= end;
        if ((!impl_->animating && !soon) || impl_->last >= end) break;
    }
    return frames;
}

void Terminal::resize(int columns, int rows) {
    Impl& d = *impl_;
    columns = std::max(columns, 1), rows = std::max(rows, 1);
    if (columns == d.columns && rows == d.rows) return;
    d.columns = columns, d.rows = rows;
    std::vector<InputEvent> events{ResizeEvent{.size = d.size(), .scale = 1}};
    d.frame_of(std::move(events), d.index < 0 ? seconds(0) : d.last);
}

Point Terminal::point_of(int column, int row) const noexcept {
    const mrender::cells::Grid& grid = impl_->options.grid;
    return {(static_cast<float>(column) + 0.5f) * grid.column, (static_cast<float>(row) + 0.5f) * grid.row};
}

std::optional<std::pair<int, int>> Terminal::cell_of(std::string_view id) const {
    const Runtime& runtime = *impl_->runtime;
    const NodeId node = runtime.find(id);
    if (!runtime.valid(node)) return std::nullopt;
    const Rect bounds = runtime.drawn_bounds(node);
    if (bounds.empty()) return std::nullopt;
    const Point centre = bounds.center();
    const mrender::cells::Grid& grid = impl_->options.grid;
    return std::pair{static_cast<int>(centre.x / grid.column), static_cast<int>(centre.y / grid.row)};
}

int Terminal::run() {
    namespace os = mcpplibs::os;
    using os::terminal::stream;
    Impl& d = *impl_;
    const bool typed = os::terminal::interactive(stream::in), shown = os::terminal::interactive(stream::out);
    std::optional<os::terminal::raw_mode> raw;
    if (typed && os::terminal::foreground()) {
        if (auto entered = os::terminal::raw_mode::enter({.keep_interrupt = false})) raw = std::move(*entered);
        else warn("the terminal's keys could not be read as typed: {}", entered.error().message());
    }
    mrender::cells::Colors colors = d.options.colors;
    if (colors == mrender::cells::Colors::truecolor) {
        const std::string said = os::env::get("COLORTERM").value_or("");
        if (shown && !said.contains("truecolor") && !said.contains("24bit")) colors = mrender::cells::Colors::palette256;
    }
    // The alternate screen, the cursor hidden; the mouse (buttons, drags, motion; SGR), pasting and focus reported.
    static constexpr std::string_view enter = "\x1b[?1049h\x1b[?25l\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[?2004h\x1b[?1004h";
    static constexpr std::string_view leave = "\x1b[?1004l\x1b[?2004l\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?1049l";
    if (shown) (void)os::stdio::write_output(enter);
    const auto start = std::chrono::steady_clock::now();
    // Wall time since it started, never before the last frame's (a resize's frame takes the last one's time).
    const auto clock = [&] { return std::max(seconds(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()), d.last); };
    mrender::cells::Frame written;
    const auto show = [&] {
        if (d.frame == written) return;
        (void)os::stdio::write_output(mrender::cells::ansi(written, d.frame, colors));
        (void)os::stdio::flush_output();
        written = d.frame;
    };
    if (shown)
        if (const auto size = os::terminal::size(stream::out); size && size->columns > 0 && size->rows > 0)
            resize(static_cast<int>(size->columns), static_cast<int>(size->rows));
    d.frame_of({}, clock());
    show();
    int code = 0;
    while (!d.quitting) {
        if (shown)
            if (const auto size = os::terminal::size(stream::out); size && size->columns > 0 && size->rows > 0)
                resize(static_cast<int>(size->columns), static_cast<int>(size->rows));
        // Wait for input as long as nothing is due: a frame period while something moves, else until the next
        // timer, at most a quarter second (the terminal's size, an agent's requests).
        seconds wait = seconds(0.25);
        if (d.animating) wait = d.options.frame_period;
        else if (d.next_work) wait = std::clamp(*d.next_work - clock(), seconds(0), wait);
        const auto read = os::stdio::read_input_for(std::chrono::milliseconds(static_cast<std::int64_t>(wait.count() * 1000)));
        if (!read) {
            warn("the terminal's input: {}", read.error().message());
            code = 1;
            break;
        }
        std::vector<InputEvent> events;
        if (!*read) {
            events = d.input.flush();  // a pause: an Escape alone was the key
        } else if ((*read)->empty()) {
            break;  // the input ended
        } else {
            events = d.input.feed(**read);
        }
        d.frame_of(std::move(events), clock());
        show();
    }
    if (shown) {
        (void)os::stdio::write_output(leave);
        (void)os::stdio::flush_output();
    }
    return code;
}

}  // namespace omui
