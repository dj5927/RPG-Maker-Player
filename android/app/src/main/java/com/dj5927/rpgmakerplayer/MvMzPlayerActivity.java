package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Base64;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.window.OnBackInvokedDispatcher;
import android.webkit.MimeTypeMap;
import android.webkit.JavascriptInterface;
import android.webkit.ConsoleMessage;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebChromeClient;
import android.webkit.WebViewClient;
import android.webkit.WebViewRenderProcess;
import android.webkit.WebViewRenderProcessClient;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.json.JSONArray;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class MvMzPlayerActivity extends Activity {
    public static final String EXTRA_TREE_URI = "treeUri";
    public static final String EXTRA_WEB_ROOT_ID = "webRootId";
    public static final String EXTRA_SAVE_ROOT_ID = "saveRootId";
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_ENGINE = "engine";
    public static final String EXTRA_PAD_ENABLED = "padEnabled";
    public static final String EXTRA_PAD_LARGE = "padLarge";
    public static final String EXTRA_KEYMAP = "keymap";
    public static final String EXTRA_RENDER_MODE = "renderMode";
    public static final String EXTRA_LOCALE = "locale";
    public static final String EXTRA_MOUSE_MODE = "mouseMode";
    public static final String EXTRA_LOG_PATH = "logPath";
    public static final String EXTRA_PATCH_DB_PATH = "centralPatchDbPath";
    private static final String HOST = "rpgmaker.local";

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final ScheduledExecutorService webConsoleLogWriter =
            Executors.newSingleThreadScheduledExecutor();
    private final ArrayList<GameLog.LogEntry> pendingConsoleLog = new ArrayList<>();
    private boolean consoleFlushScheduled;
    private void enqueueWebConsoleLog(String level, String message) {
        if (webConsoleLogWriter.isShutdown()) return;
        synchronized (pendingConsoleLog) {
            pendingConsoleLog.add(new GameLog.LogEntry(
                    System.currentTimeMillis(), "JS/" + level, message));
            if (!consoleFlushScheduled) {
                consoleFlushScheduled = true;
                webConsoleLogWriter.schedule(this::flushWebConsoleLog, 75, TimeUnit.MILLISECONDS);
            }
        }
    }

    private void flushWebConsoleLog() {
        ArrayList<GameLog.LogEntry> batch;
        synchronized (pendingConsoleLog) {
            if (pendingConsoleLog.isEmpty()) {
                consoleFlushScheduled = false;
                return;
            }
            batch = new ArrayList<>(pendingConsoleLog);
            pendingConsoleLog.clear();
            consoleFlushScheduled = false;
        }
        GameLog.appendBatch(gameLogPath, batch);
    }

    private final Map<String, String> pathToDocumentId = new HashMap<>();
    private final Map<String, String> pathToDocumentIdLower = new HashMap<>();
    private final Map<String, String> pathToDocumentIdCompat = new HashMap<>();
    private volatile boolean webIndexComplete;
    private volatile boolean staticResourceIndexValid = true;
    private final Map<String, String> indexedResourceDirectories = new HashMap<>();
    private final Map<String, List<String>> indexedResourceChildren = new HashMap<>();
    private final AtomicInteger indexedMovieExistenceQueries = new AtomicInteger();
    private Uri treeUri;
    private String webRootId;
    private String saveRootId;
    private String engineName;
    private final Map<String, String> gameFolderSaveCache = new HashMap<>();
    private final Map<String, SaveFileRef> gameFolderSaveFiles = new HashMap<>();
    private String defaultGameSaveDirectoryId;
    private final Map<Integer, NodeDoc> legacyMvSaveIndex = new HashMap<>();
    private final Map<Integer, String> legacyMvRawCache = new HashMap<>();
    private final Map<String, NodeDoc> legacyLocalStorageIndex = new HashMap<>();
    private final Map<String, String> legacyLocalStorageRawCache = new HashMap<>();
    private WebView webView;
    private MvMzCompatibility.Profile compatibilityProfile;
    private MvMzGamePatches gamePatches = MvMzGamePatches.empty();
    private String renderMode = "AUTO";
    private int[] keyMap;
    private boolean exitDialogVisible;
    private TextView loadingView;
    private boolean explicitExitRequested;
    private boolean mouseModeEnabled;
    private VirtualGamepadView gamepadView;
    private float hardwareMouseX = 0.5f;
    private float hardwareMouseY = 0.5f;
    private float hardwareRightX;
    private float hardwareRightY;
    private boolean hardwareMouseTicking;
    private long hardwareMouseLastFrameNanos;
    private boolean hardwareLeftClickDown;
    private boolean hardwareRightClickDown;
    private String gameLogPath;

    private final Runnable hardwareMouseTick = new Runnable() {
        @Override public void run() {
            if (!mouseModeEnabled || webView == null) {
                hardwareMouseTicking = false;
                return;
            }
            if (Math.abs(hardwareRightX) < 0.001f && Math.abs(hardwareRightY) < 0.001f) {
                hardwareMouseTicking = false;
                hardwareMouseLastFrameNanos = 0L;
                return;
            }
            long now = System.nanoTime();
            float dt = hardwareMouseLastFrameNanos == 0L ? (1f / 60f) :
                    Math.min(0.05f, (now - hardwareMouseLastFrameNanos) / 1_000_000_000f);
            hardwareMouseLastFrameNanos = now;
            float speed = 0.90f * dt;
            hardwareMouseX = clamp01(hardwareMouseX + hardwareRightX * speed);
            hardwareMouseY = clamp01(hardwareMouseY + hardwareRightY * speed);
            sendMouseMove(hardwareMouseX, hardwareMouseY);
            if (gamepadView != null) {
                gamepadView.setMouseCursorNormalized(hardwareMouseX, hardwareMouseY);
            }
            View decor = getWindow().getDecorView();
            if (decor != null) decor.postOnAnimation(this);
            else hardwareMouseTicking = false;
        }
    };

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        GameLocaleSettings.applyProcess(getIntent().getStringExtra(EXTRA_LOCALE));
        String tree = getIntent().getStringExtra(EXTRA_TREE_URI);
        if (tree == null || tree.isEmpty()) {
            showFatalPreparationError("게임 폴더 권한 정보가 없습니다.");
            return;
        }
        treeUri = Uri.parse(tree);
        webRootId = getIntent().getStringExtra(EXTRA_WEB_ROOT_ID);
        if (webRootId == null || webRootId.isEmpty()) {
            showFatalPreparationError("MV/MZ 웹 루트 정보를 찾지 못했습니다.");
            return;
        }
        saveRootId = getIntent().getStringExtra(EXTRA_SAVE_ROOT_ID);
        if (saveRootId == null || saveRootId.isEmpty()) saveRootId = webRootId;
        engineName = getIntent().getStringExtra(EXTRA_ENGINE);
        keyMap = getIntent().getIntArrayExtra(EXTRA_KEYMAP);
        mouseModeEnabled = getIntent().getBooleanExtra(EXTRA_MOUSE_MODE, false);
        gameLogPath = getIntent().getStringExtra(EXTRA_LOG_PATH);
        String requestedRenderMode = getIntent().getStringExtra(EXTRA_RENDER_MODE);
        if ("WEBGL".equals(requestedRenderMode) || "CANVAS".equals(requestedRenderMode)) {
            renderMode = requestedRenderMode;
        }
        GameLog.append(gameLogPath, "MVMZ",
                "onCreate engine=" + engineName + " renderMode=" + renderMode +
                        " webRootId=" + webRootId);

        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::showExitConfirmation);
        }

        TextView loading = new TextView(this);
        loadingView = loading;
        loading.setBackgroundColor(Color.rgb(12, 17, 24));
        loading.setTextColor(Color.WHITE);
        loading.setTextSize(18);
        loading.setGravity(android.view.Gravity.CENTER);
        loading.setText("MV/MZ 게임 파일 인덱싱 중…\n" + getIntent().getStringExtra(EXTRA_TITLE));
        setContentView(loading);
        loading.post(this::enterImmersive);

        worker.submit(() -> {
            try {
                buildIndex();
                loadGamePatches();
                prepareGameFolderSaveCache();
                runOnUiThread(() -> {
                    try {
                        startWebView();
                    } catch (Throwable e) {
                        android.util.Log.e("RPGMP-MVMZ", "WebView startup failed", e);
                        showFatalPreparationError("MV/MZ WebView 시작 실패\n" +
                                e.getClass().getSimpleName() + ": " + String.valueOf(e.getMessage()));
                    }
                });
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "MV/MZ indexing failed", e);
                runOnUiThread(() -> showFatalPreparationError("실행 준비 실패\n" + e));
            }

        });
    }

    private void showFatalPreparationError(String message) {
        TextView view = loadingView;
        if (view == null) {
            view = new TextView(this);
            view.setBackgroundColor(Color.rgb(12, 17, 24));
            view.setTextColor(Color.WHITE);
            view.setTextSize(17);
            view.setGravity(android.view.Gravity.CENTER);
            view.setPadding(32, 24, 32, 24);
            loadingView = view;
            setContentView(view);
        }
        view.setText(message + "\n\n뒤로가기를 누르면 런처로 돌아갑니다.");
    }

    private void loadGamePatches() {
        InputStream central = MvMzCentralPatches.findManifest(
                getContentResolver(), treeUri,
                getIntent().getStringExtra(EXTRA_PATCH_DB_PATH),
                path -> {
                    String docId = resolveIndexedDocumentId("/" + path);
                    return docId == null ? null : getContentResolver().openInputStream(
                            GameScanner.documentUri(treeUri, docId));
                },
                engineName,
                message -> GameLog.append(gameLogPath, "PATCH", message));
        if (central != null) {
            gamePatches = MvMzGamePatches.load(central, engineName,
                    message -> GameLog.append(gameLogPath, "PATCH", message));
            return;
        }
        String id = resolveIndexedDocumentId("/rpgmp-patches.json");
        if (id == null) return;
        try {
            InputStream in = getContentResolver().openInputStream(
                    GameScanner.documentUri(treeUri, id));
            gamePatches = MvMzGamePatches.load(in, engineName,
                    message -> GameLog.append(gameLogPath, "PATCH", message));
        } catch (Exception error) {
            GameLog.append(gameLogPath, "PATCH",
                    "manifest load skipped: " + String.valueOf(error));
        }
    }

    private void buildIndex() throws Exception {
        webIndexComplete = false;
        staticResourceIndexValid = true;
        indexedResourceDirectories.clear();
        indexedResourceChildren.clear();
        ArrayDeque<PathNode> queue = new ArrayDeque<>();
        queue.add(new PathNode(webRootId, ""));
        int visited = 0;
        while (!queue.isEmpty() && visited < 30000) {
            PathNode node = queue.removeFirst();
            visited++;
            List<GameScanner.Child> children = GameScanner.listChildren(this, treeUri, node.documentId);
            ArrayList<String> childNames = new ArrayList<>(children.size());
            for (GameScanner.Child child : children) {
                childNames.add(child.name);
                if (node.path.isEmpty() && child.directory && "saves".equalsIgnoreCase(child.name)) continue;
                String path = node.path.isEmpty() ? child.name : node.path + "/" + child.name;
                if (child.directory) {
                    queue.addLast(new PathNode(child.documentId, path));
                    indexedResourceDirectories.put(compatPathKey("/" + path), child.documentId);
                }
                else {
                    String webPath = "/" + path;
                    pathToDocumentId.put(webPath, child.documentId);
                    pathToDocumentIdLower.put(webPath.toLowerCase(Locale.ROOT), child.documentId);
                    String compatKey = compatPathKey(webPath);
                    if (!pathToDocumentIdCompat.containsKey(compatKey)) {
                        pathToDocumentIdCompat.put(compatKey, child.documentId);
                    } else if (!child.documentId.equals(pathToDocumentIdCompat.get(compatKey))) {
                        pathToDocumentIdCompat.put(compatKey, null);
                    }
                }
            }
            indexedResourceChildren.put(compatPathKey("/" + node.path), childNames);
        }
        webIndexComplete = queue.isEmpty();
        compatibilityProfile = MvMzCompatibility.inspect(
                pathToDocumentId.keySet(), getIntent().getStringExtra(EXTRA_ENGINE));
        compatibilityProfile.pixiTextureSlotRisk = detectPixiTextureSlotRisk();
        compatibilityProfile.nodeCompat = detectNodeCompat();
        compatibilityProfile.mouseNativeInput = MvMzCompatibility.usesMouseNativeInput(
                readIndexedText("/js/plugins.js", 512 * 1024));
        GameLog.append(gameLogPath, "PROFILE", compatibilityProfile.json());
    }

    private boolean detectPixiTextureSlotRisk() {
        String pixi = readIndexedText("/js/libs/pixi.js", 1536 * 1024);
        return pixi.contains("nextTexture._virtalBoundId === -1") &&
                pixi.contains("currentGroup.ids[textureCount] = nextTexture._virtalBoundId") &&
                pixi.contains("this.renderer.bindTexture(currentTexture, group.ids[_j], true)");
    }

    private boolean detectNodeCompat() {
        String managers = readIndexedText("/js/rpg_managers.js", 512 * 1024);
        // Stock MV itself contains require('fs')/require('path') inside
        // StorageManager's desktop-only local-save implementation.  Looking for
        // "require(" anywhere therefore incorrectly classifies virtually every
        // MV game as NW.js once our shim is injected.  Only direct Node use
        // before StorageManager (for example Yariste's top-level lng.txt read)
        // or enabled plugin code should enable the Node compatibility shim.
        int storagePos = managers.indexOf("function StorageManager");
        String managerPrelude = storagePos >= 0 ? managers.substring(0, storagePos) : managers;
        if (containsDirectNodeUse(managerPrelude)) return true;

        String plugins = readIndexedText("/js/plugins.js", 512 * 1024);
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(
                "\\\"name\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*\\\"status\\\"\\s*:\\s*true")
                .matcher(plugins);
        int scanned = 0;
        while (matcher.find() && scanned < 256) {
            scanned++;
            String name = matcher.group(1);
            if (name == null || name.isEmpty() || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) continue;
            String source = readIndexedText("/js/plugins/" + name + ".js", 768 * 1024);
            if (containsDirectNodeUse(source)) return true;
        }
        return false;
    }

    private static boolean containsDirectNodeUse(String source) {
        if (source == null || source.isEmpty()) return false;
        return source.contains("require(") || source.contains("require (") ||
                source.contains("process.mainModule") || source.contains("nw.gui") ||
                source.contains("child_process");
    }

    private String readIndexedText(String path, int maxBytes) {
        String id = resolveIndexedDocumentId(path);
        if (id == null) return "";
        try (InputStream in = getContentResolver().openInputStream(GameScanner.documentUri(treeUri, id));
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) return "";
            byte[] buffer = new byte[8192];
            int total = 0;
            while (total < maxBytes) {
                int read = in.read(buffer, 0, Math.min(buffer.length, maxBytes - total));
                if (read < 0) break;
                if (read > 0) { out.write(buffer, 0, read); total += read; }
            }
            return out.toString(StandardCharsets.UTF_8.name());
        } catch (Exception ignored) {
            return "";
        }
    }

    private String resolveIndexedDocumentId(String path) {
        if (path == null) return null;
        String id = pathToDocumentId.get(path);
        if (id != null) return id;
        id = pathToDocumentIdLower.get(path.toLowerCase(Locale.ROOT));
        if (id != null) return id;
        return pathToDocumentIdCompat.get(compatPathKey(path));
    }

    private static String compatPathKey(String path) {
        if (path == null) return "";
        String p = path.replace('\\', '/');
        while (p.contains("//")) p = p.replace("//", "/");
        if (!p.startsWith("/")) p = "/" + p;
        p = Normalizer.normalize(p, Normalizer.Form.NFKC);
        return p.toLowerCase(Locale.ROOT);
    }

    private void installRenderDiagnostics(WebView view) {
        String script = "(function install(){try{" +
                "if(window.__rpgmpRenderDiag)return;" +
                "(function screenProbe(){try{var c=document.querySelector('#gameCanvas')||document.querySelector('canvas');" +
                "if(!c){setTimeout(screenProbe,600);return;}" +
                "var r=c.getBoundingClientRect();" +
                "console.log('[RPGMP-SCREEN] viewport='+innerWidth+'x'+innerHeight+" +
                "' canvasBacking='+c.width+'x'+c.height+' css='+Math.round(r.width)+'x'+Math.round(r.height)+" +
                "' left='+Math.round(r.left)+' top='+Math.round(r.top)+" +
                "' graphics='+((window.Graphics&&Graphics.width)||'?')+'x'+((window.Graphics&&Graphics.height)||'?')+" +
                "' scale='+((window.Graphics&&Graphics._realScale)||'?'));}" +
                "catch(e){console.log('[RPGMP-SCREEN] '+e);}})();" +
                "if(!window.Scene_Base||!window.Game_Screen||!window.SceneManager){setTimeout(install,250);return;}" +
                "var log=function(s){try{console.log('[RPGMP-DIAG] '+s);}catch(_e){}};" +
                "var sfi=Scene_Base.prototype.startFadeIn,sfo=Scene_Base.prototype.startFadeOut;" +
                "Scene_Base.prototype.startFadeIn=function(d,w){log('SceneFadeIn d='+d+' white='+!!w+' scene='+(this.constructor&&this.constructor.name));return sfi.apply(this,arguments);};" +
                "Scene_Base.prototype.startFadeOut=function(d,w){log('SceneFadeOut d='+d+' white='+!!w+' scene='+(this.constructor&&this.constructor.name));return sfo.apply(this,arguments);};" +
                "var gfi=Game_Screen.prototype.startFadeIn,gfo=Game_Screen.prototype.startFadeOut;" +
                "Game_Screen.prototype.startFadeIn=function(d){log('ScreenFadeIn d='+d);return gfi.apply(this,arguments);};" +
                "Game_Screen.prototype.startFadeOut=function(d){log('ScreenFadeOut d='+d);return gfo.apply(this,arguments);};" +
                "var rr=SceneManager.renderScene,last=0;" +
                "SceneManager.renderScene=function(){try{var sc=this._scene,gs=window.$gameScreen;" +
                "var active=(sc&&sc._fadeDuration>0)||(gs&&(gs._fadeOutDuration>0||gs._fadeInDuration>0));" +
                "if(active){var n=(window.performance&&performance.now)?performance.now():Date.now();" +
                "var dt=last?Math.round((n-last)*100)/100:0;last=n;" +
                "var op=sc&&sc._fadeSprite?Math.round(sc._fadeSprite.opacity*100)/100:'na';" +
                "var br=gs&&gs.brightness?gs.brightness():'na';" +
                "log('FadeFrame dt='+dt+' sceneDur='+(sc?sc._fadeDuration:'na')+' op='+op+" +
                "' screenOut='+(gs?gs._fadeOutDuration:'na')+' screenIn='+(gs?gs._fadeInDuration:'na')+' brightness='+br);}" +
                "else{last=0;}}catch(_f){}return rr.apply(this,arguments);};" +
                "if(SceneManager.snap){var snap=SceneManager.snap;SceneManager.snap=function(){log('SceneManager.snap waitBlur='+" +
                "String(window.villaA_waitBlur_switch_on)+' snapBack='+String(window.villaA_waitBlur_switch_snapBack));return snap.apply(this,arguments);};}" +
                "try{if(window.Graphics&&typeof Graphics.render==='function'&&!Graphics.__rpgmpDiagRender){" +
                "var gr=Graphics.render;Graphics.render=function(stage){var bs=this._skipCount||0;" +
                "var t0=(window.performance&&performance.now)?performance.now():Date.now();" +
                "var rv=gr.apply(this,arguments);var t1=(window.performance&&performance.now)?performance.now():Date.now();" +
                "var ms=Math.round((t1-t0)*100)/100;var as=this._skipCount||0;" +
                "if(bs>0||as>0||ms>=14.5){log('GraphicsRender frame='+this.frameCount+' beforeSkip='+bs+' afterSkip='+as+" +
                "' rendered='+String(this._rendered)+' elapsed='+ms);}" +
                "return rv;};Graphics.__rpgmpDiagRender=true;}}catch(_gr){}" +
                "(function hookGl(){try{if(!window.Graphics||!Graphics._renderer||!Graphics._renderer.gl){setTimeout(hookGl,250);return;}" +
                "var gl=Graphics._renderer.gl;if(gl.__rpgmpActiveTextureDiag)return;" +
                "var max=gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS),orig=gl.activeTexture.bind(gl);" +
                "log('GLInit maxTextureUnits='+max+' boundTextures='+(Graphics._renderer.boundTextures?Graphics._renderer.boundTextures.length:'na'));" +
                "gl.activeTexture=function(v){var unit=v-gl.TEXTURE0;if(unit<0||unit>=max){" +
                "var sc=window.SceneManager&&SceneManager._scene;log('GL_BAD_ACTIVE_TEXTURE unit='+unit+' max='+max+" +
                "' bound='+(Graphics._renderer.boundTextures?Graphics._renderer.boundTextures.length:'na')+" +
                "' frame='+(Graphics.frameCount||0)+' scene='+(sc&&sc.constructor?sc.constructor.name:'na'));}" +
                "return orig(v);};gl.__rpgmpActiveTextureDiag=true;" +
                "var cv=Graphics._canvas;if(cv&&!cv.__rpgmpContextDiag){" +
                "cv.addEventListener('webglcontextlost',function(e){log('WEBGL_CONTEXT_LOST frame='+(Graphics.frameCount||0));},false);" +
                "cv.addEventListener('webglcontextrestored',function(){log('WEBGL_CONTEXT_RESTORED frame='+(Graphics.frameCount||0));},false);" +
                "cv.__rpgmpContextDiag=true;}" +
                "}catch(e){log('GL diagnostic hook failed '+String(e));}})();" +
                "document.addEventListener('visibilitychange',function(){log('visibility '+document.visibilityState);});" +
                "window.addEventListener('error',function(e){log('window.error '+e.message+' '+e.filename+':'+e.lineno);});" +
                "window.addEventListener('unhandledrejection',function(e){log('unhandledrejection '+String(e.reason));});" +
                "window.__rpgmpRenderDiag=true;log('render diagnostics active');" +
                "}catch(e){try{console.warn('[RPGMP-DIAG] install failed '+e);}catch(_x){}}})();";
        view.evaluateJavascript(script, null);
    }

    private void installGameScreenFit(WebView view) {
        String js = "(function fit(){try{" +
                "var g=window.Graphics;" +
                "if(!g||!g._canvas||!g._width||!g._height){setTimeout(fit,350);return;}" +
                "if(g.__rpgmpAutoFitInstalled)return;" +
                "g.__rpgmpAutoFitInstalled=true;" +
                "var apply=function(){try{" +
                "if(g.__rpgmpApplyingFit)return;" +
                "var w=document.documentElement.clientWidth||innerWidth,h=document.documentElement.clientHeight||innerHeight;" +
                "var s=Math.min(w/g._width,h/g._height);" +
                "if(!isFinite(s)||s<=0)return;" +
                "var before=g._realScale;" +
                "if(g._canvas){g._canvas.style.imageRendering='pixelated';}" +
                "if(Math.abs(before-s)<0.02)return;" +
                "g.__rpgmpApplyingFit=true;" +
                "g._realScale=s;" +
                "if(typeof g._updateAllElements==='function')g._updateAllElements();" +
                "console.log('[RPGMP-FIT] viewport='+w+'x'+h+' game='+g._width+'x'+g._height+' old='+before+' new='+s);" +
                "}catch(e){console.log('[RPGMP-FIT] error='+e);}finally{g.__rpgmpApplyingFit=false;}};" +
                "var old=g._updateRealScale;" +
                "if(typeof old==='function')g._updateRealScale=function(){if(g.__rpgmpApplyingFit)return;old.apply(this,arguments);apply();};" +
                "window.addEventListener('resize',apply);" +
                "apply();setInterval(apply,1300);" +
                "}catch(e){console.log('[RPGMP-FIT] initialization error='+e);}})();";
        view.evaluateJavascript(js, null);
    }

    private void installTransitionPerformanceProbe(WebView view) {
        String js = "(function probe(){try{" +
                "if(!window.SceneManager||!window.Scene_Map){setTimeout(probe,350);return;}" +
                "if(SceneManager.__rpgmpTransitionProbe)return;" +
                "SceneManager.__rpgmpTransitionProbe=true;" +
                "var now=function(){return performance.now();};" +
                "var log=function(n,dt){if(dt>=8)console.log('[RPGMP-PERF] '+n+' ms='+dt.toFixed(1));};" +
                "var sm=SceneManager;" +
                "['snap','changeScene','updateScene'].forEach(function(n){" +
                "var old=sm[n];if(typeof old!=='function')return;" +
                "sm[n]=function(){var t=now();try{return old.apply(this,arguments);}finally{log('SceneManager.'+n,now()-t);}};});" +
                "var p=Scene_Map.prototype;" +
                "['create','start','onMapLoaded'].forEach(function(n){" +
                "var old=p[n];if(typeof old!=='function')return;" +
                "p[n]=function(){var t=now();try{return old.apply(this,arguments);}finally{log('Scene_Map.'+n,now()-t);}};});" +
                "var last=now(),longFrames=0;" +
                "function tick(t){var dt=t-last;last=t;if(dt>90&&longFrames++<30)log('frame_gap',dt);requestAnimationFrame(tick);}" +
                "requestAnimationFrame(tick);" +
                "console.log('[RPGMP-PERF] transition probe active');" +
                "}catch(e){console.warn('[RPGMP-PERF] '+e);}})();";
        view.evaluateJavascript(js, null);
    }

    private void startWebView() {
        WebView web = new WebView(this);
        webView = web;
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        web.setBackgroundColor(Color.BLACK);
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setDomStorageEnabled(true);
        web.getSettings().setDatabaseEnabled(true);
        web.getSettings().setMediaPlaybackRequiresUserGesture(false);
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(true);
        web.getSettings().setSupportZoom(false);
        web.setFocusable(true);
        web.setFocusableInTouchMode(true);
        web.setOnTouchListener((v, event) -> {
            if (mouseModeEnabled) {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE) {
                    float width = Math.max(1f, v.getWidth());
                    float height = Math.max(1f, v.getHeight());
                    hardwareMouseX = clamp01(event.getX() / width);
                    hardwareMouseY = clamp01(event.getY() / height);
                    if (gamepadView != null) {
                        gamepadView.setMouseCursorNormalized(hardwareMouseX, hardwareMouseY);
                    }
                    warpMousePosition(hardwareMouseX, hardwareMouseY);
                }
            }
            // Never consume the touch: the MV/MZ WebView must still receive its
            // original touchstart/touchmove/touchend sequence.
            return false;
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onConsoleMessage(ConsoleMessage message) {
                String level = message == null || message.messageLevel() == null
                        ? "LOG" : message.messageLevel().name();
                String text = message == null ? "" :
                        message.message() + " @" + message.sourceId() + ":" + message.lineNumber();
                enqueueWebConsoleLog(level, text);
                return true;
            }
        });
        web.addJavascriptInterface(new PersistentSaveBridge(), "__RPGMP_SAVE__");
        web.addJavascriptInterface(new NodeCompatBridge(), "__RPGMP_NODE__");
        if (android.os.Build.VERSION.SDK_INT >= 21) {
            web.getSettings().setMixedContentMode(android.webkit.WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        }
        web.setWebViewClient(new WebViewClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                Uri uri = request.getUrl();
                if (!HOST.equals(uri.getHost())) return null;
                try {
                    String path = Uri.decode(uri.getEncodedPath());
                    if (path == null || path.equals("/")) path = "/index.html";
                    if (path.equalsIgnoreCase("/__rpgmp_font.ttf")) {
                        return new WebResourceResponse("font/ttf", null,
                                getAssets().open("fonts/wqymicrohei.ttf"));
                    }
                    if (path.equalsIgnoreCase("/__rpgmp_node_compat.js")) {
                        return new WebResourceResponse("application/javascript", "UTF-8",
                                getAssets().open("mvmz_node_compat.js"));
                    }
                    String id = resolveRequestDocumentId(uri, path);
                    if (id == null) {
                        android.util.Log.w("RPGMP-MVMZ", "RESOURCE_MISS " + uri);
                        GameLog.append(gameLogPath, "RESOURCE", "MISS " + uri);
                        return notFoundResponse(path);
                    }
                    InputStream input = getContentResolver().openInputStream(GameScanner.documentUri(treeUri, id));
                    if (input == null) {
                        android.util.Log.w("RPGMP-MVMZ", "RESOURCE_OPEN_NULL " + path);
                        GameLog.append(gameLogPath, "RESOURCE", "OPEN_NULL " + path);
                        return notFoundResponse(path);
                    }
                    // Apply manifest replacements to the ORIGINAL game bytes.
                    // This keeps sha256_before consistent with SteamOS and
                    // leaves built-in Android compatibility rewrites afterward.
                    if (gamePatches.has(path)) {
                        input = gamePatches.apply(path, input,
                                message -> GameLog.append(gameLogPath, "PATCH", message));
                    }
                    if (path.equalsIgnoreCase("/index.html") && input != null && compatibilityProfile != null) {
                        input = MvMzCompatibility.rewriteIndexHtml(input, compatibilityProfile);
                    } else if (path.equalsIgnoreCase("/js/libs/pixi.js") &&
                            input != null && compatibilityProfile != null) {
                        input = MvMzCompatibility.rewritePixiJs(input, compatibilityProfile);
                    }
                    return new WebResourceResponse(mime(path), encoding(path), input);
                } catch (Exception e) {
                    android.util.Log.e("RPGMP-MVMZ", "RESOURCE_EXCEPTION " + uri, e);
                    GameLog.append(gameLogPath, "RESOURCE",
                            "EXCEPTION " + uri + " " + e.getClass().getSimpleName() +
                                    ": " + String.valueOf(e.getMessage()));
                    return notFoundResponse(String.valueOf(uri.getPath()));
                }
            }

            @Override public void onPageFinished(WebView view, String url) {
                super.onPageFinished(view, url);
                GameLog.append(gameLogPath, "WEBVIEW", "pageFinished " + url);
                if (getIntent().getBooleanExtra("mvmzDiagnostics", false)) {
                    installRenderDiagnostics(view);
                    installTransitionPerformanceProbe(view);
                }
                installGameScreenFit(view);
            }

            @Override public boolean onRenderProcessGone(WebView view,
                                                          android.webkit.RenderProcessGoneDetail detail) {
                android.util.Log.e("RPGMP-MVMZ", "WebView renderer gone crashed=" + detail.didCrash());
                GameLog.append(gameLogPath, "RENDERER", "gone crashed=" + detail.didCrash());
                if (view == webView) webView = null;
                try { view.destroy(); } catch (Throwable ignored) {}
                runOnUiThread(() -> showFatalPreparationError(
                        detail.didCrash() ? "MV/MZ 렌더러가 비정상 종료되었습니다." :
                                "MV/MZ 렌더러가 시스템에 의해 종료되었습니다."));
                return true;
            }
        });
        if (Build.VERSION.SDK_INT >= 29) {
            web.setWebViewRenderProcessClient(new WebViewRenderProcessClient() {
                @Override public void onRenderProcessUnresponsive(WebView view, WebViewRenderProcess renderer) {
                    android.util.Log.w("RPGMP-MVMZ", "WebView renderer unresponsive");
                    GameLog.append(gameLogPath, "RENDERER", "unresponsive");
                }

                @Override public void onRenderProcessResponsive(WebView view, WebViewRenderProcess renderer) {
                    android.util.Log.i("RPGMP-MVMZ", "WebView renderer responsive");
                    GameLog.append(gameLogPath, "RENDERER", "responsive");
                }
            });
        }
        FrameLayout root = new FrameLayout(this);
        root.addView(web, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        if (getIntent().getBooleanExtra(EXTRA_PAD_ENABLED, true)) {
            VirtualGamepadView pad = new VirtualGamepadView(this);
            gamepadView = pad;
            pad.setLargeMode(getIntent().getBooleanExtra(EXTRA_PAD_LARGE, false));
            pad.setMouseMode(mouseModeEnabled);
            if (mouseModeEnabled) {
                pad.setMouseListener(new VirtualGamepadView.MouseListener() {
                    @Override public void onMove(float normalizedX, float normalizedY) {
                        sendMouseMove(normalizedX, normalizedY);
                    }

                    @Override public void onButton(int button, boolean down) {
                        sendMouseButton(button, down);
                    }

                    @Override public void onWheel(float deltaY) {
                        sendMouseWheel(deltaY);
                    }
                });
                pad.setMouseCursorNormalized(hardwareMouseX, hardwareMouseY);
            } else {
                pad.setListener((keyCode, down) ->
                        sendRpgKey(GameKeyMap.remap(keyCode, keyMap), down));
            }
            root.addView(pad, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        }
        setContentView(root);
        loadingView = null;
        web.requestFocus(View.FOCUS_DOWN);
        String effectiveRender = renderMode;
        if ("AUTO".equals(effectiveRender) && compatibilityProfile != null &&
                (compatibilityProfile.khas || compatibilityProfile.pixiFilters)) {
            effectiveRender = "WEBGL";
        }
        String renderQuery = "WEBGL".equals(effectiveRender) ? "?webgl" :
                ("CANVAS".equals(effectiveRender) ? "?canvas" : "");
        web.loadUrl("https://" + HOST + "/index.html" + renderQuery);
    }

    private String resolveRequestDocumentId(Uri uri, String decodedPath) {
        String id = resolveIndexedDocumentId(decodedPath);
        if (id != null) return id;

        if (decodedPath != null && decodedPath.indexOf('%') >= 0) {
            String secondDecode = Uri.decode(decodedPath);
            if (!secondDecode.equals(decodedPath)) {
                id = resolveIndexedDocumentId(secondDecode);
                if (id != null) return id;
            }
        }

        String fragment = uri == null ? null : uri.getFragment();
        if (fragment != null && !fragment.isEmpty()) {
            id = resolveIndexedDocumentId(decodedPath + "#" + Uri.decode(fragment));
            if (id != null) return id;
        }
        return null;
    }

    private static WebResourceResponse notFoundResponse(String path) {
        byte[] body = ("Not found: " + String.valueOf(path)).getBytes(StandardCharsets.UTF_8);
        return new WebResourceResponse("text/plain", "UTF-8", 404, "Not Found",
                Collections.emptyMap(), new ByteArrayInputStream(body));
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (event.getSource() & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
            int key = event.getKeyCode();
            if (mouseModeEnabled && (key == KeyEvent.KEYCODE_BUTTON_L2 ||
                    key == KeyEvent.KEYCODE_BUTTON_R2)) {
                boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
                setHardwareMouseButton(key == KeyEvent.KEYCODE_BUTTON_L2 ? 0 : 2, down);
                return true;
            }
            if (isMappedGameKey(key)) {
                sendRpgKey(GameKeyMap.remap(key, keyMap), event.getAction() == KeyEvent.ACTION_DOWN);
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_SCROLL &&
                (((event.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) ||
                        ((event.getSource() & InputDevice.SOURCE_ROTARY_ENCODER) ==
                                InputDevice.SOURCE_ROTARY_ENCODER))) {
            float scroll = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (Math.abs(scroll) > 0.001f) {
                sendMouseWheel(-scroll * 120f);
                return true;
            }
        }
        if (mouseModeEnabled && event.getAction() == MotionEvent.ACTION_MOVE &&
                (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            float rx = readRightStickAxis(event, MotionEvent.AXIS_RX, MotionEvent.AXIS_Z);
            float ry = readRightStickAxis(event, MotionEvent.AXIS_RY, MotionEvent.AXIS_RZ);
            boolean used = Math.abs(rx) > 0.001f || Math.abs(ry) > 0.001f;
            hardwareRightX = shapeMouseAxis(rx);
            hardwareRightY = shapeMouseAxis(ry);
            if ((Math.abs(hardwareRightX) > 0.001f || Math.abs(hardwareRightY) > 0.001f) &&
                    !hardwareMouseTicking) {
                hardwareMouseTicking = true;
                View decor = getWindow().getDecorView();
                if (decor != null) decor.postOnAnimation(hardwareMouseTick);
            }

            float lTrigger = Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
                    event.getAxisValue(MotionEvent.AXIS_BRAKE));
            float rTrigger = Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER),
                    event.getAxisValue(MotionEvent.AXIS_GAS));
            used |= lTrigger > 0.01f || rTrigger > 0.01f ||
                    hardwareLeftClickDown || hardwareRightClickDown;
            if (lTrigger > 0.01f || hardwareLeftClickDown) {
                setHardwareMouseButton(0, lTrigger >= 0.55f);
            }
            if (rTrigger > 0.01f || hardwareRightClickDown) {
                setHardwareMouseButton(2, rTrigger >= 0.55f);
            }
            if (used) return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override public void onBackPressed() {
        if (webView == null && loadingView != null) {
            finish();
            return;
        }
        showExitConfirmation();
    }

    private void showExitConfirmation() {
        if (exitDialogVisible || isFinishing() || isDestroyed()) return;
        exitDialogVisible = true;
        new AlertDialog.Builder(this)
                .setTitle(UiText.t(this, "게임 종료"))
                .setMessage(UiText.t(this, "게임을 종료하시겠습니까?"))
                .setPositiveButton(UiText.t(this, "예"), (dialog, which) -> requestRealExit())
                .setNegativeButton(UiText.t(this, "아니오"), (dialog, which) -> exitDialogVisible = false)
                .setOnCancelListener(dialog -> exitDialogVisible = false)
                .show();
    }

    private void requestRealExit() {
        exitDialogVisible = false;
        explicitExitRequested = true;
        WebView web = webView;
        webView = null;
        if (web != null) {
            try { web.stopLoading(); } catch (Throwable ignored) {}
            try { web.loadUrl("about:blank"); } catch (Throwable ignored) {}
            try { web.removeAllViews(); } catch (Throwable ignored) {}
            try { web.destroy(); } catch (Throwable ignored) {}
        }
        worker.shutdownNow();
        finish();
    }

    private static boolean isMappedGameKey(int key) {
        return key == KeyEvent.KEYCODE_DPAD_UP || key == KeyEvent.KEYCODE_DPAD_DOWN ||
                key == KeyEvent.KEYCODE_DPAD_LEFT || key == KeyEvent.KEYCODE_DPAD_RIGHT ||
                key == KeyEvent.KEYCODE_BUTTON_A || key == KeyEvent.KEYCODE_BUTTON_B ||
                key == KeyEvent.KEYCODE_BUTTON_X || key == KeyEvent.KEYCODE_BUTTON_Y ||
                key == KeyEvent.KEYCODE_BUTTON_L1 || key == KeyEvent.KEYCODE_BUTTON_R1 ||
                key == KeyEvent.KEYCODE_BUTTON_L2 || key == KeyEvent.KEYCODE_BUTTON_R2 ||
                key == KeyEvent.KEYCODE_BUTTON_START || key == KeyEvent.KEYCODE_BUTTON_SELECT;
    }

    private float readRightStickAxis(MotionEvent event, int preferredAxis, int fallbackAxis) {
        InputDevice device = event.getDevice();
        int axis = fallbackAxis;
        if (device != null && device.getMotionRange(preferredAxis) != null) axis = preferredAxis;
        float value = event.getAxisValue(axis);
        float flat = 0.12f;
        if (device != null) {
            InputDevice.MotionRange range = device.getMotionRange(axis);
            if (range != null) flat = Math.max(flat, range.getFlat());
        }
        return Math.abs(value) <= flat ? 0f : value;
    }

    private static float shapeMouseAxis(float value) {
        float dead = 0.18f;
        float abs = Math.abs(value);
        if (abs <= dead) return 0f;
        float normalized = Math.min(1f, (abs - dead) / (1f - dead));
        float curved = normalized * normalized * (0.35f + normalized * 0.65f);
        return Math.copySign(curved, value);
    }

    private void setHardwareMouseButton(int button, boolean down) {
        if (button == 0) {
            if (hardwareLeftClickDown == down) return;
            hardwareLeftClickDown = down;
        } else {
            if (hardwareRightClickDown == down) return;
            hardwareRightClickDown = down;
        }
        sendMouseButton(button, down);
    }

    private static float clamp01(float value) {
        return Math.max(0f, Math.min(1f, value));
    }

    private void sendMouseMove(float normalizedX, float normalizedY) {
        if (webView == null) return;
        float nx = Math.max(0f, Math.min(1f, normalizedX));
        float ny = Math.max(0f, Math.min(1f, normalizedY));
        String script = "(function(){" +
                "var x=Math.max(0,Math.min(window.innerWidth-1," + nx + "*window.innerWidth));" +
                "var y=Math.max(0,Math.min(window.innerHeight-1," + ny + "*window.innerHeight));" +
                "window.__rpgmpMouseX=x;window.__rpgmpMouseY=y;" +
                "var t=document.elementFromPoint(x,y)||document;" +
                "t.dispatchEvent(new MouseEvent('mousemove',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:0,buttons:0}));" +
                "})();";
        webView.evaluateJavascript(script, null);
    }

    private void warpMousePosition(float normalizedX, float normalizedY) {
        if (webView == null) return;
        float nx = Math.max(0f, Math.min(1f, normalizedX));
        float ny = Math.max(0f, Math.min(1f, normalizedY));
        String script = "(function(){" +
                "var x=Math.max(0,Math.min(window.innerWidth-1," + nx + "*window.innerWidth));" +
                "var y=Math.max(0,Math.min(window.innerHeight-1," + ny + "*window.innerHeight));" +
                "window.__rpgmpMouseX=x;window.__rpgmpMouseY=y;" +
                "try{if(window.TouchInput&&window.Graphics){" +
                "var px=x+(window.pageXOffset||0),py=y+(window.pageYOffset||0);" +
                "TouchInput.mouseX=Graphics.pageToCanvasX(px);" +
                "TouchInput.mouseY=Graphics.pageToCanvasY(py);" +
                "}}catch(_e){}" +
                "})();";
        webView.evaluateJavascript(script, null);
    }

    private void sendMouseButton(int button, boolean down) {
        if (webView == null) return;
        int safeButton = button == 2 ? 2 : 0;
        int buttons = down ? (safeButton == 2 ? 2 : 1) : 0;
        String type = down ? "mousedown" : "mouseup";
        String script = "(function(){" +
                "var x=(window.__rpgmpMouseX==null?window.innerWidth/2:window.__rpgmpMouseX);" +
                "var y=(window.__rpgmpMouseY==null?window.innerHeight/2:window.__rpgmpMouseY);" +
                "var t=document.elementFromPoint(x,y)||document;" +
                "t.dispatchEvent(new MouseEvent('" + type + "',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:" +
                safeButton + ",buttons:" + buttons + "}));" +
                (!down && safeButton == 0 ?
                        "t.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:0,buttons:0}));" : "") +
                (!down && safeButton == 2 ?
                        "t.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:2,buttons:0}));" : "") +
                "})();";
        webView.evaluateJavascript(script, null);
    }

    private void sendMouseWheel(float deltaY) {
        if (webView == null) return;
        float safeDelta = deltaY < 0f ? -120f : 120f;
        String script = "(function(){" +
                "var x=(window.__rpgmpMouseX==null?window.innerWidth/2:window.__rpgmpMouseX);" +
                "var y=(window.__rpgmpMouseY==null?window.innerHeight/2:window.__rpgmpMouseY);" +
                "var t=document.elementFromPoint(x,y)||document;" +
                "t.dispatchEvent(new WheelEvent('wheel',{bubbles:true,cancelable:true," +
                "clientX:x,clientY:y,deltaX:0,deltaY:" + safeDelta + ",deltaMode:0}));" +
                "})();";
        webView.evaluateJavascript(script, null);
    }

    private void sendRpgKey(int androidKeyCode, boolean down) {
        if (webView == null) return;
        int jsCode;
        String inputName;
        switch (androidKeyCode) {
            case KeyEvent.KEYCODE_DPAD_UP: jsCode = 38; inputName = "up"; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: jsCode = 40; inputName = "down"; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: jsCode = 37; inputName = "left"; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: jsCode = 39; inputName = "right"; break;
            case KeyEvent.KEYCODE_BUTTON_A: jsCode = 13; inputName = "ok"; break;
            case KeyEvent.KEYCODE_BUTTON_B: jsCode = 27; inputName = "escape"; break;
            case KeyEvent.KEYCODE_BUTTON_X: jsCode = 16; inputName = "shift"; break;
            case KeyEvent.KEYCODE_BUTTON_Y:
            case KeyEvent.KEYCODE_BUTTON_R1: jsCode = 34; inputName = "pagedown"; break;
            case KeyEvent.KEYCODE_BUTTON_L1: jsCode = 33; inputName = "pageup"; break;
            case KeyEvent.KEYCODE_BUTTON_L2: jsCode = 16; inputName = "shift"; break;
            case KeyEvent.KEYCODE_BUTTON_R2: jsCode = 27; inputName = "escape"; break;
            case KeyEvent.KEYCODE_BUTTON_START: jsCode = 13; inputName = "ok"; break;
            case KeyEvent.KEYCODE_BUTTON_SELECT: jsCode = 27; inputName = "escape"; break;
            default: return;
        }
        String eventName = down ? "keydown" : "keyup";
        String value = down ? "true" : "false";
        String script = "(function(){" +
                "try{if(window.__rpgmpResumeAudio)window.__rpgmpResumeAudio();}catch(_ra){}" +
                "if(window.Input&&Input._currentState){Input._currentState['" + inputName + "']=" + value + ";}" +
                "var e=new KeyboardEvent('" + eventName + "',{bubbles:true,cancelable:true});" +
                "try{Object.defineProperty(e,'keyCode',{get:function(){return " + jsCode + ";}});" +
                "Object.defineProperty(e,'which',{get:function(){return " + jsCode + ";}});}catch(_e){}" +
                "document.dispatchEvent(e);window.dispatchEvent(e);})();";
        webView.evaluateJavascript(script, null);
    }

    private static String mime(String path) {
        String ext = MimeTypeMap.getFileExtensionFromUrl(path);
        String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(
                ext == null ? "" : ext.toLowerCase(Locale.ROOT));
        if (type != null) return type;
        if (path.endsWith(".json")) return "application/json";
        if (path.endsWith(".m4a")) return "audio/mp4";
        if (path.endsWith(".ogg")) return "audio/ogg";
        return "application/octet-stream";
    }

    private static String encoding(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".html") || lower.endsWith(".htm") || lower.endsWith(".js") ||
                lower.endsWith(".css") || lower.endsWith(".json") || lower.endsWith(".txt")) return "UTF-8";
        return null;
    }

    private void enterImmersive() {
        View decor = getWindow().getDecorView();
        if (decor == null) return;
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        try {
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
            if (Build.VERSION.SDK_INT >= 29) {
                getWindow().setStatusBarContrastEnforced(false);
                getWindow().setNavigationBarContrastEnforced(false);
            }
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
                if (!decor.isAttachedToWindow()) {
                    decor.post(this::enterImmersive);
                    return;
                }
                WindowInsetsController c = decor.getWindowInsetsController();
                if (c != null) {
                    c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            }
        } catch (Throwable t) {
            android.util.Log.w("RPGMP-MVMZ", "immersive fallback", t);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        View decor = getWindow().getDecorView();
        if (decor != null) {
            decor.post(this::enterImmersive);
            decor.postDelayed(this::enterImmersive, 250);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            View decor = getWindow().getDecorView();
            if (decor != null) {
                decor.post(this::enterImmersive);
                decor.postDelayed(this::enterImmersive, 250);
            }
        }
    }

    @Override protected void onDestroy() {
        flushWebConsoleLog();
        webConsoleLogWriter.shutdown();
        worker.shutdownNow();
        WebView web = webView;
        webView = null;
        if (web != null) {
            try { web.stopLoading(); } catch (Throwable ignored) {}
            try { web.removeAllViews(); } catch (Throwable ignored) {}
            try { web.destroy(); } catch (Throwable ignored) {}
        }
        super.onDestroy();
        if (explicitExitRequested) {
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
                    () -> android.os.Process.killProcess(android.os.Process.myPid()), 700);
        }
    }

    private void prepareGameFolderSaveCache() throws Exception {
        gameFolderSaveCache.clear();
        gameFolderSaveFiles.clear();
        defaultGameSaveDirectoryId = null;
        boolean mv = "MV".equalsIgnoreCase(engineName);
        boolean mz = "MZ".equalsIgnoreCase(engineName);
        if (!mv && !mz) return;
        GameScanner.Child saveDir = null;
        GameScanner.Child ndataDir = null;
        for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, webRootId)) {
            if (!child.directory || child.name == null) continue;
            if ("save".equalsIgnoreCase(child.name)) saveDir = child;
            else if (mv && "ndata".equalsIgnoreCase(child.name)) ndataDir = child;
        }
        if (saveDir != null) {
            defaultGameSaveDirectoryId = saveDir.documentId;
            indexGameSaveDirectory(saveDir.documentId, mv, mz);
        }
        if (mv && ndataDir != null) indexGameSaveDirectory(ndataDir.documentId, true, false);
        android.util.Log.i("RPGMP-MVMZ", "game-folder save cache engine=" + engineName +
                " keys=" + gameFolderSaveCache.size());
    }

    private void indexGameSaveDirectory(String directoryId, boolean mv, boolean mz) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, directoryId)) {
            if (child.directory || child.name == null) continue;
            String lower = child.name.toLowerCase(Locale.ROOT);
            String key = null;
            if (mv) key = mvLogicalKeyForFile(lower);
            else if (mz && lower.endsWith(".rmmzsave")) {
                key = "mz_" + child.name.substring(0, child.name.length() - ".rmmzsave".length());
            }
            if (key == null) continue;
            SaveFileRef old = gameFolderSaveFiles.get(key);
            if (old != null && old.lastModified > child.lastModified) continue;
            String raw = readNodeDocText(new NodeDoc(child.documentId, false, child.lastModified));
            if (raw == null) continue;
            gameFolderSaveCache.put(key, raw);
            gameFolderSaveFiles.put(key,
                    new SaveFileRef(directoryId, child.documentId, child.name, child.lastModified));
        }
    }

    private static String mvLogicalKeyForFile(String lowerName) {
        if (lowerName == null) return null;
        boolean backup = lowerName.endsWith(".rpgsave.bak");
        String base = backup
                ? lowerName.substring(0, lowerName.length() - ".bak".length())
                : lowerName;
        if (!base.endsWith(".rpgsave")) return null;
        if ("common.rpgsave".equals(base)) return backup ? null : "ls_RPG Common";
        int id;
        if ("config.rpgsave".equals(base)) id = -1;
        else if ("global.rpgsave".equals(base)) id = 0;
        else if (base.startsWith("file")) {
            String n = base.substring(4, base.length() - ".rpgsave".length());
            try { id = Integer.parseInt(n); } catch (Exception ignored) { return null; }
        } else return null;
        return (backup ? "mvbak_" : "mv_") + id;
    }

    private String physicalFileNameForSaveKey(String key) {
        if (key == null) return null;
        if (key.startsWith("mv_") || key.startsWith("mvbak_")) {
            boolean backup = key.startsWith("mvbak_");
            String raw = key.substring(backup ? "mvbak_".length() : "mv_".length());
            int id;
            try { id = Integer.parseInt(raw); } catch (Exception ignored) { return null; }
            String name = id < 0 ? "config.rpgsave" : id == 0 ? "global.rpgsave" : "file" + id + ".rpgsave";
            return backup ? name + ".bak" : name;
        }
        if ("ls_RPG Common".equals(key)) return "common.rpgsave";
        if (key.startsWith("mz_")) {
            String name = key.substring(3);
            if (name.isEmpty() || name.contains("/") || name.contains("\\") || name.contains("..")) return null;
            return name + ".rmmzsave";
        }
        return null;
    }

    private String ensureDefaultGameSaveDirectory() throws Exception {
        if (defaultGameSaveDirectoryId != null) return defaultGameSaveDirectoryId;
        Uri created = DocumentsContract.createDocument(getContentResolver(),
                GameScanner.documentUri(treeUri, webRootId),
                DocumentsContract.Document.MIME_TYPE_DIR, "save");
        if (created == null) throw new IllegalStateException("Cannot create game save directory");
        defaultGameSaveDirectoryId = DocumentsContract.getDocumentId(created);
        return defaultGameSaveDirectoryId;
    }

    private void putGameFolderSave(String key, String value) throws Exception {
        String fileName = physicalFileNameForSaveKey(key);
        if (fileName == null) return;
        SaveFileRef ref = gameFolderSaveFiles.get(key);
        String parentId = ref != null ? ref.parentDocumentId : ensureDefaultGameSaveDirectory();
        Uri uri = ref != null
                ? GameScanner.documentUri(treeUri, ref.documentId)
                : DocumentsContract.createDocument(getContentResolver(),
                        GameScanner.documentUri(treeUri, parentId),
                        "application/octet-stream", fileName);
        if (uri == null) throw new IllegalStateException("Cannot create game save file: " + fileName);
        String data = value == null ? "" : value;
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) throw new IllegalStateException("Cannot write game save file: " + fileName);
            out.write(data.getBytes(StandardCharsets.UTF_8));
        }
        String documentId = DocumentsContract.getDocumentId(uri);
        gameFolderSaveCache.put(key, data);
        gameFolderSaveFiles.put(key,
                new SaveFileRef(parentId, documentId, fileName, System.currentTimeMillis()));
    }

    private void removeGameFolderSave(String key) throws Exception {
        SaveFileRef ref = gameFolderSaveFiles.remove(key);
        gameFolderSaveCache.remove(key);
        if (ref != null) {
            DocumentsContract.deleteDocument(getContentResolver(),
                    GameScanner.documentUri(treeUri, ref.documentId));
        }
    }

    private static final class SaveFileRef {
        final String parentDocumentId;
        final String documentId;
        final String fileName;
        final long lastModified;
        SaveFileRef(String parentDocumentId, String documentId, String fileName, long lastModified) {
            this.parentDocumentId = parentDocumentId;
            this.documentId = documentId;
            this.fileName = fileName;
            this.lastModified = lastModified;
        }
    }

    private final class PersistentSaveBridge {
        @JavascriptInterface
        public void put(String key, String value) {
            try {
                putGameFolderSave(key, value);
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "persistent save put failed key=" + key, e);
            }
        }

        @JavascriptInterface
        public String get(String key) {
            try {
                return gameFolderSaveCache.get(key);
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "persistent save get failed key=" + key, e);
                return null;
            }
        }

        @JavascriptInterface
        public boolean exists(String key) {
            try {
                return gameFolderSaveCache.containsKey(key);
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "persistent save exists failed key=" + key, e);
                return false;
            }
        }

        @JavascriptInterface
        public void remove(String key) {
            try {
                removeGameFolderSave(key);
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "persistent save remove failed key=" + key, e);
            }
        }

        @JavascriptInterface
        public String keys(String prefix) {
            try {
                JSONArray out = new JSONArray();
                for (String key : gameFolderSaveCache.keySet()) {
                    if (prefix == null || prefix.isEmpty() || key.startsWith(prefix)) out.put(key);
                }
                return out.toString();
            } catch (Exception e) {
                android.util.Log.e("RPGMP-MVMZ", "persistent save keys failed prefix=" + prefix, e);
                return "[]";
            }
        }

        @JavascriptInterface
        public boolean legacyMvExists(int savefileId) {
            return legacyMvSaveIndex.containsKey(savefileId);
        }

        @JavascriptInterface
        public String legacyMvRaw(int savefileId) {
            try {
                if (legacyMvRawCache.containsKey(savefileId)) return legacyMvRawCache.get(savefileId);
                NodeDoc doc = legacyMvSaveIndex.get(savefileId);
                if (doc == null || doc.directory) return null;
                String raw = readNodeDocText(doc);
                if (raw != null) legacyMvRawCache.put(savefileId, raw);
                android.util.Log.i("RPGMP-MVMZ", "legacy MV save read id=" + savefileId +
                        " bytes=" + (raw == null ? 0 : raw.length()) + " mtime=" + doc.lastModified);
                return raw;
            } catch (Exception e) {
                android.util.Log.w("RPGMP-MVMZ", "legacy MV save read failed id=" + savefileId, e);
                return null;
            }
        }

        @JavascriptInterface
        public String legacyLocalStorageRaw(String key) {
            try {
                if (key == null) return null;
                if (legacyLocalStorageRawCache.containsKey(key)) return legacyLocalStorageRawCache.get(key);
                NodeDoc doc = legacyLocalStorageIndex.get(key);
                if (doc == null || doc.directory) return null;
                String raw = readNodeDocText(doc);
                if (raw != null) legacyLocalStorageRawCache.put(key, raw);
                return raw;
            } catch (Exception e) {
                android.util.Log.w("RPGMP-MVMZ", "legacy localStorage read failed key=" + key, e);
                return null;
            }
        }
    }

    private void prepareLegacyMvSaveIndex() throws Exception {
        legacyMvSaveIndex.clear();
        legacyMvRawCache.clear();
        legacyLocalStorageIndex.clear();
        legacyLocalStorageRawCache.clear();
        indexLegacyMvRoot(webRootId);
        if (saveRootId != null && !saveRootId.equals(webRootId)) indexLegacyMvRoot(saveRootId);
        android.util.Log.i("RPGMP-MVMZ", "legacy MV save index slots=" + legacyMvSaveIndex.size() +
                " localKeys=" + legacyLocalStorageIndex.size());
    }

    private void indexLegacyMvRoot(String rootId) throws Exception {
        if (rootId == null) return;
        GameScanner.Child saveDir = null;
        GameScanner.Child ndataDir = null;
        for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, rootId)) {
            if (child.name == null) continue;
            if (child.directory) {
                if ("save".equalsIgnoreCase(child.name)) saveDir = child;
                else if ("ndata".equalsIgnoreCase(child.name)) ndataDir = child;
                continue;
            }
            String lower = child.name.toLowerCase(Locale.ROOT);
            if (lower.startsWith("save") && lower.endsWith(".rpgsave")) {
                indexLegacyMvFile(child, lower.substring(4));
            } else if (lower.startsWith("ndata") && lower.endsWith(".rpgsave")) {
                indexLegacyMvFile(child, lower.substring(5));
            }
        }
        if (saveDir != null) indexLegacyMvDirectory(saveDir.documentId);
        if (ndataDir != null) indexLegacyMvDirectory(ndataDir.documentId);
    }

    private void indexLegacyMvDirectory(String directoryDocumentId) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, directoryDocumentId)) {
            if (child.directory || child.name == null) continue;
            indexLegacyMvFile(child, child.name.toLowerCase(Locale.ROOT));
        }
    }

    private void indexLegacyMvFile(GameScanner.Child child, String lowerName) {
        if (lowerName == null || !lowerName.endsWith(".rpgsave")) return;
        NodeDoc doc = new NodeDoc(child.documentId, false, child.lastModified);
        Integer id = null;
        if ("config.rpgsave".equals(lowerName)) id = -1;
        else if ("global.rpgsave".equals(lowerName)) id = 0;
        else if (lowerName.startsWith("file") && lowerName.endsWith(".rpgsave")) {
            String number = lowerName.substring(4, lowerName.length() - ".rpgsave".length());
            try { id = Integer.parseInt(number); } catch (Exception ignored) {}
        }
        if (id != null) putNewestLegacyDoc(legacyMvSaveIndex, id, doc);
        if ("common.rpgsave".equals(lowerName)) putNewestLegacyDoc(legacyLocalStorageIndex, "RPG Common", doc);
    }

    private static <K> void putNewestLegacyDoc(Map<K, NodeDoc> map, K key, NodeDoc doc) {
        NodeDoc old = map.get(key);
        if (old == null || doc.lastModified > old.lastModified) map.put(key, doc);
    }

    private String readNodeDocText(NodeDoc doc) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(
                GameScanner.documentUri(treeUri, doc.documentId));
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) return null;
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) >= 0) {
                if (read > 0) out.write(buffer, 0, read);
            }
            return out.toString(StandardCharsets.UTF_8.name());
        }
    }

    private NodeDoc resolveNodeRead(String rawPath) throws Exception {
        boolean absolute = rawPath != null && rawPath.replace('\\', '/').startsWith("/");
        String normalized = normalizeNodePath(rawPath);
        if (normalized == null) return null;
        NodeDoc outer = findNodeDocument(saveRootId, normalized);
        String legacy = legacyCollapsedNodePath(normalized);
        NodeDoc oldOuter = legacy == null ? null : findNodeDocument(saveRootId, legacy);
        NodeDoc selectedOuter = chooseNodeSaveCandidate(normalized, legacy, outer, oldOuter);
        if (selectedOuter != null) return selectedOuter;
        if (absolute || webRootId == null || webRootId.equals(saveRootId)) return null;
        NodeDoc web = findNodeDocument(webRootId, normalized);
        NodeDoc oldWeb = legacy == null ? null : findNodeDocument(webRootId, legacy);
        return chooseNodeSaveCandidate(normalized, legacy, web, oldWeb);
    }

    // All MV/MZ games may use NW.js fs.existsSync/statSync/readdirSync
    // synchronously, even if their data is JSON, CSV, RCSV, or something else.
    // Only immutable game resources are accelerated. Save/config paths use the
    // original SAF resolver, and any game write into resource paths disables
    // the snapshot for the rest of the session to avoid stale negative caches.
    private String indexedStaticResourcePath(String rawPath) {
        if (!webIndexComplete || !staticResourceIndexValid) return null;
        String normalized = normalizeNodePath(rawPath);
        if (normalized == null) return null;
        String relative;
        if (saveRootId != null && saveRootId.equals(webRootId)) {
            relative = normalized;
        } else {
            if (!normalized.startsWith("www/")) return null;
            relative = normalized.substring(4);
        }
        String lower = relative.toLowerCase(Locale.ROOT);
        int slash = lower.indexOf('/');
        String top = slash < 0 ? lower : lower.substring(0, slash);
        if (!("movies".equals(top) || "img".equals(top) || "audio".equals(top) ||
                "data".equals(top) || "js".equals(top) || "fonts".equals(top) ||
                "css".equals(top))) return null;
        return "/" + relative;
    }

    private Boolean indexedStaticResourceExists(String path) {
        String resource = indexedStaticResourcePath(path);
        if (resource == null) return null;
        boolean exists = resolveIndexedDocumentId(resource) != null ||
                indexedResourceDirectories.containsKey(compatPathKey(resource));
        int count = indexedMovieExistenceQueries.incrementAndGet();
        if (count == 1 || count == 100 || count == 250 || count == 500) {
            GameLog.append(gameLogPath, "NODE",
                    "indexed static exists checks=" + count +
                            " lastFound=" + exists);
        }
        return exists;
    }

    private Integer indexedStaticResourceType(String path) {
        String resource = indexedStaticResourcePath(path);
        if (resource == null) return null;
        if (indexedResourceDirectories.containsKey(compatPathKey(resource))) return 2;
        return resolveIndexedDocumentId(resource) != null ? 1 : 0;
    }

    private String indexedStaticResourceListing(String path) {
        String resource = indexedStaticResourcePath(path);
        if (resource == null) return null;
        List<String> children = indexedResourceChildren.get(compatPathKey(resource));
        if (children == null) return null;
        JSONArray result = new JSONArray();
        for (String child : children) result.put(child);
        return result.toString();
    }

    private void invalidateStaticResourceIndex(String path) {
        if (indexedStaticResourcePath(path) != null) {
            staticResourceIndexValid = false;
            GameLog.append(gameLogPath, "NODE", "static index invalidated by write: " + path);
        }
    }

    private NodeDoc chooseNodeSaveCandidate(String normalized, String legacy,
                                            NodeDoc normal, NodeDoc old) {
        if (normal == null && old == null) return null;
        if (normal == null) {
            android.util.Log.w("RPGMP-NODE", "legacy collapsed save path fallback " + normalized + " -> " + legacy);
            return old;
        }
        if (old == null) return normal;
        if (isNodeSavePath(normalized) && old.lastModified > normal.lastModified) {
            android.util.Log.w("RPGMP-NODE", "newer legacy save selected " + normalized + " -> " + legacy +
                    " legacyMtime=" + old.lastModified + " normalMtime=" + normal.lastModified);
            return old;
        }
        return normal;
    }

    private static boolean isNodeSavePath(String normalized) {
        if (normalized == null) return false;
        String p = normalized.toLowerCase(Locale.ROOT);
        return p.endsWith(".rpgsave") || p.endsWith(".rpgsave.bak");
    }

    private static String legacyCollapsedNodePath(String normalized) {
        if (normalized == null || normalized.isEmpty()) return null;
        for (String dir : new String[] { "save", "ndata" }) {
            String atRoot = dir + "/";
            if (normalized.startsWith(atRoot) && normalized.length() > atRoot.length()) {
                return dir + normalized.substring(atRoot.length());
            }
            String marker = "/" + dir + "/";
            int pos = normalized.lastIndexOf(marker);
            if (pos >= 0 && pos + marker.length() < normalized.length()) {
                return normalized.substring(0, pos + 1) + dir +
                        normalized.substring(pos + marker.length());
            }
        }
        return null;
    }

    private NodeDoc resolveNodeWrite(String rawPath) throws Exception {
        String normalized = normalizeNodePath(rawPath);
        if (normalized == null || normalized.isEmpty()) return null;
        return findNodeDocument(saveRootId, normalized);
    }

    private NodeDoc findNodeDocument(String rootId, String relative) throws Exception {
        if (rootId == null) return null;
        if (relative == null || relative.isEmpty() || ".".equals(relative)) return new NodeDoc(rootId, true, 0L);
        String current = rootId;
        String[] parts = relative.split("/");
        for (int i = 0; i < parts.length; i++) {
            GameScanner.Child match = null;
            for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, current)) {
                if (child.name.equals(parts[i])) { match = child; break; }
                if (match == null && child.name.equalsIgnoreCase(parts[i])) match = child;
            }
            if (match == null) return null;
            if (i < parts.length - 1 && !match.directory) return null;
            current = match.documentId;
            if (i == parts.length - 1) return new NodeDoc(match.documentId, match.directory, match.lastModified);
        }
        return null;
    }

    private String ensureNodeDirectory(String relative) throws Exception {
        String normalized = normalizeNodePath(relative);
        if (normalized == null) throw new IllegalArgumentException("Unsafe node path");
        String current = saveRootId;
        if (normalized.isEmpty()) return current;
        for (String part : normalized.split("/")) {
            GameScanner.Child match = null;
            for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, current)) {
                if (child.directory && child.name.equals(part)) { match = child; break; }
                if (match == null && child.directory && child.name.equalsIgnoreCase(part)) match = child;
            }
            if (match != null) { current = match.documentId; continue; }
            Uri created = DocumentsContract.createDocument(getContentResolver(),
                    GameScanner.documentUri(treeUri, current), DocumentsContract.Document.MIME_TYPE_DIR, part);
            if (created == null) throw new IllegalStateException("Cannot create directory: " + part);
            current = DocumentsContract.getDocumentId(created);
        }
        return current;
    }

    private boolean writeNodeBytes(String rawPath, byte[] data) throws Exception {
        String normalized = normalizeNodePath(rawPath);
        if (normalized == null || normalized.isEmpty()) return false;
        int slash = normalized.lastIndexOf('/');
        String parentPath = slash < 0 ? "" : normalized.substring(0, slash);
        String name = slash < 0 ? normalized : normalized.substring(slash + 1);
        if (name.isEmpty()) return false;
        String parentId = ensureNodeDirectory(parentPath);
        GameScanner.Child existing = null;
        for (GameScanner.Child child : GameScanner.listChildren(this, treeUri, parentId)) {
            if (!child.directory && child.name.equals(name)) { existing = child; break; }
            if (existing == null && !child.directory && child.name.equalsIgnoreCase(name)) existing = child;
        }
        Uri uri = existing == null ? DocumentsContract.createDocument(getContentResolver(),
                GameScanner.documentUri(treeUri, parentId), "application/octet-stream", name)
                : GameScanner.documentUri(treeUri, existing.documentId);
        if (uri == null) return false;
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) return false;
            out.write(data == null ? new byte[0] : data);
            return true;
        }
    }

    private static String normalizeNodePath(String rawPath) {
        if (rawPath == null) return null;
        String raw = rawPath.trim().replace('\\', '/');
        int query = raw.indexOf('?');
        if (query >= 0) raw = raw.substring(0, query);
        while (raw.startsWith("/")) raw = raw.substring(1);
        ArrayDeque<String> stack = new ArrayDeque<>();
        for (String part : raw.split("/")) {
            if (part.isEmpty() || ".".equals(part)) continue;
            if ("..".equals(part)) {
                if (stack.isEmpty()) return null;
                stack.removeLast();
            } else if (part.indexOf('\0') >= 0 || part.indexOf(':') >= 0) return null;
            else stack.addLast(part);
        }
        StringBuilder out = new StringBuilder();
        for (String part : stack) {
            if (out.length() > 0) out.append('/');
            out.append(part);
        }
        return out.toString();
    }

    private static final class NodeDoc {
        final String documentId;
        final boolean directory;
        final long lastModified;
        NodeDoc(String documentId, boolean directory, long lastModified) {
            this.documentId = documentId;
            this.directory = directory;
            this.lastModified = lastModified;
        }
    }

    private final class NodeCompatBridge {
        private static final int MAX_NODE_FILE = 128 * 1024 * 1024;

        @JavascriptInterface public String mainModuleFilename() {
            if (saveRootId == null || saveRootId.equals(webRootId)) return "/index.html";
            try {
                for (GameScanner.Child child : GameScanner.listChildren(MvMzPlayerActivity.this, treeUri, saveRootId)) {
                    if (child.directory && webRootId.equals(child.documentId)) return "/" + child.name + "/index.html";
                }
            } catch (Exception ignored) {}
            return "/www/index.html";
        }

        @JavascriptInterface public String readBase64(String path) {
            try {
                NodeDoc doc = resolveNodeRead(path);
                if (doc == null || doc.directory) return null;
                if (isNodeSavePath(normalizeNodePath(path))) {
                    android.util.Log.i("RPGMP-NODE", "read save path=" + path + " mtime=" + doc.lastModified);
                }
                try (InputStream in = getContentResolver().openInputStream(GameScanner.documentUri(treeUri, doc.documentId));
                     ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                    if (in == null) return null;
                    byte[] buffer = new byte[16 * 1024];
                    int total = 0;
                    while (true) {
                        int read = in.read(buffer);
                        if (read < 0) break;
                        total += read;
                        if (total > MAX_NODE_FILE) throw new IllegalStateException("Node file too large");
                        out.write(buffer, 0, read);
                    }
                    return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP);
                }
            } catch (Exception e) {
                android.util.Log.w("RPGMP-NODE", "read failed path=" + path, e);
                return null;
            }
        }

        @JavascriptInterface public boolean exists(String path) {
            try {
                Boolean indexed = indexedStaticResourceExists(path);
                if (indexed != null) return indexed;
                return resolveNodeRead(path) != null;
            } catch (Exception ignored) { return false; }
        }

        @JavascriptInterface public int statType(String path) {
            try {
                Integer indexed = indexedStaticResourceType(path);
                if (indexed != null) return indexed;
                NodeDoc d = resolveNodeRead(path); return d == null ? 0 : (d.directory ? 2 : 1);
            }
            catch (Exception ignored) { return 0; }
        }

        @JavascriptInterface public String list(String path) {
            try {
                String indexed = indexedStaticResourceListing(path);
                if (indexed != null) return indexed;
                NodeDoc d = resolveNodeRead(path);
                if (d == null || !d.directory) return "[]";
                JSONArray result = new JSONArray();
                for (GameScanner.Child child : GameScanner.listChildren(MvMzPlayerActivity.this, treeUri, d.documentId)) result.put(child.name);
                return result.toString();
            } catch (Exception e) { return "[]"; }
        }

        @JavascriptInterface public boolean mkdir(String path) {
            try { String p = normalizeNodePath(path); if (p == null) return false; invalidateStaticResourceIndex(path); ensureNodeDirectory(p); return true; }
            catch (Exception e) { android.util.Log.w("RPGMP-NODE", "mkdir failed path=" + path, e); return false; }
        }

        @JavascriptInterface public boolean writeBase64(String path, String base64) {
            try {
                invalidateStaticResourceIndex(path);
                boolean ok = writeNodeBytes(path, Base64.decode(base64 == null ? "" : base64, Base64.DEFAULT));
                if (ok && isNodeSavePath(normalizeNodePath(path))) {
                    android.util.Log.i("RPGMP-NODE", "write save path=" + path);
                }
                return ok;
            }
            catch (Exception e) { android.util.Log.w("RPGMP-NODE", "write failed path=" + path, e); return false; }
        }

        @JavascriptInterface public boolean touch(String path, boolean truncate) {
            try {
                if (truncate) invalidateStaticResourceIndex(path);
                NodeDoc existing = resolveNodeWrite(path);
                if (existing != null && !truncate) return !existing.directory;
                return writeNodeBytes(path, new byte[0]);
            } catch (Exception e) { return false; }
        }

        @JavascriptInterface public boolean remove(String path) {
            try {
                invalidateStaticResourceIndex(path);
                NodeDoc d = resolveNodeWrite(path);
                return d != null && DocumentsContract.deleteDocument(getContentResolver(), GameScanner.documentUri(treeUri, d.documentId));
            } catch (Exception e) { return false; }
        }

        @JavascriptInterface public boolean rename(String from, String to) {
            try {
                invalidateStaticResourceIndex(from);
                invalidateStaticResourceIndex(to);
                NodeDoc src = resolveNodeRead(from);
                if (src == null || src.directory) return false;
                String encoded = readBase64(from);
                if (encoded == null || !writeNodeBytes(to, Base64.decode(encoded, Base64.DEFAULT))) return false;
                return DocumentsContract.deleteDocument(getContentResolver(), GameScanner.documentUri(treeUri, src.documentId));
            } catch (Exception e) { return false; }
        }
    }

    private static final class PathNode {
        final String documentId;
        final String path;
        PathNode(String documentId, String path) { this.documentId = documentId; this.path = path; }
    }
}
