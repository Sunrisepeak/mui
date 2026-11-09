// Data modules evaluated by QuickJS-ng (omui.lang.ts:data): a runtime and a
// context of their own for each, with only what gives the same answer every
// time (no Date, no Math.random, no WeakRef, no I/O), bounded in memory,
// stack and time; imports are "openmui/define" and the module's own files
// inside its root.
module omui.lang.ts;

import std;
import mcpplibs.os;
import mcpplibs.mwrap.quickjs_ng;

namespace omui::lang::ts {

using namespace mcpplibs::mwrap::quickjs_ng;          // quickjs.h's API
using namespace mcpplibs::mwrap::quickjs_ng::macros;  // its macros (JS_UNDEFINED, JS_EVAL_TYPE_MODULE …)

namespace {

// Run before the module: Math.random goes, and the check of what it exports.
constexpr std::string_view setup = R"js(
Math.random = () => { throw new Error("Math.random is not here: a data module gives the same data every time"); };
globalThis.__omuiData = (result) => {
  if (result === null || typeof result !== "object" || typeof result.kind !== "string" || !("value" in result))
    throw new TypeError("the default export is not a define…() result: export default defineContract({ … })");
  const seen = new Set();
  const place = (path) => (path === "" ? "/" : path);
  const walk = (value, path) => {
    switch (typeof value) {
      case "string":
      case "boolean":
        return;
      case "number":
        if (!Number.isFinite(value)) throw new TypeError(`${place(path)}: ${value} is not a number data holds`);
        return;
      case "object": {
        if (value === null) return;
        if (seen.has(value)) throw new TypeError(`${place(path)}: the value holds itself`);
        seen.add(value);
        if (Array.isArray(value)) {
          value.forEach((item, index) => walk(item, `${path}/${index}`));
        } else {
          const prototype = Object.getPrototypeOf(value);
          if (prototype !== Object.prototype && prototype !== null)
            throw new TypeError(`${place(path)}: a ${value.constructor?.name ?? "object"} is not plain data`);
          for (const key of Object.keys(value)) walk(value[key], `${path}/${key.replaceAll("~", "~0").replaceAll("/", "~1")}`);
        }
        seen.delete(value);
        return;
      }
      default:
        throw new TypeError(`${place(path)}: ${value === undefined ? "undefined" : "a " + typeof value} is not data`);
    }
  };
  walk(result.value, "");
  return JSON.stringify(result.value);
};
)js";

struct Evaluation {
    std::filesystem::path root;
    std::chrono::steady_clock::time_point deadline;
    bool timed_out = false;
};

int interrupt(JSRuntime*, void* opaque) {
    auto* evaluation = static_cast<Evaluation*>(opaque);
    if (std::chrono::steady_clock::now() < evaluation->deadline) return 0;
    evaluation->timed_out = true;
    return 1;
}

std::string text(JSContext* context, JSValueConst value) {
    std::size_t size = 0;
    const char* characters = JS_ToCStringLen(context, &size, value);
    if (!characters) return {};
    std::string out(characters, size);
    JS_FreeCString(context, characters);
    return out;
}

// What was thrown (freed here), with where; running out of time says so.
std::string thrown_text(JSContext* context, JSValue thrown, const Evaluation& evaluation) {
    std::string out = evaluation.timed_out ? std::string("it ran out of its time (a loop that does not end?)") : text(context, thrown);
    JSValue stack = JS_GetPropertyStr(context, thrown, "stack");
    if (!evaluation.timed_out && JS_IsString(stack))
        if (const auto where = text(context, stack); !where.empty()) out += std::format("\n{}", where);
    JS_FreeValue(context, stack);
    JS_FreeValue(context, thrown);
    return out;
}

std::string exception_text(JSContext* context, const Evaluation& evaluation) { return thrown_text(context, JS_GetException(context), evaluation); }

JSModuleDef* compiled(JSContext* context, const std::string& code, const char* name) {
    JSValue module = JS_Eval(context, code.data(), code.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module)) return nullptr;
    auto* definition = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
    JS_FreeValue(context, module);
    return definition;
}

// "openmui/define", or the module's own file by path inside the root.
JSModuleDef* load(JSContext* context, const char* name, void* opaque) {
    const auto& evaluation = *static_cast<Evaluation*>(opaque);
    const std::string_view module = name;
    if (module == "openmui/define") return compiled(context, define_module_source(), name);
    const auto path = mcpplibs::os::fs::from_utf8(module).lexically_normal();
    const auto inside = path.lexically_relative(evaluation.root);
    if (!path.is_absolute() || inside.empty() || *inside.begin() == "..") {
        JS_ThrowReferenceError(context, "a data module imports \"openmui/define\" and its own files inside its directory, not `%s`", name);
        return nullptr;
    }
    for (const std::string_view suffix : {"", ".ts", ".mts", ".js", ".mjs", "/index.ts", "/index.js"}) {
        auto candidate = path;
        candidate += mcpplibs::os::fs::from_utf8(suffix);
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error)) continue;
        auto code = compile(candidate);
        if (!code) {
            JS_ThrowSyntaxError(context, "%s", code.error().c_str());
            return nullptr;
        }
        return compiled(context, *code, name);
    }
    JS_ThrowReferenceError(context, "no module `%s`", name);
    return nullptr;
}

std::expected<Data, std::string> run(JSRuntime* runtime, JSContext* context, const std::string& code, const std::string& name,
                                     Evaluation& evaluation) {
    JSValue prepared = JS_Eval(context, setup.data(), setup.size(), "<setup>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(prepared)) return std::unexpected(exception_text(context, evaluation));
    JS_FreeValue(context, prepared);
    JSValue module = JS_Eval(context, code.data(), code.size(), name.c_str(), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module)) return std::unexpected(exception_text(context, evaluation));
    auto* definition = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
    JSValue evaluated = JS_EvalFunction(context, module);
    if (JS_IsException(evaluated)) return std::unexpected(exception_text(context, evaluation));
    JSContext* job_context = nullptr;
    for (int ran = 1; ran > 0;) {
        ran = JS_ExecutePendingJob(runtime, &job_context);
        if (ran < 0) {
            JS_FreeValue(context, evaluated);
            return std::unexpected(exception_text(job_context, evaluation));
        }
    }
    if (JS_IsPromise(evaluated)) {
        const auto state = JS_PromiseState(context, evaluated);
        if (state == JS_PROMISE_REJECTED) {
            std::string out = thrown_text(context, JS_PromiseResult(context, evaluated), evaluation);
            JS_FreeValue(context, evaluated);
            return std::unexpected(out);
        }
        if (state == JS_PROMISE_PENDING) {
            JS_FreeValue(context, evaluated);
            return std::unexpected(std::string("the module did not finish (it waits on something that never comes)"));
        }
    }
    JS_FreeValue(context, evaluated);
    JSValue exports = JS_GetModuleNamespace(context, definition);
    JSValue exported = JS_GetPropertyStr(context, exports, "default");
    JS_FreeValue(context, exports);
    JSValue global = JS_GetGlobalObject(context);
    JSValue check = JS_GetPropertyStr(context, global, "__omuiData");
    JS_FreeValue(context, global);
    JSValue json = JS_Call(context, check, JS_UNDEFINED, 1, &exported);
    JS_FreeValue(context, check);
    if (JS_IsException(json)) {
        JS_FreeValue(context, exported);
        return std::unexpected(exception_text(context, evaluation));
    }
    JSValue kind = JS_GetPropertyStr(context, exported, "kind");
    Data data{text(context, kind), text(context, json)};
    JS_FreeValue(context, kind);
    JS_FreeValue(context, json);
    JS_FreeValue(context, exported);
    return data;
}

}  // namespace

std::expected<Data, std::string> evaluate(const std::filesystem::path& file, const DataOptions& options) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot be read", mcpplibs::os::fs::utf8(file)));
    const std::string source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::error_code error;
    const auto absolute = std::filesystem::absolute(file, error);
    return evaluate_source(source, mcpplibs::os::fs::utf8(error ? file : absolute), options);
}

std::expected<Data, std::string> evaluate_source(std::string_view source, std::string_view name, const DataOptions& options) {
    auto code = compile_source(source, name);
    if (!code) return std::unexpected(code.error());
    const auto path = mcpplibs::os::fs::from_utf8(name).lexically_normal();
    Evaluation evaluation{options.root.empty() ? path.parent_path() : options.root.lexically_normal(),
                          std::chrono::steady_clock::now() + options.time};
    JSRuntime* runtime = JS_NewRuntime();
    JS_SetMemoryLimit(runtime, options.memory_bytes);
    JS_SetMaxStackSize(runtime, 1024 * 1024);
    JS_SetInterruptHandler(runtime, interrupt, &evaluation);
    JS_SetModuleLoaderFunc(runtime, nullptr, load, &evaluation);
    JSContext* context = JS_NewContextRaw(runtime);
    JS_AddIntrinsicBaseObjects(context);
    JS_AddIntrinsicEval(context);
    JS_AddIntrinsicRegExpCompiler(context);
    JS_AddIntrinsicRegExp(context);
    JS_AddIntrinsicJSON(context);
    JS_AddIntrinsicProxy(context);
    JS_AddIntrinsicMapSet(context);
    JS_AddIntrinsicTypedArrays(context);
    JS_AddIntrinsicPromise(context);
    JS_AddIntrinsicBigInt(context);
    auto result = run(runtime, context, *code, mcpplibs::os::fs::utf8(path), evaluation);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    if (!result) return std::unexpected(std::format("{}: {}", name, result.error()));
    return result;
}

}  // namespace omui::lang::ts
