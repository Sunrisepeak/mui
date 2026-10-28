// openmui.native.fuzzy — openmui.fuzzy for scripts (component-binding §8).
//
//     (void)omui::natives::fuzzy::define();   // a host, once, before scripts run
//
//     import { fuzzy } from "openmui:native/fuzzy";      // TypeScript
//     const best = await fuzzy.rank(query, files, 20);
//
// The interface is the contract's (native/fuzzy.native.omui.toml), generated
// into :fuzzy_native; this package only implements it.
export module openmui.native.fuzzy;

export import :fuzzy_native;
