#include "ui_sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <ostream>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> steamSoundDirectories() {
  std::vector<fs::path> dirs;
  auto addEnv = [&](const char* name) {
    if (const char* value = std::getenv(name); value && *value)
      dirs.emplace_back(fs::path(value) / "steamui/sounds");
  };
  addEnv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
  addEnv("STEAM_INSTALL_PATH");
  if (const char* home = std::getenv("HOME"); home && *home) {
    const fs::path h(home);
    dirs.emplace_back(h / ".local/share/Steam/steamui/sounds");
    dirs.emplace_back(h / ".steam/steam/steamui/sounds");
    dirs.emplace_back(h / ".steam/root/steamui/sounds");
  }
  dirs.emplace_back("/usr/lib/steam/steamui/sounds");
  return dirs;
}

}  // namespace

UiSoundPlayer::~UiSoundPlayer() {
  if (device_) SDL_CloseAudioDevice(device_);
}

bool UiSoundPlayer::loadWav(const char* path, std::vector<Uint8>& output) {
  SDL_AudioSpec sourceSpec{};
  Uint8* sourceBuffer = nullptr;
  Uint32 sourceLength = 0;
  if (!SDL_LoadWAV(path, &sourceSpec, &sourceBuffer, &sourceLength)) return false;

  SDL_AudioCVT cvt{};
  const int build = SDL_BuildAudioCVT(&cvt,
                                      sourceSpec.format, sourceSpec.channels, sourceSpec.freq,
                                      spec_.format, spec_.channels, spec_.freq);
  if (build < 0) {
    SDL_FreeWAV(sourceBuffer);
    return false;
  }
  if (build == 0) {
    output.assign(sourceBuffer, sourceBuffer + sourceLength);
    SDL_FreeWAV(sourceBuffer);
    return true;
  }

  cvt.len = static_cast<int>(sourceLength);
  cvt.buf = static_cast<Uint8*>(SDL_malloc(static_cast<std::size_t>(cvt.len) * cvt.len_mult));
  if (!cvt.buf) {
    SDL_FreeWAV(sourceBuffer);
    return false;
  }
  SDL_memcpy(cvt.buf, sourceBuffer, sourceLength);
  SDL_FreeWAV(sourceBuffer);
  if (SDL_ConvertAudio(&cvt) != 0) {
    SDL_free(cvt.buf);
    return false;
  }
  output.assign(cvt.buf, cvt.buf + cvt.len_cvt);
  SDL_free(cvt.buf);
  return true;
}

std::vector<Uint8> UiSoundPlayer::synth(float seconds, float startHz, float endHz, float gain) const {
  const int frames = std::max(1, static_cast<int>(spec_.freq * seconds));
  std::vector<float> samples(static_cast<std::size_t>(frames) * 2);
  float phase = 0.0f;
  constexpr float twoPi = 6.2831853071795864769f;
  for (int i = 0; i < frames; ++i) {
    const float t = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 0.0f;
    const float hz = startHz + (endHz - startHz) * t;
    phase += twoPi * hz / static_cast<float>(spec_.freq);
    const float attack = std::min(1.0f, t * 18.0f);
    const float decay = std::pow(std::max(0.0f, 1.0f - t), 2.2f);
    const float sample = std::sin(phase) * gain * attack * decay;
    samples[static_cast<std::size_t>(i) * 2] = sample;
    samples[static_cast<std::size_t>(i) * 2 + 1] = sample;
  }
  std::vector<Uint8> bytes(samples.size() * sizeof(float));
  SDL_memcpy(bytes.data(), samples.data(), bytes.size());
  return bytes;
}

bool UiSoundPlayer::init(std::ostream& log) {
  SDL_AudioSpec desired{};
  desired.freq = 48000;
  desired.format = AUDIO_F32SYS;
  desired.channels = 2;
  desired.samples = 512;
  desired.callback = nullptr;
  device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &spec_, 0);
  if (!device_) {
    log << "ui sound disabled | SDL_OpenAudioDevice failed: " << SDL_GetError() << '\n';
    return false;
  }

  const std::array<const char*, 4> names{{
    "deck_ui_misc_10.wav",
    "deck_ui_bumper_end_02.wav",
    "deck_ui_side_menu_fly_in.wav",
    "deck_ui_side_menu_fly_out.wav",
  }};
  for (const auto& dir : steamSoundDirectories()) {
    std::error_code ec;
    bool complete = true;
    for (const char* name : names) {
      if (!fs::is_regular_file(dir / name, ec)) {
        complete = false;
        break;
      }
    }
    if (!complete) continue;

    std::array<std::vector<Uint8>, 4> loaded;
    bool ok = true;
    for (std::size_t i = 0; i < names.size(); ++i) {
      const std::string file = (dir / names[i]).string();
      if (!loadWav(file.c_str(), loaded[i])) {
        ok = false;
        break;
      }
    }
    if (ok) {
      clips_ = std::move(loaded);
      usingSteamSounds_ = true;
      log << "ui sound ready | source=" << dir.string() << '\n';
      break;
    }
  }

  if (!usingSteamSounds_) {
    clips_[0] = synth(0.045f, 920.0f, 1040.0f, 0.18f);
    clips_[1] = synth(0.075f, 1450.0f, 780.0f, 0.22f);
    clips_[2] = synth(0.155f, 340.0f, 880.0f, 0.16f);
    clips_[3] = synth(0.155f, 820.0f, 300.0f, 0.16f);
    log << "ui sound ready | source=builtin-fallback\n";
  }

  SDL_PauseAudioDevice(device_, 0);
  return true;
}

void UiSoundPlayer::play(UiSoundKind kind) {
  if (!device_) return;
  const auto& clip = clips_[static_cast<std::size_t>(kind)];
  if (clip.empty()) return;
  SDL_ClearQueuedAudio(device_);
  SDL_QueueAudio(device_, clip.data(), static_cast<Uint32>(clip.size()));
}
