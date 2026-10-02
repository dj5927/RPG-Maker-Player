(function() {
  'use strict';

  var ready = window.__mkxpFontCompatReady = window.__mkxpFontCompatReady || {};

  function log(text) {
    try { console.log('[MKXP FONT] ' + text); } catch (_) {}
  }

  if (typeof require !== 'function' || typeof FontFace === 'undefined' || !document.fonts) {
    log('compat unavailable require=' + (typeof require) + ' FontFace=' + (typeof FontFace));
    return;
  }

  var fs;
  var path;
  try {
    fs = require('fs');
    path = require('path');
  } catch (e) {
    log('node modules unavailable: ' + e);
    return;
  }

  var mainFile = '';
  try {
    mainFile = process.mainModule && process.mainModule.filename ? process.mainModule.filename : '';
  } catch (_) {}
  if (!mainFile) {
    log('main module path unavailable');
    return;
  }

  var webRoot = path.dirname(mainFile);
  var fontDir = path.join(webRoot, 'fonts');
  var globalFontDir = '';
  try { globalFontDir = process.env.MKXP_GLOBAL_FONT_DIR || ''; } catch (_) {}
  var cssPath = path.join(fontDir, 'gamefont.css');
  var cacheDir = path.join(process.cwd(), 'mkxp-fontcompat');
  var css = '';
  try {
    css = fs.readFileSync(cssPath, 'utf8');
  } catch (e) {
    log('gamefont.css unavailable; MZ/global fallback remains active: ' + e);
  }

  function resolveInDirectory(dir, requested) {
    if (!dir) return '';
    var direct = path.join(dir, requested);
    try {
      if (fs.existsSync(direct)) return direct;
    } catch (_) {}

    var wanted = String(path.basename(requested)).normalize('NFC').toLowerCase();
    try {
      var names = fs.readdirSync(dir);
      for (var i = 0; i < names.length; i++) {
        if (String(names[i]).normalize('NFC').toLowerCase() === wanted) {
          return path.join(dir, names[i]);
        }
      }
    } catch (_) {}
    return '';
  }

  function resolveFontFile(requested) {
    if (/^(?:data:|https?:|blob:)/i.test(requested)) return '';
    var local = resolveInDirectory(fontDir, requested);
    if (local) return local;
    var fallback = resolveInDirectory(globalFontDir, requested);
    if (fallback) {
      log('global fallback ' + requested + ' -> ' + fallback);
      return fallback;
    }
    return '';
  }

  function mimeFor(ext) {
    ext = String(ext || '').toLowerCase();
    if (ext === '.otf') return 'font/otf';
    if (ext === '.woff') return 'font/woff';
    if (ext === '.woff2') return 'font/woff2';
    return 'font/ttf';
  }

  function install(family, requested, index) {
    var source = resolveFontFile(requested);
    if (!source) {
      log(family + ' source unresolved: ' + requested);
      return;
    }

    var ext = path.extname(source) || '.ttf';
    var aliasName = 'mkxp_font_' + index + ext.toLowerCase();
    var aliasPath = path.join(cacheDir, aliasName);
    try {
      if (!fs.existsSync(cacheDir)) fs.mkdirSync(cacheDir);
      if (fs.existsSync(aliasPath)) fs.unlinkSync(aliasPath);
      try {
        fs.linkSync(source, aliasPath);
      } catch (_) {
        fs.copyFileSync(source, aliasPath);
      }
      log(family + ' alias ' + path.basename(source) + ' -> ' + aliasName);
    } catch (e) {
      log(family + ' alias failed: ' + e);
      return;
    }

    try {
      var bytes = fs.readFileSync(aliasPath);
      var dataUrl = 'data:' + mimeFor(ext) + ';base64,' + bytes.toString('base64');
      var face = new FontFace(family, 'url("' + dataUrl + '")');
      document.fonts.add(face);
      face.load().then(function() {
        ready[family] = true;
        var ok = false;
        try { ok = document.fonts.check('10px "' + family + '"'); } catch (_) {}
        log(family + ' loaded alias=' + aliasName + ' status=' + face.status + ' check=' + ok + ' readyOverride=true');
      }).catch(function(error) {
        log(family + ' load failed alias=' + aliasName + ' error=' + error);
      });
    } catch (e) {
      log(family + ' FontFace failed: ' + e);
    }
  }

  var blocks = css.match(/@font-face\s*\{[^}]*\}/ig) || [];
  var found = 0;
  for (var i = 0; i < blocks.length; i++) {
    var familyMatch = /font-family\s*:\s*["']?([^;"'}]+)["']?\s*;/i.exec(blocks[i]);
    var sourceMatch = /src\s*:\s*[^;]*url\(\s*["']?([^"')]+)["']?\s*\)/i.exec(blocks[i]);
    if (!familyMatch || !sourceMatch) continue;
    var family = String(familyMatch[1]).replace(/^\s+|\s+$/g, '');
    var requested = String(sourceMatch[1]).replace(/^\s+|\s+$/g, '');
    if (!family || !requested) continue;
    found++;
    install(family, requested, found);
  }
  if (!found) {
    log('no local @font-face entries found; waiting for runtime font requests');
  }

  var mzHookTimer = setInterval(function() {
    try {
      if (typeof FontManager === 'undefined' || typeof FontManager._startLoading !== 'function') return;
      if (FontManager._startLoading.__mkxpGlobalFontFallback) {
        clearInterval(mzHookTimer);
        return;
      }
      var originalStartLoading = FontManager._startLoading;
      var mzAliasIndex = 1000;
      var wrappedStartLoading = function(family, url) {
        var requested = String(url || '').replace(/^fonts[\/]/i, '');
        try { requested = decodeURIComponent(requested); } catch (_) {}
        var local = resolveInDirectory(fontDir, requested);
        if (local) return originalStartLoading.apply(this, arguments);

        var fallback = resolveInDirectory(globalFontDir, requested);
        if (!fallback) return originalStartLoading.apply(this, arguments);

        log('MZ global fallback ' + family + ' ' + requested + ' -> ' + fallback);
        var bytes = fs.readFileSync(fallback);
        var ext = path.extname(fallback) || '.ttf';
        var dataUrl = 'data:' + mimeFor(ext) + ';base64,' + bytes.toString('base64');
        var face = new FontFace(family, 'url("' + dataUrl + '")');
        this._urls[family] = url;
        this._states[family] = 'loading';
        var manager = this;
        face.load().then(function() {
          document.fonts.add(face);
          manager._states[family] = 'loaded';
          ready[family] = true;
          log('MZ global loaded family=' + family + ' alias=' + (++mzAliasIndex));
        }).catch(function(error) {
          manager._states[family] = 'error';
          log('MZ global load failed family=' + family + ' error=' + error);
        });
      };
      wrappedStartLoading.__mkxpGlobalFontFallback = true;
      FontManager._startLoading = wrappedStartLoading;
      clearInterval(mzHookTimer);
      log('FontManager global fallback hook installed');
    } catch (e) {
      log('FontManager fallback hook error: ' + e);
    }
  }, 50);

  var hookTimer = setInterval(function() {
    try {
      if (typeof Graphics === 'undefined' || typeof Graphics.isFontLoaded !== 'function') return;
      if (Graphics.isFontLoaded.__mkxpFontCompat) {
        clearInterval(hookTimer);
        return;
      }
      var original = Graphics.isFontLoaded;
      var wrapped = function(name) {
        if (ready[name]) return true;
        return original.apply(this, arguments);
      };
      wrapped.__mkxpFontCompat = true;
      Graphics.isFontLoaded = wrapped;
      clearInterval(hookTimer);
      log('Graphics.isFontLoaded compatibility hook installed');
    } catch (e) {
      log('Graphics.isFontLoaded hook error: ' + e);
    }
  }, 50);
})();
