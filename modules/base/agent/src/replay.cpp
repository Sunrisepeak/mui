// How a subject was designed, as data a release plays (mlibs.agent:replay; replay@1).
module mlibs.agent;

import std;
import mlibs.hash;
import mlibs.json;

namespace mlibs::agent {

namespace {

std::string key_of(std::string_view text) { return std::format("{:016x}", mlibs::hash::Fnv1a{}.text(text).value()); }

}  // namespace

std::expected<Replay, std::string> read_replay(std::string_view text, std::string_view file) {
    auto document = mlibs::json::Document::parse(text, std::string(file));
    if (!document) return std::unexpected(document.error().text());
    const auto fail = [&](const mlibs::json::Node& at, std::string message, std::string hint = {}) {
        return std::unexpected(document->problem(at, std::move(message), std::move(hint)).text());
    };
    const mlibs::json::Node root = document->root();
    if (!root.is_object()) return fail(root, "a replay is an object", R"({"$format": "replay@1", "replay": {…}})");
    if (root["$format"].text() != std::optional<std::string>(replay_format))
        return fail(root["$format"] ? root["$format"] : root, std::format("a replay names its format: \"$format\": \"{}\"", replay_format),
                    "design.history.export writes one");
    const mlibs::json::Node replay = root["replay"];
    if (!replay.is_object()) return fail(replay ? replay : root, "no replay: {\"replay\": {\"steps\": […], \"texts\": {…}}}");
    Replay out;
    out.doc = replay["doc"].text().value_or("");
    out.subject = replay["subject"].text().value_or("");
    out.branch = replay["branch"].text().value_or("");
    const mlibs::json::Node texts = replay["texts"];
    if (!texts.is_object()) return fail(texts ? texts : replay, "the texts are an object: a key → a text");
    const mlibs::json::Node steps = replay["steps"];
    if (!steps.is_array() || steps.size() == 0) return fail(steps ? steps : replay, "a replay has steps, oldest first");
    for (const mlibs::json::Node& step : steps.items()) {
        if (!step.is_object()) return fail(step, "a step is an object: {caption, actor, seconds, files}");
        ReplayStep made;
        made.caption = step["caption"].text().value_or("");
        made.actor = step["actor"].text().value_or("");
        if (const mlibs::json::Node seconds = step["seconds"]) {
            const auto value = seconds.number();
            if (!value || !(*value > 0) || *value > 60) return fail(seconds, "a step's seconds on screen are a number above 0, at most 60", "1.5");
            made.seconds = *value;
        }
        const mlibs::json::Node files = step["files"];
        if (!files.is_object() || files.size() == 0) return fail(files ? files : step, "a step names its sources' texts: {\"files\": {\"app/main.ts\": key}}");
        for (const auto& [path, key] : files.members()) {
            const auto named = key.text();
            if (!named) return fail(key, "a source's text is named by its key in `texts`");
            const auto found = texts[*named].text();
            if (!found) return fail(key, std::format("no text `{}` in /replay/texts", *named));
            made.texts.emplace(path, *found);
        }
        out.steps.push_back(std::move(made));
    }
    return out;
}

std::string write_replay(const Replay& replay) {
    // Each text once, its key its content's hash (a different text of the same hash gets the next free key).
    std::map<std::string, std::string> texts;
    std::map<std::string_view, std::string> keys;
    for (const ReplayStep& step : replay.steps)
        for (const auto& [path, text] : step.texts) {
            if (keys.contains(text)) continue;
            std::string key = key_of(text);
            for (int attempt = 1; texts.contains(key) && texts.at(key) != text; ++attempt) key = std::format("{}-{}", key_of(text), attempt);
            texts.emplace(key, text);
            keys.emplace(text, key);
        }
    std::string out = std::format("{{\n  \"$format\": {},\n  \"replay\": {{\n    \"doc\": {},\n    \"subject\": {},\n    \"branch\": {},\n    \"steps\": [",
                                  json_text(replay_format), json_text(replay.doc), json_text(replay.subject), json_text(replay.branch));
    for (std::size_t index = 0; index < replay.steps.size(); ++index) {
        const ReplayStep& step = replay.steps[index];
        out += std::format("{}\n      {{\"caption\": {}, \"actor\": {}, \"seconds\": {}, \"files\": {{", index ? "," : "", json_text(step.caption),
                           json_text(step.actor), step.seconds);
        bool first = true;
        for (const auto& [path, text] : step.texts) out += std::format("{}{}: {}", first ? "" : ", ", json_text(path), json_text(keys.at(text))), first = false;
        out += "}}";
    }
    out += "\n    ],\n    \"texts\": {";
    bool first = true;
    for (const auto& [key, text] : texts) out += std::format("{}\n      {}: {}", first ? "" : ",", json_text(key), json_text(text)), first = false;
    out += "\n    }\n  }\n}\n";
    return out;
}

}  // namespace mlibs::agent
