#pragma once

#include <SDL.h>

#include <memory>
#include <string>

namespace rpgmp {

double steamMouseAxisUnit(Sint16 raw);
bool steamMouseTriggerPressed(Sint16 raw, bool wasPressed);
bool steamMouseTouchscreenCandidate(bool directProperty, bool pointerProperty,
                                    const std::string& deviceName,
                                    double rawAspect, double displayAspect);

class SteamMouseBridge {
public:
  explicit SteamMouseBridge(bool controllerMouseMode);
  ~SteamMouseBridge();

  SteamMouseBridge(const SteamMouseBridge&) = delete;
  SteamMouseBridge& operator=(const SteamMouseBridge&) = delete;

  bool initialize(int displayWidth, int displayHeight, std::string& diagnostic);
  bool ready() const;
  bool controllerMode() const;

  void updateController(SDL_GameController* controller, Uint64 nowMs);
  bool pollTwoFingerTouch(std::string& source);
  bool handleSdlTouchEvent(const SDL_Event& event, std::string& source);
  void releaseControllerButtons();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rpgmp