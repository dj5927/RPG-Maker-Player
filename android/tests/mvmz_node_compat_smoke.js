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

console.log('MVMZ_NODE_COMPAT_SMOKE_PASS');
