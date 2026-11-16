// omui-devtools gpu: packages ordered as `sort -V` orders them (the newest driver is the last), and an ELF
// library's machine read from its header (xim's x86_64 Vulkan loader on an aarch64 machine is told apart).
import std;
import mcpplibs.mtest;
import omui.devtools.gpu;

using namespace mcpplibs::mtest;
namespace gpu = omui::devtools::gpu;

UTest cases{"devtools.gpu", [] {
    "versions in `sort -V` order: digits by their value"_test = [] {
        expect(gpu::version_less("vulkan-loader/1.4.9/lib", "vulkan-loader/1.4.313/lib"));
        expect(!gpu::version_less("vulkan-loader/1.4.313/lib", "vulkan-loader/1.4.9/lib"));
        expect(gpu::version_less("mesa-lavapipe/26.2.1", "mesa-lavapipe/26.10.0"));
        expect(gpu::version_less("a/1.2", "a/1.2.1") && !gpu::version_less("a/1.2", "a/1.2"));
    };

    "an ELF library's machine from its header; anything else is not ELF"_test = [] {
        const auto scratch = std::filesystem::temp_directory_path() / "omui-devtools-gpu-test";
        std::filesystem::create_directories(scratch);
        const auto write = [&](std::string_view name, std::uint8_t data, std::uint16_t machine) {
            std::array<unsigned char, 64> header{0x7F, 'E', 'L', 'F', 2, data};
            header[18] = static_cast<unsigned char>(data == 2 ? machine >> 8 : machine & 0xFF);
            header[19] = static_cast<unsigned char>(data == 2 ? machine & 0xFF : machine >> 8);
            std::ofstream(scratch / name, std::ios::binary).write(reinterpret_cast<const char*>(header.data()), header.size());
            return scratch / name;
        };
        expect(eq(gpu::elf_machine(write("x86", 1, 62)).value_or(""), std::string("x86_64")));
        expect(eq(gpu::elf_machine(write("arm", 1, 183)).value_or(""), std::string("aarch64")));
        expect(eq(gpu::elf_machine(write("big", 2, 183)).value_or(""), std::string("aarch64")));
        std::ofstream(scratch / "text") << "not a library at all";
        expect(!gpu::elf_machine(scratch / "text").has_value());
        expect(!gpu::elf_machine(scratch / "missing").has_value());
        std::filesystem::remove_all(scratch);
    };
}};
