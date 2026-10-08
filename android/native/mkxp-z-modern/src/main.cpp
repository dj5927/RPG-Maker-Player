/*
** main.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef VK_NO_PROTOTYPES
#  define VK_NO_PROTOTYPES
#endif

#include "icon.png.xxd"

#include <alc.h>
#include <alext.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_sound.h>
#include <SDL_ttf.h>

#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <dlfcn.h>
#include <ucontext.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "binding.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "display/gl/gl-debug.h"
#include "display/gl/gl-fun.h"

#include "filesystem/filesystem.h"

#include "system/system.h"

#if defined(__WIN32__)
#include "resource.h"
#include <processenv.h>
#include <winsock2.h>
#include "util/win-consoleutils.h"

// Try to work around buggy GL drivers that tend to be in Optimus laptops
// by forcing MKXP to use the dedicated card instead of the integrated one
#include <windows.h>
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifdef MKXPZ_STEAM
#include "steamshim_child.h"
#endif

#ifdef __APPLE__
#include <Availability.h>
#include "TouchBar.h"
#endif

#if !defined(__ANDROID__) && !defined(__APPLE__) && !defined(_WIN32)
#  define MKXPZ_CHECK_FOR_WAYLAND_SUPPORT
#endif

#if defined(MKXPZ_HAVE_ANGLE) && defined(MKXPZ_HAVE_ANGLE_VULKAN)
#  define MKXPZ_CHECK_FOR_LAVAPIPE
#  include <volk.h>
#endif

#ifndef MKXPZ_INIT_GL_LATER
#define GLINIT_SHOWERROR(s) showInitError(s)
#else
#define GLINIT_SHOWERROR(s) rgssThreadError(threadData, s)
#endif

#ifdef MKXPZ_HAVE_ANGLE
bool mkxp_use_angle = true;
#endif // MKXPZ_HAVE_ANGLE

static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg);
static void showInitError(const std::string &msg);

#ifdef __ANDROID__
static volatile sig_atomic_t rpgmpCrashHandling = 0;
static volatile sig_atomic_t rpgmpCrashScriptIndex = -1;
static volatile sig_atomic_t rpgmpCrashRubyLine = -1;
static volatile sig_atomic_t rpgmpRgssTid = -1;
static uintptr_t rpgmpRgssStackLow = 0;
static uintptr_t rpgmpRgssStackHigh = 0;
static char rpgmpCrashScriptName[128] = "<none>";
static char rpgmpCrashRubyPath[192] = "<none>";
static uintptr_t rpgmpModernLibraryBase = 0;

static void rpgmpRawLog(const char *text) {
  const char *path = getenv("RPGMP_GAME_LOG");
  if (!path || !*path || !text) return;
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0) return;
  write(fd, text, strlen(text));
  fsync(fd);
  close(fd);
}

__attribute__((constructor))
static void rpgmpModernLibraryLoaded() {
  Dl_info info;
  memset(&info, 0, sizeof(info));
  if (dladdr(reinterpret_cast<void *>(&rpgmpModernLibraryLoaded), &info) != 0 &&
      info.dli_fbase != nullptr)
    rpgmpModernLibraryBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
  rpgmpRawLog("[RPGMP-MODERN] library constructor reached\n");
}

extern "C" void rpgmpSetCrashContext(long index, const char *name) {
  rpgmpCrashScriptIndex = static_cast<sig_atomic_t>(index);
  rpgmpCrashRubyLine = -1;
  rpgmpCrashRubyPath[0] = '\0';
  if (!name) {
    rpgmpCrashScriptName[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < sizeof(rpgmpCrashScriptName) && name[i]; ++i)
    rpgmpCrashScriptName[i] = name[i];
  rpgmpCrashScriptName[i] = '\0';
}

extern "C" void rpgmpSetCrashRubyLine(long line) {
  rpgmpCrashRubyLine = static_cast<sig_atomic_t>(line);
}

extern "C" void rpgmpSetCrashRubyPath(const char *path) {
  if (!path) {
    rpgmpCrashRubyPath[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < sizeof(rpgmpCrashRubyPath) && path[i]; ++i)
    rpgmpCrashRubyPath[i] = path[i];
  rpgmpCrashRubyPath[i] = '\0';
}

static size_t rpgmpAppendLiteral(char *dst, size_t pos, size_t cap, const char *src) {
  while (src && *src && pos + 1 < cap)
    dst[pos++] = *src++;
  return pos;
}

static size_t rpgmpAppendInt(char *dst, size_t pos, size_t cap, long value) {
  char tmp[32];
  size_t n = 0;
  if (value < 0) {
    if (pos + 1 < cap) dst[pos++] = '-';
    value = -value;
  }
  do {
    tmp[n++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value && n < sizeof(tmp));
  while (n && pos + 1 < cap)
    dst[pos++] = tmp[--n];
  return pos;
}

static size_t rpgmpAppendHex(char *dst, size_t pos, size_t cap, uintptr_t value) {
  static const char digits[] = "0123456789abcdef";
  char tmp[2 * sizeof(uintptr_t)];
  size_t n = 0;
  do {
    tmp[n++] = digits[value & 0xf];
    value >>= 4;
  } while (value && n < sizeof(tmp));
  pos = rpgmpAppendLiteral(dst, pos, cap, "0x");
  while (n && pos + 1 < cap)
    dst[pos++] = tmp[--n];
  return pos;
}

static void rpgmpCrashSignalHandler(int sig, siginfo_t *info, void *uctx) {
  if (rpgmpCrashHandling) _exit(128 + sig);
  rpgmpCrashHandling = 1;
  const uintptr_t safeFault =
      info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
  const long crashTid = static_cast<long>(syscall(SYS_gettid));
  const char *path = getenv("RPGMP_GAME_LOG");
  if (path && *path) {
    int enterFd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (enterFd >= 0) {
      char enterLine[320];
      size_t ep = 0;
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine),
                              "[RPGMP-MODERN-CRASH-ENTER] signal=");
      ep = rpgmpAppendInt(enterLine, ep, sizeof(enterLine), sig);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " script=");
      ep = rpgmpAppendInt(enterLine, ep, sizeof(enterLine), rpgmpCrashScriptIndex);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " name=");
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), rpgmpCrashScriptName);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " ruby_line=");
      ep = rpgmpAppendInt(enterLine, ep, sizeof(enterLine), rpgmpCrashRubyLine);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " ruby_path=");
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), rpgmpCrashRubyPath);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " fault=");
      ep = rpgmpAppendHex(enterLine, ep, sizeof(enterLine), safeFault);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " tid=");
      ep = rpgmpAppendInt(enterLine, ep, sizeof(enterLine), crashTid);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), " rgss_tid=");
      ep = rpgmpAppendInt(enterLine, ep, sizeof(enterLine), rpgmpRgssTid);
      ep = rpgmpAppendLiteral(enterLine, ep, sizeof(enterLine), "\n");
      write(enterFd, enterLine, ep);
      fsync(enterFd);
      close(enterFd);
    }
  }
  if (path && *path) {
    int ctxFd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (ctxFd >= 0) {
      char beginLine[160];
      size_t bp = 0;
      bp = rpgmpAppendLiteral(beginLine, bp, sizeof(beginLine),
                              "[RPGMP-MODERN-CRASH-CTX-BEGIN] uctx=");
      bp = rpgmpAppendHex(beginLine, bp, sizeof(beginLine),
                          reinterpret_cast<uintptr_t>(uctx));
      bp = rpgmpAppendLiteral(beginLine, bp, sizeof(beginLine), "\n");
      write(ctxFd, beginLine, bp);
      fsync(ctxFd);
#if defined(__aarch64__)
      if (uctx) {
        ucontext_t *uc = reinterpret_cast<ucontext_t *>(uctx);
        uintptr_t ctxPc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
        char pcLine[160];
        size_t cp = 0;
        cp = rpgmpAppendLiteral(pcLine, cp, sizeof(pcLine),
                                "[RPGMP-MODERN-CRASH-PC] pc=");
        cp = rpgmpAppendHex(pcLine, cp, sizeof(pcLine), ctxPc);
        cp = rpgmpAppendLiteral(pcLine, cp, sizeof(pcLine), "\n");
        write(ctxFd, pcLine, cp);
        fsync(ctxFd);

        char regLine[384];
        size_t rp = 0;
        rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine),
                                "[RPGMP-MODERN-CRASH-REGS] lr=");
        rp = rpgmpAppendHex(regLine, rp, sizeof(regLine),
                            static_cast<uintptr_t>(uc->uc_mcontext.regs[30]));
        for (int reg = 0; reg < 6; ++reg) {
          rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), " x");
          rp = rpgmpAppendInt(regLine, rp, sizeof(regLine), reg);
          rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), "=");
          rp = rpgmpAppendHex(regLine, rp, sizeof(regLine),
                              static_cast<uintptr_t>(uc->uc_mcontext.regs[reg]));
        }
                rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), " sp=");
        rp = rpgmpAppendHex(regLine, rp, sizeof(regLine), static_cast<uintptr_t>(uc->uc_mcontext.sp));
        rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), " fp=");
        rp = rpgmpAppendHex(regLine, rp, sizeof(regLine), static_cast<uintptr_t>(uc->uc_mcontext.regs[29]));
        rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), " stack_low=");
        rp = rpgmpAppendHex(regLine, rp, sizeof(regLine), rpgmpRgssStackLow);
        rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), " stack_high=");
        rp = rpgmpAppendHex(regLine, rp, sizeof(regLine), rpgmpRgssStackHigh);
        rp = rpgmpAppendLiteral(regLine, rp, sizeof(regLine), "\n");
        write(ctxFd, regLine, rp);
        fsync(ctxFd);

        uintptr_t frame = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
        for (int depth = 0; depth < 48; ++depth) {
          if (!frame || frame < rpgmpRgssStackLow || frame + 16 > rpgmpRgssStackHigh || (frame & 0x7)) break;
          const uintptr_t *record = reinterpret_cast<const uintptr_t *>(frame);
          uintptr_t nextFrame = record[0];
          uintptr_t ret = record[1];
          char btLine[160];
          size_t bt = 0;
          bt = rpgmpAppendLiteral(btLine, bt, sizeof(btLine), "[RPGMP-MODERN-CRASH-BT] depth=");
          bt = rpgmpAppendInt(btLine, bt, sizeof(btLine), depth);
          bt = rpgmpAppendLiteral(btLine, bt, sizeof(btLine), " fp=");
          bt = rpgmpAppendHex(btLine, bt, sizeof(btLine), frame);
          bt = rpgmpAppendLiteral(btLine, bt, sizeof(btLine), " ret=");
          bt = rpgmpAppendHex(btLine, bt, sizeof(btLine), ret);
          bt = rpgmpAppendLiteral(btLine, bt, sizeof(btLine), "\n");
          write(ctxFd, btLine, bt);
          if (nextFrame <= frame || nextFrame > rpgmpRgssStackHigh) break;
          frame = nextFrame;
        }
        fsync(ctxFd);
      }
#endif
      close(ctxFd);
    }
  }

  uintptr_t pc = 0;
  uintptr_t lr = 0;
#if defined(__aarch64__)
  if (uctx) {
    ucontext_t *uc = reinterpret_cast<ucontext_t *>(uctx);
    pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
    lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
  }
#endif
  uintptr_t fault = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
  uintptr_t offset =
      (pc && rpgmpModernLibraryBase && pc >= rpgmpModernLibraryBase)
          ? pc - rpgmpModernLibraryBase
          : 0;
  if (path && *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
      char line[512];
      size_t p = 0;
      p = rpgmpAppendLiteral(line, p, sizeof(line), "[RPGMP-MODERN-CRASH] signal=");
      p = rpgmpAppendInt(line, p, sizeof(line), sig);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " script=");
      p = rpgmpAppendInt(line, p, sizeof(line), rpgmpCrashScriptIndex);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " name=");
      p = rpgmpAppendLiteral(line, p, sizeof(line), rpgmpCrashScriptName);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " pc=");
      p = rpgmpAppendHex(line, p, sizeof(line), pc);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " lr=");
      p = rpgmpAppendHex(line, p, sizeof(line), lr);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " fault=");
      p = rpgmpAppendHex(line, p, sizeof(line), fault);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " base=");
      p = rpgmpAppendHex(line, p, sizeof(line), rpgmpModernLibraryBase);
      p = rpgmpAppendLiteral(line, p, sizeof(line), " offset=");
      p = rpgmpAppendHex(line, p, sizeof(line), offset);
      p = rpgmpAppendLiteral(line, p, sizeof(line), "\n");
      write(fd, line, p);
      fsync(fd);
      close(fd);
    }
  }
  struct sigaction dfl;
  memset(&dfl, 0, sizeof(dfl));
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  sigaction(sig, &dfl, nullptr);
  kill(getpid(), sig);
}

static void rpgmpInstallCrashSignalHandlers() {
  const int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = rpgmpCrashSignalHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
    sigaction(signals[i], &sa, nullptr);
}

extern "C" void rpgmpReinstallCrashSignalHandlersAfterRuby() {
  rpgmpCrashHandling = 0;
  rpgmpInstallCrashSignalHandlers();
  rpgmpRawLog("[RPGMP-MODERN] CRASH-HANDLER-REINSTALLED-AFTER-RUBY\\n");
}
#endif

static void mkxp_setenv(const char *key, const char *value) {
#ifdef _WIN32
  SetEnvironmentVariableA(key, value);
#else
  if (value != nullptr) {
    setenv(key, value, true);
  } else {
    unsetenv(key);
  }
#endif
}

static inline const char *glGetStringInt(GLenum name) {
  return (const char *)gl.GetString(name);
}

static void printGLInfo() {
  const std::string renderer(glGetStringInt(GL_RENDERER));
  const std::string version(glGetStringInt(GL_VERSION));

  Debug() << "GL Vendor    :" << glGetStringInt(GL_VENDOR);
  Debug() << "GL Renderer  :" << renderer;
  Debug() << "GL Version   :" << version;
  Debug() << "GLSL Version :" << glGetStringInt(GL_SHADING_LANGUAGE_VERSION);
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData);

int rgssThreadFun(void *userdata) {
  RGSSThreadData *threadData = static_cast<RGSSThreadData *>(userdata);
#ifdef __ANDROID__
  rpgmpRgssTid = static_cast<sig_atomic_t>(syscall(SYS_gettid));
  {
    pthread_attr_t attr;
    void *stackAddr = nullptr;
    size_t stackSize = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
      if (pthread_attr_getstack(&attr, &stackAddr, &stackSize) == 0) {
        rpgmpRgssStackLow = reinterpret_cast<uintptr_t>(stackAddr);
        rpgmpRgssStackHigh = rpgmpRgssStackLow + stackSize;
        Debug() << "[RPGMP-MODERN] RGSS-STACK-RANGE low=" << reinterpret_cast<void *>(rpgmpRgssStackLow) << " high=" << reinterpret_cast<void *>(rpgmpRgssStackHigh) << " size=" << stackSize;
      }
      pthread_attr_destroy(&attr);
    }
  }
#endif
  Debug() << "[RPGMP-MODERN] RGSS-THREAD-BEGIN";

#ifdef MKXPZ_INIT_GL_LATER
  threadData->glContext =
      initGL(threadData->window, threadData->config, threadData);
  if (!threadData->glContext)
    return 0;
  Debug() << "[RPGMP-MODERN] GL-INIT-RETURN";
#else
  SDL_GL_MakeCurrent(threadData->window, threadData->glContext);
#endif

  /* Setup AL context */
  static const ALCint attrs[] = {
    /* HRTF is explicitly disabled here because it results in poor-quality audio
     * when enabled (see https://github.com/mkxp-z/mkxp-z/issues/341). By
     * default, it's enabled when OpenAL Soft detects that the user is using
     * headphones for audio drivers that support detecting if the user is using
     * headphones, and disabled regardless of whether or not the user is using
     * headphones if the audio driver does not support this detection. The HRTF
     * is required for positional audio support, so we'll need to find a way
     * around the audio quality issues and inconsistent detection of whether or
     * not the user is using headphones once we have positional audio support. */
    ALC_HRTF_SOFT, ALC_FALSE,
    0
  };
  Debug() << "[RPGMP-MODERN] OPENAL-CONTEXT-BEGIN";
#ifdef __ANDROID__
  ALCcontext *alcCtx = alcCreateContext(threadData->alcDev, nullptr);
#else
  ALCcontext *alcCtx = alcCreateContext(threadData->alcDev, attrs);
#endif

  if (!alcCtx) {
    rgssThreadError(threadData, "Error creating OpenAL context");
    return 0;
  }
  Debug() << "[RPGMP-MODERN] OPENAL-CONTEXT-END";

  alcMakeContextCurrent(alcCtx);
  Debug() << "[RPGMP-MODERN] OPENAL-MAKECURRENT-END";

  try {
    Debug() << "[RPGMP-MODERN] SHAREDSTATE-BEGIN";
    SharedState::initInstance(threadData);
    Debug() << "[RPGMP-MODERN] SHAREDSTATE-END";
  } catch (const Exception &exc) {
    rgssThreadError(threadData, exc.msg);
    alcDestroyContext(alcCtx);

    return 0;
  }

  /* Start script execution */
  scriptBinding->execute();

  threadData->rqTermAck.set();
  threadData->ethread->requestTerminate();

  SharedState::finiInstance();

  alcDestroyContext(alcCtx);

  return 0;
}

static void printRgssVersion(int ver) {
  const char *const makers[] = {"", "XP", "VX", "VX Ace"};

  char buf[128];
  snprintf(buf, sizeof(buf), "RGSS version %d (RPG Maker %s)", ver,
           makers[ver]);

  Debug() << buf;
}

static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg) {
  rtData->rgssErrorMsg = msg;
  rtData->ethread->requestTerminate();
  rtData->rqTermAck.set();
}

static void showInitError(const std::string &msg) {
  Debug() << msg;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "mkxp-z", msg.c_str(), 0);
}

static void setupWindowIcon(const Config &conf, SDL_Window *win) {
  SDL_RWops *iconSrc;

  if (conf.iconPath.empty())
    iconSrc = SDL_RWFromConstMem(mkxp_assets_icon_png, mkxp_assets_icon_png_len);
  else
    iconSrc = SDL_RWFromFile(conf.iconPath.c_str(), "rb");

  SDL_Surface *iconImg = IMG_Load_RW(iconSrc, SDL_TRUE);

  if (iconImg) {
    SDL_SetWindowIcon(win, iconImg);
    SDL_FreeSurface(iconImg);
  }
}

static SDL_Window *initVideo(const Config &conf) {
  Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_ALLOW_HIGHDPI;

  if (conf.winResizable)
    winFlags |= SDL_WINDOW_RESIZABLE;
  if (conf.fullscreen)
    winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    return nullptr;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GetHintBoolean(SDL_HINT_OPENGL_ES_DRIVER, SDL_FALSE) ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE | SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

  SDL_Window *window = SDL_CreateWindow(conf.windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, conf.defScreenW, conf.defScreenH, winFlags);
  if (window == nullptr) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  return window;
}

int main(int argc, char *argv[]) {
#ifdef __ANDROID__
    rpgmpInstallCrashSignalHandlers();
    {
      char baseLine[96];
      snprintf(baseLine, sizeof(baseLine),
               "[RPGMP-MODERN] BASE modern=0x%llx\n",
               static_cast<unsigned long long>(rpgmpModernLibraryBase));
      rpgmpRawLog(baseLine);
    }
#endif
    Debug() << "[RPGMP-MODERN] MAIN-BEGIN argc=" << argc;
    const char *rpgmpAutoFallbackEnv = getenv("RPGMP_AUTO_RUBY_FALLBACK");
    const bool rpgmpAutoFallback = rpgmpAutoFallbackEnv && std::strcmp(rpgmpAutoFallbackEnv, "1") == 0;
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    SDL_SetHint(
      SDL_HINT_OPENGL_ES_DRIVER,
      SDL_GetHintBoolean(
        SDL_HINT_OPENGL_ES_DRIVER,
#ifdef MKXPZ_USE_GLES_BY_DEFAULT
        SDL_TRUE
#else
        SDL_FALSE
#endif // MKXPZ_USE_GLES_BY_DEFAULT
      ) != SDL_FALSE ? "1" : "0"
    );

#if !defined(WORKDIR_CURRENT) && !defined(__ANDROID__)
    char dataDir[512]{};
#if defined(__linux__)
    char *tmp{};
    tmp = getenv("SRCDIR");
    if (tmp) {
      std::strncpy(dataDir, tmp, sizeof(dataDir));
    }
#endif
    if (!dataDir[0]) {
      std::strncpy(dataDir, mkxp_fs::getDefaultGameRoot().c_str(), sizeof(dataDir));
    }
    mkxp_fs::setCurrentDirectory(dataDir);
#endif

#ifdef __ANDROID__
    Debug() << "[RPGMP-MODERN] ANDROID-WORKDIR-AUTO-SKIP";
#endif

    /* now we load the config */
    Debug() << "[RPGMP-MODERN] CONFIG-READ-BEGIN";
    Config conf;
    conf.read(argc, argv);
    Debug() << "[RPGMP-MODERN] CONFIG-READ-END rgss=" << conf.rgssVersion;
    if (conf.windowTitle.empty())
      conf.windowTitle = conf.game.title;

#if defined(__WIN32__)
    // Create a debug console in debug mode
    if (conf.winConsole) {
      if (setupWindowsConsole()) {
        reopenWindowsStreams();
      } else {
        char buf[200];
        snprintf(buf, sizeof(buf), "Error allocating console: %lu",
                GetLastError());
        showInitError(std::string(buf));
      }
    }
#endif

    /* initialize SDL first */
    Debug() << "[RPGMP-MODERN] SDL-INIT-BEGIN";
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) < 0) {
      showInitError(std::string("Error initializing SDL: ") + SDL_GetError());
      return 0;
    }
    Debug() << "[RPGMP-MODERN] SDL-INIT-END";

    SDL_Window *win = nullptr;

#ifdef MKXPZ_CHECK_FOR_WAYLAND_SUPPORT
    {
      const char *sdl_videodriver = SDL_GetHint(SDL_HINT_VIDEODRIVER);
      if (sdl_videodriver == nullptr || sdl_videodriver[0] == 0) {
        /* Select SDL's Wayland video driver if SDL_VIDEODRIVER is unset and Wayland support is available on the user's machine */
        void *wayland_client = SDL_LoadObject(MKXPZ_WAYLAND_CLIENT_SONAME);
        void *wayland_cursor = SDL_LoadObject(MKXPZ_WAYLAND_CURSOR_SONAME);
        void *wayland_egl = SDL_LoadObject(MKXPZ_WAYLAND_EGL_SONAME);
        void *xkbcommon = SDL_LoadObject(MKXPZ_XKBCOMMON_SONAME);
        if (
          wayland_client != nullptr
            && wayland_cursor != nullptr
            && wayland_egl != nullptr
            && xkbcommon != nullptr
        ) {
          void *(*_wl_display_connect)(const char *name) = reinterpret_cast<void *(*)(const char *name)>(SDL_LoadFunction(wayland_client, "wl_display_connect"));
          void (*_wl_display_disconnect)(void *display) = reinterpret_cast<void (*)(void *display)>(SDL_LoadFunction(wayland_client, "wl_display_disconnect"));
          void *(*_wl_cursor_image_get_buffer)(void *image) = reinterpret_cast<void *(*)(void *image)>(SDL_LoadFunction(wayland_cursor, "wl_cursor_image_get_buffer"));
          void (*_wl_cursor_theme_destroy)(void *theme) = reinterpret_cast<void (*)(void *theme)>(SDL_LoadFunction(wayland_cursor, "wl_cursor_theme_destroy"));
          void *(*_wl_cursor_theme_get_cursor)(void *theme, const char *name) = reinterpret_cast<void *(*)(void *theme, const char *name)>(SDL_LoadFunction(wayland_cursor, "wl_cursor_theme_get_cursor"));
          void *(*_wl_cursor_theme_load)(const char *name, int size, void *shm) = reinterpret_cast<void *(*)(const char *name, int size, void *shm)>(SDL_LoadFunction(wayland_cursor, "wl_cursor_theme_load"));
          void *(*_wl_egl_window_create)(void *surface, int width, int height) = reinterpret_cast<void *(*)(void *surface, int width, int height)>(SDL_LoadFunction(wayland_egl, "wl_egl_window_create"));
          void (*_wl_egl_window_destroy)(void *egl_window) = reinterpret_cast<void (*)(void *egl_window)>(SDL_LoadFunction(wayland_egl, "wl_egl_window_destroy"));
          void (*_wl_egl_window_resize)(void *egl_window, int width, int height, int dx, int dy) = reinterpret_cast<void (*)(void *egl_window, int width, int height, int dx, int dy)>(SDL_LoadFunction(wayland_egl, "wl_egl_window_resize"));
          void *(*_xkb_context_new)(int flags) = reinterpret_cast<void *(*)(int flags)>(SDL_LoadFunction(xkbcommon, "xkb_context_new"));
          void (*_xkb_context_unref)(void *context) = reinterpret_cast<void (*)(void *context)>(SDL_LoadFunction(xkbcommon, "xkb_context_unref"));
          if (
            _wl_display_connect != nullptr
              && _wl_display_disconnect != nullptr
              && _wl_cursor_image_get_buffer != nullptr
              && _wl_cursor_theme_destroy != nullptr
              && _wl_cursor_theme_get_cursor != nullptr
              && _wl_cursor_theme_load != nullptr
              && _wl_egl_window_create != nullptr
              && _wl_egl_window_destroy != nullptr
              && _wl_egl_window_resize != nullptr
              && _xkb_context_new != nullptr
              && _xkb_context_unref != nullptr
          ) {
            void *display = _wl_display_connect(nullptr);
            if (display != nullptr) {
              _wl_display_disconnect(display);
              SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "wayland", SDL_HINT_OVERRIDE);
            }
          }
        }
        if (xkbcommon != nullptr) {
          SDL_UnloadObject(xkbcommon);
        }
        if (wayland_cursor != nullptr) {
          SDL_UnloadObject(wayland_cursor);
        }
        if (wayland_client != nullptr) {
          SDL_UnloadObject(wayland_client);
        }
      }
      sdl_videodriver = SDL_GetHint(SDL_HINT_VIDEODRIVER);
      if (sdl_videodriver == nullptr || sdl_videodriver[0] == 0) {
        /* Select SDL's X11 video driver, with fallback to KMSDRM, if SDL_VIDEODRIVER is unset and X11 support is available on the user's machine */
        void *x11 = SDL_LoadObject(MKXPZ_X11_SONAME);
        void *xcursor = SDL_LoadObject(MKXPZ_XCURSOR_SONAME);
        void *xext = SDL_LoadObject(MKXPZ_XEXT_SONAME);
        void *xfixes = SDL_LoadObject(MKXPZ_XFIXES_SONAME);
        void *xi = SDL_LoadObject(MKXPZ_XI_SONAME);
        void *xrandr = SDL_LoadObject(MKXPZ_XRANDR_SONAME);
        if (
          x11 != nullptr
            && xcursor != nullptr
            && xext != nullptr
            && xfixes != nullptr
            && xi != nullptr
            && xrandr != nullptr
        ) {
          SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "x11,kmsdrm", SDL_HINT_OVERRIDE);
        }
        if (x11 != nullptr) {
          SDL_UnloadObject(x11);
        }
        if (xcursor != nullptr) {
          SDL_UnloadObject(xcursor);
        }
        if (xext != nullptr) {
          SDL_UnloadObject(xext);
        }
        if (xfixes != nullptr) {
          SDL_UnloadObject(xfixes);
        }
        if (xi != nullptr) {
          SDL_UnloadObject(xi);
        }
        if (xrandr != nullptr) {
          SDL_UnloadObject(xrandr);
        }
      }
      sdl_videodriver = SDL_GetHint(SDL_HINT_VIDEODRIVER);
      if (sdl_videodriver == nullptr || sdl_videodriver[0] == 0) {
        /* Otherwise, use KMSDRM */
        SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "kmsdrm", SDL_HINT_OVERRIDE);
      }

      /* Prevent ANGLE from using Wayland if we haven't selected SDL's Wayland video driver */
      sdl_videodriver = SDL_GetHint(SDL_HINT_VIDEODRIVER);
      assert(sdl_videodriver != nullptr && sdl_videodriver[0] != 0); /* Should already have been explicitly set by the Wayland check above */
      if (
        (sdl_videodriver[0] != 'W' && sdl_videodriver[0] != 'w')
          || (sdl_videodriver[1] != 'A' && sdl_videodriver[1] != 'a')
          || (sdl_videodriver[2] != 'Y' && sdl_videodriver[2] != 'y')
          || (sdl_videodriver[3] != 'L' && sdl_videodriver[3] != 'l')
          || (sdl_videodriver[4] != 'A' && sdl_videodriver[4] != 'a')
          || (sdl_videodriver[5] != 'N' && sdl_videodriver[5] != 'n')
          || (sdl_videodriver[6] != 'D' && sdl_videodriver[6] != 'd')
          || sdl_videodriver[7] != 0
      ) {
        mkxp_setenv("WAYLAND_DISPLAY", nullptr);
      }
    }
#endif // MKXPZ_CHECK_FOR_WAYLAND_SUPPORT

#ifdef MKXPZ_HAVE_ANGLE
    bool angle_allow_fallback = false;
    {
      const char *angle_default_platform = getenv("ANGLE_DEFAULT_PLATFORM");
      switch (conf.renderer) {
        default:
          angle_allow_fallback = true;
          if (angle_default_platform == nullptr || angle_default_platform[0] == 0) {
#  ifdef MKXPZ_HAVE_ANGLE_METAL
            mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "metal");
#  elif !defined(MKXPZ_HAVE_ANGLE_DIRECT3D9) && !defined(MKXPZ_HAVE_ANGLE_DIRECT3D11)
#    ifdef MKXPZ_HAVE_ANGLE_VULKAN
#      ifdef MKXPZ_CHECK_FOR_LAVAPIPE
            /* Check if ANGLE's Vulkan backend would use LLVMpipe. If so, use OpenGL instead. */
            mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "gl");
            VkResult result = volkInitialize();
            VkInstance instance;
            uint32_t physicalDeviceCount;
            std::vector<VkPhysicalDevice> physicalDevices;
            if (result == VK_SUCCESS) {
              static const VkApplicationInfo applicationInfo {
                /*sType=*/VK_STRUCTURE_TYPE_APPLICATION_INFO,
                /*pNext=*/nullptr,
                /*pApplicationName=*/"",
                /*applicationVersion=*/0,
                /*pEngineName=*/"",
                /*engineVersion=*/0,
                /*apiVersion=*/VK_API_VERSION_1_0,
              };
              static const VkInstanceCreateInfo instanceCreateInfo {
                /*sType=*/VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                /*pNext=*/nullptr,
                /*flags=*/0,
                /*pApplicationInfo=*/&applicationInfo,
                /*enabledLayerCount=*/0,
                /*ppEnabledLayerNames=*/nullptr,
                /*enabledExtensionCount=*/0,
                /*ppEnabledExtensionNames=*/nullptr,
              };
              result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
            }
            if (result == VK_SUCCESS) {
              volkLoadInstance(instance);
              result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
              if (result == VK_SUCCESS) {
                physicalDevices.resize(physicalDeviceCount);
                result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());
              }
              if (result == VK_SUCCESS && !physicalDevices.empty()) {
                VkPhysicalDeviceProperties physicalDeviceProperties;
                VkPhysicalDevice preferredPhysicalDevice = physicalDevices[0];
                const char *anglePreferredDevice = getenv("ANGLE_PREFERRED_DEVICE");
                if (anglePreferredDevice == nullptr) {
                  anglePreferredDevice = "";
                }
                for (VkPhysicalDevice physicalDevice : physicalDevices) {
                  vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
                  if (std::strcmp(physicalDeviceProperties.deviceName, anglePreferredDevice) == 0) {
                    preferredPhysicalDevice = physicalDevice;
                    break;
                  }
                }
                vkGetPhysicalDeviceProperties(preferredPhysicalDevice, &physicalDeviceProperties);
                if (std::strncmp(physicalDeviceProperties.deviceName, "llvmpipe ", sizeof "llvmpipe " - 1) != 0) {
                  mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "vulkan");
                }
              }
              vkDestroyInstance(instance, nullptr);
            }
#      else
            mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "vulkan");
#      endif // MKXPZ_CHECK_FOR_LAVAPIPE
#    else
            mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "gl");
#    endif // MKXPZ_HAVE_ANGLE_VULKAN
#  endif
          }
          break;
#ifdef MKXPZ_HAVE_ANGLE_NULL
        case 1:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "null");
          break;
#endif // MKXPZ_HAVE_ANGLE_NULL
        case 2:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "gl");
          break;
#ifdef MKXPZ_HAVE_ANGLE_VULKAN
        case 3:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "vulkan");
          break;
#endif // MKXPZ_HAVE_ANGLE_VULKAN
#ifdef MKXPZ_HAVE_ANGLE_METAL
        case 4:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "metal");
          break;
#elif defined(MKXPZ_HAVE_ANGLE_DIRECT3D9) || defined(MKXPZ_HAVE_ANGLE_DIRECT3D11)
        case 4:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", nullptr);
          break;
#elif defined(MKXPZ_HAVE_ANGLE_VULKAN)
        case 4:
          mkxp_setenv("ANGLE_DEFAULT_PLATFORM", "vulkan");
          break;
#endif // MKXPZ_HAVE_ANGLE_METAL
      }
      angle_default_platform = getenv("ANGLE_DEFAULT_PLATFORM");
      if (angle_default_platform != nullptr && std::strcmp(angle_default_platform, "gl") == 0) {
        mkxp_use_angle = false;
      }
    }

    if (mkxp_use_angle) {
      bool sdl_hint_opengl_es_driver = SDL_GetHintBoolean(SDL_HINT_OPENGL_ES_DRIVER, SDL_FALSE);
      bool sdl_hint_video_x11_force_egl = SDL_GetHintBoolean(SDL_HINT_VIDEO_X11_FORCE_EGL, SDL_FALSE);
      SDL_SetHintWithPriority(SDL_HINT_OPENGL_ES_DRIVER, "1", SDL_HINT_OVERRIDE);
      SDL_SetHintWithPriority(SDL_HINT_VIDEO_X11_FORCE_EGL, "1", SDL_HINT_OVERRIDE);
      if (angle_allow_fallback && (win = initVideo(conf)) == nullptr) {
        // Try again without ANGLE
        mkxp_use_angle = false;
        SDL_SetHintWithPriority(SDL_HINT_OPENGL_ES_DRIVER, sdl_hint_opengl_es_driver ? "1" : "0", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_X11_FORCE_EGL, sdl_hint_video_x11_force_egl ? "1" : "0", SDL_HINT_OVERRIDE);
      }
    }
#endif // MKXPZ_HAVE_ANGLE

    if (win == nullptr && (win = initVideo(conf)) == nullptr) {
      showInitError(std::string("Error creating window: ") + SDL_GetError());
      return 0;
    }

    if (!EventThread::allocUserEvents()) {
      showInitError("Error allocating SDL user events");
      return 0;
    }

#ifdef MKXPZ_STEAM
    if (!STEAMSHIM_init()) {
      showInitError("Failed to initialize Steamworks. The application cannot "
                    "continue launching.");
      SDL_Quit();
      return 0;
    }
#endif

    assert(conf.rgssVersion >= 1 && conf.rgssVersion <= 3);
    printRgssVersion(conf.rgssVersion);

    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (IMG_Init(imgFlags) != imgFlags) {
      showInitError(std::string("Error initializing SDL_image: ") +
                    SDL_GetError());
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }

    if (TTF_Init() < 0) {
      showInitError(std::string("Error initializing SDL_ttf: ") +
                    SDL_GetError());
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }

    if (Sound_Init() == 0) {
      showInitError(std::string("Error initializing SDL_sound: ") +
                    Sound_GetError());
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }
#if defined(__WIN32__)
    WSAData wsadata = {0};
    if (WSAStartup(0x101, &wsadata) || wsadata.wVersion != 0x101) {
      char buf[200];
      snprintf(buf, sizeof(buf), "Error initializing winsock: %08X",
               WSAGetLastError());
      showInitError(
          std::string(buf)); // Not an error worth ending the program over
    }
#endif
    
#ifdef __APPLE__
    {
        std::string downloadsPath = "/Users/" + mkxp_sys::getUserName() + "/Downloads";
        
        if (mkxp_fs::getCurrentDirectory().find(downloadsPath) == 0) {
            showInitError(conf.game.title +
                          " cannot run from the Downloads directory.\n\n" +
                          "Please move the application to the Applications folder (or anywhere else) " +
                          "and try again.");
#ifdef MKXPZ_STEAM
            STEAMSHIM_deinit();
#endif
            return 0;
        }
    }
#endif
    
#ifdef __APPLE__
#define DEBUG_FSELECT_MSG "Select the folder from which to load game files. This is the folder containing the game's INI."
#define DEBUG_FSELECT_PROMPT "Load Game"
    if (conf.manualFolderSelect) {
        std::string dataDirStr = mkxp_fs::selectPath(win, DEBUG_FSELECT_MSG, DEBUG_FSELECT_PROMPT);
        if (!dataDirStr.empty()) {
            conf.gameFolder = dataDirStr;
            mkxp_fs::setCurrentDirectory(dataDirStr.c_str());
            Debug() << "Current directory set to" << dataDirStr;
            conf.read(argc, argv);
            conf.readGameINI();
        }
    }
#endif

    /* OSX and Windows have their own native ways of
     * dealing with icons; don't interfere with them */
#ifdef __LINUX__
    setupWindowIcon(conf, win);
#else
    (void)setupWindowIcon;
#endif

    ALCdevice *alcDev = alcOpenDevice(0);

    if (!alcDev) {
      showInitError("Could not detect an available audio device.");
      SDL_DestroyWindow(win);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif
      return 0;
    }

    SDL_DisplayMode mode;
    SDL_GetDisplayMode(0, 0, &mode);

    /* Can't sync to display refresh rate if its value is unknown */
    if (!mode.refresh_rate)
      conf.syncToRefreshrate = false;

    EventThread eventThread;

#ifndef MKXPZ_INIT_GL_LATER
    SDL_GLContext glCtx = initGL(win, conf, 0);
#else
    SDL_GLContext glCtx = NULL;
#endif

    RGSSThreadData rtData(&eventThread, argv[0], win, alcDev, mode.refresh_rate,
                          mkxp_sys::getScalingFactor(), conf, glCtx);

    int winW, winH, drwW, drwH;
    SDL_GetWindowSize(win, &winW, &winH);
    rtData.windowSizeMsg.post(Vec2i(winW, winH));
    
    SDL_GL_GetDrawableSize(win, &drwW, &drwH);
    rtData.drawableSizeMsg.post(Vec2i(drwW, drwH));

    /* Load and post key bindings */
    rtData.bindingUpdateMsg.post(loadBindings(conf));
    
#ifdef __APPLE__
    // Create Touch Bar
    initTouchBar(win, conf);
#endif

    /* Start RGSS thread. Ruby 3.1 can exhaust Android pthread defaults during require/eval. */
#ifdef __ANDROID__
    static const size_t rpgmpRgssStackSize = 8u * 1024u * 1024u;
    Debug() << "[RPGMP-MODERN] RGSS-THREAD-STACK=" << rpgmpRgssStackSize;
    SDL_Thread *rgssThread = SDL_CreateThreadWithStackSize(rgssThreadFun, "rgss", rpgmpRgssStackSize, &rtData);
#else
    SDL_Thread *rgssThread = SDL_CreateThread(rgssThreadFun, "rgss", &rtData);
#endif

    /* Start event processing */
    eventThread.process(rtData);

    /* Request RGSS thread to stop */
    rtData.rqTerm.set();

    /* Wait for RGSS thread response */
    for (int i = 0; i < 1000; ++i) {
      /* We can stop waiting when the request was ack'd */
      if (rtData.rqTermAck) {
        Debug() << "RGSS thread ack'd request after" << i * 10 << "ms";
        break;
      }

      /* Give RGSS thread some time to respond */
      SDL_Delay(10);
    }

    /* If RGSS thread ack'd request, wait for it to shutdown,
     * otherwise abandon hope and just end the process as is. */
    if (rtData.rqTermAck)
      SDL_WaitThread(rgssThread, 0);
    else
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
          std::string("The RGSS script seems to be stuck. "+conf.game.title+" will now force quit.").c_str(),
          win);

    if (!rtData.rgssErrorMsg.empty()) {
      Debug() << rtData.rgssErrorMsg;
      if (!rpgmpAutoFallback) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
                                 rtData.rgssErrorMsg.c_str(), win);
      }
    }

    if (rtData.glContext)
      SDL_GL_DeleteContext(rtData.glContext);

    /* Clean up any remainin events */
    eventThread.cleanup();

    Debug() << "Shutting down.";

    alcCloseDevice(alcDev);
    SDL_DestroyWindow(win);

#if defined(__WIN32__)
    if (wsadata.wVersion)
      WSACleanup();
#endif

#ifdef MKXPZ_STEAM
    STEAMSHIM_deinit();
#endif
    Sound_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData) {
  SDL_GLContext glCtx{};

  /* Setup GL context. Must be done in main thread since macOS 10.15 */
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
  if (conf.debugMode)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

  glCtx = SDL_GL_CreateContext(win);

  if (!glCtx) {
    GLINIT_SHOWERROR(std::string("Could not create OpenGL context: ") + SDL_GetError());
    return 0;
  }

  try {
    initGLFunctions();
  } catch (const Exception &exc) {
    GLINIT_SHOWERROR(exc.msg);
    SDL_GL_DeleteContext(glCtx);

    return 0;
  }

  if (!conf.enableBlitting)
    gl.BlitFramebuffer = 0;

  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(win);

  printGLInfo();

  Debug() << "[RPGMP-MODERN] VSYNC-BEGIN";
  bool vsync = conf.vsync || conf.syncToRefreshrate;
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);
  Debug() << "[RPGMP-MODERN] VSYNC-END";

  // GLDebugLogger dLogger;
  return glCtx;
}


