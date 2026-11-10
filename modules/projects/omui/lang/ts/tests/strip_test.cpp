// TypeScript to JavaScript by blanking the types (strip_types, D-139): what
// is left for each form, byte for byte (the cases say which parts go, in
// tools' words: see tests/typescript); the problems of what is not erasable,
// with their places; and esbuild as the oracle — for every case, the corpus
// and every TypeScript file of the repository, esbuild makes the same
// JavaScript of the stripped text as of the TypeScript.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.lang.ts;

using namespace mcpplibs::mtest;
namespace ts = omui::lang::ts;

namespace {

struct Case {
    std::string_view name, source, stripped;
};

constexpr std::array<Case, 33> forms{{
    {"annotations",
     R"ts(let x: number = 1;)ts",
     R"ts(let x         = 1;)ts"},
    {"a function's type parameters, optional parameter, return type",
     R"ts(function f<T>(a: T, b?: string): T { return a; })ts",
     R"ts(function f   (a   , b         )    { return a; })ts"},
    {"a generic async arrow",
     R"ts(const f = async <T,>(x: T): Promise<T> => x;)ts",
     R"ts(const f = async     (x   )             => x;)ts"},
    {"as, !, satisfies",
     R"ts(const y = (a as any)!.b satisfies B;)ts",
     R"ts(const y = (a       ) .b            ;)ts"},
    {"type arguments of a call; a < b > c compares",
     R"ts(f<string>(x); a < b > c;)ts",
     R"ts(f        (x); a < b > c;)ts"},
    {"interface and type alias: gone, a `;` where each began",
     R"ts(let a = 1
interface A { x: number }
type B = A | null;
[a].map(String);)ts",
     R"ts(let a = 1
                         
;                 
[a].map(String);)ts"},
    {"a class: modifiers, optional and declared fields, overloads, index signatures",
     R"ts(class C<T> extends B<T> implements I, J {
  private x?: number;
  declare y: string;
  static z = 1;
  m(): void;
  m(a?: number): void {}
  [key: string]: any;
})ts",
     R"ts(class C    extends B                    {
          x         ;
                    
  static z = 1;
            
  m(a         )       {}
                     
})ts"},
    {"import type and export type: gone; a type specifier with its comma",
     R"ts(import type { A } from "a";
import { type B, c } from "b";
export type { A };
export { c, type B };)ts",
     R"ts(                           
import {         c } from "b";
                  
export { c,        };)ts"},
    {"a statement ending in a type: a `;` in the blank",
     R"ts(let a = b as T
(c))ts",
     R"ts(let a = b    ;
(c))ts"},
    {"an arrow's return type over lines: its `)` moved to the type's end",
     R"ts(const f = (a): {
  x: number
} => a;)ts",
     R"ts(const f = (a    
           
) => a;)ts"},
    {"templates and regular expressions",
     R"ts(const t = `${(x as number) / 2}`; const r = /<T>(a: b)/g;)ts",
     R"ts(const t = `${(x          ) / 2}`; const r = /<T>(a: b)/g;)ts"},
    {"a this parameter: gone with its comma",
     R"ts(function g(this: Window, a: number) {})ts",
     R"ts(function g(              a        ) {})ts"},
    {"an overload signature: gone",
     R"ts(function h(a: string): void;
function h(a: any) {})ts",
     R"ts(                            
function h(a     ) {})ts"},
    {"a removed statement between two that would join: a `;` keeps them apart",
     R"ts(let a = b
type T = number
(c || d).run())ts",
     R"ts(let a = b
;              
(c || d).run())ts"},
    {"a removed member between two that would join",
     R"ts(class G {
  x = 1
  declare y: number
  *gen() {}
})ts",
     R"ts(class G {
  x = 1
  ;                
  *gen() {}
})ts"},
    {"declare: gone",
     R"ts(declare const VERSION: string;
declare module "m" { export const x: number; }
let v = 1;)ts",
     R"ts(                              
                                              
let v = 1;)ts"},
    {"conditional types with infer",
     R"ts(type U<T> = T extends [infer H extends string, ...infer R] ? H : never;)ts",
     R"ts(                                                                       )ts"},
    {"an object literal's methods",
     R"ts(const o = { m<T>(a: T): T { return a; }, get g(): number { return 1; } };)ts",
     R"ts(const o = { m   (a   )    { return a; }, get g()         { return 1; } };)ts"},
    {"optional chaining and !",
     R"ts(a?.b!.c;)ts",
     R"ts(a?.b .c;)ts"},
    {"a function type",
     R"ts(let cb: (a: number) => void = () => {};)ts",
     R"ts(let cb                      = () => {};)ts"},
    {"a type in parentheses as a return type",
     R"ts(const k = (a: number): (A | B) => a;)ts",
     R"ts(const k = (a        )          => a;)ts"},
    {"an abstract class",
     R"ts(abstract class Q { abstract m(): void; protected abstract p: number; n() {} })ts",
     R"ts(         class Q {                                                   n() {} })ts"},
    {"a ? (b) : c => d is a conditional",
     R"ts(const w = a ? (b) : c => d;)ts",
     R"ts(const w = a ? (b) : c => d;)ts"},
    {"an arrow with a return type in a conditional's middle",
     R"ts(const w = a ? (b): string => b : c;)ts",
     R"ts(const w = a ? (b)         => b : c;)ts"},
    {"type predicates and assertions",
     R"ts(function isS(x: unknown): x is string { return true; }
function must(x: unknown): asserts x {})ts",
     R"ts(function isS(x         )              { return true; }
function must(x         )            {})ts"},
    {"keyof typeof",
     R"ts(let k: keyof typeof obj = "a";)ts",
     R"ts(let k                   = "a";)ts"},
    {"a class field ending in a type before a computed one",
     R"ts(class F {
  x: number
  [y] = 1
})ts",
     R"ts(class F {
  x       ;
  [y] = 1
})ts"},
    {"generic arrows as arguments",
     R"ts(use<T>((x: T) => x, async (y): Promise<void> => {});)ts",
     R"ts(use   ((x   ) => x, async (y)                => {});)ts"},
    {"definite assignment",
     R"ts(let d!: number;)ts",
     R"ts(let d         ;)ts"},
    {"JavaScript stays as it is",
     R"ts(label: for (const [k, v] of Object.entries(o)) { if (k < v || v > 2) continue label; x = y ? /re/g : z / 2; })ts",
     R"ts(label: for (const [k, v] of Object.entries(o)) { if (k < v || v > 2) continue label; x = y ? /re/g : z / 2; })ts"},
    {"a catch binding's type",
     R"ts(try { f(); } catch (e: unknown) { g(e); })ts",
     R"ts(try { f(); } catch (e         ) { g(e); })ts"},
    {"type arguments on new and in extends",
     R"ts(class D extends Map<string, number> {}
const m = new Map<string, number>();)ts",
     R"ts(class D extends Map                 {}
const m = new Map                ();)ts"},
    {"tagged template with type arguments",
     R"ts(const s = tag<T>`a${b}c`;)ts",
     R"ts(const s = tag   `a${b}c`;)ts"},
}};

struct Refused {
    std::string_view source;
    std::size_t line, column;
    std::string_view starts;
};

constexpr std::array refused{
    Refused{"enum E { A }", 1, 1, "`enum` is not erasable"},
    Refused{"let a = 1;\nconst enum E { A }", 2, 1, "`enum` is not erasable"},
    Refused{"namespace N { export const x = 1; }", 1, 1, "`namespace` is not erasable"},
    Refused{"class P { constructor(private x: number) {} }", 1, 23, "a parameter property is not erasable"},
    Refused{"import x = require(\"y\");", 1, 1, "`import x = …` is not erasable"},
    Refused{"export = x;", 1, 1, "`export =` is not erasable"},
    Refused{"let v = <T>w;", 1, 9, "`<T>value` is not erasable"},
    Refused{"@sealed class S {}", 1, 1, "a decorator does not run here"},
    Refused{"const x: number = ;", 1, 19, "an expression expected"},
    Refused{"let s = \"open;", 1, 15, "a string that does not end"},
};

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// esbuild's JavaScript of `source` read with `loader`, or why there is none.
std::expected<std::string, std::string> esbuild(const std::filesystem::path& program, std::string_view source, std::string_view loader) {
    mcpplibs::os::process::spawn_options options;
    options.program = program;
    options.args = {std::format("--loader={}", loader), "--log-level=error", "--charset=utf8",
                    R"(--tsconfig-raw={"compilerOptions":{"verbatimModuleSyntax":true,"useDefineForClassFields":true}})"};
    options.in = mcpplibs::os::process::stdio::pipe;
    auto ran = mcpplibs::os::process::run(std::move(options), source);
    if (!ran) return std::unexpected(ran.error().message());
    if (!ran->status.success()) return std::unexpected(ran->err);
    return std::move(ran->out);
}

// The repository's TypeScript programs (not declarations), outside build trees.
std::vector<std::filesystem::path> repository_typescript() {
    auto root = here();
    while (!root.empty() && !std::filesystem::exists(root / "AGENTS.md") && root != root.parent_path()) root = root.parent_path();
    std::vector<std::filesystem::path> files;
    for (const auto top : {"examples", "showcase", "modules"}) {
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(root / top, error); !error && it != std::filesystem::recursive_directory_iterator();
             it.increment(error)) {
            const auto name = it->path().filename().string();
            if (it->is_directory() && (name == "target" || name == "node_modules" || name.starts_with("."))) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".ts" && !name.ends_with(".d.ts")) files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

}  // namespace

UTest cases{"lang.ts strip", [] {
    "each form blanked as the cases say, every line and column kept"_test = [] {
        for (const auto& [name, source, stripped] : forms) {
            auto out = ts::strip_types(source);
            expect(out.has_value()) << std::format("{}: {}", name, out ? "" : ts::to_text(out.error()));
            if (!out) continue;
            expect(eq(*out, std::string(stripped))) << name;
            expect(eq(out->size(), source.size())) << name;
        }
    };

    "JavaScript is left as it is"_test = [] {
        const std::string_view program = "import { a } from \"m\";\nexport default async function* g(x = 1, ...rest) {\n  for await (const y of rest) yield y < x ? `${y}` : /x/u;\n}\n";
        auto out = ts::strip_types(program);
        expect(out.has_value() && *out == program) << (out ? *out : ts::to_text(out.error()));
    };

    "what is not erasable: each problem with its place and what to write"_test = [] {
        for (const auto& [source, line, column, starts] : refused) {
            auto out = ts::strip_types(source);
            expect(!out.has_value()) << source;
            if (out) continue;
            const auto& first = out.error().front();
            expect(eq(first.line, line) && eq(first.column, column) && first.text.starts_with(starts)) << std::format("{}: {}", source, ts::to_text(out.error()));
        }
        auto several = ts::strip_types("enum A { X }\nenum B { Y }\n");
        expect(!several && several.error().size() == 2) << "every problem, not the first only";
    };

    "esbuild makes the same JavaScript of the stripped text as of the TypeScript (cases, corpus, the repository's files)"_test = [] {
        const auto chosen = mcpplibs::os::env::get("OMUI_ESBUILD");
        const auto program = chosen && !chosen->empty() ? std::optional(mcpplibs::os::fs::from_utf8(*chosen)) : mcpplibs::os::env::find_executable("esbuild");
        if (!program) {
            // The workspace declares it ([xlings.workspace], when = "run"): on CI its absence is a failure, not a skip.
            if (mcpplibs::os::env::get("CI")) {
                // What the run's PATH holds of esbuild, to see where a provisioned one went (red list 39).
                const char separator = std::filesystem::path::preferred_separator == '\\' ? ';' : ':';
                std::string seen;
                for (const auto part : std::views::split(mcpplibs::os::env::get("PATH").value_or(""), separator)) {
                    const std::filesystem::path dir = mcpplibs::os::fs::from_utf8(std::string_view(part.begin(), part.end()));
                    std::error_code error;
                    for (const auto& entry : std::filesystem::directory_iterator(dir, error))
                        if (mcpplibs::os::fs::utf8(entry.path().filename()).starts_with("esbuild")) seen += std::format(" {}", mcpplibs::os::fs::generic_utf8(entry.path()));
                    if (mcpplibs::os::fs::generic_utf8(dir).contains("xlings")) seen += std::format(" [dir {}]", mcpplibs::os::fs::generic_utf8(dir));
                }
                // And whether a payload was installed at all, beneath the xlings and mcpp homes those directories
                // are in: no payload is a provisioning that never ran, a payload without a shim a shim not written.
                std::set<std::filesystem::path> homes;
                for (const auto part : std::views::split(mcpplibs::os::env::get("PATH").value_or(""), separator)) {
                    std::filesystem::path dir = mcpplibs::os::fs::from_utf8(std::string_view(part.begin(), part.end()));
                    for (; !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
                        if (dir.filename() == ".xlings" || mcpplibs::os::fs::utf8(dir.filename()).starts_with("xim-x-mcpp")) {
                            homes.insert(dir);
                            break;
                        }
                }
                std::string payloads;
                for (const auto& home : homes) {
                    std::error_code error;
                    int found = 0;
                    for (auto it = std::filesystem::recursive_directory_iterator(home, std::filesystem::directory_options::skip_permission_denied, error);
                         it != std::filesystem::recursive_directory_iterator() && found < 12; it.increment(error)) {
                        if (error) break;
                        if (it.depth() > 6) it.disable_recursion_pending();
                        if (mcpplibs::os::fs::utf8(it->path().filename()).contains("esbuild")) {
                            payloads += std::format(" {}", mcpplibs::os::fs::generic_utf8(it->path()));
                            ++found;
                        }
                    }
                }
                fail(std::format("no esbuild, though the workspace declares it for test runs ([xlings.workspace]); on PATH:{}; beneath {} home(s):{}",
                                 seen.empty() ? " nothing like it" : seen, homes.size(), payloads.empty() ? " nothing like it" : payloads));
            }
            skip("no esbuild (the workspace declares it: mcpp test installs it; or OMUI_ESBUILD)");
        }
        std::vector<std::pair<std::string, std::string>> sources;
        for (const auto& [name, source, stripped] : forms) sources.emplace_back(name, source);
        sources.emplace_back("corpus.ts", file_text(here() / "typescript" / "corpus.ts"));
        const auto files = repository_typescript();
        expect(files.size() >= 9) << std::format("{} files", files.size());
        for (const auto& file : files) sources.emplace_back(mcpplibs::os::fs::utf8(file), file_text(file));
        for (const auto& [name, source] : sources) {
            auto stripped = ts::strip_types(source);
            expect(stripped.has_value()) << std::format("{}: {}", name, stripped ? "" : ts::to_text(stripped.error()));
            if (!stripped) continue;
            auto from_typescript = esbuild(*program, source, "ts");
            auto from_stripped = esbuild(*program, *stripped, "js");
            expect(from_typescript.has_value()) << std::format("{}: {}", name, from_typescript ? "" : from_typescript.error());
            expect(from_stripped.has_value()) << std::format("{}: {}\n{}", name, from_stripped ? "" : from_stripped.error(), *stripped);
            if (from_typescript && from_stripped) expect(eq(*from_stripped, *from_typescript)) << name;
        }
    };

    "linear: four times the text takes about four times as long (no backtracking that grows), and little of it"_test = [] {
        const auto corpus = file_text(here() / "typescript" / "corpus.ts");
        const auto timed = [&](int copies) {
            std::string text;
            for (int copy = 0; copy < copies; ++copy) text += corpus;  // names repeat: no matter here
            auto best = std::numeric_limits<double>::max();
            for (int round = 0; round < 3; ++round) {
                const auto start = std::chrono::steady_clock::now();
                auto out = ts::strip_types(text);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                expect(out.has_value()) << (out ? "" : ts::to_text(out.error()));
            }
            return std::pair{best, std::ranges::count(text, '\n')};
        };
        const auto [small, small_lines] = timed(10);
        const auto [large, large_lines] = timed(40);
        std::println("strip_types: {} lines in {:.2f} ms, {} lines in {:.2f} ms (unoptimised)", small_lines, small, large_lines, large);
        expect(large < 6.0 * small + 1.0) << std::format("{:.2f} ms then {:.2f} ms", small, large);
        expect(large < 20.0 * static_cast<double>(large_lines) / 1000.0 + 10.0) << std::format("{:.2f} ms for {} lines", large, large_lines);
    };
}};
