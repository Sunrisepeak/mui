module mlibs.testing.api;

import std;

namespace mlibs::testing {

std::filesystem::path scratch_dir(std::string_view test_name) {
    auto dir = std::filesystem::temp_directory_path() / "openmui-tests" / std::string(test_name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}  // namespace mlibs::testing
