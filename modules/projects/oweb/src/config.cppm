// oweb:config — a site's configuration: `oweb.config.ts` in its directory (design §3.2, D-285).
//
//     import { defineSite } from "oweb/define";
//     export default defineSite({
//       title: "Field notes",
//       url: "https://example.org/notes/",
//       languages: ["en", "zh"],
//       routes: [                                   // stated: these pages, in this order — the navigation's
//         { path: "/", source: "pages/index.md" },
//         { path: "/start/", source: "pages/guide/start.md", title: "Start here" },
//       ],
//       budgets: { readable: 100000 },
//     });
//
// It is declared data (kind `site@1`, this package its owner): evaluated once, deterministically, to a snapshot,
// and the snapshot is what is read. A field that is none of the kind's, or of the wrong shape, is a problem at its
// place, naming what the kind has. A site without the file is the defaults: every page of pages/, by address.
export module oweb:config;

import std;
import :site;

export namespace oweb {

// What a configuration imports its define function from, and that module's JavaScript (`defineSite`).
inline constexpr std::string_view define_name = "oweb/define";
[[nodiscard]] std::string define_module();

// A site's options as its oweb.config.ts states them, over the defaults; the defaults where it has none.
// `stated`: a configuration that stands elsewhere than in the site's directory — the same pages as another site (a
// product's documents behind its own first page, one corpus built two ways). Its pages are the site's (`source`,
// from the site's directory); what it states beside them — an engine, a route's built page, a catalog — is from
// its own directory. One that is not there is an error, not the defaults.
[[nodiscard]] std::expected<SiteOptions, std::string> configured(const std::filesystem::path& site, const std::filesystem::path& stated = {});

// The same from a configuration's snapshot (`{"$format":"site@1","site":{…}}`); `file` names it in a problem.
[[nodiscard]] std::expected<SiteOptions, std::string> options_from(std::string_view snapshot, std::string file);

}  // namespace oweb
