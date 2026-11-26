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
[[nodiscard]] std::expected<SiteOptions, std::string> configured(const std::filesystem::path& site);

// The same from a configuration's snapshot (`{"$format":"site@1","site":{…}}`); `file` names it in a problem.
[[nodiscard]] std::expected<SiteOptions, std::string> options_from(std::string_view snapshot, std::string file);

}  // namespace oweb
