//
//  config.cpp
//  Player
//
//  Created by ゾロアーク on 11/21/20.
//

#include "config.h"
#include <SDL_filesystem.h>
#include <assert.h>

#include <stdint.h>
#include <vector>

#include "filesystem/filesystem.h"
#include "util/exception.h"
#include "util/debugwriter.h"
#include "util/sdl-util.h"
#include "util/util.h"

#include "util/json5pp.hpp"

#include "util/iniconfig.h"
#include "util/encoding.h"

#include "system/system.h"


namespace json = json5pp;

std::string prefPath(const char *org, const char *app) {
    char *path = SDL_GetPrefPath(org, app);
    if (!path)
        return std::string("");
    std::string ret(path);
    SDL_free(path);
    return ret;
}

void fillStringVec(json::value &item, std::vector<std::string> &vector) {
    if (!item.is_array()) {
        if (item.is_string()) {
            vector.push_back(item.as_string());
        }
        return;
    }
    auto &array = item.as_array();
    for (size_t i = 0; i < array.size(); i++) {
        if (!array[i].is_string())
            continue;
        
        vector.push_back(array[i].as_string());
    }
}

bool copyObject(json::value &dest, json::value &src, const char *objectName = "") {
    assert(dest.is_object());
    if (src.is_null())
        return false;
    
    if (!src.is_object())
        return false;
    
    auto &srcVec = src.as_object();
    auto &destVec = dest.as_object();
    
    for (auto it : srcVec) {
        // Specifically processs this object later.
        if (it.second.is_object() && destVec[it.first].is_object())
            continue;
        
        if ((it.second.is_array() && destVec[it.first].is_array())    ||
            (it.second.is_number() && destVec[it.first].is_number())  ||
            (it.second.is_string() && destVec[it.first].is_string())  ||
            (it.second.is_boolean() && destVec[it.first].is_boolean()) ||
            (destVec[it.first].is_null()))
        {
            destVec[it.first] = it.second;
        }
        else {
            Debug() << "Invalid variable in configuration:" << objectName << it.first;
        }
    }
    return true;
}

bool getEnvironmentBool(const char *env, bool defaultValue) {
    const char *e = SDL_getenv(env);
    if (!e)
        return defaultValue;
    
    if (!strcmp(e, "0"))
        return false;
    else if (!strcmp(e, "1"))
        return true;
    
    return defaultValue;
}

json::value readConfFile(const char *path) {
    
    json::value ret(0);
    if (!mkxp_fs::fileExists(path)) {
        return json::object({});
    }
    
    try {
        std::string cfg = mkxp_fs::contentsOfFileAsString(path);
        ret = json::parse5(Encoding::convertString(cfg));
    }
    catch (const std::exception &e) {
        Debug() << "Failed to parse" << path << ":" << e.what();
    }
    catch (const Exception &e) {
        Debug() << "Failed to parse" << path << ":" << "Unknown encoding";
    }
    
    if (!ret.is_object())
        ret = json::object({});
    
    return ret;
}

#define CONF_FILE "mkxp.json"

Config::Config() {}

void Config::read(int argc, char *argv[]) {
    auto optsJ = json::object({
        {"rgssVersion", 0},
        {"debugMode", false},
        {"displayFPS", false},
        {"printFPS", false},
        {"winResizable", true},
        {"fullscreen", false},
        {"fixedAspectRatio", true},
        {"smoothScaling", 0},
        {"smoothScalingDown", 0},
        {"bitmapSmoothScaling", 0},
        {"bitmapSmoothScalingDown", 0},
        {"smoothScalingMipmaps", false},
        {"bicubicSharpness", 100},
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
        {"xbrzScalingFactor", 1.},
#endif
        {"enableHires", false},
        {"textureScalingFactor", 1.},
        {"framebufferScalingFactor", 1.},
        {"atlasScalingFactor", 1.},
        {"vsync", false},
        {"defScreenW", 0},
        {"defScreenH", 0},
        {"windowTitle", ""},
        {"fixedFramerate", 0},
        {"frameSkip", false},
        {"syncToRefreshrate", false},
        {"solidFonts", json::array({})},
        {"renderer", 0},
        {"subImageFix", false},
#ifdef __WIN32__
        {"enableBlitting", false},
#else
        {"enableBlitting", true},
#endif
        {"integerScalingActive", false},
        {"integerScalingLastMile", true},
        {"maxTextureSize", 0},
        {"gameFolder", ""},
        {"anyAltToggleFS", false},
        {"enableReset", true},
        {"enableSettings", true},
        {"allowSymlinks", true},
        {"dataPathOrg", ""},
        {"dataPathApp", ""},
        {"iconPath", ""},
        {"execName", "Game"},
        {"midiSoundFont", ""},
        {"midiChorus", false},
        {"midiReverb", false},
        {"SESourceCount", 6},
        {"BGMTrackCount", 1},
        {"customScript", ""},
        {"pathCache", true},
        {"useScriptNames", true},
        {"preloadScript", json::array({})},
        {"postloadScript", json::array({})},
        {"RTP", json::array({})},
        {"patches", json::array({})},
        {"fontSub", json::array({})},
        {"fontScale", 0.0f},
        {"fontKerning", true},
        {"fontHinting", 3}, // TTF_HINTING_NONE
        {"fontHeightReporting", 0},
        {"fontOutlineCrop", true},
        {"rubyLoadpath", json::array({})},
        {"JITEnable", false},
        {"JITVerboseLevel", 0},
        {"JITMaxCache", 100},
        {"JITMinCalls", 10000},
        {"YJITEnable", false},
        {"dumpAtlas", false},
        {"bindingNames", json::object({
            {"a", "A"},
            {"b", "B"},
            {"c", "C"},
            {"x", "X"},
            {"y", "Y"},
            {"z", "Z"},
            {"l", "L"},
            {"r", "R"}
        })}
    });
    
    auto &opts = optsJ.as_object();
    
#define GUARD(exp) \
try { exp } catch (...) {}
    
    editor.debug = false;
    editor.battleTest = false;
    
    std::string androidConfigArg;
#ifdef MKXPZ_BUILD_ANDROID
    if (argc > 1 && argv[1] && mkxp_fs::fileExists(argv[1])) {
        std::string arg1(argv[1]);
        if (arg1.size() >= 5 && arg1.substr(arg1.size() - 5) == ".json") {
            androidConfigArg = arg1;
            Debug() << "[RPGMP-MODERN] BASE-CONFIG-ARG=" << androidConfigArg;
        }
    }
#endif

    if (argc > 1) {
        if (!strcmp(argv[1], "debug") || !strcmp(argv[1], "test"))
            editor.debug = true;
        else if (!strcmp(argv[1], "btest"))
            editor.battleTest = true;
        
        for (int i = 1; i < argc; i++) {
            bool skipAndroidConfigArg = false;
#ifdef MKXPZ_BUILD_ANDROID
            skipAndroidConfigArg = (i == 1 && !androidConfigArg.empty());
#endif
            if (strcmp(argv[i], "debug") && !skipAndroidConfigArg)
                launchArgs.push_back(argv[i]);
        }
    }
    
    json::value baseConf =
#ifdef MKXPZ_BUILD_ANDROID
        androidConfigArg.empty() ? readConfFile(CONF_FILE)
                                 : readConfFile(androidConfigArg.c_str());
#else
        readConfFile(CONF_FILE);
#endif
    copyObject(optsJ, baseConf);
    copyObject(opts["bindingNames"], baseConf.as_object()["bindingNames"], "bindingNames .");
    
#define SET_OPT_CUSTOMKEY(var, key, type) GUARD(var = opts[#key].as_##type();)
#define SET_OPT(var, type) SET_OPT_CUSTOMKEY(var, var, type)
#define SET_STRINGOPT(var, key) GUARD(var = std::string(opts[#key].as_string());)
    
    SET_STRINGOPT(gameFolder, gameFolder);
    SET_STRINGOPT(dataPathOrg, dataPathOrg);
    SET_STRINGOPT(dataPathApp, dataPathApp);
    SET_STRINGOPT(iconPath, iconPath);
    SET_STRINGOPT(execName, execName);
    SET_OPT(allowSymlinks, boolean);
    SET_OPT(pathCache, boolean);
    SET_OPT_CUSTOMKEY(jit.enabled, JITEnable, boolean);
    SET_OPT_CUSTOMKEY(jit.verboseLevel, JITVerboseLevel, integer);
    SET_OPT_CUSTOMKEY(jit.maxCache, JITMaxCache, integer);
    SET_OPT_CUSTOMKEY(jit.minCalls, JITMinCalls, integer);
    SET_OPT_CUSTOMKEY(yjit.enabled, YJITEnable, boolean);
    SET_OPT(rgssVersion, integer);
    SET_OPT(defScreenW, integer);
    SET_OPT(defScreenH, integer);
    
#ifndef MKXPZ_BUILD_ANDROID
    if (!gameFolder.empty() && !mkxp_fs::setCurrentDirectory(gameFolder.c_str())) {
        throw Exception(Exception::MKXPError, "Unable to switch into gameFolder %s", gameFolder.c_str());
    }
#else
    Debug() << "[RPGMP-MODERN] CONFIG-GAMEFOLDER=" << gameFolder;
    if (!gameFolder.empty()) {
        if (mkxp_fs::setCurrentDirectory(gameFolder.c_str()))
            Debug() << "[RPGMP-MODERN] CWD-GAMEFOLDER-SET=" << gameFolder;
        else
            Debug() << "[RPGMP-MODERN] CWD-GAMEFOLDER-SET-FAILED=" << gameFolder;
    }
#endif
    
    readGameINI();
    
    // Now check for an extra mkxp.conf in the user's save directory and merge anything else from that
    userConfPath = mkxp_fs::normalizePath(std::string(customDataPath + "/" CONF_FILE).c_str(), 0, 1);
    json::value userConf = readConfFile(userConfPath.c_str());
    copyObject(optsJ, userConf);
    
    // now RESUME
    
    SET_OPT(debugMode, boolean);
    SET_OPT(displayFPS, boolean);
    SET_OPT(printFPS, boolean);
    SET_OPT(fullscreen, boolean);
    SET_OPT(fixedAspectRatio, boolean);
    SET_OPT(smoothScaling, integer);
    SET_OPT(smoothScalingDown, integer);
    SET_OPT(bitmapSmoothScaling, integer);
    SET_OPT(bitmapSmoothScalingDown, integer);
    SET_OPT(smoothScalingMipmaps, boolean);
    SET_OPT(bicubicSharpness, integer);
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
    SET_OPT(xbrzScalingFactor, integer);
#endif
    SET_OPT(enableHires, boolean);
    SET_OPT(textureScalingFactor, number);
    SET_OPT(framebufferScalingFactor, number);
    SET_OPT(atlasScalingFactor, number);
    SET_OPT(winResizable, boolean);
    SET_OPT(vsync, boolean);
    SET_STRINGOPT(windowTitle, windowTitle);
    SET_OPT(fixedFramerate, integer);
    SET_OPT(frameSkip, boolean);
    SET_OPT(syncToRefreshrate, boolean);
    fillStringVec(opts["solidFonts"], solidFonts);
    for (std::string & solidFont : solidFonts)
        std::transform(solidFont.begin(), solidFont.end(), solidFont.begin(),
            [](unsigned char c) { return std::tolower(c); });
    SET_OPT(renderer, integer);
    SET_OPT(subImageFix, boolean);
    SET_OPT(enableBlitting, boolean);
    SET_OPT_CUSTOMKEY(integerScaling.active, integerScalingActive, boolean);
    SET_OPT_CUSTOMKEY(integerScaling.lastMileScaling, integerScalingLastMile, boolean);
    SET_OPT(maxTextureSize, integer);
    SET_OPT(anyAltToggleFS, boolean);
    SET_OPT(enableReset, boolean);
    SET_OPT(enableSettings, boolean);
    SET_STRINGOPT(midi.soundFont, midiSoundFont);
    SET_OPT_CUSTOMKEY(midi.chorus, midiChorus, boolean);
    SET_OPT_CUSTOMKEY(midi.reverb, midiReverb, boolean);
    SET_OPT_CUSTOMKEY(SE.sourceCount, SESourceCount, integer);
    SET_OPT_CUSTOMKEY(BGM.trackCount, BGMTrackCount, integer);
    SET_STRINGOPT(customScript, customScript);
    SET_OPT(useScriptNames, boolean);
    SET_OPT(dumpAtlas, boolean);
    
    fillStringVec(opts["preloadScript"], preloadScripts);
    fillStringVec(opts["postloadScript"], postloadScripts);
    fillStringVec(opts["RTP"], rtps);
    fillStringVec(opts["patches"], patches);
    fillStringVec(opts["fontSub"], fontSubs);
    for (std::string & fontSub : fontSubs)
        std::transform(fontSub.begin(), fontSub.end(), fontSub.begin(),
            [](unsigned char c) { return std::tolower(c); });
    SET_OPT(fontScale, number);
    SET_OPT(fontKerning, boolean);
    SET_OPT(fontHinting, integer);
    SET_OPT(fontHeightReporting, integer);
    SET_OPT(fontOutlineCrop, boolean);
    fillStringVec(opts["rubyLoadpath"], rubyLoadpaths);
    
    auto &bnames = opts["bindingNames"].as_object();
    
#define BINDING_NAME(btn) kbActionNames.btn = bnames[#btn].as_string()
    BINDING_NAME(a);
    BINDING_NAME(b);
    BINDING_NAME(c);
    BINDING_NAME(x);
    BINDING_NAME(y);
    BINDING_NAME(z);
    BINDING_NAME(l);
    BINDING_NAME(r);
    
    rgssVersion = clamp(rgssVersion, 0, 3);
    SE.sourceCount = clamp(SE.sourceCount, 1, 64);
    BGM.trackCount = clamp(BGM.trackCount, 1, 16);
    
    // Determine whether to open a console window on... Windows
    winConsole = getEnvironmentBool("MKXPZ_WINDOWS_CONSOLE", editor.debug);
    
    // Determine whether to allow manual selection of a game folder on startup
    // Only works on macOS atm, mainly used to test games located outside of the bundle.
    // The config is re-read after the window is already created, so some entries
    // may not take effect
    manualFolderSelect = getEnvironmentBool("MKXPZ_FOLDER_SELECT", false);
    
    raw = optsJ;
}

static void setupScreenSize(Config &conf) {
    if (conf.defScreenW <= 0)
        conf.defScreenW = (conf.rgssVersion == 1 ? 640 : 544);
    
    if (conf.defScreenH <= 0)
        conf.defScreenH = (conf.rgssVersion == 1 ? 480 : 416);
}

bool Config::fontIsSolid(const char *fontName) const {
    for (std::string solidfont : solidFonts)
        if (!strcmp(solidfont.c_str(), fontName)) return true;
    
    return false;
}

void Config::readGameINI() {
    if (!customScript.empty()) {
        game.title = customScript.c_str();
        
        if (rgssVersion == 0)
            rgssVersion = 1;
        
        setupScreenSize(*this);
        
        return;
    }
    
    std::string iniFileName(execName + ".ini");
#ifdef MKXPZ_BUILD_ANDROID
    if (!gameFolder.empty()) {
        iniFileName = gameFolder + "/" + execName + ".ini";
        Debug() << "[RPGMP-MODERN] GAMEINI-ABS=" << iniFileName;
    }
#endif
    SDLRWStream iniFile(iniFileName.c_str(), "r");
    
    bool convSuccess = false;
    if (iniFile)
    {
        INIConfiguration ic;
        if (ic.load(iniFile.stream()))
        {
            GUARD(game.title = ic.getStringProperty("Game", "Title"););
            GUARD(game.scripts = ic.getStringProperty("Game", "Scripts"););
            
            strReplace(game.scripts, '\\', '/');
            
            if (game.title.empty()) {
                Debug() << iniFileName + ": Could not find Game.Title";
            }
            
            if (game.scripts.empty())
                Debug() << iniFileName + ": Could not find Game.Scripts";
        }
    }
    else
        Debug() << "Could not read" << iniFileName;
    
    try {
        game.title = Encoding::convertString(game.title);
        convSuccess = true;
    }
    catch (const Exception &e) {
#ifdef MKXPZ_BUILD_ANDROID
        try {
            game.title = Encoding::convertString(game.title, "CP949");
            convSuccess = true;
            Debug() << "[RPGMP-MODERN] GAMEINI-TITLE-CP949-FALLBACK";
        }
        catch (const Exception &) {
            Debug() << iniFileName + ": Could not determine encoding of Game.Title";
        }
#else
        Debug() << iniFileName + ": Could not determine encoding of Game.Title";
#endif
    }
    
    if (game.title.empty() || !convSuccess)
        game.title = "mkxp-z";
    
    if (dataPathOrg.empty())
        dataPathOrg = ".";
    
    if (dataPathApp.empty())
        dataPathApp = game.title;
    
#ifdef MKXPZ_BUILD_ANDROID
    customDataPath = mkxp_fs::normalizePath(
        std::string(gameFolder + "/UserData/AppData").c_str(), 0, 1);
    if (!customDataPath.empty() && customDataPath.back() != '/')
        customDataPath += '/';
    if (mkxp_fs::createDirectories(customDataPath.c_str()))
        Debug() << "[RPGMP-MODERN] DATADIR-READY=" << customDataPath;
    else
        Debug() << "[RPGMP-MODERN] DATADIR-CREATE-FAILED=" << customDataPath;
#else
    customDataPath = mkxp_fs::normalizePath(prefPath(dataPathOrg.c_str(), dataPathApp.c_str()).c_str(), 0, 1);
#endif
    
    if (rgssVersion == 0) {
        /* Try to guess RGSS version based on Data/Scripts extension */
        rgssVersion = 1;
        
        if (!game.scripts.empty()) {
            const char *p = &game.scripts[game.scripts.size()];
            const char *head = &game.scripts[0];
            
            while (--p != head)
                if (*p == '.')
                    break;
            
            if (!strcmp(p, ".rvdata"))
                rgssVersion = 2;
            else if (!strcmp(p, ".rvdata2"))
                rgssVersion = 3;
        }
    }
    
    setupScreenSize(*this);
}
