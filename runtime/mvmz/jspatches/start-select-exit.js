(function() {
  try { console.log("[MKXP] Start+Select confirmation hook loaded"); } catch (_) {}
  var startedAt = 0;
  var requested = false;
  var HOLD_MS = 300;
  var mvPadHeld = {};
  var loggedPads = {};

  function requestExitConfirmation() {
    if (requested) return;
    requested = true;
    try {
      console.log("[MKXP] Start+Select exit confirmation requested");
      var fs = require("fs");
      var target = process && process.env ? process.env.MKXP_EXIT_REQUEST_FILE : "";
      if (target) {
        fs.writeFileSync(target, "1\n");
        return;
      }
    } catch (e) {
      try { console.log("[MKXP] exit request write failed: " + e); } catch (_) {}
    }
    // Last-resort fallback if launched outside RPG Maker Player.
    try {
      if (typeof nw !== "undefined" && nw.App && nw.App.quit) nw.App.quit();
    } catch (e2) {}
  }

  function getPads() {
    try {
      if (navigator.getGamepads) return navigator.getGamepads() || [];
      if (navigator.webkitGetGamepads) return navigator.webkitGetGamepads() || [];
    } catch (e) {}
    return [];
  }

  function buttonPressed(buttons, index) {
    var button = buttons && buttons[index];
    return !!(button && (button.pressed || button.value > 0.5));
  }

  function rawPadHeld(pad) {
    if (!pad || !pad.buttons) return false;
    if (pad.mapping === "standard") {
      return buttonPressed(pad.buttons, 8) && buttonPressed(pad.buttons, 9);
    }
    // Old Chromium/NW.js can expose Linux/Steam virtual pads without the
    // standard remap. Common raw Back/Start pairs are 6/7 or 6/8.
    return (buttonPressed(pad.buttons, 6) && buttonPressed(pad.buttons, 7)) ||
           (buttonPressed(pad.buttons, 6) && buttonPressed(pad.buttons, 8)) ||
           (buttonPressed(pad.buttons, 8) && buttonPressed(pad.buttons, 9));
  }

  function observeMvPad(pad) {
    if (!pad) return;
    var index = typeof pad.index === "number" ? pad.index : 0;
    mvPadHeld[index] = rawPadHeld(pad);
    if (!loggedPads[index]) {
      loggedPads[index] = true;
      try {
        console.log("[MKXP] MV gamepad id=" + (pad.id || "?") +
                    " mapping=" + (pad.mapping || "raw") +
                    " buttons=" + (pad.buttons ? pad.buttons.length : 0));
      } catch (_) {}
    }
  }

  function installMvInputHook() {
    try {
      if (typeof Input === "undefined" || !Input._updateGamepadState || Input._mkxpExitHook) return false;
      var original = Input._updateGamepadState;
      Input._updateGamepadState = function(gamepad) {
        observeMvPad(gamepad);
        return original.apply(this, arguments);
      };
      Input._mkxpExitHook = true;
      console.log("[MKXP] MV Input._updateGamepadState hook installed");
      return true;
    } catch (e) {}
    return false;
  }

  function mvInputHeld() {
    try {
      for (var observed in mvPadHeld) {
        if (Object.prototype.hasOwnProperty.call(mvPadHeld, observed) && mvPadHeld[observed]) return true;
      }
      if (typeof Input !== "undefined" && Input._gamepadStates) {
        for (var key in Input._gamepadStates) {
          if (!Object.prototype.hasOwnProperty.call(Input._gamepadStates, key)) continue;
          var state = Input._gamepadStates[key];
          if (state && ((state[6] && state[7]) || (state[6] && state[8]) || (state[8] && state[9]))) return true;
        }
      }
    } catch (e) {}
    return false;
  }

  function browserGamepadHeld() {
    var pads = getPads();
    for (var i = 0; pads && i < pads.length; ++i) {
      var pad = pads[i];
      if (!pad || !pad.buttons) continue;
      observeMvPad(pad);
      if (rawPadHeld(pad)) return true;
    }
    return false;
  }

  function poll() {
    var held = browserGamepadHeld() || mvInputHeld();
    var now = Date.now();
    if (held) {
      if (!startedAt) startedAt = now;
      if (!requested && now - startedAt >= HOLD_MS) requestExitConfirmation();
    } else {
      startedAt = 0;
      requested = false;
    }
    if (typeof requestAnimationFrame === "function") requestAnimationFrame(poll);
  }

  if (!installMvInputHook()) {
    var installTimer = setInterval(function() {
      if (installMvInputHook()) clearInterval(installTimer);
    }, 50);
  }

  if (typeof requestAnimationFrame === "function") requestAnimationFrame(poll);
  else setInterval(poll, 16);
})();
