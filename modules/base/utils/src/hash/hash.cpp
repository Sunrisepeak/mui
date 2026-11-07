module mlibs.hash.api;

import std;

namespace mlibs::hash {

std::string Fnv1a::hex() const { return std::format("{:016x}", h_); }

}  // namespace mlibs::hash
