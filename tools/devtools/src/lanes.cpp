module omui.devtools.lanes;

import std;
import nlohmann.json;

namespace omui::devtools::lanes {

namespace {

using json = nlohmann::json;

std::string text_at(const json& object, std::string_view key) {
    if (!object.is_object()) return {};
    const auto found = object.find(std::string(key));
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> words(std::string_view selection) {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < selection.size()) {
        while (at < selection.size() && (selection[at] == ',' || std::isspace(static_cast<unsigned char>(selection[at])))) ++at;
        const std::size_t start = at;
        while (at < selection.size() && selection[at] != ',' && !std::isspace(static_cast<unsigned char>(selection[at]))) ++at;
        if (at > start) found.push_back(selection.substr(start, at - start));
    }
    return found;
}

std::string joined(const std::vector<std::string>& names, std::string_view between = " ") {
    std::string text;
    for (const auto& name : names) {
        if (!text.empty()) text += between;
        text += name;
    }
    return text;
}

std::string row_json(const Row& row) {
    return json{{"os", row.os}, {"toolchain", row.toolchain}, {"profile", row.profile}}.dump();
}

}  // namespace

const Lane* Catalogue::lane(std::string_view name) const {
    const auto found = std::ranges::find(lanes, name, &Lane::name);
    return found == lanes.end() ? nullptr : &*found;
}

std::expected<Catalogue, std::string> read(std::string_view text) {
    const json data = json::parse(text, nullptr, false);
    if (data.is_discarded() || !data.is_object()) return std::unexpected("not JSON");
    if (!data.contains("lanes") || !data["lanes"].is_array()) return std::unexpected("no `lanes` list");
    Catalogue catalogue;
    std::set<std::string, std::less<>> names{"none"};
    for (const auto& entry : data["lanes"]) {
        Lane lane;
        lane.name = text_at(entry, "name");
        if (lane.name.empty()) return std::unexpected("a lane without a name");
        if (!names.insert(lane.name).second) return std::unexpected(std::format("`{}` is named twice", lane.name));
        lane.job = text_at(entry, "job");
        lane.workflow = text_at(entry, "workflow");
        lane.what = text_at(entry, "what");
        if (entry.contains("minutes") && entry["minutes"].is_number_integer()) lane.minutes = entry["minutes"].get<int>();
        if (entry.contains("rows") && entry["rows"].is_array())
            for (const auto& row : entry["rows"]) {
                Row made{text_at(row, "os"), text_at(row, "toolchain"), text_at(row, "profile")};
                if (made.os.empty() || made.toolchain.empty() || made.profile.empty())
                    return std::unexpected(std::format("lane `{}`: a row needs os, toolchain and profile", lane.name));
                lane.rows.push_back(std::move(made));
            }
        const int kinds = (lane.rows.empty() ? 0 : 1) + (lane.job.empty() ? 0 : 1) + (lane.workflow.empty() ? 0 : 1);
        if (kinds != 1) return std::unexpected(std::format("lane `{}`: one of rows, job or workflow", lane.name));
        catalogue.lanes.push_back(std::move(lane));
    }
    if (data.contains("groups") && data["groups"].is_array())
        for (const auto& entry : data["groups"]) {
            Group group{text_at(entry, "name"), {}, text_at(entry, "what")};
            if (group.name.empty()) return std::unexpected("a group without a name");
            if (!names.insert(group.name).second) return std::unexpected(std::format("`{}` is named twice", group.name));
            if (entry.contains("lanes") && entry["lanes"].is_array())
                for (const auto& member : entry["lanes"]) {
                    const std::string name = member.is_string() ? member.get<std::string>() : std::string();
                    if (!catalogue.lane(name)) return std::unexpected(std::format("group `{}`: no lane `{}`", group.name, name));
                    group.lanes.push_back(name);
                }
            catalogue.groups.push_back(std::move(group));
        }
    if (data.contains("events") && data["events"].is_object())
        for (const auto& item : data["events"].items()) {
            if (!item.value().is_string()) return std::unexpected(std::format("event `{}`: a selection is text", item.key()));
            catalogue.events.emplace(item.key(), item.value().get<std::string>());
        }
    for (const auto& [event, selection] : catalogue.events)
        if (auto checked = select(catalogue, selection); !checked)
            return std::unexpected(std::format("event `{}`: {}", event, checked.error()));
    return catalogue;
}

std::expected<std::vector<std::string>, std::string> select(const Catalogue& catalogue, std::string_view selection) {
    std::set<std::string, std::less<>> chosen;
    for (std::string_view word : words(selection)) {
        bool remove = false;
        if (word.front() == '-' || word.front() == '+') {
            remove = word.front() == '-';
            word.remove_prefix(1);
        }
        std::vector<std::string> named;
        if (word == "none") {
            chosen.clear();
            continue;
        }
        if (catalogue.lane(word)) {
            named.emplace_back(word);
        } else if (const auto group = std::ranges::find(catalogue.groups, word, &Group::name); group != catalogue.groups.end()) {
            named = group->lanes;
        } else {
            std::vector<std::string> lanes, groups;
            for (const auto& lane : catalogue.lanes) lanes.push_back(lane.name);
            for (const auto& group_entry : catalogue.groups) groups.push_back(group_entry.name);
            return std::unexpected(std::format("no lane or group `{}`; lanes: {}; groups: {}; and `none`", word, joined(lanes, ", "),
                                               joined(groups, ", ")));
        }
        for (auto& name : named) {
            if (remove) {
                chosen.erase(name);
            } else {
                chosen.insert(std::move(name));
            }
        }
    }
    std::vector<std::string> ordered;
    for (const auto& lane : catalogue.lanes)
        if (chosen.contains(lane.name)) ordered.push_back(lane.name);
    return ordered;
}

std::optional<std::string> trailer(std::string_view message) {
    message = trim(message);
    // The last paragraph: after the last blank line.
    std::size_t start = 0;
    for (std::size_t at = 0; at < message.size();) {
        const std::size_t end = std::min(message.find('\n', at), message.size());
        if (trim(message.substr(at, end - at)).empty()) start = end + 1;
        at = end + 1;
    }
    if (start == 0) return std::nullopt;  // a subject alone has no trailers
    std::optional<std::string> found;
    std::string_view paragraph = message.substr(std::min(start, message.size()));
    while (!paragraph.empty()) {
        const std::size_t end = std::min(paragraph.find('\n'), paragraph.size());
        const std::string_view line = trim(paragraph.substr(0, end));
        if (line.starts_with("CI:")) found = std::string(trim(line.substr(3)));
        paragraph.remove_prefix(std::min(end + 1, paragraph.size()));
    }
    return found;
}

std::expected<Plan, std::string> plan(const Catalogue& catalogue, std::string_view event, std::string_view given,
                                      std::string_view message) {
    Plan made;
    made.event = event;
    if (!trim(given).empty()) {
        made.selection = trim(given);
        made.source = "the lanes it was given";
    } else if (event == "pull_request") {
        if (auto named = trailer(message)) {
            made.selection = *named;
            made.source = "the commit's CI: trailer";
        } else {
            made.source = "a pull request's push: the lint alone";
        }
    } else if (const auto found = catalogue.events.find(event); found != catalogue.events.end()) {
        made.selection = found->second;
        made.source = std::format("{}'s default", event);
    } else {
        return std::unexpected(std::format("no default for the event `{}` (the catalogue's `events`)", event));
    }
    auto selected = select(catalogue, made.selection);
    if (!selected) return std::unexpected(std::format("{} (from {})", selected.error(), made.source));
    made.lanes = std::move(*selected);
    // A pull request's run and a request from the command line run none of ci.yml's lanes themselves.
    const bool here = event != "pull_request" && event != "request";
    for (const auto& name : made.lanes) {
        const Lane& lane = *catalogue.lane(name);
        made.minutes += lane.minutes;
        if (!lane.in_ci()) {
            made.workflows.push_back(lane.workflow);
        } else if (!here) {
            made.requested.push_back(lane.name);
        } else if (!lane.job.empty()) {
            made.jobs.push_back(lane.job);
        } else {
            made.rows.insert(made.rows.end(), lane.rows.begin(), lane.rows.end());
        }
    }
    return made;
}

std::string matrix_json(const Plan& plan) {
    std::string text = "[";
    for (const auto& row : plan.rows) {
        if (text.size() > 1) text += ',';
        text += row_json(row);
    }
    return text + "]";
}

std::string outputs(const Catalogue& catalogue, const Plan& plan) {
    std::string text = std::format("matrix={}\nrows={}\n", matrix_json(plan), plan.rows.empty() ? "false" : "true");
    for (const auto& lane : catalogue.lanes)
        if (!lane.job.empty())
            text += std::format("{}={}\n", lane.name, std::ranges::contains(plan.jobs, lane.job) ? "true" : "false");
    return text;
}

std::string describe(const Catalogue& catalogue, const Plan& plan) {
    std::string text = std::format("**{}**, lanes from {}{}\n\n", plan.event, plan.source,
                                   plan.selection.empty() ? std::string() : std::format(": `{}`", plan.selection));
    if (plan.lanes.empty()) return text + "The lint alone (about 7 minutes).\n";
    text += "| lane | where | what it checks | minutes, about |\n|---|---|---|---:|\n";
    for (const auto& name : plan.lanes) {
        const Lane& lane = *catalogue.lane(name);
        std::string where;
        if (!lane.in_ci()) {
            where = std::format("requested: {}", lane.workflow);
        } else if (std::ranges::contains(plan.requested, lane.name)) {
            where = "requested: ci.yml";
        } else {
            where = lane.job.empty() ? std::format("this run, {} row(s)", lane.rows.size()) : "this run";
        }
        text += std::format("| {} | {} | {} | {} |\n", lane.name, where, lane.what, lane.minutes);
    }
    text += std::format("\nAbout {} runner minutes besides the lint.\n", plan.minutes);
    return text;
}

}  // namespace omui::devtools::lanes
