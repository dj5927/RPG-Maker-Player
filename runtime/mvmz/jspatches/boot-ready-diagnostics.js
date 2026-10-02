(function() {
  'use strict';
  var ticks = 0;
  var lifecycleInstalled = false;

  function wrapMethod(owner, name, label) {
    if (!owner || typeof owner[name] !== 'function' || owner[name].__mkxpBootWrapped) return;
    var original = owner[name];
    var wrapped = function() {
      console.log('[MKXP BOOT] ENTER ' + label);
      var result = original.apply(this, arguments);
      console.log('[MKXP BOOT] EXIT ' + label);
      return result;
    };
    wrapped.__mkxpBootWrapped = true;
    owner[name] = wrapped;
  }

  var lifecycleTimer = setInterval(function() {
    try {
      if (lifecycleInstalled) return;
      if (typeof Scene_Map === 'undefined' || typeof Game_Map === 'undefined') return;
      wrapMethod(Scene_Map.prototype, 'isReady', 'Scene_Map.isReady');
      wrapMethod(Scene_Map.prototype, 'onMapLoaded', 'Scene_Map.onMapLoaded');
      wrapMethod(Scene_Map.prototype, 'createDisplayObjects', 'Scene_Map.createDisplayObjects');
      wrapMethod(Scene_Map.prototype, 'start', 'Scene_Map.start');
      wrapMethod(Game_Map.prototype, 'setup', 'Game_Map.setup');
      if (typeof Spriteset_Map !== 'undefined') {
        wrapMethod(Spriteset_Map.prototype, 'createLowerLayer', 'Spriteset_Map.createLowerLayer');
      }
      lifecycleInstalled = true;
      clearInterval(lifecycleTimer);
      console.log('[MKXP BOOT] lifecycle hooks installed');
    } catch (e) {
      console.log('[MKXP BOOT] lifecycle hook error: ' + e);
    }
  }, 100);
  var timer = setInterval(function() {
    ticks++;
    try {
      var db = (typeof DataManager !== 'undefined' && DataManager.isDatabaseLoaded) ? DataManager.isDatabaseLoaded() : 'na';
      var mapLoaded = (typeof DataManager !== 'undefined' && DataManager.isMapLoaded) ? DataManager.isMapLoaded() : 'na';
      var dataMap = (typeof $dataMap !== 'undefined') ? !!$dataMap : 'na';
      var gf = (typeof Graphics !== 'undefined' && Graphics.isFontLoaded) ? Graphics.isFontLoaded('GameFont') : 'na';
      var fcr = (window.__mkxpFontCompatReady && window.__mkxpFontCompatReady.GameFont) ? true : false;
      var ir = (typeof ImageManager !== 'undefined' && ImageManager.isReady) ? ImageManager.isReady() : 'na';
      var fs = (document.fonts && document.fonts.status) ? document.fonts.status : 'na';
      var fc = (document.fonts && document.fonts.check) ? document.fonts.check('10px GameFont') : 'na';
      var scene = (typeof SceneManager !== 'undefined' && SceneManager._scene && SceneManager._scene.constructor) ? SceneManager._scene.constructor.name : 'none';
      var nextScene = (typeof SceneManager !== 'undefined' && SceneManager._nextScene && SceneManager._nextScene.constructor) ? SceneManager._nextScene.constructor.name : 'none';
      var started = (typeof SceneManager !== 'undefined' && SceneManager._scene) ? !!SceneManager._scene._started : false;
      var sceneMapLoaded = (typeof SceneManager !== 'undefined' && SceneManager._scene && SceneManager._scene._mapLoaded !== undefined) ? SceneManager._scene._mapLoaded : 'na';
      var cwd = (typeof process !== 'undefined' && process.cwd) ? process.cwd() : 'na';
      var main = (typeof process !== 'undefined' && process.mainModule) ? process.mainModule.filename : 'na';
      var save = (typeof StorageManager !== 'undefined' && StorageManager.localFileDirectoryPath) ? StorageManager.localFileDirectoryPath() : 'na';
      console.log('[MKXP BOOT] db=' + db + ' mapLoaded=' + mapLoaded + ' dataMap=' + dataMap + ' imageReady=' + ir + ' gamefont=' + gf + ' fontCompatReady=' + fcr + ' fonts.status=' + fs + ' fonts.check=' + fc + ' scene=' + scene + ' sceneMapLoaded=' + sceneMapLoaded + ' next=' + nextScene + ' started=' + started + ' cwd=' + cwd + ' main=' + main + ' save=' + save);

      if (ir === false && typeof ImageManager !== 'undefined' && ImageManager._imageCache && ImageManager._imageCache._items) {
        var shown = 0;
        var items = ImageManager._imageCache._items;
        for (var key in items) {
          if (!Object.prototype.hasOwnProperty.call(items, key)) continue;
          var entry = items[key];
          var bmp = entry && entry.bitmap;
          if (!bmp || !bmp.isReady || bmp.isReady() || (bmp.isRequestOnly && bmp.isRequestOnly())) continue;
          console.log('[MKXP BOOT] pendingBitmap key=' + key + ' url=' + (bmp._url || '?') + ' state=' + (bmp._loadingState || '?') + ' requestOnly=' + (!!(bmp.isRequestOnly && bmp.isRequestOnly())) + ' error=' + (!!(bmp.isError && bmp.isError())));
          shown++;
          if (shown >= 20) break;
        }
      }
      if (scene && scene !== 'none' && started) clearInterval(timer);
    } catch (e) {
      console.log('[MKXP BOOT] diagnostic error: ' + e);
    }
    if (ticks >= 30) clearInterval(timer);
  }, 1000);
})();
