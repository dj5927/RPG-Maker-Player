package com.dj5927.rpgmakerplayer;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class MvMzCompatibilitySmoke {
    public static void main(String[] args) throws Exception {
        MvMzCompatibility.Profile profile = MvMzCompatibility.inspect(Arrays.asList(
                "/js/plugins/KhasUltraLighting.js",
                "/js/plugins/YEP_CoreEngine.js",
                "/js/plugins/VisuMZ_0_CoreEngine.js",
                "/js/plugins/Community_Basic.js",
                "/fonts/gamefont.css",
                "/fonts/AozoraMinchoRegular.ttf",
                "/audio/bgm/Title.rpgmvo"
        ), "MV");

        if (!profile.khas || !profile.yep || !profile.visuStella || !profile.communityBasic) {
            throw new AssertionError("signature profile detection failed");
        }
        if (!profile.bundledFont || !profile.encryptedOggAudio || profile.encryptedM4aAudio) {
            throw new AssertionError("font/encrypted audio profile detection failed");
        }
        if (!profile.oggAudioAvailable || profile.m4aAudioAvailable) {
            throw new AssertionError("OGG-only audio availability detection failed");
        }
        String html = "<html><head><script src=\"js/rpg_core.js\"></script>" +
                "<script src=\"js/rpg_managers.js\"></script>" +
                "<script src=\"js/main.js\"></script></head><body></body></html>";
        InputStream rewritten = MvMzCompatibility.rewriteIndexHtml(
                new java.io.ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8)), profile);
        String result = new String(rewritten.readAllBytes(), StandardCharsets.UTF_8);

        int compat = result.indexOf("rpgmp-android-compat");
        int core = result.indexOf("js/rpg_core.js");
        if (compat < 0 || core < 0 || compat > core) {
            throw new AssertionError("compat bootstrap was not injected before game scripts");
        }
        if (!result.contains("khas:true") || !result.contains("yep:true")) {
            throw new AssertionError("compat profile was not serialized into bootstrap");
        }
        if (result.contains("rpgmp-korean-font")) {
            throw new AssertionError("bundled game font must not be overridden by fallback font");
        }
        if (!result.contains("audio/ogg") || !result.contains("__rpgmpResumeAudio") ||
                !result.contains("AudioManager.audioFileExt=function(){return '.ogg';}")) {
            throw new AssertionError("encrypted OGG/audio resume compatibility bootstrap missing");
        }
        int late = result.indexOf("rpgmp-android-late");
        int managers = result.indexOf("js/rpg_managers.js");
        int main = result.indexOf("js/main.js");
        if (late < 0 || managers < 0 || main < 0 || late < managers || late > main) {
            throw new AssertionError("late audio bootstrap must run after AudioManager and before main.js");
        }
        if (!result.contains("__rpgmpAudioQueue") || !result.contains("playStaticSe','staticSe'")) {
            throw new AssertionError("startup audio queue guard missing");
        }
        if (!result.contains("__RPGMP_SAVE__") ||
                !result.contains("saveToWebStorage=function") ||
                !result.contains("saveToForage=function") ||
                !result.contains("original game-folder save bridge active")) {
            throw new AssertionError("persistent MV/MZ game-folder save bridge missing");
        }
        if (result.contains("Sprite_Picture.prototype") || result.contains("ImageManager.loadBitmap=function") ||
                result.contains("rpgmp-flicker-compat") || result.contains("__rpgmpImageWarmupStarted") ||
                result.contains("RPGMP IMAGE WARMUP")) {
            throw new AssertionError("retired flicker/image warm-up patches must not return");
        }
        if (result.contains("transitionFrameSkipRisk") || result.contains("__rpgmpTransitionNoSkip")) {
            throw new AssertionError("retired transition frame-skip guard returned");
        }
        if (result.contains("__rpgmpSmoothFade") ||
                result.contains("time-based fade presentation active")) {
            throw new AssertionError("retired time-based fade experiment returned");
        }

        profile.pixiTextureSlotRisk = true;
        String pixi = "if (nextTexture._virtalBoundId === -1) {\n" +
                "for (var j = 0; j < MAX_TEXTURES; ++j) {\n" +
                "var t = boundTextures[j];\n" +
                "if (t._enabled !== TICK) { nextTexture._virtalBoundId = j; break; }\n" +
                "}\n" +
                "}\n" +
                "                    nextTexture._enabled = TICK;\n\n" +
                "                    currentGroup.textureCount++;";
        InputStream pixiRewritten = MvMzCompatibility.rewritePixiJs(
                new java.io.ByteArrayInputStream(pixi.getBytes(StandardCharsets.UTF_8)), profile);
        String pixiResult = new String(pixiRewritten.readAllBytes(), StandardCharsets.UTF_8);
        if (!pixiResult.contains("recovered invalid texture slot -1") ||
                !pixiResult.contains("__rpgmpSlot") ||
                pixiResult.contains("RPGMP defer GC during fade")) {
            throw new AssertionError("invalid Pixi texture-slot recovery rewrite missing");
        }
        if (result.contains("preserveDrawingBuffer") || result.contains("webglPresentationRisk")) {
            throw new AssertionError("retired preserveDrawingBuffer experiment returned");
        }
        if (result.contains("aggressiveTextureGc") || result.contains("__rpgmpTextureGcGuard") ||
                result.contains("textureGC maxIdle")) {
            throw new AssertionError("ineffective A073 texture-GC guard must be absent");
        }

        MvMzCompatibility.Profile plainOgg = MvMzCompatibility.inspect(Arrays.asList(
                "/audio/bgm/Title.ogg", "/audio/se/Decision1.ogg", "/js/rpg_core.js"
        ), "MV");
        InputStream plainOggRewritten = MvMzCompatibility.rewriteIndexHtml(
                new java.io.ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8)), plainOgg);
        String plainOggResult = new String(plainOggRewritten.readAllBytes(), StandardCharsets.UTF_8);
        if (!plainOgg.oggAudioAvailable || plainOgg.m4aAudioAvailable ||
                !plainOggResult.contains("AudioManager.audioFileExt=function(){return '.ogg';}")) {
            throw new AssertionError("plain OGG-only MV audio override missing");
        }

        MvMzCompatibility.Profile noFont = MvMzCompatibility.inspect(Arrays.asList(
                "/js/rpg_core.js", "/img/system/IconSet.png"
        ), "MV");
        InputStream fallbackRewritten = MvMzCompatibility.rewriteIndexHtml(
                new java.io.ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8)), noFont);
        String fallbackResult = new String(fallbackRewritten.readAllBytes(), StandardCharsets.UTF_8);
        if (!fallbackResult.contains("font-family:GameFont") || !fallbackResult.contains("/__rpgmp_font.ttf")) {
            throw new AssertionError("fontless MV fallback GameFont injection missing");
        }

        System.out.println("MVMZ_COMPATIBILITY_SMOKE_PASS");
    }
}
