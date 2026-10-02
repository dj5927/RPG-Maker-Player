(function() {
  'use strict';

  if (typeof require !== 'function' || typeof process === 'undefined') return;

  var marker = process.env.MKXP_MVMZ_COMPAT_PROBE_FILE || '';
  if (!marker) return;

  var fs;
  try {
    fs = require('fs');
  } catch (_) {
    return;
  }

  var started = Date.now();
  var finished = false;
  var requireWebGL = process.env.MKXP_MVMZ_PROBE_REQUIRE_WEBGL === '1';

  function clean(value) {
    return String(value == null ? '' : value)
      .replace(/[\r\n|]+/g, ' ')
      .replace(/\s+/g, ' ')
      .slice(0, 900);
  }

  function quitSoon() {
    setTimeout(function() {
      try {
        if (typeof nw !== 'undefined' && nw.App && nw.App.quit) {
          nw.App.quit();
          return;
        }
      } catch (_) {}
      try {
        var gui = require('nw.gui');
        if (gui && gui.App && gui.App.quit) {
          gui.App.quit();
          return;
        }
      } catch (_) {}
      try { window.close(); } catch (_) {}
    }, 40);
  }

  function finish(state, detail) {
    if (finished) return;
    finished = true;
    try {
      fs.writeFileSync(marker, clean(state) + '|' + clean(detail) + '\n');
    } catch (_) {}
    quitSoon();
  }

  function runtimeError(event) {
    if (finished || !event) return;
    var message = '';
    try {
      if (event.error) {
        message = event.error.stack || event.error.message || String(event.error);
      } else if (event.message) {
        message = event.message;
      }
    } catch (_) {}
    if (message) finish('FAIL', 'js-error=' + message);
  }

  try {
    window.addEventListener('error', runtimeError, true);
    window.addEventListener('unhandledrejection', function(event) {
      if (finished) return;
      var reason = '';
      try {
        reason = event && event.reason
          ? (event.reason.stack || event.reason.message || String(event.reason))
          : 'unknown rejection';
      } catch (_) {
        reason = 'unknown rejection';
      }
      finish('FAIL', 'unhandled-rejection=' + reason);
    });
  } catch (_) {}

  function inspectRenderer() {
    try {
      if (typeof Graphics === 'undefined') return null;
      var renderer = Graphics._renderer || null;
      if (!renderer && Graphics.app && Graphics.app.renderer) renderer = Graphics.app.renderer;
      if (!renderer && Graphics._app && Graphics._app.renderer) renderer = Graphics._app.renderer;
      if (!renderer) return null;

      var webgl = false;
      try {
        if (typeof Graphics.isWebGL === 'function') webgl = !!Graphics.isWebGL();
      } catch (_) {}
      if (!webgl) {
        try {
          webgl = !!(renderer.gl ||
            (renderer.context && (renderer.context.gl || renderer.context.webGLVersion)));
        } catch (_) {}
      }
      return { webgl: webgl, name: webgl ? 'webgl' : 'canvas' };
    } catch (_) {
      return null;
    }
  }

  var timer = setInterval(function() {
    if (finished) {
      clearInterval(timer);
      return;
    }

    var elapsed = Date.now() - started;
    var url = '';
    try { url = String(window.location && window.location.href || ''); } catch (_) {}
    if (url.indexOf('chrome-error://') === 0) {
      finish('FAIL', 'chrome-error url=' + url);
      return;
    }

    var renderer = inspectRenderer();
    if (renderer) {
      if (requireWebGL && !renderer.webgl) {
        finish('FAIL', 'renderer=canvas webgl-required elapsed=' + elapsed);
      } else {
        finish('PASS', 'renderer=' + renderer.name + ' elapsed=' + elapsed + ' url=' + url);
      }
      return;
    }

    if (elapsed >= 7000) {
      finish('PARTIAL', 'renderer-unready elapsed=' + elapsed + ' url=' + url);
    }
  }, 100);
})();
