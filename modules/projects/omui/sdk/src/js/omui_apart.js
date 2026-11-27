// mlibs.apart on the page: what the program reads itself (the kind `data`: a catalog's listing), which the page's
// packer carried apart (omui-gen's carry_apart), put back where it was in the program's memory the first time the
// program reads it — the page's loader holds the kind's file (Module.omuiApart: texts(kind), {address: text}, fetched
// then if no one asked for it before). An Emscripten library (omui.sdk's build.mcpp); a page nobody packed has no
// Module.omuiApart, and nothing was carried apart from its program.
addToLibrary({
  omui_apart_bring__deps: ['$UTF8ToString'],
  omui_apart_bring: function (kind, length) {
    var apart = typeof Module !== 'undefined' && Module['omuiApart'];
    if (!apart) return 0;
    try {
      var texts = apart.texts(UTF8ToString(kind, length));
      if (!texts) return 0;
      var encoder = new TextEncoder();
      for (var at in texts) HEAPU8.set(encoder.encode(texts[at]), Number(at));
      return 1;
    } catch (e) {
      console.warn('omui: what the program carries apart could not be brought back: ' + (e && e.message || e));
      return 0;
    }
  },
});
