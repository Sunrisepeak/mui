// openmui.host.web.toy's half in the page: window.toy (Toy's SDK, which the
// page loads itself) called by name, its promise answered through the
// program's callback. An Emscripten library (--js-library).
addToLibrary({
  $omuiToySdk: function () {
    return (typeof window !== 'undefined' && window.toy) || null;
  },

  omui_toy_available__deps: ['$omuiToySdk'],
  omui_toy_available: function () {
    return omuiToySdk() ? 1 : 0;
  },

  // window.toy[method](args) — args a JSON value (an array spreads into
  // several arguments, null is none) — answered as done(id, ok, json) with
  // the result, or Toy's error as {type, code, message}; the program frees
  // the JSON text. Toy's rate limit (an http_error with code 307044: the
  // quota all players of one Toy share) is not an answer yet: the same
  // request is tried again after 1, 2, 4 and 8 seconds.
  omui_toy_call__deps: ['$omuiToySdk', '$UTF8ToString', '$stringToNewUTF8', '$getWasmTableEntry'],
  omui_toy_call: function (id, method, args, done) {
    var name = UTF8ToString(method);
    var text = UTF8ToString(args);
    var payload = text ? JSON.parse(text) : null;
    var answer = function (ok, value) {
      var json = stringToNewUTF8(JSON.stringify(value === undefined ? null : value));
      getWasmTableEntry(done)(id, ok ? 1 : 0, json);
    };
    var sdk = omuiToySdk();
    if (!sdk || typeof sdk[name] !== 'function') {
      answer(false, { type: 'unsupported', code: 0, message: 'no Toy SDK in this page' });
      return;
    }
    var attempt = 0;
    var go = function () {
      var call;
      try {
        call = payload === null ? sdk[name]() : Array.isArray(payload) ? sdk[name].apply(sdk, payload) : sdk[name](payload);
      } catch (e) {
        call = Promise.reject(e);
      }
      Promise.resolve(call).then(function (value) { answer(true, value); }, function (e) {
        var error = { type: (e && e.type) || 'unknown', code: (e && e.code) || 0, message: String((e && e.message) || e) };
        if (error.code === 307044 && attempt < 4) {
          setTimeout(go, 1000 * Math.pow(2, attempt++));
          return;
        }
        answer(false, error);
      });
    };
    go();
  },
});
