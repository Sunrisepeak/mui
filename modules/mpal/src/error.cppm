// mpal:error — why a service was not done.
export module mpal:error;

import std;

export namespace mpal {

struct Error {
    enum class Code : std::uint8_t {
        unsupported,  // this platform does not give it
        unavailable,  // it does, but not here now (no session bus, no service)
        refused,      // the service answered no
        failed,       // something went wrong on the way
    };
    Code        code = Code::failed;
    std::string text;
};

[[nodiscard]] inline std::unexpected<Error> failure(Error::Code code, std::string text) { return std::unexpected(Error{code, std::move(text)}); }

}  // namespace mpal
