// Injected into every page to prove the extension loaded.
(function () {
  var badge = document.createElement('div');
  badge.textContent = 'iwser extension active ✔';
  badge.style.cssText =
    'position:fixed;top:10px;right:10px;z-index:2147483647;' +
    'background:#1a73e8;color:#fff;font:12px/1.4 "Segoe UI",sans-serif;' +
    'padding:6px 10px;border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,.35)';
  (document.body || document.documentElement).appendChild(badge);
  setTimeout(function () { badge.remove(); }, 4000);
})();
