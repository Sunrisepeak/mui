// omui.lang.js on the Web (D-237, G1): a script's JavaScript runs on the page's own engine — no second engine in the
// wasm. This is the page's half of the binding; src/native/web/runtime.cpp is the program's.
//
//   program → page   omui_script_begin(id, prelude, dispatch, settled)   a script: its `openmui` module and table
//                    omui_script_run(id, run, name, code)                  a module evaluated (import(), asynchronous);
//                                                                         settled(id, run, ok, text) when it is done
//                    omui_script_call(id, function, argument, parse)       a kept function called: its result as text
//                    omui_script_release(id, function), omui_script_end(id)
//                    omui_script_pause()                                     a moment for the page (a waiting program's)
//   page → program   dispatch(id, op, arguments)                           the `__omui` table's natives (Natives, natives.cppm):
//                                                                         text in, an envelope out — {"v": value} or {"e": why}
//
// The script's functions (handlers, watches, verbs, a promise's two ends) are the page's to keep: the program names
// them by number. Nothing here blocks: a module's evaluation settles later, and the player collects it at a frame
// boundary.
addToLibrary({
  $omuiScripts__deps: ['$UTF8ToString', '$stringToNewUTF8', '$getWasmTableEntry', 'free'],
  $omuiScripts: {
    scripts: {},
    // A module's text as a URL the engine imports (data:, which a page and node both take; the same text is the same
    // module, so every module a script imports shares its `openmui`).
    // encodeURIComponent leaves ' ( ) unescaped: a module imported with single quotes would end at the first.
    url: function (text) {
      return 'data:text/javascript;charset=utf-8,' + encodeURIComponent(text).replace(/['()]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); });
    },
    // The program's answer to one of the table's natives: its value, or thrown.
    ask: function (s, op, args) {
      var opText = stringToNewUTF8(op), argsText = stringToNewUTF8(JSON.stringify(args));
      var answered = getWasmTableEntry(s.dispatch)(s.id, opText, argsText);
      _free(opText);
      _free(argsText);
      var envelope = JSON.parse(UTF8ToString(answered));
      _free(answered);
      if (envelope.e !== undefined) throw new Error(envelope.e);
      return envelope.v;
    },
    keep: function (s, f) { var id = s.next++; s.functions.set(id, f); return id; },
    // A module's name in the page's stack traces, as QuickJS names it (a data: URL has none of its own): where an
    // element was built (the prelude's builtAt) and where an error came from read the same on both engines.
    named: function (name) { return '\n//# sourceURL=' + name + '\n'; },
    // A script's `__omui`: the natives the prelude calls (`N.…`), each one a dispatch.
    table: function (s) {
      var ask = function (op) { return function () { return omuiScripts.ask(s, op, Array.prototype.slice.call(arguments)); }; };
      var keep = function (f) { return omuiScripts.keep(s, f); };
      var t = {};
      ['signalCreate', 'signalGet', 'signalSet', 'catalog', 'unmount', 'document', 'invoke', 'act', 'snapshot', 'find', 'get',
       'set', 'rows', 'splice', 'nativeCall', 'theme', 'themes', 'keepsBuiltAt', 'log'].forEach(function (op) { t[op] = ask(op); });
      t.signalWatch = function (signal, f) { return omuiScripts.ask(s, 'signalWatch', [signal, keep(f)]); };
      t.mount = function (slot, tree, handlers) { return omuiScripts.ask(s, 'mount', [slot, tree, handlers.map(keep)]); };
      t.verb = function (name, options, f) { return omuiScripts.ask(s, 'verb', [name, options, keep(f)]); };
      // An async native: a promise whose two ends the program settles by number.
      t.nativeAsync = function (module, fn, args) {
        return new Promise(function (resolve, reject) {
          omuiScripts.ask(s, 'nativeAsync', [module, fn, args, keep(resolve), keep(reject)]);
        });
      };
      return t;
    },
    // A module's imports rewritten to URLs: "openmui" to the script's own module, "openmui:native/<name>" and a
    // module by path to theirs (the program gives their text: a path's with its types blanked).
    linked: function (s, code, name) {
      return code.replace(/(\bfrom\s*|\bimport\s*\(?\s*)(["'])([^"']+)\2/g, function (whole, head, quote, specifier) {
        if (specifier === 'openmui') return head + quote + s.preludeUrl + quote;
        if (specifier.indexOf('openmui:native/') === 0 || specifier.charAt(0) === '.' || specifier.charAt(0) === '/') {
          var key = specifier.charAt(0) === '.' ? name + '\u0000' + specifier : specifier;
          if (!s.modules.has(key)) {
            var found = omuiScripts.ask(s, 'module', [specifier, name]);  // {name, code}
            s.modules.set(key, omuiScripts.url(omuiScripts.linked(s, found.code, found.name) + omuiScripts.named(found.name)));
          }
          return head + quote + s.modules.get(key) + quote;
        }
        // A bare name is no module here (as the program says it, whichever engine runs the script).
        throw new Error('no module `' + specifier + '` (scripts import "openmui", "openmui:native/<name>" and their own files by path)');
      });
    },
  },

  omui_script_begin__deps: ['$omuiScripts', '$UTF8ToString'],
  omui_script_begin: function (id, prelude, dispatch, settled) {
    var s = { id: id, dispatch: dispatch, settled: settled, functions: new Map(), next: 1, modules: new Map() };
    globalThis.__omuiScripts = globalThis.__omuiScripts || {};
    globalThis.__omuiScripts[id] = omuiScripts.table(s);
    // The prelude reads its table from the realm; on the page every script has its own.
    var text = UTF8ToString(prelude).replace('const N = globalThis.__omui;', 'const N = globalThis.__omuiScripts[' + id + '];');
    s.preludeUrl = omuiScripts.url(text + omuiScripts.named('openmui'));
    omuiScripts.scripts[id] = s;
  },

  omui_script_run__deps: ['$omuiScripts', '$UTF8ToString', '$stringToNewUTF8', '$getWasmTableEntry'],
  omui_script_run: function (id, run, name, code) {
    var s = omuiScripts.scripts[id];
    var moduleName = UTF8ToString(name);
    var settle = function (ok, text) {
      if (!omuiScripts.scripts[id]) return;  // ended meanwhile
      getWasmTableEntry(s.settled)(id, run, ok ? 1 : 0, stringToNewUTF8(text));
    };
    var url;
    try {
      // A page keeps a module by its URL, and a module's URL is its text: each run links its imports afresh (an edited
      // module is new text, an unchanged one the module already there), and its own text names the run, so a run of
      // the same text is evaluated again.
      s.modules = new Map();
      var text = omuiScripts.linked(s, UTF8ToString(code), moduleName) + omuiScripts.named(moduleName + '#run=' + run);
      url = omuiScripts.url(text);
    } catch (e) {
      settle(false, moduleName + ': ' + String(e && e.message !== undefined ? e.message : e));
      return;
    }
    // Its name first, as QuickJS says where (a module from a data: URL has no name of its own on the page).
    import(/* webpackIgnore: true */ url).then(function () { settle(true, ''); }, function (e) {
      settle(false, moduleName + ': ' + String(e && e.message !== undefined ? e.message : e) + (e && e.stack ? '\n' + e.stack : ''));
    });
  },

  omui_script_call__deps: ['$omuiScripts', '$UTF8ToString', '$stringToNewUTF8'],
  omui_script_call: function (id, fn, argument, parse) {
    var s = omuiScripts.scripts[id];
    var f = s && s.functions.get(fn);
    if (!f) return 0;
    var text = UTF8ToString(argument);
    try {
      var result = f(parse ? JSON.parse(text) : text);
      return stringToNewUTF8(result === undefined ? '' : String(result));
    } catch (e) {
      console.error('openmui js:', e);
      return 0;
    }
  },

  // A moment for the page (a macrotask: what import() settles in): 1 after it; -1 where this program cannot wait
  // (built without -sASYNCIFY).
  // Whether a pause lets the page run: a program linked to suspend (-sASYNCIFY).
  omui_script_can_pause: function () {
#if ASYNCIFY
    return 1;
#else
    return 0;
#endif
  },
  omui_script_pause__async: 'auto',
  // A task of its own, not setTimeout(0): a timer set from a timer's callback waits at least 4 ms after five levels
  // (HTML's clamping), and a program waiting here resumes in the last one's.
  omui_script_pause: function () {
#if ASYNCIFY
    return new Promise(function (resolve) {
      if (typeof MessageChannel !== 'function') return setTimeout(function () { resolve(1); }, 0);
      var channel = new MessageChannel();
      channel.port1.onmessage = function () { channel.port1.close(); resolve(1); };
      channel.port2.postMessage(0);
    });
#else
    return -1;
#endif
  },

  omui_script_release__deps: ['$omuiScripts'],
  omui_script_release: function (id, fn) {
    var s = omuiScripts.scripts[id];
    if (s) s.functions.delete(fn);
  },

  omui_script_end__deps: ['$omuiScripts'],
  omui_script_end: function (id) {
    var s = omuiScripts.scripts[id];
    if (!s) return;
    delete omuiScripts.scripts[id];
    if (globalThis.__omuiScripts) delete globalThis.__omuiScripts[id];
  },
});
