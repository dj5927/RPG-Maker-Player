/*
** keybindings.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
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

#include "keybindings.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <string>
#include <vector>

struct KbBindingData
{
	SDL_Scancode source;
	Input::ButtonCode target;

	void add(BDescVec &d) const
	{
		SourceDesc src;
		src.type = Key;
		src.d.scan = source;

		BindingDesc desc;
		desc.src = src;
		desc.target = target;

		d.push_back(desc);
	}
};

struct CtrlBindingData
{
    SDL_GameControllerButton source;
    Input::ButtonCode target;
    
    void add(BDescVec &d) const
    {
        SourceDesc src;
        src.type = CButton;
        src.d.cb = source;
        
        BindingDesc desc;
        desc.src = src;
        desc.target = target;
        
        d.push_back(desc);
    }
};

/* Common */
static const KbBindingData defaultKbBindings[] =
{
	{ SDL_SCANCODE_LEFT,   Input::Left  },
	{ SDL_SCANCODE_RIGHT,  Input::Right },
	{ SDL_SCANCODE_UP,     Input::Up    },
	{ SDL_SCANCODE_DOWN,   Input::Down  },
    
	{ SDL_SCANCODE_SPACE,  Input::C     },
	{ SDL_SCANCODE_RETURN, Input::C     },
	{ SDL_SCANCODE_ESCAPE, Input::B     },
	{ SDL_SCANCODE_KP_0,   Input::B     },
	{ SDL_SCANCODE_LSHIFT, Input::A     },
	{ SDL_SCANCODE_X,      Input::B     },
	{ SDL_SCANCODE_D,      Input::Z     },
	{ SDL_SCANCODE_Q,      Input::L     },
	{ SDL_SCANCODE_W,      Input::R     },
	{ SDL_SCANCODE_A,      Input::X     },
	{ SDL_SCANCODE_S,      Input::Y     }
};

/* RGSS1 */
static const KbBindingData defaultKbBindings1[] =
{
	{ SDL_SCANCODE_Z,      Input::A     },
	{ SDL_SCANCODE_C,      Input::C     },
};

/* RGSS2 and higher */
static const KbBindingData defaultKbBindings2[] =
{
	{ SDL_SCANCODE_Z,      Input::C     }
};

static elementsN(defaultKbBindings);
static elementsN(defaultKbBindings1);
static elementsN(defaultKbBindings2);

static const CtrlBindingData defaultCtrlBindings[] =
{
	{ SDL_CONTROLLER_BUTTON_X, Input::A  },
	{ SDL_CONTROLLER_BUTTON_B, Input::B  },
	{ SDL_CONTROLLER_BUTTON_A, Input::C },
	{ SDL_CONTROLLER_BUTTON_Y, Input::X  },
	{ SDL_CONTROLLER_BUTTON_LEFTSTICK, Input::Y  },
	{ SDL_CONTROLLER_BUTTON_RIGHTSTICK, Input::Z },
	{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Input::L  },
	{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Input::R  },
    
    { SDL_CONTROLLER_BUTTON_DPAD_UP, Input::Up },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN, Input::Down },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, Input::Left },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Input::Right }
};

static elementsN(defaultCtrlBindings);

struct ControllerRemap
{
    SDL_GameControllerButton from;
    SDL_GameControllerButton to;
};

static std::string trimAscii(std::string value)
{
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ' || value.back() == '\t'))
        value.pop_back();
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
        ++start;
    return value.substr(start);
}

static SDL_GameControllerButton remapButtonFromName(std::string value)
{
    value = trimAscii(value);
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'a' && value[i] <= 'z')
            value[i] = value[i] - 'a' + 'A';

    if (value == "A") return SDL_CONTROLLER_BUTTON_A;
    if (value == "B") return SDL_CONTROLLER_BUTTON_B;
    if (value == "X") return SDL_CONTROLLER_BUTTON_X;
    if (value == "Y") return SDL_CONTROLLER_BUTTON_Y;
    if (value == "L3" || value == "LEFTSTICK") return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (value == "R3" || value == "RIGHTSTICK") return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    if (value == "L1" || value == "LEFTSHOULDER") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (value == "R1" || value == "RIGHTSHOULDER") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    return SDL_CONTROLLER_BUTTON_INVALID;
}


static const char *remapButtonName(SDL_GameControllerButton button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L1";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
    default: return "?";
    }
}

static std::vector<ControllerRemap> loadControllerRemaps()
{
    std::vector<ControllerRemap> result;
    const char *path = getenv("MKXP_CONTROLLER_REMAP_FILE");
    if (!path || !*path)
        return result;

    FILE *f = fopen(path, "rb");
    if (!f)
        return result;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        std::string text = trimAscii(line);
        if (text.empty() || text[0] == '#')
            continue;
        size_t eq = text.find('=');
        if (eq == std::string::npos)
            continue;
        SDL_GameControllerButton from = remapButtonFromName(text.substr(0, eq));
        SDL_GameControllerButton to = remapButtonFromName(text.substr(eq + 1));
        if (from == SDL_CONTROLLER_BUTTON_INVALID || to == SDL_CONTROLLER_BUTTON_INVALID)
            continue;
        result.push_back({from, to});
    }
    fclose(f);
    return result;
}

static void applyControllerRemaps(BDescVec &bindings)
{
    const std::vector<ControllerRemap> remaps = loadControllerRemaps();
    if (remaps.empty())
        return;

    for (size_t i = 0; i < remaps.size(); ++i)
        fprintf(stderr, "[controller remap] %s -> %s\n",
                remapButtonName(remaps[i].from), remapButtonName(remaps[i].to));

    bool claimed[SDL_CONTROLLER_BUTTON_MAX] = {};
    for (size_t i = 0; i < remaps.size(); ++i)
        claimed[remaps[i].to] = true;

    BDescVec out;
    out.reserve(bindings.size());

    for (size_t i = 0; i < bindings.size(); ++i)
    {
        BindingDesc desc = bindings[i];
        if (desc.src.type != CButton)
        {
            out.push_back(desc);
            continue;
        }

        const SDL_GameControllerButton original = desc.src.d.cb;
        bool moved = false;
        for (size_t j = 0; j < remaps.size(); ++j)
        {
            if (remaps[j].from == original)
            {
                desc.src.d.cb = remaps[j].to;
                out.push_back(desc);
                moved = true;
                break;
            }
        }

        if (moved)
            continue;

        if (original >= 0 && original < SDL_CONTROLLER_BUTTON_MAX && claimed[original])
            continue;

        out.push_back(desc);
    }

    bindings.swap(out);
}

static void addAxisBinding(BDescVec &d, SDL_GameControllerAxis axis, AxisDir dir, Input::ButtonCode target)
{
	SourceDesc src;
	src.type = CAxis;
	src.d.ca.axis = axis;
	src.d.ca.dir = dir;

	BindingDesc desc;
	desc.src = src;
	desc.target = target;

	d.push_back(desc);
}

BDescVec genDefaultBindings(const Config &conf)
{
	BDescVec d;

	for (size_t i = 0; i < defaultKbBindingsN; ++i)
		defaultKbBindings[i].add(d);

	if (conf.rgssVersion == 1)
		for (size_t i = 0; i < defaultKbBindings1N; ++i)
			defaultKbBindings1[i].add(d);
	else
		for (size_t i = 0; i < defaultKbBindings2N; ++i)
			defaultKbBindings2[i].add(d);

	for (size_t i = 0; i < defaultCtrlBindingsN; ++i)
		defaultCtrlBindings[i].add(d);

	addAxisBinding(d, SDL_CONTROLLER_AXIS_LEFTX, Negative, Input::Left );
	addAxisBinding(d, SDL_CONTROLLER_AXIS_LEFTX, Positive, Input::Right);
	addAxisBinding(d, SDL_CONTROLLER_AXIS_LEFTY, Negative, Input::Up   );
	addAxisBinding(d, SDL_CONTROLLER_AXIS_LEFTY, Positive, Input::Down );

	return d;
}

#define FORMAT_VER 3

struct Header
{
	uint32_t formVer;
	uint32_t rgssVer;
	uint32_t count;
};

static void buildPath(const std::string &dir, uint32_t rgssVersion,
                      char *out, size_t outSize)
{
	snprintf(out, outSize, "%skeybindings.mkxp%u", dir.c_str(), rgssVersion);
}

static bool writeBindings(const BDescVec &d, const std::string &dir,
                          uint32_t rgssVersion)
{
	if (dir.empty())
		return false;

	char path[1024];
	buildPath(dir, rgssVersion, path, sizeof(path));

	FILE *f = fopen(path, "wb");

	if (!f)
		return false;

	Header hd;
	hd.formVer = FORMAT_VER;
	hd.rgssVer = rgssVersion;
	hd.count = d.size();

	if (fwrite(&hd, sizeof(hd), 1, f) < 1)
	{
		fclose(f);
		return false;
	}

	if (fwrite(&d[0], sizeof(d[0]), hd.count, f) < hd.count)
	{
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

void storeBindings(const BDescVec &d, const Config &conf)
{
    writeBindings(d, conf.customDataPath, conf.rgssVersion);
}

#define READ(ptr, size, n, f) if (fread(ptr, size, n, f) < n) return false

static bool verifyDesc(const BindingDesc &desc)
{
	const Input::ButtonCode codes[] =
	{
	    Input::None,
	    Input::Down, Input::Left, Input::Right, Input::Up,
	    Input::A, Input::B, Input::C,
	    Input::X, Input::Y, Input::Z,
	    Input::L, Input::R,
	    Input::Shift, Input::Ctrl, Input::Alt,
	    Input::F5, Input::F6, Input::F7, Input::F8, Input::F9
	};

	elementsN(codes);
	size_t i;

	for (i = 0; i < codesN; ++i)
		if (desc.target == codes[i])
			break;

	if (i == codesN)
		return false;

	const SourceDesc &src = desc.src;

	switch (src.type)
	{
	case Invalid:
		return true;
	case Key:
		return src.d.scan < SDL_NUM_SCANCODES;
            
    case CButton:
        return true;

	case CAxis:
		return src.d.ca.dir == Negative || src.d.ca.dir == Positive;
	default:
		return false;
	}
}

static bool readBindings(BDescVec &out, const std::string &dir,
                         uint32_t rgssVersion)
{
	if (dir.empty())
		return false;

	char path[1024];
	buildPath(dir, rgssVersion, path, sizeof(path));

	FILE *f = fopen(path, "rb");

	if (!f)
		return false;

	Header hd;
	if (fread(&hd, sizeof(hd), 1, f) < 1)
	{
		fclose(f);
		return false;
	}

	if (hd.formVer != FORMAT_VER)
		return false;
	if (hd.rgssVer != rgssVersion)
		return false;
	/* Arbitrary max value */
	if (hd.count > 1024)
		return false;

	out.resize(hd.count);
	if (fread(&out[0], sizeof(out[0]), hd.count, f) < hd.count)
	{
		fclose(f);
		return false;
	}

	for (size_t i = 0; i < hd.count; ++i)
		if (!verifyDesc(out[i]))
			return false;

	return true;
}

BDescVec loadBindings(const Config &conf)
{
	BDescVec d;

	if (!readBindings(d, conf.customDataPath, conf.rgssVersion))
        d = genDefaultBindings(conf);

    applyControllerRemaps(d);
    return d;
}
