#include "fluid-fun.h"

#include <string.h>
#include <SDL_loadso.h>
#include <SDL_platform.h>

#include "debugwriter.h"

#if __ANDROID__
#define FLUID_LIB "libfluidlite.so"
#elif __LINUX__
#define FLUID_LIB "libfluidsynth.so.3"
#elif MKXPZ_BUILD_XCODE
#define FLUID_LIB "@rpath/libfluidsynth.dylib"
#elif __APPLE__
#define FLUID_LIB "libfluidsynth.3.dylib"
#elif __WIN32__
#define FLUID_LIB "fluidsynth.dll"
#else
#error "platform not recognized"
#endif

struct FluidFunctions fluid;
bool rpgmpFluidLiteActive = false;
#ifndef SHARED_FLUID
static void *so;
#endif

void initFluidFunctions()
{
#ifdef SHARED_FLUID

#define FLUID_FUN(name, type) \
	fluid.name = fluid_##name;

#define FLUID_FUN2(name, type, real_name) \
	fluid.name = real_name;

#else
	so = SDL_LoadObject(FLUID_LIB);
#if __ANDROID__
	rpgmpFluidLiteActive = (so != 0);
	if (!so)
	{
		Debug() << "[RPGMP MIDI A169] FluidLite load failed; trying original FluidSynth";
		so = SDL_LoadObject("libfluidsynth.so");
	}
#endif

	if (!so)
		goto fail;

#define FLUID_FUN(name, type) \
	fluid.name = (type) SDL_LoadFunction(so, "fluid_" #name); \
	if (!fluid.name) \
		goto fail;

#define FLUID_FUN2(name, type, real_name) \
	fluid.name = (type) SDL_LoadFunction(so, #real_name); \
	if (!fluid.name) \
		goto fail;
#endif

FLUID_FUNCS
FLUID_FUNCS2

#if __ANDROID__
	Debug() << "[RPGMP MIDI A169] backend="
	        << (rpgmpFluidLiteActive ? "FluidLite" : "FluidSynth-fallback");
#endif

	return;

#ifndef SHARED_FLUID
fail:
	Debug() << "Failed to load " FLUID_LIB ". Midi playback is disabled.";

	memset(&fluid, 0, sizeof(fluid));
	SDL_UnloadObject(so);
	so = 0;
#endif
}
