// quay-play — Quay on this machine's device.
//
//   quay-play shot --out DIR [--size WxH] [--every N] [--to N] [--reference]
//       the guide's game (quay::Guide, the scripted player that knows the harbour) drawn frame by frame and every
//       Nth frame written as DIR/quay-FFFFF.png: the signature shot, the same pixels every run on a device and
//       its reference. --reference draws with the CPU reference instead (no device needed). Says what it did as
//       one JSON line.
//
// Exit 0 done, 1 the device or a frame failed (said why), 2 a wrong command.
import std;
import mlibs.rhi;
import mlibs.image;
import xrender.rhi.sdl;
import omui.showcase.quay.picture;
import mcpplibs.cmdline;
import mcpplibs.os;

namespace {

namespace cmd = mcpplibs::cmdline;
namespace rhi = mlibs::rhi;
namespace picture = quay::picture;

std::int64_t number(const cmd::ParsedArgs& parsed, std::string_view name, std::int64_t otherwise) {
    const auto text = parsed.value(name);
    if (!text) return otherwise;
    std::int64_t value = otherwise;
    const auto [end, failed] = std::from_chars(text->data(), text->data() + text->size(), value);
    return failed == std::errc{} && end == text->data() + text->size() ? value : -1;
}

int fail(std::string_view why, int code = 1) {
    std::println(std::cerr, "quay-play: {}", why);
    return code;
}

bool written(const std::filesystem::path& file, const std::uint8_t* rgba, int width, int height) {
    const mlibs::image::View view{rgba, width, height, width * 4, mlibs::image::Alpha::straight};
    return mlibs::image::save_png(file, view).has_value();
}

int shot(const cmd::ParsedArgs& parsed) {
    const auto out = parsed.value("out");
    if (!out) return fail("shot needs --out DIR", 2);
    int width = 960, height = 540;
    if (const auto size = parsed.value("size")) {
        const auto by = size->find('x');
        if (by == std::string::npos || std::from_chars(size->data(), size->data() + by, width).ec != std::errc{} ||
            std::from_chars(size->data() + by + 1, size->data() + size->size(), height).ec != std::errc{} || width < 16 || height < 16)
            return fail("--size is WxH, each 16 or more", 2);
    }
    const std::int64_t every = number(parsed, "every", 30), to = number(parsed, "to", 60 * 300);
    if (every < 1 || to < 1) return fail("--every and --to are whole numbers above 0", 2);
    const bool reference = parsed.is_flag_set("reference");
    const std::filesystem::path directory = mcpplibs::os::fs::from_utf8(*out);
    std::error_code made;
    std::filesystem::create_directories(directory, made);
    if (made) return fail(std::format("{}: {}", *out, made.message()));

    auto drawn = picture::Picture::make(quay::level::harbour());
    if (!drawn) return fail(drawn.error());
    std::unique_ptr<rhi::Device> device;
    std::optional<picture::OnDevice> on_device;
    if (!reference) {
        xrender::rhi::sdl::Options options;
        options.formats = xrender::mesh::gpu::Pass::shader_formats();
        if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
        auto created = xrender::rhi::sdl::create(options);
        if (!created) return fail(std::format("no device ({}); --reference draws without one", created.error().message));
        device = std::move(*created);
        auto prepared = picture::OnDevice::make(*device, *drawn, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        if (!prepared) return fail(std::format("the picture on the device: {}", prepared.error().message));
        on_device.emplace(std::move(*prepared));
    }

    quay::State state = quay::start(quay::level::harbour());
    quay::Guide guide;
    std::size_t files = 0;
    rhi::CommandList list;
    while (state.frame <= static_cast<std::uint64_t>(to)) {
        if (state.frame % static_cast<std::uint64_t>(every) == 0 || state.outcome != quay::Outcome::playing) {
            const auto file = directory / std::format("quay-{:05}.png", state.frame);
            if (reference) {
                drawn->draw(state, {.width = width, .height = height});
                if (!written(file, drawn->shown().data(), width, height)) return fail(std::format("{} could not be written", mcpplibs::os::fs::generic_utf8(file)));
            } else {
                drawn->prepare(state);
                list.clear();
                if (!on_device->record(list, *drawn)) return fail(std::format("frame {} could not be recorded", state.frame));
                const auto submitted = device->submit(list);
                if (!submitted || !device->wait(*submitted)) return fail(std::format("frame {} failed on the device", state.frame));
                const auto pixels = device->read(on_device->colour(), rhi::Region{0, 0, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
                if (!pixels) return fail(std::format("frame {} could not be read back: {}", state.frame, pixels.error().message));
                if (!written(file, reinterpret_cast<const std::uint8_t*>(pixels->data()), width, height)) return fail(std::format("{} could not be written", mcpplibs::os::fs::generic_utf8(file)));
            }
            ++files;
        }
        if (state.outcome != quay::Outcome::playing) break;
        quay::step(state, guide(state));
    }
    std::println(R"({{"frames":{},"written":{},"outcome":"{}","why":"{}","drawn_by":"{}","size":[{},{}],"out":"{}"}})", state.frame, files,
                 quay::outcome_names[static_cast<std::size_t>(state.outcome)], state.why, reference ? std::string("reference") : device->caps().device, width, height,
                 mcpplibs::os::fs::generic_utf8(directory));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    using cmd::Option;
    cmd::App app = cmd::App("quay-play")
        .description("Quay on this machine's device")
        .subcommand(cmd::App("shot")
            .description("The guide's game drawn frame by frame, every Nth frame written as a PNG: the same pixels every run")
            .option(Option("out").takes_value().value_name("DIR").help("where the frames go (quay-FFFFF.png)"))
            .option(Option("size").takes_value().value_name("WxH").help("pixels (default 960x540)"))
            .option(Option("every").takes_value().value_name("N").help("write every Nth frame (default 30), and the last"))
            .option(Option("to").takes_value().value_name("N").help("stop at this frame (default: the game's end)"))
            .option(Option("reference").help("draw with the CPU reference, no device")));
    cmd::Argv args(argv, argv + argc);
    auto parsed = app.parse_from(args);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        return fail(parsed.error().message, 2);
    }
    if (!parsed->has_subcommand()) {
        std::println("quay-play shot --out DIR …   (quay-play <command> --help)");
        return 0;
    }
    if (parsed->subcommand_name() == "shot") return shot(parsed->subcommand()->get());
    return 2;
}
