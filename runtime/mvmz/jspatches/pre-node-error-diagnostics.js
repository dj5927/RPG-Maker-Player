(function() {
  'use strict';
  function out(text) {
    try { console.log('[MKXP PRENODE] ' + text); } catch (_) {}
  }
  try { out('url=' + String(window.location.href) + ' require=' + (typeof require)); } catch (_) {}
  try {
    window.addEventListener('error', function(ev) {
      try {
        if (ev && ev.target && ev.target !== window) {
          out('resource error src=' + (ev.target.src || ev.target.href || '?'));
        } else if (ev && ev.message) {
          out('window error ' + ev.message + ' @ ' + (ev.filename || '?') + ':' + (ev.lineno || 0));
        }
      } catch (_) {}
    }, true);
  } catch (_) {}
})();
