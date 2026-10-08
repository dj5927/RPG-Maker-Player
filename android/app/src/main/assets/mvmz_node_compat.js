(function (g) {
  'use strict';
  if (typeof g.require === 'function' && g.require.__rpgmpNodeCompat) return;
  var bridge = g.__RPGMP_NODE__;
  if (!bridge) return;

  function normalizePath(value) {
    var raw = String(value == null ? '' : value).replace(/\\/g, '/');
    var absolute = raw.charAt(0) === '/';
    var trailing = raw.length > 1 && raw.charAt(raw.length - 1) === '/';
    var out = [];
    raw.split('/').forEach(function (part) {
      if (!part || part === '.') return;
      if (part === '..') { if (out.length) out.pop(); return; }
      out.push(part);
    });
    var body = out.join('/');
    var result = absolute ? '/' + body : (body || '.');
    if (trailing && result !== '/' && result !== '.') result += '/';
    return result;
  }
  function dirname(value) {
    var p = normalizePath(value).replace(/\/$/, '');
    var i = p.lastIndexOf('/');
    if (i < 0) return '.';
    return i === 0 ? '/' : p.substring(0, i);
  }
  function basename(value, ext) {
    var p = normalizePath(value).replace(/\/$/, '');
    var base = p.substring(p.lastIndexOf('/') + 1);
    if (ext && base.endsWith(String(ext))) base = base.substring(0, base.length - String(ext).length);
    return base;
  }
  function join() {
    return normalizePath(Array.prototype.slice.call(arguments).filter(function (x) {
      return x != null && String(x) !== '';
    }).join('/'));
  }
  function extname(value) {
    var base = basename(value), i = base.lastIndexOf('.');
    return i > 0 ? base.substring(i) : '';
  }
  var pathModule = {
    sep: '/', delimiter: ':', normalize: normalizePath, join: join,
    dirname: dirname, basename: basename, extname: extname,
    isAbsolute: function (p) { return String(p || '').replace(/\\/g, '/').charAt(0) === '/'; },
    resolve: function () {
      var parts = Array.prototype.slice.call(arguments), current = '/';
      parts.forEach(function (part) {
        part = String(part == null ? '' : part).replace(/\\/g, '/');
        if (!part) return;
        current = part.charAt(0) === '/' ? part : join(current, part);
      });
      return normalizePath(current);
    },
    relative: function (from, to) {
      var a = normalizePath(from).replace(/\/$/, '').replace(/^\//, '').split('/');
      var b = normalizePath(to).replace(/\/$/, '').replace(/^\//, '').split('/');
      while (a.length && b.length && a[0] === b[0]) { a.shift(); b.shift(); }
      return a.map(function () { return '..'; }).concat(b).join('/');
    }
  };

  function decodeBase64(text) {
    var bin = atob(String(text || ''));
    var out = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i) & 255;
    return out;
  }
  function encodeBase64(data) {
    var bin = '', chunk = 0x8000;
    for (var o = 0; o < data.length; o += chunk) {
      var end = Math.min(data.length, o + chunk), part = '';
      for (var i = o; i < end; i++) part += String.fromCharCode(data[i]);
      bin += part;
    }
    return btoa(bin);
  }
  function normalizeEncoding(value) {
    var enc = String(value || 'utf8').toLowerCase().replace(/[-_]/g, '');
    if (enc === 'utf' || enc === 'utf8') return 'utf8';
    if (enc === 'binary') return 'latin1';
    return enc;
  }
  var encoder = new TextEncoder(), decoder = new TextDecoder('utf-8');
  class BufferCompat extends Uint8Array {
    static from(value, encoding) {
      if (typeof value === 'string') {
        var enc = normalizeEncoding(encoding);
        if (enc === 'base64') return new BufferCompat(decodeBase64(value));
        if (enc === 'hex') {
          var hex = value.replace(/\s+/g, ''), out = new BufferCompat(Math.floor(hex.length / 2));
          for (var i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16) || 0;
          return out;
        }
        if (enc === 'latin1' || enc === 'ascii') {
          var bytes = new BufferCompat(value.length);
          for (var j = 0; j < bytes.length; j++) bytes[j] = value.charCodeAt(j) & 255;
          return bytes;
        }
        return new BufferCompat(encoder.encode(value));
      }
      if (value instanceof ArrayBuffer) return new BufferCompat(new Uint8Array(value));
      if (ArrayBuffer.isView(value)) return new BufferCompat(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
      return new BufferCompat(value == null ? 0 : value);
    }
    static alloc(size, fill, encoding) {
      var out = new BufferCompat(Math.max(0, Number(size) || 0));
      if (fill != null) {
        if (typeof fill === 'string') {
          var pattern = BufferCompat.from(fill, encoding);
          for (var i = 0; pattern.length && i < out.length; i++) out[i] = pattern[i % pattern.length];
        } else out.fill(Number(fill) & 255);
      }
      return out;
    }
    static allocUnsafe(size) { return BufferCompat.alloc(size); }
    static isBuffer(value) { return value instanceof BufferCompat; }
    static byteLength(value, encoding) { return BufferCompat.from(value, encoding).length; }
    static concat(list, totalLength) {
      var src = Array.isArray(list) ? list.map(function (x) { return BufferCompat.from(x); }) : [];
      var length = totalLength == null ? src.reduce(function (n, x) { return n + x.length; }, 0) : Number(totalLength) || 0;
      var out = new BufferCompat(length), offset = 0;
      src.forEach(function (x) { if (offset < out.length) { var p = x.subarray(0, out.length - offset); out.set(p, offset); offset += p.length; } });
      return out;
    }
    toString(encoding, start, end) {
      var enc = normalizeEncoding(encoding), a = Math.max(0, Number(start) || 0);
      var b = end == null ? this.length : Math.max(a, Math.min(this.length, Number(end) || 0));
      var view = this.subarray(a, b);
      if (enc === 'base64') return encodeBase64(view);
      if (enc === 'hex') { var h = ''; for (var i = 0; i < view.length; i++) h += view[i].toString(16).padStart(2, '0'); return h; }
      if (enc === 'latin1' || enc === 'ascii') { var s = ''; for (var j = 0; j < view.length; j++) s += String.fromCharCode(view[j] & (enc === 'ascii' ? 127 : 255)); return s; }
      return decoder.decode(view);
    }
  }

  function optionEncoding(options) { return typeof options === 'string' ? options : (options && options.encoding); }
  function dataToBuffer(data, options) {
    if (typeof data === 'string') return BufferCompat.from(data, optionEncoding(options));
    return BufferCompat.from(data);
  }
  var nextFd = 100;
  var fsModule = {
    readFileSync: function (p, options) {
      var raw = bridge.readBase64(String(p));
      if (raw == null) throw new Error("ENOENT: no such file, open '" + p + "'");
      var buf = BufferCompat.from(raw, 'base64'), enc = optionEncoding(options);
      return enc ? buf.toString(enc) : buf;
    },
    existsSync: function (p) { return !!bridge.exists(String(p)); },
    mkdirSync: function (p) { if (!bridge.mkdir(String(p))) throw new Error("EACCES: mkdir '" + p + "'"); },
    writeFileSync: function (p, data, options) {
      var buf = dataToBuffer(data, options);
      if (!bridge.writeBase64(String(p), buf.toString('base64'))) throw new Error("EACCES: write '" + p + "'");
    },
    appendFileSync: function (p, data, options) {
      var old = fsModule.existsSync(p) ? fsModule.readFileSync(p) : BufferCompat.alloc(0);
      fsModule.writeFileSync(p, BufferCompat.concat([old, dataToBuffer(data, options)]));
    },
    unlinkSync: function (p) { if (!bridge.remove(String(p))) throw new Error("ENOENT: unlink '" + p + "'"); },
    readdirSync: function (p) { try { return JSON.parse(bridge.list(String(p)) || '[]'); } catch (_) { return []; } },
    statSync: function (p) {
      var t = Number(bridge.statType(String(p)) || 0);
      if (!t) throw new Error("ENOENT: stat '" + p + "'");
      return { isFile: function () { return t === 1; }, isDirectory: function () { return t === 2; }, size: 0 };
    },
    renameSync: function (a, b) { if (!bridge.rename(String(a), String(b))) throw new Error('rename failed'); },
    openSync: function (p, flags) {
      flags = String(flags || 'r');
      if (/[wa+]/.test(flags)) { if (!bridge.touch(String(p), flags.indexOf('w') >= 0)) throw new Error('open failed'); }
      else if (!fsModule.existsSync(p)) throw new Error("ENOENT: open '" + p + "'");
      return nextFd++;
    },
    closeSync: function () {},
    createWriteStream: function (p) {
      var chunks = [], ended = false, stream = {};
      stream.write = function (x) { if (ended) return false; chunks.push(dataToBuffer(x)); return true; };
      stream.end = function (x) { if (ended) return; if (x != null && String(x).length) chunks.push(dataToBuffer(x)); ended = true; fsModule.writeFileSync(p, BufferCompat.concat(chunks)); };
      stream.on = stream.once = function () { return stream; };
      return stream;
    }
  };
  fsModule.readFile = function (p, o, cb) { if (typeof o === 'function') { cb = o; o = undefined; } try { var v = fsModule.readFileSync(p, o); setTimeout(function () { cb && cb(null, v); }, 0); } catch (e) { setTimeout(function () { cb && cb(e); }, 0); } };
  fsModule.writeFile = function (p, d, o, cb) { if (typeof o === 'function') { cb = o; o = undefined; } try { fsModule.writeFileSync(p, d, o); setTimeout(function () { cb && cb(null); }, 0); } catch (e) { setTimeout(function () { cb && cb(e); }, 0); } };
  fsModule.mkdir = function (p, o, cb) { if (typeof o === 'function') cb = o; try { fsModule.mkdirSync(p); setTimeout(function () { cb && cb(null); }, 0); } catch (e) { setTimeout(function () { cb && cb(e); }, 0); } };
  fsModule.unlink = function (p, cb) { try { fsModule.unlinkSync(p); setTimeout(function () { cb && cb(null); }, 0); } catch (e) { setTimeout(function () { cb && cb(e); }, 0); } };
  fsModule.rename = function (a, b, cb) { try { fsModule.renameSync(a, b); setTimeout(function () { cb && cb(null); }, 0); } catch (e) { setTimeout(function () { cb && cb(e); }, 0); } };

  var winStub = {
    showDevTools: function () { return null; }, isDevToolsOpen: function () { return false; },
    close: function () {}, focus: function () {}, blur: function () {}, show: function () {}, hide: function () {},
    minimize: function () {}, maximize: function () {}, restore: function () {}, moveTo: function () {}, moveBy: function () {},
    resizeTo: function () {}, resizeBy: function () {}
  };
  var nwModule = {
    Window: { get: function () { return winStub; }, open: function (_u, _o, cb) { if (cb) cb(winStub); return winStub; } },
    App: { argv: [], fullArgv: [], quit: function () {}, dataPath: '/' },
    Shell: { openExternal: function () {}, openItem: function () {}, showItemInFolder: function () {} }
  };
  var childProcess = { exec: function (_cmd, options, cb) { if (typeof options === 'function') cb = options; if (cb) setTimeout(function () { cb(null, '', ''); }, 0); return { pid: 0, kill: function () {}, on: function () { return this; }, once: function () { return this; } }; } };
  var greenworks = {
    _version: 'rpgmp-android-stub', initAPI: function () { return false; }, isSteamRunning: function () { return false; },
    isCloudEnabledForUser: function () { return false; }, isCloudEnabled: function () { return false; }, enableCloud: function () {},
    getSteamId: function () { return null; }, getAchievementNames: function () { return []; }, getNumberOfAchievements: function () { return 0; },
    getCurrentGameLanguage: function () { return 'koreana'; }, isGameOverlayEnabled: function () { return false; },
    activateGameOverlay: function () {}, activateGameOverlayToWebPage: function () {},
    activateAchievement: function (_n, ok) { if (ok) ok(); }, clearAchievement: function (_n, ok) { if (ok) ok(); },
    getAchievement: function (_n, ok) { if (ok) ok(false); }, getNumberOfPlayers: function (ok) { if (ok) ok(0); },
    getCloudQuota: function (ok) { if (ok) ok(0, 0); }, saveTextToFile: function (_f, _c, ok) { if (ok) ok(); },
    readTextFromFile: function (_f, _ok, fail) { if (fail) fail(new Error('Steam cloud unavailable on Android')); },
    on: function () { return greenworks; }, emit: function () { return false; }
  };

  var requireCompat = function (name) {
    var id = String(name || '').replace(/\\/g, '/');
    if (/^(?:\.\/)?js\/libs\/greenworks(?:\.js)?$/i.test(id)) return greenworks;
    if (id === 'fs' || id === 'node:fs') return fsModule;
    if (id === 'path' || id === 'node:path') return pathModule;
    if (id === 'buffer' || id === 'node:buffer') return { Buffer: BufferCompat, SlowBuffer: BufferCompat };
    if (id === 'nw.gui' || id === 'nw') return nwModule;
    if (id === 'child_process' || id === 'node:child_process') return childProcess;
    throw new Error('RPGMP Android require module not supported: ' + id);
  };
  requireCompat.__rpgmpNodeCompat = true;
  g.require = requireCompat;
  g.Buffer = g.Buffer || BufferCompat;
  g.process = g.process || {};
  try { g.process.mainModule = { filename: bridge.mainModuleFilename() || '/index.html' }; } catch (_) {}
  if (typeof g.process.cwd !== 'function') try { g.process.cwd = function () { return '/'; }; } catch (_) {}
  if (!g.process.platform) try { g.process.platform = 'linux'; } catch (_) {}
  if (!g.process.arch) try { g.process.arch = 'arm64'; } catch (_) {}
  if (!g.process.versions) try { g.process.versions = {}; } catch (_) {}
  if (typeof g.process.on !== 'function') try { g.process.on = function () { return g.process; }; } catch (_) {}
  if (typeof g.process.once !== 'function') try { g.process.once = function () { return g.process; }; } catch (_) {}
  if (typeof g.process.exit !== 'function') try { g.process.exit = function () {}; } catch (_) {}
  g.nw = g.nw || nwModule;
  console.log('[RPGMP NODE] NW.js compatibility active');
})(window);
