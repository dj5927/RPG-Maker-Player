package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.VelocityTracker;
import android.widget.OverScroller;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public final class LauncherView extends View {
    public enum Page { HOME, LIBRARY, SETTINGS }

    private static final String[] PLATFORM_LABELS = {
            "전체", "2K/2K3", "XP", "VX", "VX Ace", "MV", "MZ"
    };
    private static final GameEntry.Engine[] PLATFORM_ENGINES = {
            null, GameEntry.Engine.EASYRPG, GameEntry.Engine.XP, GameEntry.Engine.VX,
            GameEntry.Engine.VXACE, GameEntry.Engine.MV, GameEntry.Engine.MZ
    };

    public interface Listener {
        void onPickGameFolder();
        void onRescan();
        void onEngineSettings();
        void onLaunch(GameEntry game);
        void onGameSettings(GameEntry game);
    }

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<GameEntry> games = new ArrayList<>();
    private final ArrayList<GameEntry> filteredGames = new ArrayList<>();
    private final ArrayList<GameEntry> recentGames = new ArrayList<>();
    private final ArrayList<String> recentIds = new ArrayList<>();
    private final HashMap<String, Bitmap> thumbnails = new HashMap<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final int touchSlop;
    private final OverScroller scroller;
    private Listener listener;
    private Page page = Page.HOME;
    private String folderLabel = "선택되지 않음";
    private String status = "게임 폴더를 선택하세요";
    private float scrollOffsetPx = 0f;
    private float touchStartScrollOffset = 0f;
    private VelocityTracker velocityTracker;
    private float downX;
    private float downY;
    private GameEntry pressedGame;
    private boolean dragging;
    private boolean longPressTriggered;
    private boolean busy;
    private String busyMessage = "게임 준비 중…";
    private int selectedSettingsIndex;
    private boolean controllerCursorVisible;
    private Bitmap backgroundBitmap;
    private Bitmap brandLogoBitmap;
    private int platformIndex;
    private int selectedHomeSection;
    private int selectedHomeRecentIndex;
    private int selectedHomeLibraryIndex;
    private int selectedLibraryIndex;
    private boolean navFocus;
    private int selectedNavIndex;
    private String touchArmedGameId;
    private String marqueeGameId;
    private long marqueeStartedMs;

    private final Runnable longPress = () -> {
        if (dragging || pressedGame == null) return;
        longPressTriggered = true;
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        if (listener != null) listener.onGameSettings(pressedGame);
    };

    public LauncherView(Context context) {
        super(context);
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        scroller = new OverScroller(context);
        setFocusable(true);
        setClickable(true);
        text.setTypeface(android.graphics.Typeface.create("sans", android.graphics.Typeface.NORMAL));
        setBackgroundColor(Color.rgb(17, 23, 32));
        try (InputStream input = context.getAssets().open("ui/launcher_bg_v048.png")) {
            backgroundBitmap = BitmapFactory.decodeStream(input);
        } catch (Exception ignored) {}
        try (InputStream input = context.getAssets().open("ui/brand_logo.png")) {
            brandLogoBitmap = BitmapFactory.decodeStream(input);
        } catch (Exception ignored) {}
    }

    public void setListener(Listener listener) { this.listener = listener; }

    public void resetTouchInteractionState() {
        handler.removeCallbacks(longPress);
        recycleVelocityTracker();
        pressedGame = null;
        dragging = false;
        longPressTriggered = false;
        touchArmedGameId = null;
        navFocus = false;
        invalidate();
    }

    public void setGames(List<GameEntry> source) {
        resetTouchInteractionState();
        games.clear();
        for (Bitmap bitmap : thumbnails.values()) {
            if (bitmap != null && !bitmap.isRecycled()) bitmap.recycle();
        }
        thumbnails.clear();
        if (source != null) games.addAll(source);
        rebuildCollections();
        status = games.isEmpty() ? tr("검색된 게임이 없습니다") :
                games.size() + ("ja".equals(UiText.language(getContext())) ? " ゲーム" :
                        "en".equals(UiText.language(getContext())) ? " games" : "개 게임");
        invalidate();
    }

    public void setRecentIds(List<String> ids) {
        recentIds.clear();
        if (ids != null) {
            for (String id : ids) {
                if (id != null && !id.isEmpty() && !recentIds.contains(id)) recentIds.add(id);
                if (recentIds.size() >= 5) break;
            }
        }
        rebuildRecentGames();
        invalidate();
    }

    private void rebuildCollections() {
        filteredGames.clear();
        GameEntry.Engine wanted = PLATFORM_ENGINES[platformIndex];
        for (GameEntry game : games) {
            if (wanted == null || game.engine == wanted) filteredGames.add(game);
        }
        rebuildRecentGames();
        selectedLibraryIndex = clampIndex(selectedLibraryIndex, filteredGames.size());
        selectedHomeLibraryIndex = clampIndex(selectedHomeLibraryIndex, Math.min(6, filteredGames.size()));
        selectedHomeRecentIndex = clampIndex(selectedHomeRecentIndex, recentGames.size());
        if (recentGames.isEmpty()) selectedHomeSection = 1;
        clampScrollOffset();
    }

    private void rebuildRecentGames() {
        recentGames.clear();
        for (String id : recentIds) {
            for (GameEntry game : games) {
                if (id.equals(game.stableId())) {
                    recentGames.add(game);
                    break;
                }
            }
            if (recentGames.size() >= 5) break;
        }
        selectedHomeRecentIndex = clampIndex(selectedHomeRecentIndex, recentGames.size());
    }

    private static int clampIndex(int index, int count) {
        if (count <= 0) return 0;
        return Math.max(0, Math.min(index, count - 1));
    }

    public void setGameThumbnail(String stableId, Bitmap bitmap) {
        if (stableId == null || bitmap == null) return;
        thumbnails.put(stableId, bitmap);
        invalidate();
    }

    public Bitmap getGameThumbnail(String stableId) {
        if (stableId == null) return null;
        Bitmap bitmap = thumbnails.get(stableId);
        return bitmap != null && !bitmap.isRecycled() ? bitmap : null;
    }

    public void setFolderLabel(String label) {
        folderLabel = label == null ? tr("선택되지 않음") : label;
        invalidate();
    }

    public void setStatus(String value) {
        status = value == null ? "" : tr(value);
        invalidate();
    }

    public void setBusy(boolean value, String message) {
        busy = value;
        if (message != null && !message.isEmpty()) busyMessage = tr(message);
        if (busy) {
            handler.removeCallbacks(longPress);
            pressedGame = null;
            dragging = false;
        }
        invalidate();
    }

    public void setBusyMessage(String message) {
        if (message != null && !message.isEmpty()) busyMessage = message;
        if (busy) invalidate();
    }

    private float uiScale() {
        float density = getResources().getDisplayMetrics().density;
        if (getWidth() <= 0 || getHeight() <= 0) return Math.max(1f, Math.min(density, 1.8f));
        float fit = Math.min(getWidth() / 1280f, getHeight() / 720f);
        return Math.max(1f, Math.min(density, Math.max(1f, fit) * 1.55f));
    }

    private float s(float value) { return value * uiScale(); }

    private float ts(float value) {
        float density = getResources().getDisplayMetrics().scaledDensity;
        return value * Math.max(1f, Math.min(density, uiScale() * 1.18f));
    }

    private float sidebar() {
        return Math.min(getWidth() * 0.30f, Math.max(s(150), getWidth() * 0.15f));
    }

    @Override protected void onDraw(Canvas c) {
        super.onDraw(c);
        float w = getWidth(), h = getHeight();
        drawBackground(c, w, h);
        float sidebar = sidebar();
        paint.setColor(Color.argb(165, 7, 16, 27));
        c.drawRect(0, 0, sidebar, h, paint);
        drawBrand(c, sidebar);
        drawNav(c, sidebar, h);
        paint.setColor(Color.argb(76, 10, 24, 38));
        c.drawRoundRect(new RectF(sidebar + s(10), s(10), w - s(10), h - s(10)), s(14), s(14), paint);
        if (page == Page.HOME) drawHome(c, sidebar, w, h);
        else if (page == Page.LIBRARY) drawLibrary(c, sidebar, w, h);
        else drawSettings(c, sidebar, w, h);
        drawButtonHints(c, sidebar, w, h);
        if (busy) drawBusyOverlay(c, w, h);
    }

    private void drawBackground(Canvas c, float w, float h) {
        if (backgroundBitmap != null && !backgroundBitmap.isRecycled()) {
            Rect src = centerCropRect(backgroundBitmap, new RectF(0, 0, w, h));
            paint.setAlpha(255);
            c.drawBitmap(backgroundBitmap, src, new RectF(0, 0, w, h), paint);
        } else {
            c.drawColor(Color.rgb(8, 14, 22));
        }
        paint.setColor(Color.argb(112, 2, 8, 14));
        c.drawRect(0, 0, w, h, paint);
    }

    private void drawBusyOverlay(Canvas c, float w, float h) {
        paint.setColor(Color.argb(185, 4, 8, 14));
        c.drawRect(0, 0, w, h, paint);

        float boxW = Math.min(w * 0.56f, s(330));
        float boxH = Math.min(h * 0.28f, s(92));
        float left = (w - boxW) * 0.5f;
        float top = (h - boxH) * 0.5f;
        paint.setColor(Color.rgb(27, 37, 50));
        c.drawRoundRect(new RectF(left, top, left + boxW, top + boxH), s(14), s(14), paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(2f, s(1.2f)));
        paint.setColor(Color.rgb(75, 112, 151));
        c.drawRoundRect(new RectF(left, top, left + boxW, top + boxH), s(14), s(14), paint);
        paint.setStyle(Paint.Style.FILL);

        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        text.setTextSize(ts(17));
        text.setColor(Color.WHITE);
        c.drawText(tr("게임 실행 준비중"), w * 0.5f, top + boxH * 0.57f, text);
        text.setFakeBoldText(false);
        text.setTextAlign(Paint.Align.LEFT);
    }

    private void drawBrand(Canvas c, float sidebar) {
        if (brandLogoBitmap == null || brandLogoBitmap.isRecycled()) return;
        float maxW = Math.max(s(90), sidebar - s(20));
        float maxH = s(58);
        float ratio = brandLogoBitmap.getWidth() / (float) Math.max(1, brandLogoBitmap.getHeight());
        float drawW = maxW;
        float drawH = drawW / ratio;
        if (drawH > maxH) {
            drawH = maxH;
            drawW = drawH * ratio;
        }
        float left = s(10);
        float top = s(12);
        paint.setAlpha(255);
        c.drawBitmap(brandLogoBitmap, null,
                new RectF(left, top, left + drawW, top + drawH), paint);
    }

    private void drawNav(Canvas c, float sidebar, float h) {
        String[] labels = {tr("홈"), tr("라이브러리"), tr("설정")};
        Page[] pages = {Page.HOME, Page.LIBRARY, Page.SETTINGS};
        float start = s(101);
        float itemH = s(52);
        for (int i = 0; i < labels.length; i++) {
            float top = start + i * itemH;
            float cy = top + itemH * 0.56f;
            if (page == pages[i]) {
                paint.setColor(Color.argb(145, 40, 93, 132));
                c.drawRoundRect(new RectF(s(10), top + s(5), sidebar - s(10), top + itemH - s(5)), s(10), s(10), paint);
            }
            if (controllerCursorVisible && navFocus && selectedNavIndex == i) {
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(Math.max(3f, s(2f)));
                paint.setColor(Color.rgb(116, 196, 255));
                c.drawRoundRect(new RectF(s(10), top + s(5), sidebar - s(10), top + itemH - s(5)), s(10), s(10), paint);
                paint.setStyle(Paint.Style.FILL);
            }
            text.setColor(page == pages[i] ? Color.WHITE : Color.rgb(174, 183, 196));
            text.setTextSize(ts(14));
            text.setFakeBoldText(page == pages[i]);
            c.drawText(labels[i], s(18), cy, text);
        }
        text.setFakeBoldText(false);
    }

    private void drawHome(Canvas c, float sidebar, float w, float h) {
        float left = sidebar + s(22);
        float right = w - s(20);

        text.setColor(Color.WHITE);
        text.setFakeBoldText(true);
        text.setTextSize(ts(15));
        c.drawText(tr("최근 플레이"), left, s(58), text);
        text.setFakeBoldText(false);

        float recentTop = homeRecentTop();
        float recentH = homeRecentHeight();
        if (recentGames.isEmpty()) {
            paint.setColor(Color.argb(102, 27, 42, 58));
            c.drawRoundRect(new RectF(left, recentTop, right, recentTop + recentH), s(11), s(11), paint);
            text.setTextSize(ts(10));
            text.setColor(Color.rgb(179, 195, 211));
            c.drawText(tr("게임을 실행하면 최근 플레이에 표시됩니다."), left + s(16), recentTop + recentH * 0.56f, text);
        } else {
            drawHorizontalRow(c, recentGames, left, right, recentTop, recentH, 5,
                    controllerCursorVisible && !navFocus && selectedHomeSection == 0,
                    selectedHomeRecentIndex);
        }

        float libHeaderY = homeLibraryHeaderY();
        text.setFakeBoldText(true);
        text.setTextSize(ts(15));
        text.setColor(Color.WHITE);
        c.drawText(tr("내 라이브러리"), left, libHeaderY, text);
        text.setFakeBoldText(false);
        drawPlatformTabs(c, left + s(105), libHeaderY - s(20), right);

        float libTop = homeLibraryTop();
        float libH = homeLibraryHeight();
        if (filteredGames.isEmpty()) {
            paint.setColor(Color.argb(102, 27, 42, 58));
            c.drawRoundRect(new RectF(left, libTop, right, libTop + libH), s(11), s(11), paint);
            text.setTextSize(ts(10));
            text.setColor(Color.rgb(179, 195, 211));
            c.drawText(tr("선택한 플랫폼에 게임이 없습니다."), left + s(16), libTop + libH * 0.55f, text);
        } else {
            int visibleCount = Math.min(6, filteredGames.size());
            drawHorizontalRow(c, filteredGames.subList(0, visibleCount), left, right, libTop, libH, 6,
                    controllerCursorVisible && !navFocus && selectedHomeSection == 1,
                    selectedHomeLibraryIndex);
        }
    }

    private void drawLibrary(Canvas c, float sidebar, float w, float h) {
        float left = sidebar + s(22);
        float right = w - s(20);
        text.setColor(Color.WHITE);
        text.setFakeBoldText(true);
        text.setTextSize(ts(22));
        c.drawText(tr("내 라이브러리"), left, s(48), text);
        text.setFakeBoldText(false);
        drawPlatformTabs(c, left, s(60), right);
        text.setTextSize(ts(9));
        text.setColor(Color.rgb(174, 190, 208));
        String gameCount = "ja".equals(UiText.language(getContext())) ?
                filteredGames.size() + " ゲーム" :
                "en".equals(UiText.language(getContext())) ?
                        filteredGames.size() + " games" : filteredGames.size() + "개 게임";
        c.drawText(gameCount + " · " + tr(PLATFORM_LABELS[platformIndex]), left, s(103), text);
        CardLayout layout = libraryLayout();
        drawLibraryCards(c, layout);
    }

    private void drawHorizontalRow(Canvas c, List<GameEntry> list, float left, float right,
                                   float top, float height, int slots,
                                   boolean rowSelected, int selectedIndex) {
        if (list == null || list.isEmpty()) return;
        int visible = Math.min(slots, list.size());
        float gap = s(8);
        float cardW = (right - left - gap * (slots - 1)) / slots;
        for (int i = 0; i < visible; i++) {
            float x = left + i * (cardW + gap);
            RectF card = new RectF(x, top, x + cardW, top + height);
            drawGameCard(c, card, list.get(i), rowSelected && selectedIndex == i);
        }
    }

    private void drawLibraryCards(Canvas c, CardLayout layout) {
        if (filteredGames.isEmpty()) return;
        float stride = rowStride(layout);
        float viewportBottom = layout.top + layout.cardH * 2f + layout.gap;
        int firstRow = Math.max(0, (int) Math.floor(scrollOffsetPx / stride));
        int lastRow = Math.min((filteredGames.size() + layout.cols - 1) / layout.cols - 1, firstRow + 3);
        int save = c.save();
        c.clipRect(layout.left, layout.top, getWidth() - s(20), viewportBottom);
        for (int row = firstRow; row <= lastRow; row++) {
            for (int col = 0; col < layout.cols; col++) {
                int i = row * layout.cols + col;
                if (i >= filteredGames.size()) break;
                RectF card = layout.rectForAbsolute(row, col, scrollOffsetPx);
                if (card.bottom < layout.top || card.top > viewportBottom) continue;
                drawGameCard(c, card, filteredGames.get(i),
                        controllerCursorVisible && !navFocus && i == selectedLibraryIndex);
            }
        }
        c.restoreToCount(save);
    }

    private void drawGameCard(Canvas c, RectF card, GameEntry game, boolean selected) {
        float footerH = Math.min(s(31), card.height() * 0.22f);
        RectF imageRect = new RectF(card.left, card.top, card.right, card.bottom - footerH);
        paint.setColor(Color.argb(142, 28, 43, 58));
        c.drawRoundRect(card, s(12), s(12), paint);
        Bitmap thumbnail = thumbnails.get(game.stableId());
        if (thumbnail != null && !thumbnail.isRecycled()) {
            int save = c.save();
            Path clip = new Path();
            clip.addRoundRect(card, s(12), s(12), Path.Direction.CW);
            c.clipPath(clip);
            Rect src = centerCropRect(thumbnail, imageRect);
            paint.setAlpha(245);
            c.drawBitmap(thumbnail, src, imageRect, paint);
            paint.setAlpha(255);
            c.restoreToCount(save);
        } else {
            paint.setColor(Color.argb(92, 54, 72, 88));
            c.drawRoundRect(imageRect, s(10), s(10), paint);
        }

        paint.setColor(Color.argb(150, 14, 23, 32));
        c.drawRect(card.left, card.bottom - footerH, card.right, card.bottom, paint);

        String badge = game.engine.label;
        text.setTextSize(ts(7.5f));
        text.setFakeBoldText(true);
        float badgeW = text.measureText(badge) + s(12);
        float badgeH = s(19);
        RectF badgeRect = new RectF(imageRect.left + s(7), imageRect.bottom - badgeH - s(7),
                imageRect.left + s(7) + badgeW, imageRect.bottom - s(7));
        paint.setColor(Color.argb(118, 82, 88, 96));
        c.drawRoundRect(badgeRect, s(7), s(7), paint);
        text.setColor(Color.WHITE);
        c.drawText(badge, badgeRect.left + s(6), badgeRect.bottom - s(5), text);
        text.setFakeBoldText(false);

        text.setTextSize(ts(16.5f));
        text.setColor(Color.WHITE);
        Paint.FontMetrics fm = text.getFontMetrics();
        float titleY = card.bottom - footerH * 0.5f - (fm.ascent + fm.descent) * 0.5f;
        float titleLeft = card.left + s(8);
        float titleRight = card.right - s(8);
        float titleWidth = Math.max(0f, titleRight - titleLeft);
        String gameTitle = game.title == null ? "" : game.title;
        float measured = text.measureText(gameTitle);
        int titleSave = c.save();
        c.clipRect(titleLeft, card.bottom - footerH, titleRight, card.bottom);
        if (measured <= titleWidth) {
            text.setTextAlign(Paint.Align.CENTER);
            c.drawText(gameTitle, card.centerX(), titleY, text);
            if (selected) {
                marqueeGameId = game.stableId();
                marqueeStartedMs = SystemClock.uptimeMillis();
            }
        } else {
            text.setTextAlign(Paint.Align.LEFT);
            float offset = 0f;
            if (selected) {
                String id = game.stableId();
                long now = SystemClock.uptimeMillis();
                if (!id.equals(marqueeGameId)) {
                    marqueeGameId = id;
                    marqueeStartedMs = now;
                }
                float distance = Math.max(0f, measured - titleWidth);
                long startPause = 700L;
                long endPause = 850L;
                float speed = Math.max(18f, s(24f));
                long travel = Math.max(1L, (long) ((distance / speed) * 1000f));
                long cycle = startPause + travel + endPause;
                long elapsed = Math.max(0L, now - marqueeStartedMs) % cycle;
                if (elapsed > startPause) {
                    if (elapsed < startPause + travel) {
                        offset = distance * (elapsed - startPause) / (float) travel;
                    } else {
                        offset = distance;
                    }
                }
                postInvalidateOnAnimation();
            }
            c.drawText(gameTitle, titleLeft - offset, titleY, text);
        }
        c.restoreToCount(titleSave);
        text.setTextAlign(Paint.Align.LEFT);

        if (selected) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(3f, s(2.1f)));
            paint.setColor(Color.rgb(111, 199, 255));
            RectF cursor = new RectF(card);
            cursor.inset(s(1.5f), s(1.5f));
            c.drawRoundRect(cursor, s(12), s(12), paint);
            paint.setStyle(Paint.Style.FILL);
        }
    }

    private void drawPlatformTabs(Canvas c, float left, float top, float right) {
        RectF[] rects = platformTabRects(left, top, right);
        for (int i = 0; i < rects.length; i++) {
            RectF r = rects[i];
            boolean active = i == platformIndex;
            paint.setColor(active ? Color.argb(180, 45, 91, 125) : Color.argb(95, 28, 44, 58));
            c.drawRoundRect(r, r.height() * 0.5f, r.height() * 0.5f, paint);
            text.setTextSize(ts(7.6f));
            text.setFakeBoldText(active);
            text.setColor(active ? Color.WHITE : Color.rgb(185, 200, 216));
            text.setTextAlign(Paint.Align.CENTER);
            Paint.FontMetrics fm = text.getFontMetrics();
            c.drawText(tr(PLATFORM_LABELS[i]), r.centerX(),
                    r.centerY() - (fm.ascent + fm.descent) * 0.5f, text);
        }
        text.setTextAlign(Paint.Align.LEFT);
        text.setFakeBoldText(false);
    }

    private RectF[] platformTabRects(float left, float top, float right) {
        RectF[] out = new RectF[PLATFORM_LABELS.length];
        text.setTextSize(ts(7.6f));
        float gap = s(4.5f);
        float h = s(22);
        float[] widths = new float[PLATFORM_LABELS.length];
        float total = gap * (PLATFORM_LABELS.length - 1);
        for (int i = 0; i < PLATFORM_LABELS.length; i++) {
            widths[i] = Math.max(s(38), text.measureText(tr(PLATFORM_LABELS[i])) + s(16));
            total += widths[i];
        }
        float available = Math.max(s(180), right - left);
        float scale = total > available ? available / total : 1f;
        float x = left;
        for (int i = 0; i < PLATFORM_LABELS.length; i++) {
            float w = widths[i] * scale;
            out[i] = new RectF(x, top, x + w, top + h);
            x += w + gap * scale;
        }
        return out;
    }

    private void drawButtonHints(Canvas c, float sidebar, float w, float h) {
        float y = h - s(20);
        float x = w - s(18);
        if (page == Page.HOME || page == Page.LIBRARY) {
            x = drawHintRight(c, x, y, "L2/R2", tr("플랫폼"), Color.rgb(106, 118, 132), false);
            x = drawHintRight(c, x - s(10), y, "SELECT", tr("설정"), Color.rgb(106, 118, 132), false);
            x = drawHintRight(c, x - s(10), y, "B", tr("뒤로"), Color.rgb(214, 72, 89), true);
            drawHintRight(c, x - s(10), y, "A", tr("선택"), Color.rgb(72, 194, 124), true);
        } else {
            x = drawHintRight(c, x, y, "B", tr("뒤로"), Color.rgb(214, 72, 89), true);
            drawHintRight(c, x - s(10), y, "A", tr("선택"), Color.rgb(72, 194, 124), true);
        }
    }

    private float drawHintRight(Canvas c, float right, float y, String icon, String desc,
                                int color, boolean circle) {
        text.setTextSize(ts(8.2f));
        text.setColor(Color.rgb(215, 226, 237));
        text.setTextAlign(Paint.Align.RIGHT);
        c.drawText(desc, right, y, text);
        float descW = text.measureText(desc);
        float iconRight = right - descW - s(7);
        text.setTextSize(ts(circle ? 7.6f : 6.6f));
        float iconTextW = text.measureText(icon);
        float iconW = circle ? s(18) : Math.max(s(30), iconTextW + s(12));
        float iconH = s(18);
        RectF iconRect = new RectF(iconRight - iconW, y - iconH + s(3), iconRight, y + s(3));
        paint.setColor(color);
        c.drawRoundRect(iconRect, circle ? iconH * 0.5f : s(6), circle ? iconH * 0.5f : s(6), paint);
        text.setColor(Color.WHITE);
        text.setFakeBoldText(true);
        text.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics fm = text.getFontMetrics();
        c.drawText(icon, iconRect.centerX(), iconRect.centerY() - (fm.ascent + fm.descent) * 0.5f, text);
        text.setFakeBoldText(false);
        text.setTextAlign(Paint.Align.LEFT);
        return iconRect.left;
    }

    private static Rect centerCropRect(Bitmap bitmap, RectF dst) {
        int bw = bitmap.getWidth();
        int bh = bitmap.getHeight();
        if (bw <= 0 || bh <= 0) return new Rect(0, 0, Math.max(1, bw), Math.max(1, bh));
        float srcAspect = bw / (float) bh;
        float dstAspect = dst.width() / Math.max(1f, dst.height());
        if (srcAspect > dstAspect) {
            int width = Math.max(1, Math.round(bh * dstAspect));
            int left = Math.max(0, (bw - width) / 2);
            return new Rect(left, 0, Math.min(bw, left + width), bh);
        }
        int height = Math.max(1, Math.round(bw / dstAspect));
        int top = Math.max(0, (bh - height) / 2);
        return new Rect(0, top, bw, Math.min(bh, top + height));
    }

    private void drawSettings(Canvas c, float sidebar, float w, float h) {
        float left = sidebar + s(26);
        title(c, tr("설정"), left, s(48));
        SettingsLayout layout = settingsLayout();
        settingRow(c, layout, 0, tr("게임 폴더"), folderLabel, true);
        settingRow(c, layout, 1, tr("라이브러리 재검색"), tr("선택한 폴더 전체 자동 스캔"), true);
        settingRow(c, layout, 2, tr("엔진별 설정"), tr("게임별 설정이 없을 경우 사용되는 기본값 설정"), true);
        settingRow(c, layout, 3, tr("지원 엔진"), "2K / 2K3 / XP / VX / VX Ace / MV / MZ", false);
    }

    private void settingRow(Canvas c, SettingsLayout layout, int index, String label, String value, boolean action) {
        RectF row = layout.rectFor(index);
        paint.setColor(action ? Color.rgb(33, 45, 61) : Color.rgb(28, 37, 49));
        c.drawRoundRect(row, s(10), s(10), paint);
        text.setColor(Color.WHITE);
        text.setTextSize(ts(14));
        c.drawText(label, row.left + s(14), row.top + row.height() * 0.43f, text);
        text.setColor(Color.rgb(152, 169, 188));
        text.setTextSize(ts(10));
        c.drawText(trim(value, 62), row.left + s(14), row.top + row.height() * 0.76f, text);
        if (controllerCursorVisible && page == Page.SETTINGS && index == selectedSettingsIndex) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(3f, s(2.2f)));
            paint.setColor(Color.rgb(105, 185, 255));
            RectF cursor = new RectF(row);
            cursor.inset(s(2), s(2));
            c.drawRoundRect(cursor, s(10), s(10), paint);
            paint.setStyle(Paint.Style.FILL);
        }
    }

    private void title(Canvas c, String value, float x, float y) {
        text.setFakeBoldText(true);
        text.setTextSize(ts(24));
        text.setColor(Color.WHITE);
        c.drawText(value, x, y, text);
        text.setFakeBoldText(false);
    }

    private String tr(String value) {
        return UiText.t(getContext(), value);
    }

    private void subtitle(Canvas c, String value, float x, float y) {
        text.setTextSize(ts(11));
        text.setColor(Color.rgb(142, 156, 175));
        c.drawText(trim(value, 70), x, y, text);
    }

    private int columns() {
        if (getWidth() <= 0) return 4;
        float left = sidebar() + s(22);
        float right = getWidth() - s(20);
        float gap = s(10);
        int cols = (int) ((right - left + gap) / (s(150) + gap));
        return Math.max(3, Math.min(5, cols));
    }

    private CardLayout libraryLayout() {
        float left = sidebar() + s(22);
        float right = getWidth() - s(20);
        float top = s(114);
        float bottom = getHeight() - s(50);
        float gap = s(10);
        int cols = columns();
        float cardW = (right - left - gap * (cols - 1)) / cols;
        float availableH = Math.max(s(150), bottom - top - gap);
        float proportionalMax = cardW * 1.55f;
        float absoluteMax = s(190);
        float cardH = Math.max(s(92), Math.min(availableH / 2f,
                Math.min(proportionalMax, absoluteMax)));
        return new CardLayout(left, top, gap, cardW, cardH, cols);
    }

    private SettingsLayout settingsLayout() {
        float left = sidebar() + s(22);
        float right = getWidth() - s(20);
        float top = s(72);
        float bottom = getHeight() - s(52);
        float gap = s(8);
        float rowH = (bottom - top - gap * 3f) / 4f;
        rowH = Math.max(s(43), Math.min(s(64), rowH));
        return new SettingsLayout(left, right, top, gap, rowH);
    }

    private float homeRecentTop() { return s(70); }
    private float homeRecentHeight() { return Math.min(s(162), getHeight() * 0.245f); }
    private float homeLibraryHeaderY() { return homeRecentTop() + homeRecentHeight() + s(34); }
    private float homeLibraryTop() { return homeLibraryHeaderY() + s(12); }
    private float homeLibraryHeight() {
        float left = sidebar() + s(22);
        float right = getWidth() - s(20);
        float gap = s(8);
        float cardW = Math.max(s(60), (right - left - gap * 5f) / 6f);
        float available = Math.max(s(104), getHeight() - homeLibraryTop() - s(48));
        float proportionalMax = cardW * 1.55f;
        float absoluteMax = s(190);
        return Math.max(s(104), Math.min(available, Math.min(proportionalMax, absoluteMax)));
    }

    private GameEntry findTouchedGame(float x, float y) {
        if (page == Page.SETTINGS) return null;
        float left = sidebar() + s(22);
        float right = getWidth() - s(20);
        if (page == Page.HOME) {
            if (y >= homeRecentTop() && y <= homeRecentTop() + homeRecentHeight()) {
                int idx = rowIndexAt(x, left, right, 5);
                if (idx >= 0 && idx < recentGames.size()) return recentGames.get(idx);
            }
            if (y >= homeLibraryTop() && y <= homeLibraryTop() + homeLibraryHeight()) {
                int idx = rowIndexAt(x, left, right, 6);
                if (idx >= 0 && idx < Math.min(6, filteredGames.size())) return filteredGames.get(idx);
            }
            return null;
        }
        CardLayout layout = libraryLayout();
        float viewportBottom = layout.top + layout.cardH * 2f + layout.gap;
        if (y < layout.top || y > viewportBottom || x < layout.left || x > getWidth() - s(20)) return null;
        float stride = rowStride(layout);
        float contentY = y - layout.top + scrollOffsetPx;
        int row = (int) Math.floor(contentY / stride);
        float withinRow = contentY - row * stride;
        if (withinRow < 0 || withinRow > layout.cardH) return null;
        float relX = x - layout.left;
        int col = (int) Math.floor(relX / (layout.cardW + layout.gap));
        if (col < 0 || col >= layout.cols) return null;
        float withinCol = relX - col * (layout.cardW + layout.gap);
        if (withinCol < 0 || withinCol > layout.cardW) return null;
        int index = row * layout.cols + col;
        return index >= 0 && index < filteredGames.size() ? filteredGames.get(index) : null;
    }

    private void selectTouchedGame(GameEntry game, float y) {
        if (game == null) return;
        String id = game.stableId();
        if (page == Page.HOME) {
            if (y >= homeRecentTop() && y <= homeRecentTop() + homeRecentHeight()) {
                for (int i = 0; i < recentGames.size(); i++) {
                    if (id.equals(recentGames.get(i).stableId())) {
                        selectedHomeSection = 0;
                        selectedHomeRecentIndex = i;
                        return;
                    }
                }
            }
            int count = Math.min(6, filteredGames.size());
            for (int i = 0; i < count; i++) {
                if (id.equals(filteredGames.get(i).stableId())) {
                    selectedHomeSection = 1;
                    selectedHomeLibraryIndex = i;
                    return;
                }
            }
            return;
        }
        if (page == Page.LIBRARY) {
            for (int i = 0; i < filteredGames.size(); i++) {
                if (id.equals(filteredGames.get(i).stableId())) {
                    selectedLibraryIndex = i;
                    return;
                }
            }
        }
    }

    private int rowIndexAt(float x, float left, float right, int slots) {
        if (x < left || x > right) return -1;
        float gap = s(8);
        float cardW = (right - left - gap * (slots - 1)) / slots;
        float rel = x - left;
        int idx = (int) Math.floor(rel / (cardW + gap));
        if (idx < 0 || idx >= slots) return -1;
        float within = rel - idx * (cardW + gap);
        return within <= cardW ? idx : -1;
    }

    private int maxScrollRow(int count, int cols) {
        int rows = (count + cols - 1) / cols;
        return Math.max(0, rows - 2);
    }

    private void setPage(Page value) {
        page = value;
        touchArmedGameId = null;
        selectedNavIndex = value.ordinal();
        navFocus = false;
        if (page != Page.LIBRARY) {
            scroller.abortAnimation();
            scrollOffsetPx = 0f;
        } else {
            clampScrollOffset();
        }
        invalidate();
    }

    private boolean handleNavTouch(float x, float y) {
        if (x >= sidebar()) return false;
        float start = s(101);
        float itemH = s(52);
        if (y >= start && y < start + itemH) setPage(Page.HOME);
        else if (y >= start + itemH && y < start + itemH * 2) setPage(Page.LIBRARY);
        else if (y >= start + itemH * 2 && y < start + itemH * 3) setPage(Page.SETTINGS);
        return true;
    }

    private boolean handlePlatformTouch(float x, float y) {
        if (page != Page.HOME && page != Page.LIBRARY) return false;
        float right = getWidth() - s(20);
        float left;
        float top;
        if (page == Page.HOME) {
            left = sidebar() + s(22) + s(105);
            top = homeLibraryHeaderY() - s(20);
        } else {
            left = sidebar() + s(22);
            top = s(60);
        }
        RectF[] rects = platformTabRects(left, top, right);
        for (int i = 0; i < rects.length; i++) {
            if (rects[i].contains(x, y)) {
                setPlatformIndex(i);
                return true;
            }
        }
        return false;
    }

    private boolean handleSettingsTouch(float x, float y) {
        if (page != Page.SETTINGS || x < sidebar()) return false;
        SettingsLayout layout = settingsLayout();
        if (layout.rectFor(0).contains(x, y) && listener != null) listener.onPickGameFolder();
        else if (layout.rectFor(1).contains(x, y) && listener != null) listener.onRescan();
        else if (layout.rectFor(2).contains(x, y) && listener != null) listener.onEngineSettings();
        return true;
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (busy) {
            handler.removeCallbacks(longPress);
            recycleVelocityTracker();
            return true;
        }
        float x = event.getX(), y = event.getY();
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                if (!scroller.isFinished()) scroller.abortAnimation();
                recycleVelocityTracker();
                velocityTracker = VelocityTracker.obtain();
                velocityTracker.addMovement(event);
                downX = x;
                downY = y;
                touchStartScrollOffset = scrollOffsetPx;
                dragging = false;
                longPressTriggered = false;
                pressedGame = findTouchedGame(x, y);
                if (pressedGame != null) handler.postDelayed(longPress, ViewConfiguration.getLongPressTimeout());
                return true;
            case MotionEvent.ACTION_MOVE:
                if (velocityTracker != null) velocityTracker.addMovement(event);
                if (!dragging && Math.hypot(x - downX, y - downY) > touchSlop * 1.2f) {
                    dragging = true;
                    touchArmedGameId = null;
                    handler.removeCallbacks(longPress);
                }
                if (dragging && page == Page.LIBRARY && downX >= sidebar()) {
                    CardLayout layout = libraryLayout();
                    float max = maxScrollOffset(layout);
                    scrollOffsetPx = Math.max(0f, Math.min(max, touchStartScrollOffset + (downY - y)));
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                handler.removeCallbacks(longPress);
                pressedGame = null;
                dragging = false;
                touchArmedGameId = null;
                recycleVelocityTracker();
                return true;
            case MotionEvent.ACTION_UP:
                handler.removeCallbacks(longPress);
                if (velocityTracker != null) velocityTracker.addMovement(event);
                if (longPressTriggered) {
                    pressedGame = null;
                    touchArmedGameId = null;
                    recycleVelocityTracker();
                    performClick();
                    return true;
                }
                float totalMove = (float) Math.hypot(x - downX, y - downY);
                if ((dragging || totalMove > touchSlop) && page == Page.LIBRARY && downX >= sidebar()) {
                    CardLayout layout = libraryLayout();
                    int max = Math.round(maxScrollOffset(layout));
                    if (velocityTracker != null) {
                        velocityTracker.computeCurrentVelocity(1000, ViewConfiguration.get(getContext()).getScaledMaximumFlingVelocity());
                        int velocityY = Math.round(-velocityTracker.getYVelocity());
                        if (Math.abs(velocityY) > ViewConfiguration.get(getContext()).getScaledMinimumFlingVelocity()) {
                            scroller.fling(0, Math.round(scrollOffsetPx), 0, velocityY, 0, 0, 0, max);
                            postInvalidateOnAnimation();
                        }
                    }
                    pressedGame = null;
                    dragging = false;
                    touchArmedGameId = null;
                    recycleVelocityTracker();
                    performClick();
                    return true;
                }
                if (dragging || totalMove > touchSlop) {
                    pressedGame = null;
                    dragging = false;
                    touchArmedGameId = null;
                    recycleVelocityTracker();
                    performClick();
                    return true;
                }
                recycleVelocityTracker();
                if (handleNavTouch(x, y) || handlePlatformTouch(x, y) || handleSettingsTouch(x, y)) {
                    pressedGame = null;
                    touchArmedGameId = null;
                    performClick();
                    return true;
                }
                GameEntry upGame = findTouchedGame(x, y);
                if (upGame != null && pressedGame != null &&
                        upGame.stableId().equals(pressedGame.stableId())) {
                    String id = upGame.stableId();
                    boolean secondTap = id.equals(touchArmedGameId);
                    selectTouchedGame(upGame, y);
                    controllerCursorVisible = true;
                    navFocus = false;
                    if (secondTap && listener != null) {
                        touchArmedGameId = null;
                        listener.onLaunch(upGame);
                    } else {
                        touchArmedGameId = id;
                        invalidate();
                    }
                } else {
                    touchArmedGameId = null;
                }
                pressedGame = null;
                performClick();
                return true;
            default:
                return true;
        }
    }

    @Override public void computeScroll() {
        super.computeScroll();
        if (scroller.computeScrollOffset()) {
            scrollOffsetPx = scroller.getCurrY();
            clampScrollOffset();
            postInvalidateOnAnimation();
        }
    }

    public boolean scrollLibraryPage(int direction) {
        if (page != Page.LIBRARY || filteredGames.isEmpty() || direction == 0) return false;
        CardLayout layout = libraryLayout();
        float max = maxScrollOffset(layout);
        controllerCursorVisible = true;
        navFocus = false;
        int pageItems = layout.cols * 2;
        selectedLibraryIndex = clampIndex(selectedLibraryIndex +
                (direction > 0 ? pageItems : -pageItems), filteredGames.size());
        if (max <= 0f) {
            invalidate();
            return true;
        }
        float pageDistance = rowStride(layout) * 2f;
        float target = Math.max(0f, Math.min(max, scrollOffsetPx + (direction > 0 ? pageDistance : -pageDistance)));
        scroller.abortAnimation();
        scroller.startScroll(0, Math.round(scrollOffsetPx), 0, Math.round(target - scrollOffsetPx), 260);
        postInvalidateOnAnimation();
        return true;
    }

    public boolean handleControllerKey(int keyCode) {
        if (busy) return true;
        touchArmedGameId = null;
        controllerCursorVisible = true;
        if (navFocus) return handleNavController(keyCode);

        if ((keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2) &&
                (page == Page.HOME || page == Page.LIBRARY)) {
            cyclePlatform(keyCode == KeyEvent.KEYCODE_BUTTON_R2 ? 1 : -1);
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_BUTTON_B || keyCode == KeyEvent.KEYCODE_BACK) {
            if (page != Page.HOME) setPage(Page.HOME);
            else {
                navFocus = true;
                selectedNavIndex = 0;
                invalidate();
            }
            return true;
        }

        if (page == Page.SETTINGS) return handleSettingsController(keyCode);
        if (page == Page.HOME) return handleHomeController(keyCode);
        return handleLibraryController(keyCode);
    }

    private boolean handleNavController(int keyCode) {
        if (keyCode == KeyEvent.KEYCODE_DPAD_UP) {
            selectedNavIndex = Math.max(0, selectedNavIndex - 1);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
            selectedNavIndex = Math.min(2, selectedNavIndex + 1);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT || keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            navFocus = false;
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                keyCode == KeyEvent.KEYCODE_ENTER) {
            setPage(Page.values()[selectedNavIndex]);
            navFocus = false;
            invalidate();
            return true;
        }
        return false;
    }

    private boolean handleSettingsController(int keyCode) {
        if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
            navFocus = true;
            selectedNavIndex = 2;
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_UP) {
            selectedSettingsIndex = Math.max(0, selectedSettingsIndex - 1);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
            selectedSettingsIndex = Math.min(3, selectedSettingsIndex + 1);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                keyCode == KeyEvent.KEYCODE_ENTER) {
            if (listener != null && selectedSettingsIndex == 0) listener.onPickGameFolder();
            else if (listener != null && selectedSettingsIndex == 1) listener.onRescan();
            else if (listener != null && selectedSettingsIndex == 2) listener.onEngineSettings();
            return true;
        }
        return false;
    }

    private boolean handleHomeController(int keyCode) {
        if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
            if (selectedHomeSection == 0) {
                if (selectedHomeRecentIndex > 0) selectedHomeRecentIndex--;
                else { navFocus = true; selectedNavIndex = 0; }
            } else {
                if (selectedHomeLibraryIndex > 0) selectedHomeLibraryIndex--;
                else { navFocus = true; selectedNavIndex = 0; }
            }
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) {
            if (selectedHomeSection == 0 && !recentGames.isEmpty()) {
                selectedHomeRecentIndex = Math.min(recentGames.size() - 1, selectedHomeRecentIndex + 1);
            } else if (selectedHomeSection == 1 && !filteredGames.isEmpty()) {
                selectedHomeLibraryIndex = Math.min(Math.min(6, filteredGames.size()) - 1,
                        selectedHomeLibraryIndex + 1);
            }
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_UP) {
            if (!recentGames.isEmpty()) selectedHomeSection = 0;
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
            if (selectedHomeSection == 0) {
                selectedHomeSection = 1;
            } else {
                setPage(Page.LIBRARY);
                selectedLibraryIndex = clampIndex(selectedHomeLibraryIndex, filteredGames.size());
                ensureControllerSelectionVisible(libraryLayout());
            }
            invalidate();
            return true;
        }
        GameEntry selected = selectedHomeGame();
        if (selected == null) return false;
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                keyCode == KeyEvent.KEYCODE_ENTER) {
            if (listener != null) listener.onLaunch(selected);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (listener != null) listener.onGameSettings(selected);
            return true;
        }
        return false;
    }

    private boolean handleLibraryController(int keyCode) {
        if (filteredGames.isEmpty()) {
            if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
                navFocus = true;
                selectedNavIndex = 1;
                invalidate();
                return true;
            }
            return false;
        }
        CardLayout layout = libraryLayout();
        int cols = layout.cols;
        int next = selectedLibraryIndex;
        if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
            if (selectedLibraryIndex % cols > 0) next--;
            else { navFocus = true; selectedNavIndex = 1; invalidate(); return true; }
        } else if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) {
            if (selectedLibraryIndex + 1 < filteredGames.size() &&
                    selectedLibraryIndex / cols == (selectedLibraryIndex + 1) / cols) next++;
        } else if (keyCode == KeyEvent.KEYCODE_DPAD_UP) {
            next = Math.max(0, selectedLibraryIndex - cols);
        } else if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
            next = Math.min(filteredGames.size() - 1, selectedLibraryIndex + cols);
        } else if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                keyCode == KeyEvent.KEYCODE_ENTER) {
            if (listener != null) listener.onLaunch(filteredGames.get(selectedLibraryIndex));
            return true;
        } else if (keyCode == KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (listener != null) listener.onGameSettings(filteredGames.get(selectedLibraryIndex));
            return true;
        } else {
            return false;
        }
        selectedLibraryIndex = next;
        ensureControllerSelectionVisible(layout);
        invalidate();
        return true;
    }

    private GameEntry selectedHomeGame() {
        if (selectedHomeSection == 0) {
            return recentGames.isEmpty() ? null : recentGames.get(clampIndex(selectedHomeRecentIndex, recentGames.size()));
        }
        int count = Math.min(6, filteredGames.size());
        return count <= 0 ? null : filteredGames.get(clampIndex(selectedHomeLibraryIndex, count));
    }

    private void setPlatformIndex(int index) {
        int count = PLATFORM_LABELS.length;
        platformIndex = ((index % count) + count) % count;
        selectedHomeLibraryIndex = 0;
        selectedLibraryIndex = 0;
        scrollOffsetPx = 0f;
        scroller.abortAnimation();
        rebuildCollections();
        invalidate();
    }

    private void cyclePlatform(int delta) { setPlatformIndex(platformIndex + delta); }

    private void ensureControllerSelectionVisible(CardLayout layout) {
        if (page != Page.LIBRARY || filteredGames.isEmpty()) return;
        int row = selectedLibraryIndex / layout.cols;
        float stride = rowStride(layout);
        int firstVisibleRow = Math.max(0, (int) Math.floor(scrollOffsetPx / stride));
        int desiredFirst = firstVisibleRow;
        if (row < firstVisibleRow) desiredFirst = row;
        else if (row > firstVisibleRow + 1) desiredFirst = row - 1;
        float target = Math.max(0f, Math.min(maxScrollOffset(layout), desiredFirst * stride));
        if (Math.abs(target - scrollOffsetPx) < 1f) return;
        scroller.abortAnimation();
        scroller.startScroll(0, Math.round(scrollOffsetPx), 0,
                Math.round(target - scrollOffsetPx), 180);
        postInvalidateOnAnimation();
    }

    private void recycleVelocityTracker() {
        if (velocityTracker != null) {
            velocityTracker.recycle();
            velocityTracker = null;
        }
    }

    private float rowStride(CardLayout layout) {
        return layout.cardH + layout.gap;
    }

    private float maxScrollOffset(CardLayout layout) {
        return maxScrollRow(filteredGames.size(), layout.cols) * rowStride(layout);
    }

    private void clampScrollOffset() {
        if (getWidth() <= 0 || getHeight() <= 0 || filteredGames.isEmpty()) {
            scrollOffsetPx = 0f;
            return;
        }
        CardLayout layout = libraryLayout();
        scrollOffsetPx = Math.max(0f, Math.min(maxScrollOffset(layout), scrollOffsetPx));
    }

    @Override public boolean performClick() {
        super.performClick();
        return true;
    }

    private static String trim(String s, int max) {
        if (s == null) return "";
        return s.length() <= max ? s : s.substring(0, Math.max(1, max - 1)) + "…";
    }

    private static int engineColor(GameEntry.Engine e) {
        switch (e) {
            case MV: return Color.rgb(85, 119, 208);
            case MZ: return Color.rgb(128, 88, 205);
            case XP: return Color.rgb(68, 155, 112);
            case VX: return Color.rgb(173, 110, 64);
            case VXACE: return Color.rgb(183, 83, 98);
            default: return Color.rgb(85, 138, 160);
        }
    }

    private static final class CardLayout {
        final float left, top, gap, cardW, cardH;
        final int cols;
        CardLayout(float left, float top, float gap, float cardW, float cardH, int cols) {
            this.left = left; this.top = top; this.gap = gap; this.cardW = cardW; this.cardH = cardH; this.cols = cols;
        }
        RectF rectFor(int visibleIndex) {
            int row = visibleIndex / cols;
            int col = visibleIndex % cols;
            float x = left + col * (cardW + gap);
            float y = top + row * (cardH + gap);
            return new RectF(x, y, x + cardW, y + cardH);
        }
        RectF rectForAbsolute(int row, int col, float scrollOffset) {
            float x = left + col * (cardW + gap);
            float y = top + row * (cardH + gap) - scrollOffset;
            return new RectF(x, y, x + cardW, y + cardH);
        }
    }

    private static final class SettingsLayout {
        final float left, right, top, gap, rowH;
        SettingsLayout(float left, float right, float top, float gap, float rowH) {
            this.left = left; this.right = right; this.top = top; this.gap = gap; this.rowH = rowH;
        }
        RectF rectFor(int index) {
            float y = top + index * (rowH + gap);
            return new RectF(left, y, right, y + rowH);
        }
    }
}