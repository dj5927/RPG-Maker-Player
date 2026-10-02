(function() {
  'use strict';

  var path = require('path');
  var fs = require('fs');
  var realExistsSync = fs.existsSync.bind(fs);
  var realReaddirSync = fs.readdirSync.bind(fs);

  function resolveCI(input) {
    if (typeof input !== 'string') return null;
    if (realExistsSync(input)) return input;
    var abs = path.resolve(input);
    var parsed = path.parse(abs);
    var current = parsed.root;
    var rest = abs.slice(parsed.root.length);
    var parts = rest.split(path.sep);
    for (var i = 0; i < parts.length; ++i) {
      var part = parts[i];
      if (!part) continue;
      var entries;
      try {
        entries = realReaddirSync(current || '.');
      } catch (e) {
        return null;
      }
      var wanted = part.toLowerCase();
      var actual = null;
      for (var j = 0; j < entries.length; ++j) {
        if (String(entries[j]).toLowerCase() === wanted) {
          actual = String(entries[j]);
          break;
        }
      }
      if (actual === null) return null;
      current = path.join(current, actual);
    }
    return realExistsSync(current) ? current : null;
  }

  function mapped(input) {
    var found = resolveCI(input);
    return found === null ? input : found;
  }

  var syncNames = [
    'accessSync', 'lstatSync', 'openSync', 'readFileSync', 'readlinkSync',
    'realpathSync', 'readdirSync', 'statSync'
  ];
  for (var s = 0; s < syncNames.length; ++s) {
    (function(name) {
      if (typeof fs[name] !== 'function') return;
      var original = fs[name];
      fs[name] = function() {
        var args = Array.prototype.slice.call(arguments);
        if (args.length && typeof args[0] === 'string') args[0] = mapped(args[0]);
        return original.apply(fs, args);
      };
    })(syncNames[s]);
  }

  fs.existsSync = function(input) {
    return realExistsSync(input) || resolveCI(input) !== null;
  };

  var callbackNames = ['access', 'lstat', 'open', 'readFile', 'readdir', 'stat'];
  for (var a = 0; a < callbackNames.length; ++a) {
    (function(name) {
      if (typeof fs[name] !== 'function') return;
      var original = fs[name];
      fs[name] = function() {
        var args = Array.prototype.slice.call(arguments);
        if (args.length && typeof args[0] === 'string') args[0] = mapped(args[0]);
        return original.apply(fs, args);
      };
    })(callbackNames[a]);
  }

  try {
    if (typeof chrome !== 'undefined' && chrome.webRequest && chrome.runtime) {
      chrome.webRequest.onBeforeRequest.addListener(function(details) {
        try {
          var url = new URL(details.url);
          var rel = decodeURIComponent(url.pathname.replace(/^\/+/, ''));
          var found = resolveCI(rel);
          if (found !== null) {
            var relative = path.relative(process.cwd(), found).split(path.sep).join('/');
            return { redirectUrl: chrome.runtime.getURL(relative) };
          }
        } catch (e) {}
        return {};
      }, { urls: [chrome.runtime.getURL('/*')] }, ['blocking']);
    }
  } catch (e) {}

  console.log('[MKXP] Legacy NW.js case-insensitive path fallback enabled');
})();

