// omui.lang.ts:declare — TypeScript declarations of components and native
// modules, written in one place (W1 step 5): a package's stubs (omui-gen, from
// its contracts) and an application's types (the player, from what its
// runtime registered) are the same text for the same component.
//
//     ComponentShape button{.name = "button", .doc = "…", .props = {{"text", "string"}}};
//     auto text = declare_components({&button, 1}, {.header = "…", .standalone = true});
//
// A component is a `<Name>Props` interface (its own properties, then the
// node's: `Omit<NodeProps, …>` drops a node property it redefines) and a
// builder taking `(props?, children?)` or children alone. A native module is
// `declare module "openmui:native/<name>"`. The binding API they refer to
// (`Operand`, `NodeProps`, `Children`, `Handler`, `View`) is the spec's
// stubs/openmui.d.ts.
export module omui.lang.ts:declare;

import std;

export namespace omui::lang::ts {

// A property: its type as a contract spells it (`bool`, `i32`, `f64`,
// `string`, `color`, `enum`, `list<string>`, `any`, or alternatives
// `f64 | min | max`), an enum's values.
struct PropShape {
    std::string              name, type;
    std::vector<std::string> values;
    bool                     bindable = false;
    std::string              doc;
};
struct FieldShape {
    std::string name, type, doc;
};
struct EventShape {
    std::string             name;
    std::vector<FieldShape> args;
};
// A component as a contract and the runtime's catalogue (omuiCatalog) both describe it.
struct ComponentShape {
    std::string             name, doc;
    std::vector<PropShape>  props;
    std::vector<EventShape> events;
    std::string             origin;        // the package it comes from, when known
    bool                    spec = false;  // in the spec's registry: portable (D-157)
};

// A native module (component-binding §8): its records and functions, types as
// its contract spells them (`bool`, `i32`, `u32`, `i64`, `f64`, `string`,
// `any`, a record's name, `T[]`, `T?`).
struct RecordShape {
    std::string             name, doc;
    std::vector<FieldShape> fields;
};
struct FunctionShape {
    std::string             name, doc, returns;
    std::vector<FieldShape> params;
    bool                    async = false;
};
struct NativeShape {
    std::string                name, doc;
    std::vector<RecordShape>   records;
    std::vector<FunctionShape> functions;
};

struct DeclareOptions {
    std::string header;       // the comment the text starts with, a line each (`// ` added)
    bool standalone = true;   // a file of its own, importing the API's types from "openmui"; else inside that module
};

[[nodiscard]] std::string declare_components(std::span<const ComponentShape> components, const DeclareOptions& options);
[[nodiscard]] std::string declare_natives(std::span<const NativeShape> natives, const DeclareOptions& options);

// A contract's snake_case name as TypeScript writes it: `text_box` → `textBox` (a
// builder, a property) or `TextBox` (a type).
[[nodiscard]] std::string camel_case(std::string_view snake);
[[nodiscard]] std::string pascal_case(std::string_view snake);

}  // namespace omui::lang::ts
