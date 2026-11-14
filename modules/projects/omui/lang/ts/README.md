# omui.lang.ts

TypeScript for openmui with no tool: its types blanked to the JavaScript under them, here and on the Web, and data modules evaluated, deterministic and bounded, to the JSON they stand for (D-138, D-139).

TypeScript is the one upper-level language of omui: code, config and data at once. This package is what reads it. It is a package of its own (D-99) because both the runtime (omui.lang.js) and the build-time generator (omui-gen) read TypeScript, and the generator must not depend on the runtime.

## Types blanked (`:strip`)

`strip_types(source)` gives the JavaScript left when every type is replaced by spaces. Every line break stays, so a line and column in an error are the TypeScript's, and there is no source map to keep. `compile(file)` and `compile_source(text, name)` give that JavaScript or the problems as `name:line:column: text`.

- **The subset** is the erasable one: TypeScript 5.8's `erasableSyntaxOnly` with `verbatimModuleSyntax`, the subset Node runs as it is. That covers annotations, `interface`, `type`, `declare`, `as`, `satisfies`, `!`, generics, `implements`, access modifiers, overloads, `abstract` members, index signatures, `import type` and `type` specifiers.
- **What is not erasable** is a problem, with its place and what to write instead: `enum`, `namespace` with code, parameter properties, `import x =`, `export =`, `<T>value` and decorators. These are forms TypeScript would have to turn into other code.
- **How statements end** is kept. Where blanking would change it, a `;` goes into the blank: after a type that ends a statement, and where a whole statement or member goes. An arrow's return type over several lines has its `)` moved to the type's last column, since JavaScript allows no line break between `)` and `=>`.
- **It checks nothing.** Types are checked by tsc (`omui check`) against the `.d.ts` generated from the contracts.
- **How it works**: one pass of a skimming parser. It knows JavaScript's grammar well enough to tell where a type begins and ends, and builds nothing. Ambiguities are decided as TypeScript decides them: an arrow's head is tried and taken back when it is not one, and `f<T>(x)` is a call with type arguments only when what follows could not continue an expression. Its cost is linear. The tests hold it to a few milliseconds per thousand lines in an unoptimised build.
- **Its tests** check it byte for byte against its cases, and against esbuild. For every case, for `tests/typescript/corpus.ts` and for every `.ts` file in the repository, esbuild must make the same JavaScript of the stripped text as of the TypeScript.

## Data modules (`:data`)

```ts
// caption_button.contract.omui.ts
import { defineContract } from "openmui/define";
import { enumOf } from "./shared";

export default defineContract({
  caption_button: { role: "button", props: { kind: enumOf(["minimize", "close"], "close", "What it does.") } },
});
```

`evaluate(file)` gives `{kind, json}`: the define function's kind and the value as JSON text, keys in the module's order. That JSON is the canonical form the tools read. The define functions come by layer (D-157): `"openmui/define"` has the spec's formats (`defineContract`, `defineNative`, `defineView`), `"omui/define"` the implementation's (`defineConfig`, `defineOps`, `defineTheme`, `defineStyle`, `defineMotion`).

- **The same answer every time.** The module runs in a QuickJS-ng runtime of its own. It has no `Date`, no `Math.random`, no `WeakRef` and no I/O.
- **Imports** are `"openmui/define"`, `"omui/define"` and the module's own files by path, inside its directory (or `DataOptions::root`).
- **It stops.** Memory, stack and time are bounded; a loop that does not end runs out of its time.
- **Data only.** The default export must be a `define…()` result holding objects, arrays, strings, finite numbers, booleans and `null`. A function, `undefined`, `NaN`, a `Map` or a cycle is a problem at its JSON pointer (`/props/kind/default: undefined is not data`).
- **Not on the Web.** A page reads TypeScript but does not evaluate data modules: they belong to the build, and the page gets their JSON.
