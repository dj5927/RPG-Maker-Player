package com.dj5927.rpgmakerplayer;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.regex.Pattern;

public final class MvMzCompatibility {
    public static final class Profile {
        public final String engine;
        public boolean khas;
        public boolean yep;
        public boolean visuStella;
        public boolean pixiFilters;
        public boolean communityBasic;
        public boolean bundledFont;
        public boolean encryptedOggAudio;
        public boolean encryptedM4aAudio;
        public boolean oggAudioAvailable;
        public boolean m4aAudioAvailable;
        public boolean plusPath;
        public boolean nonAsciiPath;
        public boolean nodeCompat;
        public boolean pixiTextureSlotRisk;
        public boolean mouseNativeInput;

        Profile(String engine) {
            this.engine = engine == null ? "UNKNOWN" : engine;
        }

        String json() {
            return "{" +
                    "engine:'" + js(engine) + "'," +
                    "khas:" + khas + "," +
                    "yep:" + yep + "," +
                    "visuStella:" + visuStella + "," +
                    "pixiFilters:" + pixiFilters + "," +
                    "communityBasic:" + communityBasic + "," +
                    "bundledFont:" + bundledFont + "," +
                    "encryptedOggAudio:" + encryptedOggAudio + "," +
                    "encryptedM4aAudio:" + encryptedM4aAudio + "," +
                    "oggAudioAvailable:" + oggAudioAvailable + "," +
                    "m4aAudioAvailable:" + m4aAudioAvailable + "," +
                    "plusPath:" + plusPath + "," +
                    "nonAsciiPath:" + nonAsciiPath + "," +
                    "nodeCompat:" + nodeCompat + "," +
                    "pixiTextureSlotRisk:" + pixiTextureSlotRisk + "," +
                    "mouseNativeInput:" + mouseNativeInput +
                    "}";
        }
    }

    private MvMzCompatibility() {}

    public static Profile inspect(Iterable<String> paths, String engine) {
        Profile profile = new Profile(engine);
        for (String raw : paths) {
            if (raw == null) continue;
            if (raw.indexOf('+') >= 0) profile.plusPath = true;
            for (int i = 0; i < raw.length(); i++) {
                if (raw.charAt(i) > 127) {
                    profile.nonAsciiPath = true;
                    break;
                }
            }
            String p = raw.toLowerCase(Locale.ROOT);
            if (p.contains("/khas") || p.contains("khasultralighting") || p.contains("khas_graphics")) profile.khas = true;
            if (p.contains("/yep_") || p.contains("yep_coreengine")) profile.yep = true;
            if (p.contains("visustella") || p.contains("visumz_")) profile.visuStella = true;
            if (p.contains("pixifilter") || p.contains("pixi-filter") || p.contains("pixi_filters")) profile.pixiFilters = true;
            if (p.contains("community_basic") || p.contains("communitybasic")) profile.communityBasic = true;
            if (p.startsWith("/fonts/") &&
                    (p.endsWith(".ttf") || p.endsWith(".otf") || p.endsWith(".woff") ||
                            p.endsWith(".woff2") || p.endsWith("gamefont.css"))) profile.bundledFont = true;
            if (p.endsWith(".rpgmvo")) { profile.encryptedOggAudio = true; profile.oggAudioAvailable = true; }
            if (p.endsWith(".rpgmvm")) { profile.encryptedM4aAudio = true; profile.m4aAudioAvailable = true; }
            if (p.endsWith(".ogg")) profile.oggAudioAvailable = true;
            if (p.endsWith(".m4a")) profile.m4aAudioAvailable = true;
        }
        return profile;
    }

    public static InputStream rewriteIndexHtml(InputStream input, Profile profile) throws Exception {
        byte[] original = readAll(input);
        String html = new String(original, StandardCharsets.UTF_8);
        if (html.contains("id=\"rpgmp-android-compat\"")) {
            return new ByteArrayInputStream(original);
        }

        String fontStyle = profile.bundledFont ? "" :
                "<style id=\"rpgmp-korean-font\">" +
                "@font-face{font-family:GameFont;src:url('/__rpgmp_font.ttf') format('truetype');font-weight:normal;font-style:normal;}" +
                "@font-face{font-family:'rmmz-mainfont';src:url('/__rpgmp_font.ttf') format('truetype');font-weight:normal;font-style:normal;}" +
                "</style>";

        String nodeBootstrap = profile.nodeCompat ?
                "<script id=\"rpgmp-node-compat\" src=\"/__rpgmp_node_compat.js\"></script>" : "";
        String mouseBootstrap = profile.mouseNativeInput ? buildMouseTouchBootstrap() : "";
        String bootstrap = fontStyle + nodeBootstrap + "<script id=\"rpgmp-android-compat\">" +
                "window.__RPGMP_ANDROID__=" + profile.json() + ";" +
                "window.__RPGMP_ANDROID__.webview=true;" +
                "document.addEventListener('contextmenu',function(e){e.preventDefault();},{passive:false});" +
                mouseBootstrap +
                (profile.oggAudioAvailable && !profile.m4aAudioAvailable ?
                        "(function(){var p=HTMLMediaElement&&HTMLMediaElement.prototype;if(p&&p.canPlayType&&!p.__rpgmpCanPlay){var old=p.canPlayType;p.canPlayType=function(t){if(t&&String(t).toLowerCase().indexOf('audio/ogg')>=0)return 'probably';return old.call(this,t);};p.__rpgmpCanPlay=true;}})();" : "") +
                "window.__rpgmpResumeAudio=function(){try{if(window.WebAudio&&WebAudio._context&&WebAudio._context.state==='suspended'){WebAudio._context.resume();}}catch(_a){}" +
                "try{if(window.AudioManager&&AudioManager._context&&AudioManager._context.state==='suspended'){AudioManager._context.resume();}}catch(_b){}};" +
                "document.addEventListener('DOMContentLoaded',function(){" +
                "document.documentElement.style.touchAction='none';" +
                "if(document.body){document.body.style.touchAction='none';document.body.style.overscrollBehavior='none';}" +
                "['pointerdown','touchstart','mousedown','keydown'].forEach(function(n){document.addEventListener(n,window.__rpgmpResumeAudio,true);});" +
                "var tries=0;var timer=setInterval(function(){tries++;window.__rpgmpResumeAudio();" +
                (profile.oggAudioAvailable && !profile.m4aAudioAvailable ?
                        "try{if(window.AudioManager&&AudioManager.audioFileExt&&!AudioManager.__rpgmpAudioExt){AudioManager.audioFileExt=function(){return '.ogg';};AudioManager.__rpgmpAudioExt=true;}}catch(_ext){}" : "") +
                "if(tries>120)clearInterval(timer);},250);" +
                "});" +
                "</script>";

        int head = indexOfIgnoreCase(html, "<head>");
        if (head >= 0) {
            int insert = head + 6;
            html = html.substring(0, insert) + bootstrap + html.substring(insert);
        } else {
            html = bootstrap + html;
        }

        String lateBootstrap = buildLateBootstrap(profile);
        int mainJs = indexOfIgnoreCase(html, "js/main.js");
        if (mainJs >= 0) {
            int scriptStart = html.lastIndexOf("<script", mainJs);
            if (scriptStart >= 0) {
                html = html.substring(0, scriptStart) + lateBootstrap + html.substring(scriptStart);
            } else {
                html += lateBootstrap;
            }
        } else {
            int bodyEnd = indexOfIgnoreCase(html, "</body>");
            if (bodyEnd >= 0) html = html.substring(0, bodyEnd) + lateBootstrap + html.substring(bodyEnd);
            else html += lateBootstrap;
        }
        return new ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8));
    }

    static boolean usesMouseNativeInput(String pluginsJs) {
        if (pluginsJs == null || pluginsJs.isEmpty()) return false;
        return enabledPlugin(pluginsJs, "Mousu_base") ||
                enabledPlugin(pluginsJs, "MousePointerExtend") ||
                enabledPlugin(pluginsJs, "MPP_SimpleTouch3") ||
                enabledPlugin(pluginsJs, "FTKR_InterlockMouseAndWindow");
    }

    private static boolean enabledPlugin(String pluginsJs, String name) {
        Pattern pattern = Pattern.compile(
                "\\{\\s*\\\"name\\\"\\s*:\\s*\\\"" + Pattern.quote(name) +
                        "\\\"\\s*,\\s*\\\"status\\\"\\s*:\\s*true",
                Pattern.CASE_INSENSITIVE);
        return pattern.matcher(pluginsJs).find();
    }

    private static String buildMouseTouchBootstrap() {
        return "(function(){if(window.__rpgmpTouchMouseSync)return;" +
                "window.__rpgmpTouchMouseSync=true;" +
                "function s(e){try{var a=e.changedTouches;if(!a||!a.length)return;var p=a[0];" +
                "if(!window.TouchInput||!window.Graphics)return;" +
                "var px=(p.pageX==null?p.clientX+(window.pageXOffset||0):p.pageX);" +
                "var py=(p.pageY==null?p.clientY+(window.pageYOffset||0):p.pageY);" +
                "TouchInput.mouseX=Graphics.pageToCanvasX(px);" +
                "TouchInput.mouseY=Graphics.pageToCanvasY(py);" +
                "}catch(_m){}}" +
                "document.addEventListener('touchstart',s,true);" +
                "document.addEventListener('touchmove',s,true);" +
                "})();";
    }

    public static InputStream rewritePixiJs(InputStream input, Profile profile) throws Exception {
        byte[] original = readAll(input);
        if (profile == null || !profile.pixiTextureSlotRisk) {
            return new ByteArrayInputStream(original);
        }
        String js = new String(original, StandardCharsets.UTF_8);
        String marker =
                "                    nextTexture._enabled = TICK;\r\n" +
                "\r\n" +
                "                    currentGroup.textureCount++;";
        if (!js.contains(marker)) {
            marker =
                    "                    nextTexture._enabled = TICK;\n" +
                    "\n" +
                    "                    currentGroup.textureCount++;";
        }
        if (!js.contains(marker)) {
            return new ByteArrayInputStream(original);
        }
        String replacement =
                "                    if (nextTexture._virtalBoundId === -1) {\n" +
                "                        TICK++;\n" +
                "                        currentGroup.size = i - currentGroup.start;\n" +
                "                        textureCount = 0;\n" +
                "                        currentGroup = groups[groupCount++];\n" +
                "                        currentGroup.blend = blendMode;\n" +
                "                        currentGroup.textureCount = 0;\n" +
                "                        currentGroup.start = i;\n" +
                "                        for (var __rpgmpSlot = 0; __rpgmpSlot < MAX_TEXTURES; ++__rpgmpSlot) {\n" +
                "                            var __rpgmpBound = boundTextures[__rpgmpSlot];\n" +
                "                            if (__rpgmpBound._enabled !== TICK) {\n" +
                "                                __rpgmpBound._virtalBoundId = -1;\n" +
                "                                nextTexture._virtalBoundId = __rpgmpSlot;\n" +
                "                                boundTextures[__rpgmpSlot] = nextTexture;\n" +
                "                                if (typeof console !== 'undefined' && console.warn) console.warn('[RPGMP PIXI] recovered invalid texture slot -1 -> ' + __rpgmpSlot);\n" +
                "                                break;\n" +
                "                            }\n" +
                "                        }\n" +
                "                    }\n" +
                "                    nextTexture._enabled = TICK;\n\n" +
                "                    currentGroup.textureCount++;";
        js = js.replace(marker, replacement);
        return new ByteArrayInputStream(js.getBytes(StandardCharsets.UTF_8));
    }

    private static String buildLateBootstrap(Profile profile) {
        StringBuilder js = new StringBuilder();
        js.append("<script id=\"rpgmp-android-late\">");
        js.append(buildPersistentSaveBootstrap(profile));
        if (profile != null && "MV".equalsIgnoreCase(profile.engine)) {
            js.append(buildMvLegacyUiBootstrap());
        }
        if (profile.oggAudioAvailable && !profile.m4aAudioAvailable) {
            js.append("try{if(window.AudioManager&&AudioManager.audioFileExt){AudioManager.audioFileExt=function(){return '.ogg';};AudioManager.__rpgmpAudioExt=true;}}catch(_ext){};");
        }
        js.append("window.__rpgmpAudioQueue=window.__rpgmpAudioQueue||{bgm:null,bgs:null,me:null,se:[],staticSe:[]};");
        js.append("window.__rpgmpAudioContext=function(){try{return window.WebAudio&&WebAudio._context?WebAudio._context:null;}catch(_e){return null;}};");
        js.append("window.__rpgmpFlushAudio=function(){try{if(!window.AudioManager)return;var q=window.__rpgmpAudioQueue,a;"
                + "if(q.bgm){a=q.bgm;q.bgm=null;AudioManager.playBgm.apply(AudioManager,a);}"
                + "if(q.bgs){a=q.bgs;q.bgs=null;AudioManager.playBgs.apply(AudioManager,a);}"
                + "if(q.me){a=q.me;q.me=null;AudioManager.playMe.apply(AudioManager,a);}"
                + "while(q.se.length){a=q.se.shift();AudioManager.playSe.apply(AudioManager,a);}"
                + "while(q.staticSe.length){a=q.staticSe.shift();AudioManager.playStaticSe.apply(AudioManager,a);}}catch(_f){}};");
        js.append("window.__rpgmpResumeAudio=function(){var done=function(){setTimeout(window.__rpgmpFlushAudio,0);};try{var c=window.__rpgmpAudioContext();"
                + "if(c&&c.state==='suspended'&&typeof c.resume==='function'){var p=c.resume();if(p&&typeof p.then==='function')p.then(done,done);else done();}else done();}catch(_r){done();}};");
        js.append("(function(){try{if(!window.AudioManager||AudioManager.__rpgmpGuard)return;var q=window.__rpgmpAudioQueue;"
                + "[['playBgm','bgm',0],['playBgs','bgs',0],['playMe','me',0],['playSe','se',1],['playStaticSe','staticSe',1]].forEach(function(x){"
                + "var n=x[0],slot=x[1],multi=x[2],old=AudioManager[n];if(typeof old!=='function')return;AudioManager[n]=function(){var c=window.__rpgmpAudioContext();"
                + "if(c&&c.state==='suspended'){var args=Array.prototype.slice.call(arguments);if(multi){q[slot].push(args);if(q[slot].length>16)q[slot].shift();}else q[slot]=args;return;}return old.apply(this,arguments);};});"
                + "AudioManager.__rpgmpGuard=true;}catch(_g){}})();");
        js.append("</script>");
        return js.toString();
    }

    private static String buildMvLegacyUiBootstrap() {
        StringBuilder js = new StringBuilder();
        js.append("(function(){try{");
        js.append("if(window.Bitmap&&Bitmap.prototype&&!Bitmap.prototype.__rpgmpDrawTextAlign){");
        js.append("var od=Bitmap.prototype.drawText;Bitmap.prototype.drawText=function(t,x,y,mw,lh,a){");
        js.append("if(a!=='left'&&a!=='center'&&a!=='right'&&a!=='start'&&a!=='end')a='left';");
        js.append("return od.call(this,t,x,y,mw,lh,a);};Bitmap.prototype.__rpgmpDrawTextAlign=true;");
        js.append("console.log('[RPGMP MVUI] drawText align compatibility active');}");
        js.append("window.__rpgmpRepairSkillHolder=function(reason){try{");
        js.append("if(!window.$dataSystem||!$dataSystem.variables||$dataSystem.variables[44]!=='☆現在のスキルホルダー'||!window.$gameVariables)return false;");
        js.append("var raw=$gameVariables.value(44),n=Number(raw);");
        js.append("if(!isFinite(n)||Math.floor(n)!==n||n<0||n>5){$gameVariables.setValue(44,0);");
        js.append("console.warn('[RPGMP MAPSKILL] repaired holder old='+raw+' -> 0 reason='+reason);return true;}");
        js.append("}catch(e){console.warn('[RPGMP MAPSKILL] repair failed',e);}return false;};");
        js.append("if(window.Game_Interpreter&&Game_Interpreter.prototype&&!Game_Interpreter.prototype.__rpgmpSkillHolderRepair){");
        js.append("var c117=Game_Interpreter.prototype.command117;Game_Interpreter.prototype.command117=function(){");
        js.append("var id=this._params&&this._params[0];if(id===119||id===122){");
        js.append("window.__rpgmpRepairSkillHolder('common='+id);");
        js.append("}");
        js.append("return c117.apply(this,arguments);};Game_Interpreter.prototype.__rpgmpSkillHolderRepair=true;}");
        js.append("}catch(e){console.error('[RPGMP MVUI] compatibility install failed',e);}})();");
        return js.toString();
    }

    private static String buildPersistentSaveBootstrap(Profile profile) {
        StringBuilder js = new StringBuilder();
        js.append("(function(){try{var B=window.__RPGMP_SAVE__,S=window.StorageManager;if(!B||!S||S.__rpgmpPersistent)return;");
        boolean mv = profile != null && "MV".equalsIgnoreCase(profile.engine);
        if (mv) {
            js.append("try{if(window.Storage&&Storage.prototype&&!Storage.prototype.__rpgmpPersistent){"
                    + "var sp=Storage.prototype,og=sp.getItem,os=sp.setItem,or=sp.removeItem;"
                    + "sp.getItem=function(k){var s=String(k);if(this===window.localStorage&&s==='RPG Common'){"
                    + "var v=B.get('ls_RPG Common');if(v!=null)return String(v);}"
                    + "return og.call(this,k);};"
                    + "sp.setItem=function(k,v){var s=String(k);if(this===window.localStorage&&s==='RPG Common')B.put('ls_RPG Common',String(v));return os.call(this,k,v);};"
                    + "sp.removeItem=function(k){var s=String(k);if(this===window.localStorage&&s==='RPG Common')B.remove('ls_RPG Common');return or.call(this,k);};"
                    + "sp.__rpgmpPersistent=true;console.log('[RPGMP SAVE] MV common-save game-folder bridge active');}}catch(_ls){console.warn('[RPGMP SAVE] localStorage bridge failed',_ls);}");
        }
        js.append("if(typeof S.saveToWebStorage==='function'){"
                + "S.saveToWebStorage=function(id,json){var raw=window.LZString?LZString.compressToBase64(String(json)):String(json);B.put('mv_'+id,raw);};"
                + "S.loadFromWebStorage=function(id){var raw=B.get('mv_'+id);if(raw==null)return '';"
                + "if(window.LZString){var json=LZString.decompressFromBase64(String(raw));return json==null?'':json;}return String(raw);};"
                + "S.webStorageExists=function(id){return !!B.exists('mv_'+id);};"
                + "S.removeWebStorage=function(id){B.remove('mv_'+id);};"
                + "S.backup=function(id){var v=B.get('mv_'+id);if(v!=null)B.put('mvbak_'+id,String(v));};"
                + "S.backupExists=function(id){return !!B.exists('mvbak_'+id);};"
                + "S.cleanBackup=function(id){B.remove('mvbak_'+id);};"
                + "S.restoreBackup=function(id){var v=B.get('mvbak_'+id);if(v!=null){B.put('mv_'+id,String(v));B.remove('mvbak_'+id);}};"
                + "}");
        js.append("if(typeof S.saveToForage==='function'){"
                + "S.saveToForage=function(name,zip){B.put('mz_'+name,String(zip));return Promise.resolve();};"
                + "S.loadFromForage=function(name){var v=B.get('mz_'+name);return Promise.resolve(v==null?null:String(v));};"
                + "S.forageExists=function(name){return !!B.exists('mz_'+name);};"
                + "S.removeForage=function(name){B.remove('mz_'+name);return Promise.resolve();};"
                + "S.updateForageKeys=function(){var merge=function(){var a=[];try{a=JSON.parse(B.keys('mz_')||'[]');}catch(_k){}"
                + "var base=[];a.forEach(function(k){var n=k.substring(3);var fk=typeof S.forageKey==='function'?S.forageKey(n):n;if(base.indexOf(fk)<0)base.push(fk);});"
                + "S._forageKeys=base;S._forageKeysUpdated=true;};"
                + "merge();return Promise.resolve();};"
                + "}");
        if (mv) {
            js.append("if(typeof S.isLocalMode==='function'){S.isLocalMode=function(){return false;};S.__rpgmpForcedWebStorage=true;"
                    + "console.log('[RPGMP SAVE] MV local mode disabled; persistent WebStorage active');}");
        }
        js.append("S.__rpgmpPersistent=true;console.log('[RPGMP SAVE] original game-folder save bridge active');"
                + "}catch(e){console.error('[RPGMP SAVE] bridge install failed',e);}})();");
        return js.toString();
    }

    private static byte[] readAll(InputStream input) throws Exception {
        try (InputStream in = input; ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[32 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
            return out.toByteArray();
        }
    }

    private static int indexOfIgnoreCase(String haystack, String needle) {
        return haystack.toLowerCase(Locale.ROOT).indexOf(needle.toLowerCase(Locale.ROOT));
    }

    private static String js(String text) {
        return text.replace("\\", "\\\\").replace("'", "\\'").replace("\r", "").replace("\n", "\\n");
    }

}
