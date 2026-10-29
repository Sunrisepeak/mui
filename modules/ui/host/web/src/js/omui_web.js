// openmui.host.web's half in the page: what the C++ side cannot do through
// Emscripten's HTML5 API. An Emscripten library (--js-library): each
// function is linked into the program's JavaScript by name.
addToLibrary({
  // The canvas the program draws on: Module.canvas when the page gave one,
  // else the element the selector names.
  $omuiCanvas: function (selector) {
    if (Module['canvas']) return Module['canvas'];
    return document.querySelector(selector) || document.getElementById('canvas');
  },

  // A frame of RGBA8 pixels (opaque: premultiplied is straight) put on the
  // canvas, whose backing store takes the frame's size.
  omui_web_present__deps: ['$omuiCanvas', '$UTF8ToString'],
  omui_web_present: function (selector, pixels, width, height) {
    var canvas = omuiCanvas(UTF8ToString(selector));
    if (!canvas || width <= 0 || height <= 0) return;
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    if (!Module.omuiContext || Module.omuiContextCanvas !== canvas) {
      Module.omuiContext = canvas.getContext('2d', { alpha: false });
      Module.omuiContextCanvas = canvas;
    }
    var bytes = new Uint8ClampedArray(HEAPU8.buffer, pixels, width * height * 4);
    Module.omuiContext.putImageData(new ImageData(bytes, width, height), 0, 0);
  },

  // The canvas's size in CSS pixels (openmui's logical pixels) and the
  // device's pixel ratio, into three floats.
  omui_web_canvas__deps: ['$omuiCanvas', '$UTF8ToString'],
  omui_web_canvas: function (selector, out) {
    var canvas = omuiCanvas(UTF8ToString(selector));
    var r = canvas ? canvas.getBoundingClientRect() : { width: 0, height: 0 };
    HEAPF32[(out >> 2) + 0] = r.width;
    HEAPF32[(out >> 2) + 1] = r.height;
    HEAPF32[(out >> 2) + 2] = window.devicePixelRatio || 1;
  },

  // Whether there is a page at all (node runs the Web row's tests without one).
  omui_web_page: function () {
    return typeof document !== 'undefined' ? 1 : 0;
  },

  // Whether the page is hidden (another tab, the app in the background).
  omui_web_hidden: function () {
    return document.hidden ? 1 : 0;
  },

  // The person's languages (navigator.languages), comma-separated, in memory
  // the caller frees.
  omui_web_languages__deps: ['$stringToNewUTF8'],
  omui_web_languages: function () {
    var list = (navigator.languages && navigator.languages.length) ? navigator.languages : [navigator.language || ''];
    return stringToNewUTF8(list.join(','));
  },

  // The document's title.
  omui_web_title__deps: ['$UTF8ToString'],
  omui_web_title: function (text) {
    document.title = UTF8ToString(text);
  },
});
