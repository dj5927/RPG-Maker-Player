'use strict';

const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const asset = process.argv[2];
if (!asset) throw new Error('mvmz_node_compat.js path required');
const source = fs.readFileSync(asset, 'utf8');

const bridge = {
  mainModuleFilename: () => '/www/index.html',
  readBase64: () => null,
  exists: () => false,
  mkdir: () => true,
  writeBase64: () => true,
  remove: () => true,
  list: () => '[]',
  statType: () => 0,
  rename: () => true,
  touch: () => true
};
const window = { __RPGMP_NODE__: bridge };
const context = {
  window,
  console,
  TextEncoder,
  TextDecoder,
  Uint8Array,
  ArrayBuffer,
  setTimeout: fn => { fn(); return 0; },
  atob: s => Buffer.from(String(s), 'base64').toString('binary'),
  btoa: s => Buffer.from(String(s), 'binary').toString('base64')
};
vm.createContext(context);
vm.runInContext(source, context, { filename: asset });

const path = window.require('path');
assert.strictEqual(path.join('/www', 'save/'), '/www/save/');
assert.strictEqual(path.join('/www', 'ndata/'), '/www/ndata/');
assert.strictEqual(path.normalize('/www/save/'), '/www/save/');
assert.strictEqual(path.dirname('/www/index.html'), '/www');
assert.strictEqual(path.relative('/www/save/', '/www/save/file1.rpgsave'), 'file1.rpgsave');

const nw = window.require('nw.gui');
const nodeFs = window.require('fs');
assert.strictEqual(typeof nodeFs.readdir, 'function');
const originalList = bridge.list;
const originalStat = bridge.statType;
bridge.list = () => '["a.txt","b.txt"]';
bridge.statType = p => p === '/www/text' ? 2 : 0;
let dirCallback = null;
nodeFs.readdir('/www/text', (err, files) => { dirCallback = {err, files}; });
assert.strictEqual(dirCallback.err, null);
assert.deepStrictEqual(Array.from(dirCallback.files), ['a.txt', 'b.txt']);
let missingCode = null;
nodeFs.readdir('/www/missing', err => { missingCode = err && err.message; });
assert.ok(missingCode && missingCode.includes('ENOENT'));
bridge.list = originalList;
bridge.statType = originalStat;
const win = nw.Window.get();
assert.strictEqual(typeof win.on, 'function');
assert.strictEqual(typeof win.once, 'function');
assert.strictEqual(typeof win.removeListener, 'function');
assert.strictEqual(typeof win.removeAllListeners, 'function');
assert.strictEqual(typeof win.emit, 'function');
let focusCount = 0;
const onFocus = () => { focusCount += 1; };
assert.strictEqual(win.on('focus', onFocus), win);
assert.strictEqual(win.emit('focus'), true);
assert.strictEqual(focusCount, 1);
win.removeListener('focus', onFocus);
assert.strictEqual(win.emit('focus'), false);
let closeCount = 0;
assert.strictEqual(win.once('close', () => { closeCount += 1; }), win);
win.emit('close');
win.emit('close');
assert.strictEqual(closeCount, 1);

console.log('MVMZ_NODE_COMPAT_SMOKE_PASS');
