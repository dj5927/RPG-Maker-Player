#include "steam_mouse_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rpgmp {

double steamMouseAxisUnit(Sint16 raw) {
  const double value = std::clamp(static_cast<double>(raw) / 32767.0, -1.0, 1.0);
  const double magnitude = std::abs(value);
  constexpr double deadZone = 0.20;
  if (magnitude <= deadZone) return 0.0;
  const double scaled = (magnitude - deadZone) / (1.0 - deadZone);
  return std::copysign(std::pow(scaled, 1.35), value);
}

bool steamMouseTriggerPressed(Sint16 raw, bool wasPressed) {
  const int value = std::max(0, static_cast<int>(raw));
  return wasPressed ? value >= 6500 : value >= 14500;
}

bool steamMouseTouchscreenCandidate(bool directProperty, bool pointerProperty,
                                    const std::string& deviceName,
                                    double rawAspect, double displayAspect) {
  std::string lowered = deviceName;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (lowered.find("touchpad") != std::string::npos ||
      lowered.find("trackpad") != std::string::npos) return false;
  if (directProperty && !pointerProperty) return true;
  if (pointerProperty && !directProperty) return false;
  const bool likelyTouchscreen =
    lowered.find("touchscreen") != std::string::npos ||
    lowered.find("touch screen") != std::string::npos ||
    lowered.find("fts") != std::string::npos ||
    lowered.find("goodix") != std::string::npos ||
    lowered.find("elan") != std::string::npos ||
    lowered.find("stmfts") != std::string::npos ||
    lowered.find("hid-multitouch") != std::string::npos;
  if (likelyTouchscreen) return true;
  if (rawAspect <= 0.0 || displayAspect <= 0.0) return false;
  const double normalError = std::abs(std::log(rawAspect / displayAspect));
  const double rotatedError = std::abs(std::log((1.0 / rawAspect) / displayAspect));
  return std::min(normalError, rotatedError) < 0.45;
}

struct SteamMouseBridge::Impl {
  explicit Impl(bool enabled) : controllerMouseMode(enabled) {}

  bool controllerMouseMode = false;
  int displayWidth = 1280;
  int displayHeight = 800;
  Uint64 lastControllerUpdate = 0;
  double carryX = 0.0;
  double carryY = 0.0;
  bool leftDown = false;
  bool rightDown = false;
  Uint64 lastTwoFingerClickAt = 0;
  Uint64 nextTouchProbeAt = 0;
  std::unordered_set<SDL_FingerID> sdlFingers;
  bool sdlTwoFingerLatched = false;
  std::unordered_set<int> xiTouches;
  bool xiTwoFingerLatched = false;

#if defined(__linux__)
  using OpenDisplayFn = void* (*)(const char*);
  using CloseDisplayFn = int (*)(void*);
  using FlushFn = int (*)(void*);
  using DefaultScreenFn = int (*)(void*);
  using FakeRelativeMotionFn = int (*)(void*, int, int, unsigned long);
  using FakeMotionFn = int (*)(void*, int, int, int, unsigned long);
  using FakeButtonFn = int (*)(void*, unsigned int, int, unsigned long);
  using QueryExtensionFn = int (*)(Display*, const char*, int*, int*, int*);
  using PendingFn = int (*)(Display*);
  using NextEventFn = int (*)(Display*, XEvent*);
  using GetEventDataFn = Bool (*)(Display*, XGenericEventCookie*);
  using FreeEventDataFn = void (*)(Display*, XGenericEventCookie*);
  using DefaultRootWindowFn = Window (*)(Display*);
  using QueryPointerFn = Bool (*)(Display*, Window, Window*, Window*, int*, int*, int*, int*, unsigned int*);
  using XIQueryVersionFn = Status (*)(Display*, int*, int*);
  using XISelectEventsFn = Status (*)(Display*, Window, XIEventMask*, int);

  void* x11 = nullptr;
  void* xtst = nullptr;
  void* xi = nullptr;
  void* display = nullptr;
  OpenDisplayFn openDisplay = nullptr;
  CloseDisplayFn closeDisplay = nullptr;
  FlushFn flush = nullptr;
  DefaultScreenFn defaultScreen = nullptr;
  FakeRelativeMotionFn fakeRelativeMotion = nullptr;
  FakeMotionFn fakeMotion = nullptr;
  FakeButtonFn fakeButton = nullptr;
  QueryExtensionFn queryExtension = nullptr;
  PendingFn pending = nullptr;
  NextEventFn nextEvent = nullptr;
  GetEventDataFn getEventData = nullptr;
  FreeEventDataFn freeEventData = nullptr;
  DefaultRootWindowFn defaultRootWindow = nullptr;
  QueryPointerFn queryPointer = nullptr;
  XIQueryVersionFn xiQueryVersion = nullptr;
  XISelectEventsFn xiSelectEvents = nullptr;
  int xiOpcode = -1;
  bool xiTouchEnabled = false;
  int screen = 0;

  struct TouchDevice {
    int fd = -1;
    std::string name;
    int slots = 0;
    int xMin = 0;
    int xMax = 0;
    int yMin = 0;
    int yMax = 0;
    int previousActive = 0;
    std::vector<int> previousTracking;
  };
  std::vector<TouchDevice> touchDevices;
  std::string touchDiscoveryDiagnostic;

  static std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }

  void closeTouchDevices() {
    for (auto& device : touchDevices) {
      if (device.fd >= 0) close(device.fd);
      device.fd = -1;
    }
    touchDevices.clear();
  }

  void shutdownX11() {
    if (display && closeDisplay) closeDisplay(display);
    display = nullptr;
    if (xi) dlclose(xi);
    if (xtst) dlclose(xtst);
    if (x11) dlclose(x11);
    xi = nullptr;
    xtst = nullptr;
    x11 = nullptr;
    openDisplay = nullptr;
    closeDisplay = nullptr;
    flush = nullptr;
    defaultScreen = nullptr;
    fakeRelativeMotion = nullptr;
    fakeMotion = nullptr;
    fakeButton = nullptr;
    queryExtension = nullptr;
    pending = nullptr;
    nextEvent = nullptr;
    getEventData = nullptr;
    freeEventData = nullptr;
    defaultRootWindow = nullptr;
    queryPointer = nullptr;
    xiQueryVersion = nullptr;
    xiSelectEvents = nullptr;
    xiOpcode = -1;
    xiTouchEnabled = false;
    xiTouches.clear();
    xiTwoFingerLatched = false;
  }

  bool xReady() const {
    return display && fakeRelativeMotion && fakeMotion && fakeButton && flush;
  }

  void moveRelative(int dx, int dy) {
    if (!xReady() || (dx == 0 && dy == 0)) return;
    fakeRelativeMotion(display, dx, dy, 0);
    flush(display);
  }

  void moveAbsolute(int x, int y) {
    if (!xReady()) return;
    fakeMotion(display, screen, x, y, 0);
    flush(display);
  }

  void setButton(unsigned int button, bool pressed) {
    if (!xReady()) return;
    fakeButton(display, button, pressed ? 1 : 0, 0);
    flush(display);
  }

  void click(unsigned int button) {
    setButton(button, true);
    setButton(button, false);
  }

  static bool readSlots(const TouchDevice& device, int code, std::vector<int>& values) {
    if (device.fd < 0 || device.slots <= 0) return false;
    std::vector<int> raw(static_cast<std::size_t>(device.slots) + 1, 0);
    raw[0] = code;
    if (ioctl(device.fd, EVIOCGMTSLOTS(raw.size() * sizeof(int)), raw.data()) < 0) return false;
    values.assign(raw.begin() + 1, raw.end());
    return true;
  }

  void discoverTouchDevices() {
    closeTouchDevices();
    int scanned = 0;
    int openDenied = 0;
    int mtRejected = 0;
    int kindRejected = 0;
    const fs::path inputRoot("/dev/input");
    std::error_code ec;
    if (!fs::is_directory(inputRoot, ec)) {
      touchDiscoveryDiagnostic = "inputRootUnavailable";
      return;
    }
    constexpr int bitsPerWord = 8 * static_cast<int>(sizeof(unsigned long));
    const double displayAspect = displayHeight > 0
      ? static_cast<double>(displayWidth) / displayHeight
      : 1.6;

    for (const auto& entry : fs::directory_iterator(inputRoot, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      const std::string fileName = entry.path().filename().string();
      if (fileName.rfind("event", 0) != 0) continue;
      ++scanned;
      const int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) { ++openDenied; continue; }

      unsigned long absBits[(ABS_MAX + bitsPerWord) / bitsPerWord]{};
      if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
        ++mtRejected;
        close(fd);
        continue;
      }
      const auto hasAbs = [&](int code) {
        return (absBits[code / bitsPerWord] & (1UL << (code % bitsPerWord))) != 0;
      };
      if (!hasAbs(ABS_MT_SLOT) || !hasAbs(ABS_MT_TRACKING_ID) ||
          !hasAbs(ABS_MT_POSITION_X) || !hasAbs(ABS_MT_POSITION_Y)) {
        ++mtRejected;
        close(fd);
        continue;
      }

      input_absinfo slotInfo{};
      input_absinfo xInfo{};
      input_absinfo yInfo{};
      if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slotInfo) < 0 ||
          ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &xInfo) < 0 ||
          ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &yInfo) < 0) {
        ++mtRejected;
        close(fd);
        continue;
      }
      const int slots = slotInfo.maximum - slotInfo.minimum + 1;
      const int xRange = xInfo.maximum - xInfo.minimum;
      const int yRange = yInfo.maximum - yInfo.minimum;
      if (slots < 2 || slots > 32 || xRange <= 0 || yRange <= 0) {
        ++mtRejected;
        close(fd);
        continue;
      }

      char nameBuffer[256]{};
      ioctl(fd, EVIOCGNAME(sizeof(nameBuffer)), nameBuffer);
      const std::string name(nameBuffer);
      unsigned long propBits[(INPUT_PROP_MAX + bitsPerWord) / bitsPerWord]{};
      const bool haveProps = ioctl(fd, EVIOCGPROP(sizeof(propBits)), propBits) >= 0;
      const auto hasProp = [&](int code) {
        return haveProps && (propBits[code / bitsPerWord] & (1UL << (code % bitsPerWord))) != 0;
      };
      const bool directProperty = hasProp(INPUT_PROP_DIRECT);
      const bool pointerProperty = hasProp(INPUT_PROP_POINTER);
      const double rawAspect = static_cast<double>(xRange) / yRange;
      if (!steamMouseTouchscreenCandidate(directProperty, pointerProperty, name,
                                          rawAspect, displayAspect)) {
        ++kindRejected;
        close(fd);
        continue;
      }

      TouchDevice device;
      device.fd = fd;
      device.name = name.empty() ? fileName : name;
      device.slots = slots;
      device.xMin = xInfo.minimum;
      device.xMax = xInfo.maximum;
      device.yMin = yInfo.minimum;
      device.yMax = yInfo.maximum;
      device.previousTracking.assign(static_cast<std::size_t>(slots), -1);
      touchDevices.push_back(std::move(device));
    }
    touchDiscoveryDiagnostic =
      "scanned=" + std::to_string(scanned) +
      " openDenied=" + std::to_string(openDenied) +
      " mtRejected=" + std::to_string(mtRejected) +
      " kindRejected=" + std::to_string(kindRejected) +
      " accepted=" + std::to_string(touchDevices.size());
    if (!touchDevices.empty()) {
      touchDiscoveryDiagnostic += " devices=";
      for (std::size_t i = 0; i < touchDevices.size(); ++i) {
        if (i != 0) touchDiscoveryDiagnostic += ",";
        touchDiscoveryDiagnostic += touchDevices[i].name;
      }
    }
  }

  bool injectTwoFingerAt(int x, int y, const std::string& eventSource, std::string& source) {
    if (!xReady()) return false;
    const Uint64 now = SDL_GetTicks64();
    if (lastTwoFingerClickAt != 0 && now - lastTwoFingerClickAt < 300) return false;
    moveAbsolute(std::clamp(x, 0, std::max(0, displayWidth - 1)),
                 std::clamp(y, 0, std::max(0, displayHeight - 1)));
    click(3);
    lastTwoFingerClickAt = now;
    source = eventSource;
    return true;
  }

  bool pollXInput2Touch(std::string& source) {
    if (!xReady() || !xiTouchEnabled || !pending || !nextEvent ||
        !getEventData || !freeEventData || !defaultRootWindow || !queryPointer) return false;
    Display* dpy = reinterpret_cast<Display*>(display);
    while (pending(dpy) > 0) {
      XEvent event{};
      nextEvent(dpy, &event);
      if (event.type != GenericEvent || event.xcookie.extension != xiOpcode) continue;
      if (!getEventData(dpy, &event.xcookie)) continue;
      bool triggered = false;
      if (event.xcookie.evtype == XI_RawTouchBegin) {
        const auto* touch = static_cast<const XIRawEvent*>(event.xcookie.data);
        const std::size_t before = xiTouches.size();
        xiTouches.insert(touch->detail);
        if (before < 2 && xiTouches.size() >= 2 && !xiTwoFingerLatched) {
          xiTwoFingerLatched = true;
          Display* dpy2 = reinterpret_cast<Display*>(display);
          const Window root = defaultRootWindow(dpy2);
          Window rootRet = 0;
          Window childRet = 0;
          int rootX = displayWidth / 2;
          int rootY = displayHeight / 2;
          int winX = 0;
          int winY = 0;
          unsigned int pointerMask = 0;
          queryPointer(dpy2, root, &rootRet, &childRet, &rootX, &rootY,
                       &winX, &winY, &pointerMask);
          triggered = injectTwoFingerAt(rootX, rootY, "xinput2-raw-touch", source);
        }
      } else if (event.xcookie.evtype == XI_RawTouchEnd) {
        const auto* touch = static_cast<const XIRawEvent*>(event.xcookie.data);
        xiTouches.erase(touch->detail);
        if (xiTouches.size() < 2) xiTwoFingerLatched = false;
      }
      freeEventData(dpy, &event.xcookie);
      if (triggered) return true;
    }
    return false;
  }

  bool pollEvdevTouch(std::string& source) {
    if (!xReady()) return false;
    const Uint64 now = SDL_GetTicks64();
    if (touchDevices.empty() && now >= nextTouchProbeAt) {
      discoverTouchDevices();
      nextTouchProbeAt = now + 2000;
    }

    for (auto& device : touchDevices) {
      std::vector<int> tracking;
      std::vector<int> xs;
      std::vector<int> ys;
      if (!readSlots(device, ABS_MT_TRACKING_ID, tracking) ||
          !readSlots(device, ABS_MT_POSITION_X, xs) ||
          !readSlots(device, ABS_MT_POSITION_Y, ys)) {
        continue;
      }
      int active = 0;
      int newestSlot = -1;
      for (int i = 0; i < device.slots; ++i) {
        if (tracking[static_cast<std::size_t>(i)] < 0) continue;
        ++active;
        if (i < static_cast<int>(device.previousTracking.size()) &&
            device.previousTracking[static_cast<std::size_t>(i)] < 0) {
          newestSlot = i;
        }
      }

      bool triggered = false;
      if (device.previousActive < 2 && active >= 2) {
        int slot = newestSlot;
        if (slot < 0) {
          for (int i = 0; i < device.slots; ++i) {
            if (tracking[static_cast<std::size_t>(i)] >= 0) {
              slot = i;
              break;
            }
          }
        }
        if (slot >= 0) {
          const double nx = std::clamp(
            static_cast<double>(xs[static_cast<std::size_t>(slot)] - device.xMin) /
            std::max(1, device.xMax - device.xMin), 0.0, 1.0);
          const double ny = std::clamp(
            static_cast<double>(ys[static_cast<std::size_t>(slot)] - device.yMin) /
            std::max(1, device.yMax - device.yMin), 0.0, 1.0);
          const int x = static_cast<int>(std::lround(nx * std::max(0, displayWidth - 1)));
          const int y = static_cast<int>(std::lround(ny * std::max(0, displayHeight - 1)));
          triggered = injectTwoFingerAt(x, y, "evdev:" + device.name, source);
        }
      }
      device.previousActive = active;
      device.previousTracking = std::move(tracking);
      if (triggered) return true;
    }
    return false;
  }
#endif
};

SteamMouseBridge::SteamMouseBridge(bool controllerMouseMode)
  : impl_(std::make_unique<Impl>(controllerMouseMode)) {}

SteamMouseBridge::~SteamMouseBridge() {
  releaseControllerButtons();
#if defined(__linux__)
  impl_->closeTouchDevices();
  impl_->shutdownX11();
#endif
}

bool SteamMouseBridge::initialize(int displayWidth, int displayHeight, std::string& diagnostic) {
  impl_->displayWidth = std::max(1, displayWidth);
  impl_->displayHeight = std::max(1, displayHeight);
  impl_->lastControllerUpdate = SDL_GetTicks64();
#if defined(__linux__)
  impl_->shutdownX11();
  impl_->x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
  impl_->xtst = dlopen("libXtst.so.6", RTLD_LAZY | RTLD_LOCAL);
  impl_->xi = dlopen("libXi.so.6", RTLD_LAZY | RTLD_LOCAL);
  if (!impl_->x11 || !impl_->xtst) {
    diagnostic = "libX11/libXtst unavailable";
    impl_->shutdownX11();
    return false;
  }
  impl_->openDisplay = reinterpret_cast<Impl::OpenDisplayFn>(dlsym(impl_->x11, "XOpenDisplay"));
  impl_->closeDisplay = reinterpret_cast<Impl::CloseDisplayFn>(dlsym(impl_->x11, "XCloseDisplay"));
  impl_->flush = reinterpret_cast<Impl::FlushFn>(dlsym(impl_->x11, "XFlush"));
  impl_->defaultScreen = reinterpret_cast<Impl::DefaultScreenFn>(dlsym(impl_->x11, "XDefaultScreen"));
  impl_->fakeRelativeMotion = reinterpret_cast<Impl::FakeRelativeMotionFn>(dlsym(impl_->xtst, "XTestFakeRelativeMotionEvent"));
  impl_->fakeMotion = reinterpret_cast<Impl::FakeMotionFn>(dlsym(impl_->xtst, "XTestFakeMotionEvent"));
  impl_->fakeButton = reinterpret_cast<Impl::FakeButtonFn>(dlsym(impl_->xtst, "XTestFakeButtonEvent"));
  impl_->queryExtension = reinterpret_cast<Impl::QueryExtensionFn>(dlsym(impl_->x11, "XQueryExtension"));
  impl_->pending = reinterpret_cast<Impl::PendingFn>(dlsym(impl_->x11, "XPending"));
  impl_->nextEvent = reinterpret_cast<Impl::NextEventFn>(dlsym(impl_->x11, "XNextEvent"));
  impl_->getEventData = reinterpret_cast<Impl::GetEventDataFn>(dlsym(impl_->x11, "XGetEventData"));
  impl_->freeEventData = reinterpret_cast<Impl::FreeEventDataFn>(dlsym(impl_->x11, "XFreeEventData"));
  impl_->defaultRootWindow = reinterpret_cast<Impl::DefaultRootWindowFn>(dlsym(impl_->x11, "XDefaultRootWindow"));
  impl_->queryPointer = reinterpret_cast<Impl::QueryPointerFn>(dlsym(impl_->x11, "XQueryPointer"));
  if (impl_->xi) {
    impl_->xiQueryVersion = reinterpret_cast<Impl::XIQueryVersionFn>(dlsym(impl_->xi, "XIQueryVersion"));
    impl_->xiSelectEvents = reinterpret_cast<Impl::XISelectEventsFn>(dlsym(impl_->xi, "XISelectEvents"));
  }
  if (!impl_->openDisplay || !impl_->closeDisplay || !impl_->flush || !impl_->defaultScreen ||
      !impl_->fakeRelativeMotion || !impl_->fakeMotion || !impl_->fakeButton) {
    diagnostic = "XTest symbols unavailable";
    impl_->shutdownX11();
    return false;
  }
  impl_->display = impl_->openDisplay(nullptr);
  if (!impl_->display) {
    diagnostic = "XOpenDisplay failed";
    impl_->shutdownX11();
    return false;
  }
  impl_->screen = impl_->defaultScreen(impl_->display);
  if (impl_->xi && impl_->queryExtension && impl_->pending && impl_->nextEvent &&
      impl_->getEventData && impl_->freeEventData && impl_->defaultRootWindow && impl_->queryPointer &&
      impl_->xiQueryVersion && impl_->xiSelectEvents) {
    int eventBase = 0;
    int errorBase = 0;
    if (impl_->queryExtension(reinterpret_cast<Display*>(impl_->display), "XInputExtension",
                              &impl_->xiOpcode, &eventBase, &errorBase)) {
      int major = 2;
      int minor = 2;
      if (impl_->xiQueryVersion(reinterpret_cast<Display*>(impl_->display), &major, &minor) == Success &&
          (major > 2 || (major == 2 && minor >= 2))) {
        unsigned char mask[(XI_LASTEVENT + 7) / 8]{};
        XISetMask(mask, XI_RawTouchBegin);
        XISetMask(mask, XI_RawTouchEnd);
        XIEventMask eventMask{XIAllMasterDevices, static_cast<int>(sizeof(mask)), mask};
        const Window root = impl_->defaultRootWindow(reinterpret_cast<Display*>(impl_->display));
        if (impl_->xiSelectEvents(reinterpret_cast<Display*>(impl_->display), root, &eventMask, 1) == Success) {
          impl_->flush(impl_->display);
          impl_->xiTouchEnabled = true;
        }
      }
    }
  }
  impl_->discoverTouchDevices();
  diagnostic = "ready touchDevices=" + std::to_string(impl_->touchDevices.size()) +
               " controllerMouse=" + (impl_->controllerMouseMode ? "1" : "0") +
               " touchProbe=" + impl_->touchDiscoveryDiagnostic +
               " xiTouch=" + (impl_->xiTouchEnabled ? "1" : "0");
  return true;
#else
  diagnostic = "non-linux";
  return false;
#endif
}

bool SteamMouseBridge::ready() const {
#if defined(__linux__)
  return impl_->xReady();
#else
  return false;
#endif
}

bool SteamMouseBridge::controllerMode() const {
  return impl_->controllerMouseMode;
}

void SteamMouseBridge::updateController(SDL_GameController* controller, Uint64 nowMs) {
#if defined(__linux__)
  if (!impl_->controllerMouseMode || !impl_->xReady()) return;
  if (!controller || !SDL_GameControllerGetAttached(controller)) {
    releaseControllerButtons();
    impl_->lastControllerUpdate = nowMs;
    impl_->carryX = 0.0;
    impl_->carryY = 0.0;
    return;
  }

  double dt = 0.016;
  if (impl_->lastControllerUpdate != 0 && nowMs >= impl_->lastControllerUpdate)
    dt = std::clamp(static_cast<double>(nowMs - impl_->lastControllerUpdate) / 1000.0, 0.0, 0.05);
  impl_->lastControllerUpdate = nowMs;

  constexpr double maxPixelsPerSecond = 1050.0;
  const double ux = steamMouseAxisUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
  const double uy = steamMouseAxisUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
  impl_->carryX += ux * maxPixelsPerSecond * dt;
  impl_->carryY += uy * maxPixelsPerSecond * dt;
  const int dx = static_cast<int>(impl_->carryX);
  const int dy = static_cast<int>(impl_->carryY);
  impl_->carryX -= dx;
  impl_->carryY -= dy;
  impl_->moveRelative(dx, dy);

  const bool leftNext = steamMouseTriggerPressed(
    SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT), impl_->leftDown);
  const bool rightNext = steamMouseTriggerPressed(
    SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), impl_->rightDown);
  if (leftNext != impl_->leftDown) {
    impl_->setButton(1, leftNext);
    impl_->leftDown = leftNext;
  }
  if (rightNext != impl_->rightDown) {
    impl_->setButton(3, rightNext);
    impl_->rightDown = rightNext;
  }
#else
  (void)controller;
  (void)nowMs;
#endif
}

bool SteamMouseBridge::pollTwoFingerTouch(std::string& source) {
#if defined(__linux__)
  if (impl_->pollXInput2Touch(source)) return true;
  return impl_->pollEvdevTouch(source);
#else
  (void)source;
  return false;
#endif
}

bool SteamMouseBridge::handleSdlTouchEvent(const SDL_Event& event, std::string& source) {
#if defined(__linux__)
  if (!impl_->xReady()) return false;
  if (event.type == SDL_FINGERDOWN) {
    impl_->sdlFingers.insert(event.tfinger.fingerId);
    if (impl_->sdlFingers.size() >= 2 && !impl_->sdlTwoFingerLatched) {
      impl_->sdlTwoFingerLatched = true;
      const int x = static_cast<int>(std::lround(
        std::clamp(static_cast<double>(event.tfinger.x), 0.0, 1.0) * (impl_->displayWidth - 1)));
      const int y = static_cast<int>(std::lround(
        std::clamp(static_cast<double>(event.tfinger.y), 0.0, 1.0) * (impl_->displayHeight - 1)));
      return impl_->injectTwoFingerAt(x, y, "sdl-touch", source);
    }
  } else if (event.type == SDL_FINGERUP) {
    impl_->sdlFingers.erase(event.tfinger.fingerId);
    if (impl_->sdlFingers.size() < 2) impl_->sdlTwoFingerLatched = false;
  }
#else
  (void)event;
  (void)source;
#endif
  return false;
}

void SteamMouseBridge::releaseControllerButtons() {
#if defined(__linux__)
  if (impl_->xReady()) {
    if (impl_->leftDown) impl_->setButton(1, false);
    if (impl_->rightDown) impl_->setButton(3, false);
  }
#endif
  impl_->leftDown = false;
  impl_->rightDown = false;
}

} // namespace rpgmp