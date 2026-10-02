#pragma once

#include <SDL.h>

#include <array>
#include <iosfwd>
#include <vector>

enum class UiSoundKind {
  Move = 0,
  Boundary = 1,
  SidebarIn = 2,
  SidebarOut = 3,
};

class UiSoundPlayer {
 public:
  UiSoundPlayer() = default;
  ~UiSoundPlayer();

  UiSoundPlayer(const UiSoundPlayer&) = delete;
  UiSoundPlayer& operator=(const UiSoundPlayer&) = delete;

  bool init(std::ostream& log);
  void play(UiSoundKind kind);
  bool usingSteamSounds() const { return usingSteamSounds_; }

 private:
  bool loadWav(const char* path, std::vector<Uint8>& output);
  std::vector<Uint8> synth(float seconds, float startHz, float endHz, float gain) const;

  SDL_AudioDeviceID device_ = 0;
  SDL_AudioSpec spec_{};
  std::array<std::vector<Uint8>, 4> clips_;
  bool usingSteamSounds_ = false;
};
