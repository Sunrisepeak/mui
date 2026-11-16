// hanim.studio:sources (sources.cppm): animation documents as a design's sources.
module hanim.studio;

import std;
import mcpplibs.os;
import mlibs.hash;

namespace hanim::studio {

namespace {

constexpr std::string_view suffix = ".animation.omui.json";
constexpr int moments = 16;

std::string utf8_name(const std::filesystem::path& path) {
    const std::u8string text = path.filename().u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string joined(const std::vector<std::string>& names) {
    std::string out = "[";
    for (const std::string& name : names) out += std::format("{}\"{}\"", out.size() > 1 ? "," : "", name);
    return out + "]";
}

}  // namespace

std::map<std::string, std::string> DocumentSources::texts() {
    std::map<std::string, std::string> out;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        const std::string name = utf8_name(entry.path());
        if (!entry.is_regular_file() || !name.ends_with(suffix)) continue;
        if (auto text = mcpplibs::os::fs::read(entry.path())) out.emplace(name, std::move(*text));
    }
    return out;
}

std::expected<std::string, std::string> DocumentSources::apply(const std::map<std::string, std::string>& texts, Reason) {
    // Checked whole first: what is written is only ever a design that reads.
    for (const auto& [path, text] : texts) {
        if (!path.ends_with(suffix) || path.contains('/') || path.contains('\\'))
            return std::unexpected(std::format("{} is no animation document of this directory (<name>{})", path, suffix));
        auto document = read(text);
        if (!document) return std::unexpected(std::format("{}: {}", path, to_text(document.error())));
        if (auto opened = Studio::open(*document); !opened) return std::unexpected(std::format("{}: {}", path, to_text(opened.error())));
    }
    const auto now = this->texts();
    std::vector<std::string> changed, removed;
    for (const auto& [path, text] : texts) {
        if (const auto there = now.find(path); there != now.end() && there->second == text) continue;
        if (auto written = mcpplibs::os::fs::write_atomic(directory_ / mcpplibs::os::fs::from_utf8(path), text); !written)
            return std::unexpected(std::format("{}: {}", path, written.error().message()));
        changed.push_back(path);
    }
    for (const auto& [path, text] : now) {
        if (texts.contains(path)) continue;
        std::error_code error;
        std::filesystem::remove(directory_ / mcpplibs::os::fs::from_utf8(path), error);
        removed.push_back(path);
    }
    return std::format(R"({{"changed":{},"removed":{}}})", joined(changed), joined(removed));
}

std::string DocumentSources::state() {
    mlibs::hash::Fnv1a hash;
    for (const auto& [path, text] : texts()) {
        hash.str(path);
        auto document = read(text);
        auto studio = document ? Studio::open(*document) : std::unexpected(document.error());
        if (!studio) {
            hash.str("does not read");
            continue;
        }
        double length = studio->duration().count();
        if (!std::isfinite(length)) length = 10;
        hash.f64(length);
        for (int moment = 0; moment < moments; ++moment) {
            auto objects = studio->state(seconds(length * moment / (moments - 1)));
            if (!objects) continue;
            for (const ObjectState& object : *objects) {
                hash.str(object.id).byte(object.drawn ? 1 : 0).f64(object.position.x).f64(object.position.y).f64(object.scale.x).f64(object.scale.y);
                hash.f64(object.rotation).f64(object.opacity);
                if (object.fill) hash.f32(object.fill->r).f32(object.fill->g).f32(object.fill->b).f32(object.fill->a);
            }
        }
    }
    return std::format("{:016x}", hash.value());
}

}  // namespace hanim::studio
