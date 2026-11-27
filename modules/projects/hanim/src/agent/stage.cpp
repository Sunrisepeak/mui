module hanim.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.error;
import mlibs.hash;
import mlibs.json;

namespace hanim::agent {

namespace {

constexpr std::string_view state_name(PlaybackState state) noexcept {
    switch (state) {
        case PlaybackState::playing: return "playing";
        case PlaybackState::paused: return "paused";
        case PlaybackState::finished: return "finished";
        case PlaybackState::cancelled: return "cancelled";
    }
    return "?";
}

// A time or a ratio as JSON: a number, or null where it has no end (a program looping forever).
std::string number(double value) { return std::isfinite(value) ? std::format("{}", value) : std::string("null"); }

std::string described(const PlaybackInfo& info) {
    return std::format("{{\"playback\":{{\"slot\":{},\"generation\":{}}},\"state\":\"{}\",\"position\":{},\"duration\":{},\"rate\":{},\"progress\":{},\"tracks\":{},"
                       "\"segments\":{}}}",
                       info.ref.slot, info.ref.generation, state_name(info.state), number(info.position.count()), number(info.duration.count()), number(info.rate),
                       number(info.progress), info.track_count, info.segment_count);
}

// The engine's failure as the kernel's error: its own code's name kept in the text.
void failed(mlibs::agent::Answer& answer, const EngineFailure& failure) {
    const mlibs::error::Description description = failure.description();
    // mlibs.error's kinds are openas's codes by name (invalid-argument, stale, refused, …).
    answer.fail(mlibs::error::name(failure.kind()), failure.message.empty() ? std::string(description.name) : std::format("{}: {}", description.name, failure.message));
}

}  // namespace

void Stage::offer(std::string name, Program program) {
    for (auto& [known, held] : offered_)
        if (known == name) {
            held = std::move(program);
            return;
        }
    offered_.emplace_back(std::move(name), std::move(program));
}

std::vector<PlaybackInfo> Stage::playing() const {
    std::vector<PlaybackInfo> out(engine_.size());
    const auto count = engine_.list(out);
    out.resize(count.value_or(0));
    return out;
}

std::uint64_t Stage::state_hash() const {
    mlibs::hash::Fnv1a hash;
    hash.f64(engine_.now().count());
    for (const PlaybackInfo& info : playing())
        hash.u64(info.ref.slot).u64(info.ref.generation).u64(static_cast<std::uint64_t>(info.state)).f64(info.position.count()).f64(info.rate).f64(info.progress);
    return hash.value();
}

void Stage::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };

    if (operation == "animation.list") {
        std::string out = std::format("{{\"now\":{},\"playbacks\":[", number(engine_.now().count()));
        bool first = true;
        for (const PlaybackInfo& info : playing()) {
            out += (first ? "" : ",") + described(info);
            first = false;
        }
        return answer.data(out + "]}");
    }
    if (operation == "animation.prepare" || operation == "animation.play") {
        const auto named = request["program"].text();
        const auto offered = named ? std::ranges::find(offered_, *named, [](const auto& entry) -> const std::string& { return entry.first; }) : offered_.end();
        if (offered == offered_.end()) {
            std::string names;
            for (const auto& [known, held] : offered_) names += std::format("{}`{}`", names.empty() ? "" : ", ", known);
            return answer.fail(named ? openas::error_code::not_found : openas::error_code::invalid_argument,
                               std::format("`program` names one the stage offers: {}", names.empty() ? std::string("none is offered") : names));
        }
        if (operation == "animation.prepare") {
            const auto prepared = engine_.prepare(offered->second);
            if (!prepared) return failed(answer, prepared.error());
            return answer.data(std::format("{{\"program\":{},\"duration\":{},\"natural_duration\":{},\"markers\":{}}}", mlibs::agent::json_text(*named),
                                           number(prepared->duration().count()), number(prepared->natural_duration().count()), prepared->markers().size()));
        }
        const auto playback = engine_.play(offered->second);
        if (!playback) return failed(answer, playback.error());
        return answer.data(std::format("{{\"playback\":{{\"slot\":{},\"generation\":{}}}}}", playback->slot, playback->generation));
    }

    // Every other operation is about one playback.
    const auto asked = request["playback"];
    const auto slot = asked["slot"].integer(), generation = asked["generation"].integer();
    if (!slot || !generation || *slot < 0 || *generation < 0) return invalid("`playback` is {\"slot\": …, \"generation\": …}, as animation.list and animation.play give it");
    PlaybackRef playback;
    playback.owner = engine_.owner();
    playback.slot = static_cast<std::uint32_t>(*slot);
    playback.generation = static_cast<std::uint64_t>(*generation);
    const auto done = [&](const std::expected<void, EngineFailure>& result) {
        if (!result) return failed(answer, result.error());
        const auto info = engine_.inspect(playback);
        // Released, it is gone: nothing more to say of it.
        answer.data(info ? described(*info) : std::string("{\"released\":true}"));
    };

    if (operation == "animation.inspect") {
        const auto info = engine_.inspect(playback);
        if (!info) return failed(answer, info.error());
        return answer.data(described(*info));
    }
    if (operation == "animation.plan") {
        const auto plan = engine_.plan(playback);
        if (!plan) return failed(answer, plan.error());
        return answer.data(std::format("{{\"duration\":{},\"natural_duration\":{},\"markers\":{}}}", number(plan->duration().count()), number(plan->natural_duration().count()),
                                       plan->markers().size()));
    }
    if (operation == "animation.seek") {
        const auto position = request["position"].number();
        if (!position || !std::isfinite(*position)) return invalid("`position`: seconds from its start");
        return done(engine_.seek(playback, seconds(*position)));
    }
    if (operation == "animation.seek-progress") {
        const auto progress = request["progress"].number();
        if (!progress || !std::isfinite(*progress)) return invalid("`progress`: 0 to 1");
        return done(engine_.seek_progress(playback, *progress));
    }
    if (operation == "animation.pause") return done(engine_.pause(playback));
    if (operation == "animation.resume") return done(engine_.resume(playback));
    if (operation == "animation.reverse") return done(engine_.reverse(playback));
    if (operation == "animation.rate") {
        const auto rate = request["rate"].number();
        if (!rate || !std::isfinite(*rate)) return invalid("`rate`: nonzero; negative plays backwards");
        return done(engine_.rate(playback, *rate));
    }
    if (operation == "animation.cancel") return done(engine_.cancel(playback, request["restore"].boolean() == true ? CancelMode::restore : CancelMode::hold));
    if (operation == "animation.release") return done(engine_.release(playback));
}

}  // namespace hanim::agent
