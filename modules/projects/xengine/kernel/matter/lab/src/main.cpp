// matter-lab — the material lab without a GUI, for people, agents and CI.
//
//   matter-lab new DIR [--matter FILE] [--look FILE] [--shape WxH] [--filled NAME]
//                                    a lab begun in DIR: the standard definition and look, or the snapshots
//                                    given (*.matter.omui.json, *.appearance.omui.json); its first commit
//   matter-lab run DIR [--script FILE] [--actor ACTOR]
//                                    requests, a JSON line each, from FILE or standard input — to the lab,
//                                    its field, its matter and its frame — each answered in records on
//                                    standard output: a batch, a test, an agent with a pipe
//   matter-lab serve DIR [--name NAME]
//                                    the lab as an endpoint agents connect to (mlibs.agent), a session each
//                                    with an identity of its own (agent:session-1, -2, …), until it is
//                                    stopped: several at one lab, each holding its part
//   matter-lab picture DIR --out PNG [--steps N] [--scale N]
//                                    the scene played N steps on and drawn: the pixels the frame subject answers
//
// One JSON answer on standard output (run: the records); exit 0 done, 1 failed (said why), 2 a wrong command.
import std;
import openas;
import mlibs.agent;
import xengine.matter.lab;
import mcpplibs.os;
import mcpplibs.cmdline;

namespace {

namespace cmdline = mcpplibs::cmdline;
namespace lab = xengine::matter::lab;
using mlibs::agent::json_text;

constexpr std::string_view usage =
    "usage: matter-lab new DIR [--matter FILE] [--look FILE] [--shape WxH] [--filled NAME] | matter-lab run DIR [--script FILE] [--actor ACTOR] | "
    "matter-lab serve DIR [--name NAME] | matter-lab picture DIR --out PNG [--steps N] [--scale N]";

int wrong(std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{},\"usage\":{}}}", json_text(text), json_text(usage));
    return 2;
}
int failed(std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{}}}", json_text(text));
    return 1;
}

std::filesystem::path path_of(std::string_view text) { return mcpplibs::os::fs::from_utf8(text); }
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::optional<std::string> read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template <class Number>
std::optional<Number> number_of(std::string_view text) {
    Number value{};
    const auto read = std::from_chars(text.data(), text.data() + text.size(), value);
    return read.ec == std::errc{} && read.ptr == text.data() + text.size() ? std::optional(value) : std::nullopt;
}

int begin(const cmdline::ParsedArgs& args) {
    lab::Begin from;
    if (const auto file = args.value("matter")) {
        auto text = read(path_of(*file));
        if (!text) return failed(std::format("{} cannot be read", *file));
        from.matter = std::move(*text);
    }
    if (const auto file = args.value("look")) {
        auto text = read(path_of(*file));
        if (!text) return failed(std::format("{} cannot be read", *file));
        from.look = std::move(*text);
    }
    if (const auto shape = args.value("shape")) {
        const std::string_view value = *shape;
        const std::size_t by = value.find('x');
        const auto width = by == std::string_view::npos ? std::nullopt : number_of<std::uint32_t>(value.substr(0, by));
        const auto height = width ? number_of<std::uint32_t>(value.substr(by + 1)) : std::nullopt;
        if (!width || !height || !*width || !*height) return wrong("--shape is WxH in cells");
        from.shape = {*width, *height};
    }
    if (const auto filled = args.value("filled")) from.filled = *filled;
    auto made = lab::Lab::create(path_of(args.positional(0)), from);
    if (!made) return failed(made.error());
    lab::Lab& here = **made;
    std::println("{{\"ok\":true,\"lab\":{},\"shape\":[{},{}],\"materials\":{},\"laws\":{},\"commit\":{}}}", json_text(utf8(here.directory())), here.field().state().shape.width,
                 here.field().state().shape.height, here.matter().program().materials().size(), here.matter().program().laws().size(),
                 json_text(here.history().resolve("HEAD").value_or(std::string())));
    return 0;
}

int run(const cmdline::ParsedArgs& args) {
    auto opened = lab::Lab::open(path_of(args.positional(0)));
    if (!opened) return failed(opened.error());
    mlibs::agent::SessionOptions options;
    if (const auto actor = args.value("actor")) {
        auto said = openas::parse(*actor);
        if (!said) return wrong("--actor is an actor as openas writes one: agent:name, person:name");
        options.actor = std::move(*said);
    }
    options.facts = (*opened)->facts();  // the lab's own book: what an earlier run asked is on record for this one
    mlibs::agent::SubjectSession session((*opened)->subjects(), std::move(options));
    std::ifstream file;
    if (const auto script = args.value("script")) {
        file.open(path_of(*script), std::ios::binary);
        if (!file) return failed(std::format("{} cannot be read", *script));
    }
    std::istream& in = file.is_open() ? static_cast<std::istream&>(file) : std::cin;
    bool all_done = true;
    for (std::string line; std::getline(in, line);) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        session.request(line, [&](std::string_view record) {
            std::println("{}", record);
            if (record.contains("\"kind\":\"error\"")) all_done = false;
        });
        session.poll();
    }
    return all_done ? 0 : 1;
}

int serve(const cmdline::ParsedArgs& args) {
    auto opened = lab::Lab::open(path_of(args.positional(0)));
    if (!opened) return failed(opened.error());
    lab::Lab& here = **opened;
    mlibs::agent::Endpoint endpoint({.name = std::string(args.value("name").value_or("matter-lab")),
                                     .app = "xengine.matter.lab",
                                     .interface_name = std::string(here.interface_name()),
                                     .spec = std::string(openas::version),
                                     .session = [&] { return here.session(); }});
    if (auto started = endpoint.start(); !started) return failed(started.error());
    std::println("{{\"ok\":true,\"endpoint\":{},\"directory\":{},\"lab\":{}}}", json_text(endpoint.name()), json_text(utf8(endpoint.directory())), json_text(utf8(here.directory())));
    std::fflush(nullptr);  // said before the first request: whoever started it reads the endpoint from here
    // Requests are answered as they come, one at a time: the lab is one world. Stopped from outside.
    while (endpoint.listening())
        if (endpoint.pump() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    return 0;
}

int picture(const cmdline::ParsedArgs& args) {
    const auto out = args.value("out");
    if (!out) return wrong("picture takes --out PNG");
    const auto steps = args.value("steps") ? number_of<std::int64_t>(*args.value("steps")) : std::optional<std::int64_t>(0);
    const auto scale = args.value("scale") ? number_of<int>(*args.value("scale")) : std::optional<int>(0);
    if (!steps || *steps < 0 || *steps > 100000) return wrong("--steps is 0 … 100000");
    if (!scale || *scale < 0 || *scale > 16) return wrong("--scale is 1 … 16 pixels a cell");
    auto opened = lab::Lab::open(path_of(args.positional(0)));
    if (!opened) return failed(opened.error());
    lab::Lab& here = **opened;
    here.run(*steps);
    const auto bytes = here.png(*scale);
    if (!bytes) return failed(bytes.error());
    const std::filesystem::path file = path_of(*out);
    std::ofstream written(file, std::ios::binary | std::ios::trunc);
    written.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!written) return failed(std::format("{} cannot be written", *out));
    std::println("{{\"ok\":true,\"picture\":{},\"bytes\":{},\"steps\":{},\"state\":{}}}", json_text(utf8(file)), bytes->size(), here.steps(), json_text(here.state()));
    return 0;
}

cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    return App("matter-lab")
        .description("The material lab without a GUI, for people, agents and CI: JSON out, exit 0 done / 1 failed / 2 a wrong command")
        .subcommand(App("new")
                        .description("a lab begun in a directory: the standard definition and look, or the snapshots given")
                        .arg(Arg("directory").required())
                        .option(Option("matter").takes_value().value_name("FILE").help("a definition's snapshot (*.matter.omui.json)"))
                        .option(Option("look").takes_value().value_name("FILE").help("an appearance's snapshot (*.appearance.omui.json)"))
                        .option(Option("shape").takes_value().value_name("WxH").help("the field, in cells (default 64x48)"))
                        .option(Option("filled").takes_value().value_name("NAME").help("the material every cell is at first (default air)")))
        .subcommand(App("run")
                        .description("requests, a JSON line each, answered in records: a batch, a test, an agent with a pipe")
                        .arg(Arg("directory").required())
                        .option(Option("script").takes_value().value_name("FILE").help("the requests (standard input when none)"))
                        .option(Option("actor").takes_value().value_name("ACTOR").help("who asks: agent:name, person:name (an agent with no name when none)")))
        .subcommand(App("serve")
                        .description("the lab as an endpoint agents connect to, a session each, until it is stopped")
                        .arg(Arg("directory").required())
                        .option(Option("name").takes_value().value_name("NAME").help("the endpoint's name (default matter-lab)")))
        .subcommand(App("picture")
                        .description("the scene played some steps on and drawn, as a PNG")
                        .arg(Arg("directory").required())
                        .option(Option("out").takes_value().value_name("PNG").help("the picture written"))
                        .option(Option("steps").takes_value().value_name("N").help("steps run first (default 0)"))
                        .option(Option("scale").takes_value().value_name("N").help("pixels a cell, 1 … 16 (default 4)")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong("a command: new, run, serve or picture");
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    if (args.positional_count() == 0) return wrong("the lab's directory");
    if (parsed->subcommand_name() == "new") return begin(args);
    if (parsed->subcommand_name() == "run") return run(args);
    if (parsed->subcommand_name() == "serve") return serve(args);
    return picture(args);
}
