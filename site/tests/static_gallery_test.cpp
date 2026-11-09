// The readable fallback has the same selected example and canonical routes
// before JavaScript, during loading, and at the live-gallery handoff.
import std;
import omui.site;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
std::size_t occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0, position = 0;
    while ((position = text.find(needle, position)) != std::string_view::npos) { ++count; position += needle.size(); }
    return count;
}
bool closed_disclosures(std::string_view html) {
    for (std::size_t at = 0; (at = html.find("<details", at)) != std::string_view::npos;) {
        const auto end = html.find('>', at);
        if (end == std::string_view::npos || html.substr(at, end - at).contains(" open")) return false;
        at = end + 1;
    }
    return true;
}
// Native closed <details> keep their bodies out of the initial reading flow.
// This structural check is complemented by the root's real browser checks.
std::string outside_disclosures(std::string_view html) {
    std::string out;
    std::size_t depth = 0;
    for (std::size_t at = 0; at < html.size();) {
        if (html.substr(at).starts_with("<details")) {
            ++depth;
            const auto end = html.find('>', at);
            if (end == std::string_view::npos) break;
            at = end + 1;
        } else if (html.substr(at).starts_with("</details>")) {
            if (depth) --depth;
            at += std::string_view("</details>").size();
        } else { if (!depth) out += html[at]; ++at; }
    }
    return out;
}
std::string_view route_section(std::string_view html, std::string_view route) {
    const auto at = html.find(std::format("<section id=\"/{}\"", route));
    if (at == std::string_view::npos) return {};
    const auto end = html.find("<section id=\"/", at + 1);
    return html.substr(at, end == std::string_view::npos ? end : end - at);
}
}

UTest cases{"site.static_gallery", [] {
    "the initial fallback shows one current example and keeps its directory and metadata closed"_test = [] {
        for (const bool zh : {false, true}) for (const bool creative : {false, true}) {
            const auto html = site::motion_gallery_html(zh, creative);
            const auto shown = outside_disclosures(html);
            const auto id = creative ? "palette-studio" : "shape-morph";
            const auto* current = site::find_motion_case(id, creative);
            expect(current != nullptr); if (!current) continue;
            expect(occurrences(html, "<article data-gallery-current=") == 1 && html.contains(std::format("data-gallery-current=\"{}\"", id))) << "the fallback starts with the same default sample as the live player";
            // The compositions' overview is a section of the motion page (under its works' heading).
            expect(occurrences(shown, "<h1>") == (creative ? 0 : 1) && occurrences(shown, "<h2>") == (creative ? 2 : 1) && shown.contains(zh ? current->zh : current->en)) << "one visible primary example precedes the closed catalogue";
            expect(shown.contains("data-gallery-preview=\"static\"") && shown.contains(zh ? "需要 JavaScript" : "require JavaScript")) << "a readable static state is explicitly distinguished from live playback";
            expect(closed_disclosures(html) && html.contains("<details data-gallery-details>") && html.contains("<details data-gallery-catalog>")) << "neither metadata nor the full directory expands by default";
            expect(!shown.contains("<dl>") && !shown.contains("data-gallery-group=")) << "implementation metadata and all category bodies stay outside the initial reading flow";
            expect(html.find("data-gallery-current=") < html.find("data-gallery-catalog")) << "the main example comes before browsing";
            expect(html.contains("max-height:24rem;overflow:auto")) << "an opened directory remains bounded on a narrow page";
            expect(!html.contains("<script") && !html.contains("<button")) << "the fallback depends on native disclosures and links, not inert scripted controls";
        }
    };
    "every design is reachable through a closed native category with its bilingual canonical link"_test = [] {
        for (const bool zh : {false, true}) for (const bool creative : {false, true}) {
            const auto html = site::motion_gallery_html(zh, creative);
            const auto catalogue_at = html.find("<details data-gallery-catalog>");
            expect(catalogue_at != std::string::npos); if (catalogue_at == std::string::npos) continue;
            const std::string_view catalogue(html.data() + catalogue_at, html.size() - catalogue_at);
            const auto entries = creative ? site::creative_motions() : site::basic_motions();
            expect(occurrences(catalogue, "<li><a href=") == entries.size()) << "all catalogue entries remain reachable without JavaScript";
            for (const auto& entry : entries) {
                const auto route = site::canonical_route(creative ? "explore" : "motion", zh ? "zh" : "en", {}, entry.id);
                expect(occurrences(catalogue, std::format("href=\"#/{}\"", route)) == 1) << route;
                expect(catalogue.contains(zh ? entry.zh : entry.en)) << "the category link uses the requested language";
                const auto group_at = catalogue.find(std::format("<details data-gallery-group=\"{}\">", entry.group));
                expect(group_at != std::string::npos && group_at < catalogue.find(std::format("href=\"#/{}\"", route))) << "the link lives inside its native category disclosure";
            }
        }
    };
    "each deep link keeps its selected content visible without expanding every other design"_test = [] {
        for (const bool zh : {false, true}) for (const bool creative : {false, true})
            for (const auto& entry : creative ? site::creative_motions() : site::basic_motions()) {
                const auto html = site::motion_gallery_html(zh, creative, entry.id);
                const auto shown = outside_disclosures(html);
                expect(shown.contains(std::format("data-gallery-current=\"{}\"", entry.id)) && shown.contains(zh ? entry.zh : entry.en) && shown.contains(zh ? entry.purpose_zh : entry.purpose_en)) << entry.id;
                expect(occurrences(shown, "<h2>") == 1 && !html.contains("data-gallery-catalog")) << "a detail renders its own content rather than a second complete directory";
                expect(html.contains(std::format("href=\"#/{}{}\"", zh ? "zh/" : "", creative ? "motion" : "docs/motion"))) << "the detail can return to the full native directory";
                expect(closed_disclosures(html) && !html.contains(" id=")) << "the fragment leaves route anchors and live handoff identity to app.cpp";
            }
    };
    "outer static route sections retain the exact selected detail and live handoff contract"_test = [] {
        const auto html = site::static_html("en");
        for (const bool zh : {false, true}) for (const bool creative : {false, true}) {
            const auto language = zh ? "zh" : "en";
            const auto place = creative ? "explore" : "motion";
            const auto overview = site::canonical_route(place, language);
            expect(route_section(html, overview).contains(site::motion_gallery_html(zh, creative))) << "the overview embeds the compact default fragment";
            for (const auto& entry : creative ? site::creative_motions() : site::basic_motions()) {
                const auto route = site::canonical_route(place, language, {}, entry.id);
                const auto section = route_section(html, route);
                expect(!section.empty() && section.contains(site::motion_gallery_html(zh, creative, entry.id))) << route;
                expect(section.contains(std::format("data-live-root=\"main/body/view/{}/page/motion_gallery/stage/scene\"", place)) && section.contains("data-live-name=")) << "deep links retain their selected-scene handoff gate";
                expect(occurrences(html, std::format("<section id=\"/{}\"", route)) == 1) << "no duplicate or competing route anchor is introduced";
            }
        }
    };
    "unknown and cross-catalog IDs report an error without silently replacing the requested sample"_test = [] {
        for (const bool zh : {false, true}) for (const bool creative : {false, true})
            for (const auto id : {"missing-example", "<script>bad()</script>", creative ? "list-grid" : "palette-studio"}) {
                const auto html = site::motion_gallery_html(zh, creative, id);
                expect(html.contains(zh ? "没有找到这个样张" : "This example was not found") && !html.contains("data-gallery-current=")) << "unknown examples do not pretend to be the default sample";
                expect(html.contains(std::format("href=\"#/{}{}\"", zh ? "zh/" : "", creative ? "motion" : "docs/motion")) && !html.contains("<script>")) << "the error stays readable and offers a safe route back to browsing";
            }
    };
}};
