// Runs once before wasm. The static view stays readable until the current
// route has both presented pixels and a populated semantic mirror.
(function () {
  var statik = document.getElementById('omui-static');
  if (!statik) return;
  var busy = false;
  var focusTarget = null;
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
  }
  var observer = new MutationObserver(function () { attempt(); });
  function attempt() {
    if (busy || statik.hidden) return;
    var mirror = document.querySelector('[data-omui="mirror"]'), page = selected();
    if (mirror) { mirror.inert = true; mirror.setAttribute('aria-hidden', 'true'); }
    if (!window.omuiHasPresented || !mirror || !page || !window.omui) return;
    var root = 'main/body/view/' + page.dataset.place;
    if (!mirror.querySelector('[data-omui-id="' + (page.dataset.liveRoot || root) + '"]')) return;
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
    });
  }
  window.addEventListener('hashchange', show);
  observer.observe(document.body, {childList:true, subtree:true});
  show();
  attempt();
})();
