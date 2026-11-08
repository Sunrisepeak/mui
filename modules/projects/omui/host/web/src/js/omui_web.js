// openmui.host.web's half in the page: what the C++ side cannot do through
// Emscripten's HTML5 API. An Emscripten library (--js-library): each
// function is linked into the program's JavaScript by name.
//
// $omuiPage holds the page's state: the hidden textarea input methods
// compose in, the semantic DOM mirror and its live regions, window.omui's
// pending requests, the clipboard as last known. What it hears goes to the
// program at once through the callback omui_web_open was given — on_page
// (kind, a, b, text) — so it keeps its order with the canvas's events:
//   1 text      a committed text (the textarea's: an input method's result,
//               a phone keyboard's letters, a paste not by the keys)
//   2 preedit   the composition so far; a, b the caret's byte offsets in it
//   3 key       a key a phone's keyboard gave only as an edit (Backspace)
//   4 activate  assistive technology activated a mirrored node (text: its id)
//   5 focus     assistive technology moved the focus to a mirrored node
//   6 agent     a request of window.omui's (text: one JSON line)
//   7 link      the agent link's state (text: JSON — state, code, port, agent, error)
// Only the person's own input counts (openas §2.7): an event a script made
// (isTrusted false) is never passed on, and window.omui's requests are the
// program's to decide as any agent's.
addToLibrary({
  // The canvas the program draws on: Module.canvas when the page gave one,
  // else the element the selector names.
  $omuiCanvas: function (selector) {
    if (Module['canvas']) return Module['canvas'];
    return document.querySelector(selector) || document.getElementById('canvas');
  },

  $omuiPage__deps: ['$omuiCanvas', '$stringToNewUTF8', '$lengthBytesUTF8', '$getWasmTableEntry'],
  $omuiPage: {
    TEXT: 1, PREEDIT: 2, KEY: 3, ACTIVATE: 4, FOCUS: 5, AGENT: 6, LINK: 7,
    LINK_PORTS: [47110, 47111, 47112, 47113],  // where a bridge (omui connect) listens, the first free of them
    callback: 0,
    selector: '#canvas',
    trusted: true,        // the event being dispatched now (window, capturing: before the canvas's)
    pasteKey: false,      // the last key down was the paste chord: its paste is the program's key's
    textarea: null,
    layer: null,          // the mirror's elements, over the canvas
    polite: null,         // live regions
    assertive: null,
    nodes: null,          // id -> element
    focused: null,        // the mirror's focus
    skip: null,           // the skip link, and the node it goes to
    skipTo: null,
    focusing: false,      // the program moving the focus (not the person)
    composing: false,
    clipboard: '',
    agents: null,         // re -> {records, resolve}
    nextAgent: 1,

    send: function (kind, a, b, text) {
      if (!omuiPage.callback) return;
      getWasmTableEntry(omuiPage.callback)(kind, a, b, stringToNewUTF8(text || ''));
    },

    hidden: function (el) {
      // Out of sight, not out of the accessibility tree (no display:none).
      var s = el.style;
      s.position = 'fixed';
      s.opacity = '0';
      s.pointerEvents = 'none';
      s.overflow = 'hidden';
      s.margin = '0';
      s.padding = '0';
      s.border = '0';
      return el;
    },

    open: function (selector, callback) {
      var P = omuiPage;
      P.selector = selector;
      P.callback = callback;
      P.nodes = new Map();
      P.agents = new Map();
      // Whether the event under way is the person's, before any other listener hears it.
      ['keydown', 'keyup', 'mousedown', 'mouseup', 'mousemove', 'mouseenter', 'mouseleave', 'wheel',
       'touchstart', 'touchend', 'touchmove', 'touchcancel'].forEach(function (type) {
        window.addEventListener(type, function (e) {
          P.trusted = e.isTrusted;
          if (type === 'keydown') P.pasteKey = e.isTrusted && (e.ctrlKey || e.metaKey) && e.code === 'KeyV';
        }, true);
      });
      var body = document.body;
      // The textarea input methods compose in; put where the caret is while a field is edited.
      var t = P.hidden(document.createElement('textarea'));
      t.setAttribute('data-omui', 'input');
      t.setAttribute('autocomplete', 'off');
      t.setAttribute('autocapitalize', 'off');
      t.setAttribute('spellcheck', 'false');
      t.style.width = '1px';
      t.style.height = '1px';
      t.style.fontSize = '16px';   // a phone does not zoom to it
      t.style.resize = 'none';
      t.addEventListener('compositionstart', P.onCompositionStart);
      t.addEventListener('compositionupdate', P.onCompositionUpdate);
      t.addEventListener('compositionend', P.onCompositionEnd);
      t.addEventListener('input', P.onInput);
      body.appendChild(t);
      P.textarea = t;
      // The skip link: the page's first stop for the Tab key, seen only while
      // focused, past what comes before the main landmark (WCAG 2.4.1).
      var skip = document.createElement('a');
      skip.setAttribute('data-omui', 'skip');
      skip.href = '#';
      skip.hidden = true;
      skip.style.cssText = 'position:fixed;left:8px;top:-60px;z-index:10;padding:10px 16px;border-radius:6px;' +
        'background:#fff;color:#0b0f16;font:600 15px system-ui,sans-serif;text-decoration:none;outline:2px solid #4c8dff';
      skip.addEventListener('focus', function () { skip.style.top = '8px'; });
      skip.addEventListener('blur', function () { skip.style.top = '-60px'; });
      skip.addEventListener('click', function (e) {
        e.preventDefault();
        if (e.isTrusted) P.followSkip();
      });
      body.insertBefore(skip, body.firstChild);
      P.skip = skip;
      // The program hears the keys on the window, before the page's elements
      // do: the skip link's keys are taken first. The first Tab while nothing
      // in the program is focused goes to the link; on it, Enter (or Space)
      // follows it, Tab goes on into the program (which takes that Tab).
      window.addEventListener('keydown', function (e) {
        if (!e.isTrusted || e.ctrlKey || e.altKey || e.metaKey) return;
        if (document.activeElement === skip) {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            P.followSkip();
          } else if (e.key === 'Tab' && !e.shiftKey) {
            omuiCanvas(P.selector).focus({ preventScroll: true });
            return;
          }
          e.stopImmediatePropagation();
          return;
        }
        if (e.key === 'Tab' && !e.shiftKey && P.skipTo && !P.focused && P.ours(document.activeElement)) {
          e.preventDefault();
          e.stopImmediatePropagation();
          skip.focus();
        }
      }, true);
      // The mirror's layer over the canvas, and the live regions.
      var layer = P.hidden(document.createElement('div'));
      layer.setAttribute('data-omui', 'mirror');
      body.appendChild(layer);
      P.layer = layer;
      P.polite = P.hidden(document.createElement('div'));
      P.polite.setAttribute('data-omui', 'live');
      P.polite.setAttribute('aria-live', 'polite');
      body.appendChild(P.polite);
      P.assertive = P.hidden(document.createElement('div'));
      P.assertive.setAttribute('data-omui', 'alert');
      P.assertive.setAttribute('aria-live', 'assertive');
      body.appendChild(P.assertive);
      document.addEventListener('paste', P.onPaste, true);
      document.addEventListener('copy', P.onCopy, true);
      document.addEventListener('cut', P.onCopy, true);
      window.addEventListener('focus', P.refreshClipboard);
      // window.omui: the page's agents, as the desktop's (openmui.tree@0.1).
      var omui = {
        request: P.request,
        describe: function () { return P.request({ op: 'describe' }); },
        snapshot: function (o) { return P.request(Object.assign({ op: 'snapshot' }, o || {})); },
        changes: function () { return P.request({ op: 'changes' }); },
        find: function (q) { return P.request({ op: 'find', q: q }); },
        act: function (node, action, o) { return P.request(Object.assign({ op: 'act', node: node, action: action }, o || {})); },
        invoke: function (verb, args, o) { return P.request(Object.assign({ op: 'invoke', verb: verb, args: args || {} }, o || {})); },
        wait: function (condition, o) { return P.request(Object.assign({ op: 'wait' }, condition || {}, o || {})); },
        capture: function (o) { return P.request(Object.assign({ op: 'capture' }, o || {})); },
        // The person's own agent, through a bridge on this machine (D-95): the pairing code to give it, and the end of it.
        link: function () { return P.linkStart(P.LINK_PORTS, P.link ? P.link.app : ''); },
        unlink: function () { P.linkStop(); },
        // How it stands (what the program shows: waiting, paired with whom, lost, expired, stopped); null before one began.
        linkState: function () {
          var l = P.link;
          return l ? { state: l.state, code: l.code, agent: l.agent, port: l.port, error: l.error || '' } : null;
        },
      };
      window.omui = Object.freeze(omui);
      P.offerWebMcp(window.omui);
    },

    // WebMCP (Part 2 §4.1, C; the W3C WebML community group's draft of
    // 2 November 2026): where the browser lets a page offer tools to the
    // person's agent (document.modelContext), the page offers its operations
    // — the same ones window.omui and omui connect give, decided the same way
    // (openas) — with no line of the application's own; those that only read
    // say so (readOnlyHint). What needs the person's answer: the draft gives
    // the page no way to ask through the browser, so the page asks the person
    // itself and, if they say yes, sends it again as theirs to allow.
    offerWebMcp: function (omui) {
      var mc = typeof document !== 'undefined' && document.modelContext;
      if (!mc || typeof mc.registerTool !== 'function') return false;
      var actor = 'agent:webmcp';
      var words = function (answer) {
        var text = answer.exitCode === 0 ? JSON.stringify(answer.data) : (answer.errors[0] && answer.errors[0].text) || ('exit ' + answer.exitCode);
        return { content: [{ type: 'text', text: text }], isError: answer.exitCode !== 0 };
      };
      var withPictures = function (answer) {
        var out = words(answer);
        ['before', 'after'].forEach(function (when) {
          var p = answer.data && answer.data[when];
          if (p && p.base64) out.content.push({ type: 'image', data: p.base64, mimeType: 'image/png' });
        });
        return out;
      };
      // An answer the person must give: asked on the page, then the request again as theirs to allow.
      var decided = function (request, answer) {
        var asks = answer.errors.filter(function (e) { return e.code === 'needs-answer'; })[0];
        if (answer.exitCode !== 2 || !asks) return Promise.resolve(words(answer));
        var yes = typeof window.confirm === 'function' && window.confirm(asks.text || 'An agent asks to do this. Allow it?');
        if (!yes) return Promise.resolve({ content: [{ type: 'text', text: 'the person declined: nothing was done' }], isError: true });
        return omui.request(Object.assign({}, request, { answer: asks.answer || 'yes', actor: actor + ';via=person:user' })).then(words);
      };
      var tool = function (name, description, properties, required, reads, run) {
        return { name: name, description: description,
                 inputSchema: { type: 'object', properties: properties, required: required || [] },
                 annotations: { readOnlyHint: reads }, execute: run };
      };
      var str = { type: 'string' }, obj = { type: 'object' }, arr = { type: 'array' }, bool = { type: 'boolean' };
      var tools = [
        tool('ui_snapshot', 'The interface as a tree of semantic nodes: role, name, value, state, actions, bounds.',
             { root: str, depth: { type: 'number' } }, [], true, function (i) { return omui.snapshot(i || {}).then(words); }),
        tool('ui_find', 'Nodes matching a query: {role, name (wildcards), id, state}.', { q: obj }, ['q'], true,
             function (i) { return omui.find(i.q).then(words); }),
        tool('ui_act', 'Performs an action a node offers (activate, toggle, set_value…); dry_run says what it would do.',
             { node: str, action: str, args: obj, dry_run: bool }, ['node', 'action'], false, function (i) {
               var request = { op: 'act', node: i.node, action: i.action, args: i.args || {}, dry_run: !!i.dry_run, actor: actor };
               return omui.request(request).then(function (a) { return decided(request, a); });
             }),
        tool('ui_invoke', 'Invokes an application verb (ui_snapshot of the app lists none; describe does).', { verb: str, args: obj }, ['verb'], false,
             function (i) {
               var request = { op: 'invoke', verb: i.verb, args: i.args || {}, actor: actor };
               return omui.request(request).then(function (a) { return decided(request, a); });
             }),
        tool('ui_transaction', 'Steps performed as one: all done, or all taken back.', { steps: arr, label: str }, ['steps'], false,
             function (i) { return omui.request({ op: 'transaction', steps: i.steps, label: i.label || '', actor: actor }).then(words); }),
        tool('ui_preview', 'What steps would make, shown before anything is allowed: the pictures before and after and the changes; nothing stays.',
             { steps: arr }, ['steps'], true, function (i) { return omui.request({ op: 'preview', steps: i.steps, actor: actor }).then(withPictures); }),
        tool('ui_undo', 'Takes back the last step of the history (redo: puts it forward again).', { redo: bool, scope: str }, [], false,
             function (i) { return omui.request({ op: i && i.redo ? 'redo' : 'undo', scope: (i && i.scope) || undefined, actor: actor }).then(words); }),
        tool('ui_capture', 'The pixels of the page, or of a node, as a PNG.', { node: str }, [], true, function (i) {
          return omui.capture(i && i.node ? { node: i.node } : {}).then(function (a) {
            if (a.exitCode !== 0 || !a.data || !a.data.base64) return words(a);
            return { content: [{ type: 'image', data: a.data.base64, mimeType: 'image/png' }] };
          });
        }),
      ];
      tools.forEach(function (t) { mc.registerTool(t); });
      omuiPage.webMcpTools = tools.map(function (t) { return t.name; });
      return true;
    },

    // The canvas's box on the page, for what is placed over it; the mirror's
    // layer follows it (set only when it moved: this runs every frame).
    over: function (r) {
      var P = omuiPage, l = P.layer;
      if (!r) {
        var canvas = omuiCanvas(P.selector);
        r = canvas ? canvas.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0 };
      }
      var key = r.left + ',' + r.top + ',' + r.width + ',' + r.height;
      if (l && l.omuiBox !== key) {
        l.omuiBox = key;
        l.style.left = r.left + 'px';
        l.style.top = r.top + 'px';
        l.style.width = r.width + 'px';
        l.style.height = r.height + 'px';
      }
      return r;
    },

    // Whether the focus is the program's to move: on nothing, the canvas,
    // the textarea or the mirror (never off another part of the page).
    ours: function (el) {
      var P = omuiPage;
      return !el || el === document.body || el === omuiCanvas(P.selector) || el === P.textarea || P.layer.contains(el);
    },

    // ── input methods ──

    onCompositionStart: function (e) {
      if (!e.isTrusted) return;
      omuiPage.composing = true;
    },
    onCompositionUpdate: function (e) {
      if (!e.isTrusted) return;
      // The caret at the composition's end, as a byte offset (openmui counts
      // bytes; the textarea's own selection lags the event).
      var data = e.data || '', at = lengthBytesUTF8(data);
      omuiPage.send(omuiPage.PREEDIT, at, at, data);
    },
    onCompositionEnd: function (e) {
      if (!e.isTrusted) return;
      var P = omuiPage;
      P.composing = false;
      P.send(P.PREEDIT, -1, -1, '');
      if (e.data) P.send(P.TEXT, 0, 0, e.data);
      P.textarea.value = '';
    },
    onInput: function (e) {
      var P = omuiPage;
      if (!e.isTrusted || e.isComposing || P.composing) return;
      var type = e.inputType || '';
      if ((type === 'insertText' || type === 'insertReplacementText') && e.data) P.send(P.TEXT, 0, 0, e.data);
      else if (type === 'insertLineBreak' || type === 'insertParagraph') P.send(P.KEY, 0, 0, 'Enter');
      else if (type === 'deleteContentBackward') P.send(P.KEY, 0, 0, 'Backspace');
      else if (type === 'deleteContentForward') P.send(P.KEY, 0, 0, 'Delete');
      P.textarea.value = '';
    },

    textInput: function (x, y, width, height, label) {
      var P = omuiPage, t = P.textarea;
      if (!t) return;
      if (width < 0) {
        if (document.activeElement === t) {
          t.blur();
          P.focusTo(P.focused);
        }
        return;
      }
      var r = P.over();
      t.style.left = (r.left + x) + 'px';
      t.style.top = (r.top + y) + 'px';
      t.style.height = Math.max(1, height) + 'px';
      if (label) t.setAttribute('aria-label', label);
      else t.removeAttribute('aria-label');
      if (document.activeElement !== t && P.ours(document.activeElement)) t.focus({ preventScroll: true });
    },

    // ── the clipboard ──

    onPaste: function (e) {
      var P = omuiPage;
      if (!e.isTrusted || !e.clipboardData) return;
      var text = e.clipboardData.getData('text/plain') || '';
      P.clipboard = text;
      if (e.target !== P.textarea) return;
      e.preventDefault();
      // The paste chord is the program's key (it reads the clipboard then);
      // a paste otherwise (a phone's menu) is text typed.
      if (!P.pasteKey && text) P.send(P.TEXT, 0, 0, text);
      P.pasteKey = false;
    },
    onCopy: function (e) {
      // The program copies when it hears the chord; the textarea has nothing to give.
      if (e.isTrusted && e.target === omuiPage.textarea) e.preventDefault();
    },
    // Back in the page: what the clipboard holds now, where the person allowed reading it.
    refreshClipboard: function () {
      var c = typeof navigator !== 'undefined' && navigator.clipboard;
      if (!c || !c.readText || !navigator.permissions) return;
      navigator.permissions.query({ name: 'clipboard-read' }).then(function (p) {
        if (p.state !== 'granted') return;
        return c.readText().then(function (t) { omuiPage.clipboard = t; });
      }).catch(function () {});
    },
    writeClipboard: function (text) {
      omuiPage.clipboard = text;
      var c = typeof navigator !== 'undefined' && navigator.clipboard;
      if (c && c.writeText) c.writeText(text).catch(function () {});
    },
    // An address outside the program: a new tab while the person's last
    // input still allows one (the program answers it a frame later, inside
    // the browser's transient activation), else this tab. Only http, https
    // and mailto (never javascript:).
    openUrl: function (url) {
      if (!/^(https?:\/\/|mailto:)/i.test(url)) return false;
      var a = typeof navigator !== 'undefined' && navigator.userActivation;
      if (!a || a.isActive) {
        window.open(url, '_blank', 'noopener,noreferrer');
        return true;
      }
      window.location.assign(url);
      return true;
    },
    // A file given to the person: downloaded under its name (the bytes
    // copied out of the program's memory first).
    offerFile: function (name, type, bytes) {
      if (typeof document === 'undefined' || typeof Blob === 'undefined' || typeof URL === 'undefined' || !URL.createObjectURL) return false;
      var url = URL.createObjectURL(new Blob([bytes.slice()], { type: type || 'application/octet-stream' }));
      var a = document.createElement('a');
      a.href = url; a.download = name; a.style.display = 'none';
      document.body.appendChild(a); a.click(); document.body.removeChild(a);
      setTimeout(function () { URL.revokeObjectURL(url); }, 60000);
      omuiPage.offered = (omuiPage.offered || []).concat([{ name: name, type: type, size: bytes.length }]);
      return true;
    },

    // ── the semantic DOM mirror ──

    element: function (id) {
      var P = omuiPage, el = P.nodes.get(id);
      if (el) return el;
      el = document.createElement('div');
      el.setAttribute('data-omui-id', id);
      el.style.position = 'absolute';
      el.addEventListener('click', function (e) {
        // The element activated, not each it lies in.
        e.stopPropagation();
        if (e.isTrusted) P.send(P.ACTIVATE, 0, 0, id);
      });
      el.addEventListener('focus', function (e) {
        if (e.isTrusted && !P.focusing) {
          P.focused = id;
          P.send(P.FOCUS, 0, 0, id);
        }
      });
      P.nodes.set(id, el);
      return el;
    },

    // An element's own text: a text node before its children.
    setText: function (el, text) {
      var first = el.firstChild;
      if (first && first.nodeType === 3) {
        if (text) first.data = text;
        else el.removeChild(first);
      } else if (text) {
        el.insertBefore(document.createTextNode(text), first);
      }
    },

    // The skip link followed: the page's focus back on the canvas, the
    // program's on the main landmark's first focusable node.
    followSkip: function () {
      var P = omuiPage;
      if (!P.skipTo) return;
      omuiCanvas(P.selector).focus({ preventScroll: true });
      P.send(P.FOCUS, 0, 0, P.skipTo);
    },

    focusTo: function (id) {
      var P = omuiPage;
      P.focused = id;
      var el = id ? P.nodes.get(id) : null;
      if (!el || document.activeElement === P.textarea || !P.ours(document.activeElement)) return;
      P.focusing = true;
      el.focus({ preventScroll: true });
      P.focusing = false;
    },

    announce: function (region, text) {
      // The same words again are still news: emptied, then said.
      region.textContent = '';
      region.textContent = text;
    },

    mirror: function (batch) {
      var P = omuiPage, b = JSON.parse(batch);
      P.over();
      (b.remove || []).forEach(function (id) {
        var el = P.nodes.get(id);
        if (!el) return;
        if (el.parentNode) el.parentNode.removeChild(el);
        P.nodes.delete(id);
      });
      var nodes = b.nodes || [];
      nodes.forEach(function (n) {
        var el = P.element(n.id);
        var set = n.set || {};
        Object.keys(set).forEach(function (k) { el.setAttribute(k, set[k]); });
        (n.unset || []).forEach(function (k) { el.removeAttribute(k); });
        if ('text' in n) P.setText(el, n.text);
        if (n.box) {
          el.style.left = n.box[0] + 'px';
          el.style.top = n.box[1] + 'px';
          el.style.width = n.box[2] + 'px';
          el.style.height = n.box[3] + 'px';
        }
      });
      // Children in order, once every element exists: only what is out of place moves.
      nodes.forEach(function (n) {
        if (!n.children) return;
        var el = P.nodes.get(n.id), at = el.firstElementChild;
        n.children.forEach(function (c) {
          var child = P.nodes.get(c);
          if (!child) return;
          if (child === at) at = at.nextElementSibling;
          else el.insertBefore(child, at);
        });
        while (at) {
          var next = at.nextElementSibling;
          el.removeChild(at);
          at = next;
        }
      });
      if (b.root) {
        var root = P.nodes.get(b.root);
        if (root && root.parentNode !== P.layer) P.layer.appendChild(root);
      }
      if ('focus' in b) P.focusTo(b.focus);
      if ('skip' in b) {
        P.skipTo = b.skip ? b.skip.to : null;
        P.skip.hidden = !b.skip;
        var zh = /^zh/.test((document.documentElement && document.documentElement.lang) || '');
        P.skip.textContent = zh ? '跳到主要内容' : 'Skip to main content';
        if (b.skip && b.skip.name) P.skip.title = b.skip.name;
      }
      if (b.live) P.announce(P.polite, b.live);
      if (b.alert) P.announce(P.assertive, b.alert);
    },

    // ── the agent link (D-95): the person's own agent, through a bridge on 127.0.0.1 ──
    //
    // Started by the program (omui_web_link_start: a "copy the prompt" press),
    // never by itself: it looks for a bridge (omui connect) at the ports given,
    // says hello with the pairing code it made and the program shows, then
    // takes the agent's requests one at a time and answers them through
    // window.omui's path (the bus: openas decides each, S-10, S-11). What it
    // is doing goes to the program as a LINK line (JSON).
    link: null,
    linkStart: function (ports, app) {
      var P = omuiPage;
      if (P.link && P.link.state !== 'stopped' && P.link.state !== 'expired') return P.link.code;
      var bytes = new Uint8Array(8);
      crypto.getRandomValues(bytes);
      var abc = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';  // no I, L, O, 0, 1: read aloud, typed again
      var code = '';
      for (var i = 0; i < 8; i++) code += abc[bytes[i] % abc.length];
      code = code.slice(0, 4) + '-' + code.slice(4);
      P.link = { code: code, ports: ports, app: app || '', state: 'waiting', port: 0, session: '', agent: '', until: Date.now() + 600000, stopped: false };
      P.linkSay();
      P.linkHello(P.link);
      return code;
    },
    linkStop: function () {
      var P = omuiPage, l = P.link;
      if (!l) return;
      l.stopped = true;
      l.state = 'stopped';
      P.linkSay();
    },
    linkSay: function () {
      var P = omuiPage, l = P.link;
      if (!l) return;
      P.send(P.LINK, 0, 0, JSON.stringify({ state: l.state, code: l.code, port: l.port, agent: l.agent, error: l.error || '' }));
    },
    linkHello: function (l) {
      var P = omuiPage;
      if (l.stopped || P.link !== l) return;
      if (Date.now() > l.until) { l.state = 'expired'; P.linkSay(); return; }
      var tries = l.ports.map(function (port) {
        return fetch('http://127.0.0.1:' + port + '/omui/v1/hello', {
          method: 'POST', headers: { 'content-type': 'application/json' }, cache: 'no-store',
          body: JSON.stringify({ code: l.code, app: l.app || '', page: (typeof location !== 'undefined' ? location.href : '') })
        }).then(function (r) {
          return r.json().then(function (body) { return { ok: r.ok, body: body, port: port }; });
        });
      });
      Promise.allSettled(tries).then(function (all) {
        if (l.stopped || P.link !== l) return;
        var paired = null, refused = null;
        all.forEach(function (a) {
          if (a.status !== 'fulfilled') return;
          if (a.value.ok && !paired) paired = a.value;
          else if (!a.value.ok) refused = a.value;
        });
        if (paired) {
          l.port = paired.port;
          l.session = paired.body.session;
          l.agent = paired.body.agent || 'an agent';
          l.state = 'paired';
          l.error = '';
          P.linkSay();
          P.linkPoll(l);
          return;
        }
        if (refused && refused.body && refused.body.error) { l.error = refused.body.error; P.linkSay(); }
        setTimeout(function () { P.linkHello(l); }, 1500);
      });
    },
    linkPoll: function (l) {
      var P = omuiPage;
      if (l.stopped || P.link !== l) return;
      var base = 'http://127.0.0.1:' + l.port + '/omui/v1/';
      fetch(base + 'next?session=' + encodeURIComponent(l.session), { cache: 'no-store' }).then(function (r) {
        if (l.stopped) return null;
        if (r.status === 204) return null;
        if (!r.ok) throw new Error('the bridge answered ' + r.status);
        return r.json();
      }).then(function (job) {
        if (!job || l.stopped) { P.linkPoll(l); return; }
        // Answered through window.omui's path, then the next.
        P.request(job.request).then(function (reply) {
          return fetch(base + 'reply?session=' + encodeURIComponent(l.session), {
            method: 'POST', headers: { 'content-type': 'application/json' }, cache: 'no-store',
            body: JSON.stringify({ id: job.id, reply: reply })
          });
        }).catch(function () {}).then(function () { P.linkPoll(l); });
      }).catch(function (e) {
        // The bridge went (stopped, restarted): look for it again a while.
        if (l.stopped || P.link !== l) return;
        l.state = 'lost';
        l.error = String(e && e.message || e);
        l.until = Date.now() + 600000;
        P.linkSay();
        setTimeout(function () { P.linkHello(l); }, 1500);
      });
    },

    // ── window.omui ──

    request: function (req) {
      var P = omuiPage;
      return new Promise(function (resolve) {
        var re = P.nextAgent++;
        P.agents.set(re, { records: [], resolve: resolve });
        P.send(P.AGENT, 0, 0, JSON.stringify(Object.assign({}, req, { re: re })));
      });
    },

    // A record answering a request; its `result` settles the promise with
    // {exitCode, data, errors, records}.
    reply: function (line) {
      var P = omuiPage, r = JSON.parse(line), pending = P.agents.get(r.re);
      if (!pending) return;
      pending.records.push(r);
      if (r.kind !== 'result') return;
      P.agents.delete(r.re);
      var data = null, errors = [];
      pending.records.forEach(function (x) {
        if (x.kind === 'data') data = x.data;
        if (x.kind === 'error') errors.push(x);
      });
      pending.resolve({ exitCode: r.exitCode, data: data, errors: errors, records: pending.records });
    },
  },

  // The page's listeners (before the canvas's: whether an event is the
  // person's is known first), the hidden textarea, the mirror's layer, the
  // live regions, window.omui; what they hear goes to `callback`.
  omui_web_open__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_open: function (selector, callback) {
    omuiPage.open(UTF8ToString(selector), callback);
  },

  // Whether the event being dispatched is the person's (isTrusted).
  omui_web_trusted__deps: ['$omuiPage'],
  omui_web_trusted: function () {
    return omuiPage.trusted ? 1 : 0;
  },

  // The textarea at the caret (logical pixels on the canvas), named for the
  // field; a negative width puts it away.
  omui_web_text_input__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_text_input: function (x, y, width, height, label) {
    omuiPage.textInput(x, y, width, height, UTF8ToString(label));
  },

  // A batch of the semantic DOM mirror's, as JSON.
  omui_web_mirror__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_mirror: function (batch) {
    omuiPage.mirror(UTF8ToString(batch));
  },

  // The agent link: looking for a bridge with a pairing code (returned, in memory the caller frees), and its end.
  omui_web_link_start__deps: ['$omuiPage', '$UTF8ToString', '$stringToNewUTF8'],
  omui_web_link_start: function (app) {
    return stringToNewUTF8(omuiPage.linkStart(omuiPage.LINK_PORTS, UTF8ToString(app)));
  },
  omui_web_link_stop__deps: ['$omuiPage'],
  omui_web_link_stop: function () { omuiPage.linkStop(); },

  omui_web_agent_reply__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_agent_reply: function (line) {
    omuiPage.reply(UTF8ToString(line));
  },

  // The clipboard as the page last knew it, in memory the caller frees.
  omui_web_clipboard__deps: ['$omuiPage', '$stringToNewUTF8'],
  omui_web_clipboard: function () {
    return stringToNewUTF8(omuiPage.clipboard);
  },

  omui_web_set_clipboard__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_set_clipboard: function (text) {
    omuiPage.writeClipboard(UTF8ToString(text));
  },

  omui_web_open_url__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_open_url: function (url) {
    return omuiPage.openUrl(UTF8ToString(url)) ? 1 : 0;
  },

  omui_web_offer_file__deps: ['$omuiPage', '$UTF8ToString'],
  omui_web_offer_file: function (name, type, bytes, size) {
    return omuiPage.offerFile(UTF8ToString(name), UTF8ToString(type), HEAPU8.subarray(bytes, bytes + size)) ? 1 : 0;
  },

  // localStorage, where the page has it (reading it throws where the person
  // turned it off).
  $omuiStorage: function () {
    try {
      return typeof localStorage !== 'undefined' && localStorage ? localStorage : null;
    } catch (e) {
      return null;
    }
  },

  omui_web_store__deps: ['$omuiStorage'],
  omui_web_store: function () {
    return omuiStorage() ? 1 : 0;
  },

  omui_web_store_get__deps: ['$omuiStorage', '$UTF8ToString', '$stringToNewUTF8'],
  omui_web_store_get: function (key) {
    var s = omuiStorage(), v = s ? s.getItem(UTF8ToString(key)) : null;
    return v === null ? 0 : stringToNewUTF8(v);
  },

  // 0 when the storage is full (or refused).
  omui_web_store_set__deps: ['$omuiStorage', '$UTF8ToString'],
  omui_web_store_set: function (key, value) {
    var s = omuiStorage();
    if (!s) return 0;
    try {
      s.setItem(UTF8ToString(key), UTF8ToString(value));
      return 1;
    } catch (e) {
      return 0;
    }
  },

  omui_web_store_remove__deps: ['$omuiStorage', '$UTF8ToString'],
  omui_web_store_remove: function (key) {
    var s = omuiStorage(), k = UTF8ToString(key);
    if (!s || s.getItem(k) === null) return 0;
    s.removeItem(k);
    return 1;
  },

  // The keys that begin with the prefix, as a JSON array.
  omui_web_store_keys__deps: ['$omuiStorage', '$UTF8ToString', '$stringToNewUTF8'],
  omui_web_store_keys: function (prefix) {
    var s = omuiStorage(), p = UTF8ToString(prefix), out = [];
    for (var i = 0; s && i < s.length; ++i) {
      var k = s.key(i);
      if (k !== null && k.indexOf(p) === 0) out.push(k);
    }
    return stringToNewUTF8(JSON.stringify(out));
  },

  // A frame of RGBA8 pixels (opaque: premultiplied is straight) put on the
  // canvas, whose backing store takes the frame's size.
  omui_web_backing__deps: ['$omuiCanvas', '$UTF8ToString'],
  omui_web_backing: function (selector, width, height) {
    var canvas = omuiCanvas(UTF8ToString(selector));
    if (!canvas || width <= 0 || height <= 0) return;
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
  },

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
    if (!Module.omuiContext) {  // the canvas gave another kind of context (a GPU's): said once, nothing drawn
      if (!Module.omuiContextSaid) err('openmui: the canvas has another kind of context; the CPU\'s frames cannot be shown on it');
      Module.omuiContextSaid = true;
      return;
    }
    var bytes = new Uint8ClampedArray(HEAPU8.buffer, pixels, width * height * 4);
    Module.omuiContext.putImageData(new ImageData(bytes, width, height), 0, 0);
  },

  // The canvas's size in CSS pixels (openmui's logical pixels) and the
  // device's pixel ratio, into three floats.
  omui_web_canvas__deps: ['$omuiCanvas', '$omuiPage', '$UTF8ToString'],
  omui_web_canvas: function (selector, out) {
    var canvas = omuiCanvas(UTF8ToString(selector));
    var r = canvas ? canvas.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0 };
    if (omuiPage.layer) omuiPage.over(r);
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
  // The page's query: one parameter (empty when absent); the page loaded
  // again with one set, or removed when the value is empty.
  omui_web_query__deps: ['$UTF8ToString', '$stringToNewUTF8'],
  omui_web_query: function (name) {
    var v = '';
    try { v = new URLSearchParams(location.search).get(UTF8ToString(name)) || ''; } catch (e) {}
    return stringToNewUTF8(v);
  },
  omui_web_matches__deps: ['$UTF8ToString'],
  omui_web_matches: function (media) {
    try { return (typeof matchMedia === 'function' && matchMedia(UTF8ToString(media)).matches) ? 1 : 0; } catch (e) { return 0; }
  },
  omui_web_reload_with__deps: ['$UTF8ToString'],
  omui_web_reload_with: function (name, value) {
    try {
      var u = new URL(location.href), n = UTF8ToString(name), v = UTF8ToString(value);
      if (v) u.searchParams.set(n, v); else u.searchParams.delete(n);
      location.replace(u.toString());
      return 1;
    } catch (e) {
      return 0;
    }
  },

  // The page's route (its address's hash without "#/"), how many times the
  // person moved it (back, forward, a typed address), and a new step of the
  // browser's history to it.
  omui_web_route__deps: ['$stringToNewUTF8'],
  omui_web_route: function () {
    var h = '';
    try { h = decodeURIComponent((location.hash || '').replace(/^#\/?/, '')); } catch (e) {}
    return stringToNewUTF8(h);
  },
  omui_web_route_moves: function () {
    if (!window.omuiRoute) {
      window.omuiRoute = { moves: 0 };
      window.addEventListener('hashchange', function () { window.omuiRoute.moves++; });
    }
    return window.omuiRoute.moves;
  },
  // The program's first frame is shown: the page's hook, once (dist-toy fetches its later files then).
  omui_web_first_frame: function () {
    window.omuiHasPresented = true;
    var f = window.omuiFirstFrame;
    window.omuiFirstFrame = null;
    if (typeof f === 'function') f();
  },
  // The program needs the faces the page deferred until asked: the page's hook.
  omui_web_want_fonts: function (codePoint) {
    var f = window.omuiWantFonts;
    if (typeof f === 'function') { if (codePoint >= 0) f(codePoint); else f(); }
  },
  // Where the application's agent.md is: beside the page.
  omui_web_agent_guide__deps: ['$stringToNewUTF8'],
  omui_web_agent_guide: function () {
    var g = '';
    try { g = new URL('agent.md', location.href.split('#')[0]).href; } catch (e) {}
    return stringToNewUTF8(g);
  },
  omui_web_set_route__deps: ['$UTF8ToString'],
  omui_web_set_route: function (route) {
    window.omuiFollowsRoute = true;  // the page's own reload on a new hash is not for it
    try {
      var h = '#/' + UTF8ToString(route);
      var now = location.hash || '#/';
      if (now !== h) history.pushState(null, '', h);  // pushState fires no hashchange: not a move of the person's
    } catch (e) {}
  },

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
