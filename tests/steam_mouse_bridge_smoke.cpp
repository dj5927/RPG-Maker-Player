#include "steam_mouse_bridge.hpp"

#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (rpgmp::steamMouseAxisUnit(0) != 0.0) return 10;
  if (rpgmp::steamMouseAxisUnit(5000) != 0.0) return 11;
  if (!(rpgmp::steamMouseAxisUnit(10000) > 0.0)) return 12;
  if (!(rpgmp::steamMouseAxisUnit(-10000) < 0.0)) return 13;
  if (!(rpgmp::steamMouseAxisUnit(32767) > 0.99)) return 14;
  if (!(rpgmp::steamMouseAxisUnit(-32768) < -0.99)) return 15;

  if (rpgmp::steamMouseTriggerPressed(10000, false)) return 20;
  if (!rpgmp::steamMouseTriggerPressed(15000, false)) return 21;
  if (!rpgmp::steamMouseTriggerPressed(8000, true)) return 22;
  if (rpgmp::steamMouseTriggerPressed(5000, true)) return 23;

  if (!rpgmp::steamMouseTouchscreenCandidate(
        true, false, "Generic Direct Touch", 0.625, 1.6)) return 24;
  if (rpgmp::steamMouseTouchscreenCandidate(
        false, true, "Generic Pointer Device", 1.6, 1.6)) return 25;
  if (!rpgmp::steamMouseTouchscreenCandidate(
        false, false, "Elan Touchscreen", 0.625, 1.6)) return 26;
  if (!rpgmp::steamMouseTouchscreenCandidate(
        false, false, "Unknown MT Device", 0.625, 1.6)) return 27;
  if (rpgmp::steamMouseTouchscreenCandidate(
        true, false, "Valve Trackpad", 1.6, 1.6)) return 28;

  if (argc > 1 && std::string(argv[1]) == "--x11") {
    rpgmp::SteamMouseBridge bridge(true);
    std::string diagnostic;
    if (!bridge.initialize(1280, 800, diagnostic)) {
      std::cerr << "bridge init failed: " << diagnostic << "\n";
      return 30;
    }
    if (!bridge.ready() || !bridge.controllerMode()) return 31;
  }

  std::cout << "STEAM_MOUSE_BRIDGE_SMOKE_PASS\n";
  return 0;
}