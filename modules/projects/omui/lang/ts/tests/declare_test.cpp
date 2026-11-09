// TypeScript declarations (omui.lang.ts:declare): a component as a
// `<Name>Props` interface and a builder, a native module as
// `declare module "openmui:native/<name>"` — the text omui-gen's stubs and an
// application's types (omui check) are both made of.
import std;
import mcpplibs.mtest;
import omui.lang.ts;

using namespace mcpplibs::mtest;
namespace ts = omui::lang::ts;

UTest cases{"lang.ts declare", [] {
    "a component: its properties as operands, a node property it redefines omitted, its events as handlers, two builders"_test = [] {
        const ts::ComponentShape box{
            .name = "text_box",
            .doc = "A line of text a person edits.",
            .props = {{.name = "text", .type = "string", .bindable = true, .doc = "What is written."},
                      {.name = "max_length", .type = "i32"},
                      {.name = "align", .type = "enum", .values = {"start", "end"}},
                      {.name = "width_mode", .type = "f64 | fill | fit"}},
            .events = {{.name = "submitted", .args = {{.name = "text", .type = "string"}}}, {.name = "cleared"}},
            .origin = "acme.ui",
        };
        const std::string text = ts::declare_components({&box, 1}, {.header = "Made by a test."});
        const auto has = [&](std::string_view part) { expect(text.contains(part)) << std::format("{}\n---\n{}", part, text); };
        has("// Made by a test.\n");
        has(R"(import type { View, NodeProps, Operand, Children, Handler } from "openmui";)");
        has(R"(export interface TextBoxProps extends Omit<NodeProps, "text" | "maxLength" | "align" | "widthMode"> {)");
        has("    /** What is written. Two-way: a signal given here is written back. */\n    text?: Operand<string>;\n");
        has("    maxLength?: Operand<number>;\n");
        has(R"(    align?: Operand<"start" | "end">;)");
        has(R"(    widthMode?: Operand<number | "fill" | "fit">;)");
        has("    onSubmitted?: Handler<{ text: string; }>;\n    onCleared?: Handler;\n");
        has("From acme.ui: not in the spec's registry (D-157).");
        // Children alone first: tsc reports a call matching neither against the properties.
        has("export declare function textBox(children: Children): View;\nexport declare function textBox(props?: TextBoxProps, children?: Children): View;\n");
    };

    "inside the module, no import; a spec component says it is portable; a doc cannot end its comment"_test = [] {
        const ts::ComponentShape button{.name = "button", .doc = "Acts */ when pressed.", .spec = true};
        const std::string text = ts::declare_components({&button, 1}, {.standalone = false});
        expect(!text.contains("import")) << text;
        expect(text.contains("In the spec's registry: portable to any openmui runtime.")) << text;
        expect(text.contains("Acts *\\/ when pressed.")) << text;
        expect(text.contains("export interface ButtonProps extends NodeProps {")) << "no property of its own: nothing omitted";
    };

    "a native module: records, a function, an async one and its Sync form, optional and list types"_test = [] {
        const ts::NativeShape fuzzy{
            .name = "fuzzy",
            .doc = "Fuzzy matching.",
            .records = {{.name = "Match", .doc = "A match.", .fields = {{.name = "index", .type = "u32"}, {.name = "char_positions", .type = "u32[]"}}}},
            .functions = {{.name = "rank", .doc = "Best first.", .returns = "Match[]", .params = {{.name = "query", .type = "string"}, {.name = "limit", .type = "u32?", .doc = "at most"}}, .async = true},
                          {.name = "best", .doc = "One.", .returns = "Match?", .params = {{.name = "query", .type = "string"}}}},
        };
        const std::string text = ts::declare_natives({&fuzzy, 1}, {});
        const auto has = [&](std::string_view part) { expect(text.contains(part)) << std::format("{}\n---\n{}", part, text); };
        has(R"(declare module "openmui:native/fuzzy" {)");
        has("        charPositions: number[];\n");
        has("     * @param limit at most\n");
        has("    export function rank(query: string, limit?: number | null): Promise<Match[]>;\n");
        has("    export function rankSync(query: string, limit?: number | null): Match[];\n");
        has("    export function best(query: string): Match | null;\n");
        has("    export const fuzzy: { rank: typeof rank; rankSync: typeof rankSync; best: typeof best };\n    export default fuzzy;\n");
    };

    "names as TypeScript writes them"_test = [] {
        expect(eq(ts::camel_case("max_width"), std::string("maxWidth")));
        expect(eq(ts::pascal_case("text_box"), std::string("TextBox")));
        expect(eq(ts::camel_case("button"), std::string("button")));
    };

    "omui/define's declarations name each of its define functions"_test = [] {
        const std::string_view text = ts::omui_define_types();
        for (const std::string_view name : {"defineConfig", "defineOps", "defineTheme", "defineStyle", "defineMotion"})
            expect(text.contains(std::format("export declare function {}(", name))) << name;
    };
}};
