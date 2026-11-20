// What a module imports, read without running it (mlibs::ts::imports, B7b): the names taken from each module as
// it exports them, type-only names left out; `import *`, `export *` and `import()` take everything of a module; the
// lines they begin on; a source that does not scan has its problems.
import std;
import mcpplibs.mtest;
import mlibs.ts;

using namespace mcpplibs::mtest;
namespace ts = mlibs::ts;

namespace {

std::string said(const std::vector<ts::Import>& read) {
    std::string out;
    for (const auto& one : read) {
        out += std::format("{}:{}{}:", one.line, one.from, one.everything ? "*" : "");
        for (const auto& name : one.names) out += " " + name;
        out += "\n";
    }
    return out;
}

}  // namespace

static UTest cases{"lang.ts imports", [] {
    "named, renamed, default, type-only, side effects and re-exports: what each takes"_test = [] {
        const auto read = ts::imports(R"ts(import { signal, button as press, type Signal, "text" as words } from "openmui";
import type { Theme } from "openmui";
import page, { row } from "./page";
import "./styles";
export { column } from "openmui";
export type { Box } from "openmui";
const x: Signal<number> = signal(1);
)ts");
        expect(read.has_value()) << (read ? "" : ts::to_text(read.error()));
        if (read)
            expect(eq(said(*read), std::string("1:openmui: signal button text\n3:./page: default row\n4:./styles:\n5:openmui: column\n"))) << said(*read);
    };

    "import *, export * and import(): everything of that module; a computed import() of a module not named"_test = [] {
        const auto read = ts::imports(R"ts(import * as ui from "openmui";
export * from "openmui:native/fuzzy";
async function later(name: string) {
  const store = await import("openmui:native/store");
  return import(name);
}
const f = (x: number): number => x;
)ts");
        expect(read.has_value()) << (read ? "" : ts::to_text(read.error()));
        if (read)
            expect(eq(said(*read), std::string("1:openmui*:\n2:openmui:native/fuzzy*:\n4:openmui:native/store*:\n5:*:\n"))) << said(*read);
    };

    "a source that does not scan: its problems, as strip_types says them"_test = [] {
        const auto read = ts::imports("import { a from \"x\";\nenum E { A }\n");
        expect(!read.has_value());
    };

    "the site's page: every name it takes from openmui"_test = [] {
        const auto read = ts::imports(R"ts(import {
  signal, eq, or, select, when, stack, column, row, box, text, button, segmented, link, path, timeline, mount, verb, invoke, copy,
  fmt, linkAgent, agentLink,
} from "openmui";
type State = "rest" | "design";
)ts");
        expect(read.has_value() && read->size() == 1);
        if (read && read->size() == 1) {
            expect(eq(read->front().from, std::string("openmui")) && eq(read->front().names.size(), 22uz) && !read->front().everything);
            expect(std::ranges::contains(read->front().names, std::string("agentLink")) && std::ranges::contains(read->front().names, std::string("timeline")));
        }
    };
}};
