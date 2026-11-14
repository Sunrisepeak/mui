// Runs once before wasm. The static view stays readable until the current
// route has both presented pixels and a populated semantic mirror.
(function () {
  var statik = document.getElementById('omui-static');
  if (!statik) return;
  var busy = false;
  var focusTarget = null;
  var preview = null, previewAbort = null, previewKey = '';
  var catalogue = null, latestLoad = window.omuiLoad || null;
  var fontEntries = [], fontTasks = Object.create(null), fontNeeded = [], fontProblem = null;
  var reduceQuery = typeof matchMedia === 'function' ? matchMedia('(prefers-reduced-motion: reduce)') : null;
  function fontStatus() {
    var active = fontNeeded.map(function (name) { return fontTasks[name]; }).filter(Boolean);
    fontProblem = (active.find(function (entry) { return entry.error; }) || {}).error || null;
    window.omuiStaticFonts = {
      supported: typeof FontFace === 'function' && !!window.omuiLoad && typeof window.omuiLoad.fontBytes === 'function',
      active: !statik.hidden, requested: fontNeeded.slice(),
      loaded: active.filter(function (entry) { return !!entry.face; }).map(function (entry) { return entry.name; }),
      pending: active.filter(function (entry) { return !entry.face && !entry.error; }).map(function (entry) { return entry.name; }),
      error: fontProblem && {id: fontProblem.id, message: fontProblem.message}
    };
    loadNotice();
  }
  function requestFonts(page, retryFailed) {
    if (!page || statik.hidden || typeof FontFace !== 'function' || !window.omuiLoad || typeof window.omuiLoad.fontBytes !== 'function') return;
    fontNeeded = fontEntries.filter(function (entry) {
      return entry.when === 'body' || entry.when === 'code' && !!page.querySelector('pre,code') || entry.when === 'chinese' && page.lang === 'zh-CN';
    }).map(function (entry) { return entry.name; });
    fontEntries.forEach(function (descriptor) {
      if (!fontNeeded.includes(descriptor.name)) return;
      var entry = fontTasks[descriptor.name] || (fontTasks[descriptor.name] = {name: descriptor.name, face: null, pending: null, error: null});
      if (entry.face || entry.pending || entry.error && !retryFailed) return;
      entry.error = null;
      var lease = null;
      entry.pending = Promise.resolve(window.omuiLoad.fontBytes(descriptor.name)).then(function (source) {
        lease = source;
        if (statik.hidden) return null;
        var face = new FontFace(descriptor.family, lease.buffer, {weight: descriptor.weight, display: 'swap'});
        return face.load();
      }).then(function (face) {
        if (face && !statik.hidden) { document.fonts.add(face); entry.face = face; }
      }).catch(function (error) {
        entry.error = {id: error && error.id, message: String(error && error.message || error)};
      }).finally(function () {
        if (lease) lease.release();
        entry.pending = null; fontStatus();
      });
    });
    fontStatus();
  }
  function releaseFonts() {
    Object.values(fontTasks).forEach(function (entry) {
      if (entry.face) { document.fonts.delete(entry.face); entry.face = null; }
    });
    fontNeeded = []; fontStatus();
  }
  function menus() {
    statik.querySelectorAll('nav[data-locale]').forEach(function (nav) {
      if (!nav._menu) {
        var links = document.createElement('div'), button = document.createElement('button');
        links.className = 'static-links'; links.id = 'omui-static-nav-' + nav.dataset.locale;
        Array.from(nav.children).forEach(function (child) { if (!child.classList.contains('brand')) links.appendChild(child); });
        button.type = 'button'; button.id = 'omui-static-menu-' + nav.dataset.locale; button.className = 'static-menu'; button.textContent = nav.dataset.locale === 'zh' ? '菜单' : 'Menu';
        button.setAttribute('aria-controls', links.id); button.setAttribute('data-omui-target', 'main/bar/menu_toggle');
        nav.append(button, links); nav._menu = button; nav._links = links; nav._open = false;
        button.addEventListener('click', function () { nav._open = !nav._open; menus(); });
        links.addEventListener('click', function (event) { if (event.target.closest('a')) { nav._open = false; menus(); } });
      }
      var compact = innerWidth < 900;
      nav.dataset.compact = String(compact);
      nav._menu.hidden = !compact; nav._links.hidden = compact && !nav._open;
      nav._menu.setAttribute('aria-expanded', String(compact && nav._open));
    });
  }
  try {
    var data = document.getElementById('omui-first-frames');
    if (data) catalogue = JSON.parse(data.textContent);
  } catch (_) { /* Readable HTML remains the fallback. */ }
  try {
    var descriptions = document.getElementById('omui-static-fonts');
    if (descriptions) fontEntries = JSON.parse(descriptions.textContent);
    if (!Array.isArray(fontEntries)) fontEntries = [];
  } catch (_) { /* Native font loading still proceeds independently. */ }
  var note = document.createElement('div');
  note.className = 'omui-loading-note';
  var notice = document.createElement('span');
  notice.setAttribute('role', 'status');
  var retry = document.createElement('button');
  retry.type = 'button'; retry.id = 'omui-load-retry'; retry.hidden = true;
  note.append(notice, retry);
  statik.appendChild(note);
  function chinese() { return statik.lang === 'zh-CN'; }
  function loadNotice() {
    var failed = latestLoad && latestLoad.error || fontProblem;
    var text = failed ? (chinese() ? '交互界面暂未就绪，页面仍可阅读。' : 'The interactive view could not load. You can still read this page.')
      : (chinese() ? '正在准备交互界面…' : 'Opening the interactive view…');
    if (notice.textContent !== text) notice.textContent = text;
    retry.hidden = !failed;
    retry.textContent = chinese() ? '重试' : 'Retry';
  }
  retry.addEventListener('click', function () {
    if (!window.omuiLoad || typeof window.omuiLoad.retry !== 'function') { location.reload(); return; }
    var pending = window.omuiLoad.retry();
    requestFonts(selected(), true);
    Promise.resolve(pending).then(function (result) {
      if (result && (result.reload_required || result.reason === 'reload_required')) location.reload();
      else requestFonts(selected(), true);
    }).catch(function () { loadNotice(); });
  });
  function clearPreview() {
    if (previewAbort) previewAbort.abort();
    previewAbort = null; previewKey = '';
    if (preview) {
      if (preview._fallback) preview._fallback.removeAttribute('hidden');
      preview.remove(); preview = null;
    }
  }
  function updatePreview() {
    if (!preview || !preview._lines) return;
    var progress = latestLoad && latestLoad.startup && latestLoad.startup.ratio;
    var known = typeof progress === 'number' && Number.isFinite(progress);
    progress = known ? Math.max(0, Math.min(1, progress)) : 0;
    preview.dataset.phase = known && progress >= 1 ? 'initializing' : 'reading';
    preview._lines.forEach(function (line, index) {
      var local = known ? Math.max(0, Math.min(1, progress * preview._lines.length - index)) : 0;
      line.element.style.strokeDashoffset = String(line.length * (1 - local));
    });
  }
  function geometry(figure, record) {
    var crop = record.crop;
    if (!Array.isArray(crop) || crop.length !== 4 || !crop.every(Number.isFinite) || crop[2] <= 0 || crop[3] <= 0 || !Array.isArray(record.nodes)) return;
    var ns = 'http://www.w3.org/2000/svg', svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', crop.join(' ')); svg.setAttribute('aria-hidden', 'true');
    svg.setAttribute('focusable', 'false');
    var nodes = record.nodes.filter(function (node) {
      var b = node.bounds, states = node.state || node.states || [];
      return node.id && (node.id === record.root || node.id.indexOf(record.root + '/') === 0) &&
        !states.includes('hidden') && !states.includes('offscreen') && Array.isArray(b) && b.length === 4 && b.every(Number.isFinite) &&
        b[2] > 0 && b[3] > 0 && b[0] < crop[0] + crop[2] && b[1] < crop[1] + crop[3] && b[0] + b[2] > crop[0] && b[1] + b[3] > crop[1];
    }).slice(0, 64);
    figure._lines = [];
    nodes.forEach(function (node) {
      var b = node.bounds, rect = document.createElementNS(ns, 'rect');
      ['x', 'y', 'width', 'height'].forEach(function (name, i) { rect.setAttribute(name, String(b[i])); });
      rect.setAttribute('rx', '4'); rect.setAttribute('data-source-node', node.id);
      var length = 2 * (b[2] + b[3]);
      rect.style.strokeDasharray = String(length); rect.style.strokeDashoffset = String(length);
      svg.appendChild(rect); figure._lines.push({element: rect, length: length});
    });
    figure.dataset.nodes = String(nodes.length);
    figure.style.width = crop[2] + 'px';
    figure.querySelector('.omui-opening-picture').appendChild(svg);
    updatePreview();
  }
  function opening(page) {
    if (!page || !catalogue || catalogue.version !== 1 || !Array.isArray(catalogue.previews)) return;
    var name = page.id.replace(/^\//, '').replace(/^zh\//, '');
    var language = page.lang === 'zh-CN' ? 'zh' : 'en';
    var options = catalogue.previews.filter(function (entry) {
      return entry && entry.route === name && entry.language === language &&
        (entry.motion || 'full') === (reduceQuery && reduceQuery.matches ? 'reduced' : 'full') &&
        Array.isArray(entry.viewport) && entry.viewport.length === 2 && entry.viewport.every(Number.isFinite) &&
        typeof entry.image === 'string' && /^assets\/[A-Za-z0-9_.-]+$/.test(entry.image) &&
        typeof entry.geometry === 'string' && /^assets\/[A-Za-z0-9_.-]+$/.test(entry.geometry);
    });
    options.sort(function (a, b) { return Math.abs(a.viewport[0] - innerWidth) - Math.abs(b.viewport[0] - innerWidth); });
    var entry = options[0];
    if (!entry) { clearPreview(); return; }
    var key = [name, language, entry.motion || 'full', entry.viewport.join('x'), entry.image, entry.geometry].join('|');
    if (previewKey === key) return;
    clearPreview(); previewKey = key;
    var figure = document.createElement('figure'), picture = document.createElement('div'), image = document.createElement('img');
    figure.className = 'omui-opening'; picture.className = 'omui-opening-picture';
    image.alt = ''; image.setAttribute('aria-hidden', 'true'); image.decoding = 'async';
    figure.hidden = true; picture.appendChild(image); figure.appendChild(picture);
    var caption = document.createElement('figcaption');
    caption.textContent = language === 'zh' ? '界面预览 · 加载完成后即可操作' : 'Preview · use it when the interactive view opens';
    figure.appendChild(caption);
    var fallback = page.querySelector('.landing-mark > svg');
    if (fallback) { figure._fallback = fallback; fallback.parentNode.appendChild(figure); }
    else {
      var lead = page.querySelector('p');
      if (lead) lead.insertAdjacentElement('afterend', figure); else page.appendChild(figure);
    }
    preview = figure;
    image.addEventListener('load', function () {
      if (preview !== figure) return;
      figure.hidden = false; figure.style.width = image.naturalWidth + 'px';
      if (fallback) fallback.setAttribute('hidden', '');
      window.omuiOpeningVisible = {route: name, language: language, motion: entry.motion || 'full', image: entry.image, geometry: entry.geometry, at: performance.now()};
    });
    image.addEventListener('error', function () { if (preview === figure) clearPreview(); });
    image.src = entry.image;
    if (typeof AbortController === 'function') previewAbort = new AbortController();
    fetch(entry.geometry, previewAbort ? {signal: previewAbort.signal} : {}).then(function (response) {
      if (!response.ok) throw new Error('Opening geometry unavailable'); return response.json();
    }).then(function (record) { if (preview === figure) geometry(figure, record); }).catch(function () { /* The real picture and readable HTML remain. */ });
  }
  function loading(event) { latestLoad = event.detail || window.omuiLoad; loadNotice(); updatePreview(); attempt(); }
  window.addEventListener('omui-load', loading);
  function rememberFocus(e) {
    var link = e.target.closest && e.target.closest('a[data-omui-target]');
    focusTarget = link ? link.getAttribute('data-omui-target') : null;
  }
  statik.addEventListener('focusin', rememberFocus);
  statik.addEventListener('click', rememberFocus);
  window.omuiFollowsRoute = true;
  function route() {
    try { return decodeURIComponent((location.hash || '').replace(/^#\/?/, '')); }
    catch (_) { return ''; }
  }
  Module.arguments = ['--route', route()];
  function selected() {
    var at = route().replace(/^\/+|\/+$/g, '').replace(/^en(?:\/|$)/, '');
    var found = document.getElementById('/' + (at || 'home'));
    if (!found || !statik.contains(found)) return statik.querySelector('section[data-place="not_found"][lang="' + (/^zh(?:\/|$)/.test(at) ? 'zh-CN' : 'en') + '"]');
    return found.closest('section[data-place]');
  }
  function show() {
    if (statik.hidden) return;
    menus();
    var page = selected();
    statik.querySelectorAll('section[data-place]').forEach(function (s) {
      s.hidden = s !== page;
      s.style.display = s === page ? 'block' : '';
    });
    var zh = page && page.lang === 'zh-CN';
    window.omuiFontLanguage = zh ? 'zh' : 'en';
    statik.querySelectorAll('nav[data-locale]').forEach(function (n) { n.hidden = n.dataset.locale !== (zh ? 'zh' : 'en'); });
    if (page && page.dataset.place === 'not_found') {
      var missing = page.querySelector('[data-missing-route]');
      if (missing) missing.textContent = route();
    }
    statik.lang = zh ? 'zh-CN' : 'en';
    document.documentElement.lang = statik.lang;
    requestFonts(page, false);
    var heading = page && page.querySelector('h1');
    if (heading && heading.nextElementSibling !== note) heading.insertAdjacentElement('afterend', note);
    loadNotice();
    opening(page);
  }
  var observer = new MutationObserver(function () { attempt(); });
  function attempt() {
    if (busy || statik.hidden) return;
    var mirror = document.querySelector('[data-omui="mirror"]'), page = selected();
    if (mirror) { mirror.inert = true; mirror.setAttribute('aria-hidden', 'true'); }
    if (!window.omuiHasPresented || !mirror || !page || !window.omui) return;
    var root = 'main/body/view/' + page.dataset.place;
    var live = mirror.querySelector('[data-omui-id="' + (page.dataset.liveRoot || root) + '"]');
    if (!live) return;
    if (page.dataset.liveName && (live.getAttribute('aria-label') || live.textContent) !== page.dataset.liveName) return;
    busy = true;
    var active = document.activeElement;
    // Fragment navigation can put DOM focus on body; keep the clicked nav destination.
    var target = (statik.contains(active) ? active.getAttribute('data-omui-target') : null) || focusTarget;
    var y = statik.scrollTop;
    var restore = y > 0 ? window.omui.act(root, 'scroll_to', {args:{x:0,y:y}}) : Promise.resolve();
    Promise.resolve(restore).catch(function () {}).then(function () {
      observer.disconnect();
      statik.hidden = true;
      statik.inert = true;
      releaseFonts();
      mirror.inert = false;
      mirror.removeAttribute('aria-hidden');
      if (target) {
        var node = mirror.querySelector('[data-omui-id="' + target + '"]');
        if (node) node.focus();
      }
      window.omuiStaticHandedOff = true;
      statik.removeEventListener('focusin', rememberFocus);
      statik.removeEventListener('click', rememberFocus);
      window.removeEventListener('hashchange', show);
      window.removeEventListener('resize', show);
      window.removeEventListener('omui-load', loading);
      if (reduceQuery && reduceQuery.removeEventListener) reduceQuery.removeEventListener('change', show);
      clearPreview();
    });
  }
  window.addEventListener('hashchange', show);
  window.addEventListener('resize', show);
  if (reduceQuery && reduceQuery.addEventListener) reduceQuery.addEventListener('change', show);
  observer.observe(document.body, {childList:true, subtree:true});
  show();
  attempt();
})();
