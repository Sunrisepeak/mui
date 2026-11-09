// Where no notification backend is written yet: said, not pretended.
module mpal;

import std;

namespace mpal {

std::expected<std::uint32_t, Error> notify(const Notification&) {
    return failure(Error::Code::unsupported, "notifications on this platform are not written yet (mpal README)");
}

}  // namespace mpal
