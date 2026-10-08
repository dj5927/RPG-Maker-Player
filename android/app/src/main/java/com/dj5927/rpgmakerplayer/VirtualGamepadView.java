package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.hardware.input.InputManager;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public final class VirtualGamepadView extends View implements InputManager.InputDeviceListener {
    public interface Listener {
        void onKey(int androidKeyCode, boolean down);
    }

    public interface MouseListener {
        void onMove(float normalizedX, float normalizedY);
        void onButton(int button, boolean down);
        void onWheel(float deltaY);
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SparseIntArray activePointers = new SparseIntArray();
    private final InputManager inputManager;
    private Listener listener;
    private MouseListener mouseListener;
    private boolean largeMode;
    private boolean mouseMode;
    private boolean inputListenerRegistered;

    private int analogPointerId = -1;
    private float analogX;
    private float analogY;
    private boolean analogUp;
    private boolean analogDown;
    private boolean analogLeft;
    private boolean analogRight;
    private float mouseTouchOriginX;
    private float mouseTouchOriginY;
    private boolean mouseTouchOriginValid;
    private float mouseCursorX = -1f;
    private float mouseCursorY = -1f;
    private boolean mouseMoveTicking;
    private long mouseLastFrameNanos;
    private boolean hardwareGamepadPresent;

    private final Runnable mouseMoveTick = new Runnable() {
        @Override public void run() {
            if (!mouseMode || analogPointerId < 0 || getWidth() <= 0 || getHeight() <= 0) {
                mouseMoveTicking = false;
                return;
            }
            float s = Math.min(getWidth(), getHeight());
            float d = s * (largeMode ? 0.10f : 0.085f);
            float baseR = d * 1.42f;
            float mag = (float) Math.sqrt(analogX * analogX + analogY * analogY);
            float dead = baseR * 0.34f;
            long now = System.nanoTime();
            float dt = mouseLastFrameNanos == 0L ? (1f / 60f) :
                    Math.min(0.05f, (now - mouseLastFrameNanos) / 1_000_000_000f);
            mouseLastFrameNanos = now;
            if (mag > dead) {
                float nx = analogX / Math.max(1f, mag);
                float ny = analogY / Math.max(1f, mag);
                float strength = Math.min(1f, (mag - dead) / Math.max(1f, baseR - dead));
                float eased = strength * strength * (3f - 2f * strength);
                float speed = s * 0.80f * dt * (0.18f + eased * 0.82f);
                mouseCursorX = clamp(mouseCursorX + nx * speed, 0f, getWidth() - 1f);
                mouseCursorY = clamp(mouseCursorY + ny * speed, 0f, getHeight() - 1f);
                notifyMouseMove();
                invalidate();
            }
            postOnAnimation(this);
        }
    };

    public VirtualGamepadView(Context context) {
        super(context);
        setBackgroundColor(Color.TRANSPARENT);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        setFocusable(false);
        inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
    }

    public void setListener(Listener listener) { this.listener = listener; }

    public void setMouseListener(MouseListener listener) { this.mouseListener = listener; }

    public void setMouseMode(boolean mouseMode) {
        if (this.mouseMode == mouseMode) return;
        releaseAllInputs();
        this.mouseMode = mouseMode;
        refreshHardwareGamepadVisibility();
        invalidate();
        if (mouseMode) post(this::notifyMouseMove);
    }

    public void setMouseCursorNormalized(float normalizedX, float normalizedY) {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        mouseCursorX = clamp(normalizedX, 0f, 1f) * getWidth();
        mouseCursorY = clamp(normalizedY, 0f, 1f) * getHeight();
        invalidate();
    }

    public void setLargeMode(boolean largeMode) {
        this.largeMode = largeMode;
        invalidate();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (inputManager != null && !inputListenerRegistered) {
            inputManager.registerInputDeviceListener(this, null);
            inputListenerRegistered = true;
        }
        post(this::refreshHardwareGamepadVisibility);
    }

    @Override protected void onDetachedFromWindow() {
        releaseAllInputs();
        if (inputManager != null && inputListenerRegistered) {
            try { inputManager.unregisterInputDeviceListener(this); } catch (Throwable ignored) {}
            inputListenerRegistered = false;
        }
        super.onDetachedFromWindow();
    }

    @Override public void onInputDeviceAdded(int deviceId) { post(this::refreshHardwareGamepadVisibility); }
    @Override public void onInputDeviceRemoved(int deviceId) { post(this::refreshHardwareGamepadVisibility); }
    @Override public void onInputDeviceChanged(int deviceId) { post(this::refreshHardwareGamepadVisibility); }

    private void refreshHardwareGamepadVisibility() {
        boolean hardwarePad = hasHardwareGamepad(getContext());
        if (hardwarePad) releaseAllInputs();
        hardwareGamepadPresent = hardwarePad;
        setVisibility(hardwarePad && !mouseMode ? GONE : VISIBLE);
        invalidate();
    }

    public static boolean hasHardwareGamepad(Context context) {
        if (context == null) return false;
        int[] ids = InputDevice.getDeviceIds();
        for (int id : ids) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            boolean gamepad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
            boolean joystick = (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
            if (gamepad || joystick) return true;
        }
        return false;
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), h = getHeight();
        float s = Math.min(w, h);
        float d = s * (largeMode ? 0.10f : 0.085f);
        float faceR = d * 0.72f;
        float smallR = d * 0.54f;

        float lx = s * 0.17f;
        float ly = h - s * 0.18f;
        if (mouseMode && hardwareGamepadPresent) {
            drawMouseCursor(canvas);
            return;
        }
        float analogCx = mouseMode && mouseTouchOriginValid ? mouseTouchOriginX : lx;
        float analogCy = mouseMode && mouseTouchOriginValid ? mouseTouchOriginY : ly;
        drawAnalog(canvas, analogCx, analogCy, d);

        float rx = w - s * 0.19f;
        float ry = h - s * 0.19f;
        final float faceGap = 1.15f;
        if (mouseMode) {
            drawButton(canvas, rx, ry + d * faceGap, faceR, "좌클릭");
            drawButton(canvas, rx + d * faceGap, ry, faceR, "우클릭");
            drawButton(canvas, rx - d * faceGap, ry, faceR, "휠↑");
            drawButton(canvas, rx, ry - d * faceGap, faceR, "휠↓");
            drawMouseCursor(canvas);
            return;
        }
        drawButton(canvas, rx, ry - d * faceGap, faceR, "Y");
        drawButton(canvas, rx, ry + d * faceGap, faceR, "A");
        drawButton(canvas, rx - d * faceGap, ry, faceR, "X");
        drawButton(canvas, rx + d * faceGap, ry, faceR, "B");

        float shoulderY = Math.max(smallR + 8f, s * 0.085f);
        drawButton(canvas, w * 0.09f, shoulderY, smallR, "L1");
        drawButton(canvas, w * 0.19f, shoulderY, smallR, "L2");
        drawButton(canvas, w * 0.81f, shoulderY, smallR, "R2");
        drawButton(canvas, w * 0.91f, shoulderY, smallR, "R1");

        float centerY = h - s * 0.075f;
        drawButton(canvas, w * 0.46f, centerY, smallR * 0.86f, "SELECT");
        drawButton(canvas, w * 0.54f, centerY, smallR * 0.86f, "START");
    }

    private void drawAnalog(Canvas canvas, float cx, float cy, float d) {
        float baseR = d * 1.42f;
        float knobR = d * 0.64f;

        fill.setColor(Color.argb(78, 18, 25, 34));
        canvas.drawCircle(cx, cy, baseR, fill);
        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(Math.max(2f, d * 0.055f));
        fill.setColor(Color.argb(145, 220, 232, 244));
        canvas.drawCircle(cx, cy, baseR, fill);
        fill.setStrokeWidth(Math.max(1.5f, d * 0.025f));
        fill.setColor(Color.argb(65, 220, 232, 244));
        canvas.drawCircle(cx, cy, baseR * 0.55f, fill);
        fill.setStyle(Paint.Style.FILL);

        float maxTravel = baseR * 0.52f;
        float dx = analogX;
        float dy = analogY;
        float mag = (float) Math.sqrt(dx * dx + dy * dy);
        if (mag > maxTravel && mag > 0f) {
            float scale = maxTravel / mag;
            dx *= scale;
            dy *= scale;
        }

        fill.setColor(Color.argb(122, 28, 38, 50));
        canvas.drawCircle(cx + dx, cy + dy, knobR, fill);
        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(Math.max(2f, d * 0.045f));
        fill.setColor(Color.argb(190, 235, 242, 250));
        canvas.drawCircle(cx + dx, cy + dy, knobR, fill);
        fill.setStyle(Paint.Style.FILL);

        fill.setColor(Color.argb(85, 235, 242, 250));
        canvas.drawCircle(cx + dx, cy + dy, knobR * 0.22f, fill);
    }

    private void drawButton(Canvas canvas, float x, float y, float r, String label) {
        fill.setColor(Color.argb(96, 20, 27, 36));
        canvas.drawCircle(x, y, r, fill);
        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(Math.max(2f, r * 0.045f));
        fill.setColor(Color.argb(165, 225, 235, 245));
        canvas.drawCircle(x, y, r, fill);
        fill.setStyle(Paint.Style.FILL);
        text.setColor(Color.argb(215, 255, 255, 255));
        float factor = label.length() > 2 ? 0.34f : 0.68f;
        text.setTextSize(r * factor);
        Paint.FontMetrics fm = text.getFontMetrics();
        canvas.drawText(label, x, y - (fm.ascent + fm.descent) / 2f, text);
    }

    private void drawMouseCursor(Canvas canvas) {
        if (mouseCursorX < 0f || mouseCursorY < 0f) return;
        float s = Math.min(getWidth(), getHeight());
        float r = Math.max(9f, s * 0.0135f);
        float shadow = Math.max(3f, s * 0.004f);

        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(Math.max(7f, s * 0.008f));
        fill.setColor(Color.argb(220, 0, 0, 0));
        canvas.drawCircle(mouseCursorX + shadow, mouseCursorY + shadow, r, fill);
        canvas.drawLine(mouseCursorX - r * 1.55f + shadow, mouseCursorY + shadow,
                mouseCursorX + r * 1.55f + shadow, mouseCursorY + shadow, fill);
        canvas.drawLine(mouseCursorX + shadow, mouseCursorY - r * 1.55f + shadow,
                mouseCursorX + shadow, mouseCursorY + r * 1.55f + shadow, fill);

        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(Math.max(5f, s * 0.0055f));
        fill.setColor(Color.argb(245, 0, 0, 0));
        canvas.drawCircle(mouseCursorX, mouseCursorY, r, fill);
        canvas.drawLine(mouseCursorX - r * 1.55f, mouseCursorY,
                mouseCursorX + r * 1.55f, mouseCursorY, fill);
        canvas.drawLine(mouseCursorX, mouseCursorY - r * 1.55f,
                mouseCursorX, mouseCursorY + r * 1.55f, fill);

        fill.setStrokeWidth(Math.max(2f, s * 0.0023f));
        fill.setColor(Color.WHITE);
        canvas.drawCircle(mouseCursorX, mouseCursorY, r, fill);
        canvas.drawLine(mouseCursorX - r * 1.55f, mouseCursorY,
                mouseCursorX + r * 1.55f, mouseCursorY, fill);
        canvas.drawLine(mouseCursorX, mouseCursorY - r * 1.55f,
                mouseCursorX, mouseCursorY + r * 1.55f, fill);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(Color.BLACK);
        canvas.drawCircle(mouseCursorX, mouseCursorY, Math.max(4f, r * 0.30f), fill);
        fill.setColor(Color.WHITE);
        canvas.drawCircle(mouseCursorX, mouseCursorY, Math.max(2f, r * 0.13f), fill);
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (mouseCursorX < 0f || mouseCursorY < 0f || oldw <= 0 || oldh <= 0) {
            mouseCursorX = w * 0.5f;
            mouseCursorY = h * 0.5f;
        } else {
            mouseCursorX = w * (mouseCursorX / oldw);
            mouseCursorY = h * (mouseCursorY / oldh);
        }
        if (mouseMode) post(this::notifyMouseMove);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (mouseMode && hardwareGamepadPresent) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            int index = event.getActionIndex();
            int pointerId = event.getPointerId(index);
            float x = event.getX(index), y = event.getY(index);
            if (analogPointerId < 0 && insideAnalogZone(x, y)) {
                analogPointerId = pointerId;
                if (mouseMode) {
                    mouseTouchOriginX = x;
                    mouseTouchOriginY = y;
                    mouseTouchOriginValid = true;
                }
                updateAnalog(x, y);
                return true;
            }
            int key = mouseMode ? hitMouseButtonKey(x, y) : hitButtonKey(x, y);
            if (key != 0) {
                if (mouseMode) handleMouseControlDown(pointerId, key);
                else {
                    activePointers.put(pointerId, key);
                    sendKey(key, true);
                }
                return true;
            }
            // The gamepad is a full-screen transparent overlay. Returning true here
            // used to swallow every touch before the MV/MZ WebView could see it.
            // Only claim gestures that actually start on a virtual control.
            return false;
        }

        if (action == MotionEvent.ACTION_POINTER_DOWN) {
            int index = event.getActionIndex();
            int pointerId = event.getPointerId(index);
            float x = event.getX(index), y = event.getY(index);
            if (analogPointerId < 0 && insideAnalogZone(x, y)) {
                analogPointerId = pointerId;
                if (mouseMode) {
                    mouseTouchOriginX = x;
                    mouseTouchOriginY = y;
                    mouseTouchOriginValid = true;
                }
                updateAnalog(x, y);
                return true;
            }
            int key = mouseMode ? hitMouseButtonKey(x, y) : hitButtonKey(x, y);
            if (key != 0) {
                if (mouseMode) handleMouseControlDown(pointerId, key);
                else {
                    activePointers.put(pointerId, key);
                    sendKey(key, true);
                }
            }
            return true;
        }

        if (action == MotionEvent.ACTION_MOVE) {
            if (analogPointerId >= 0) {
                int index = event.findPointerIndex(analogPointerId);
                if (index >= 0) updateAnalog(event.getX(index), event.getY(index));
            }
            return true;
        }

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int pointerId = event.getPointerId(event.getActionIndex());
            if (pointerId == analogPointerId) {
                releaseAnalog();
                analogPointerId = -1;
            } else {
                int key = activePointers.get(pointerId, 0);
                if (key != 0) {
                    if (mouseMode) handleMouseControlUp(key);
                    else sendKey(key, false);
                }
                activePointers.delete(pointerId);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseAllInputs();
            return true;
        }
        return true;
    }

    private boolean insideAnalogZone(float x, float y) {
        float s = Math.min(getWidth(), getHeight());
        float d = s * (largeMode ? 0.10f : 0.085f);
        float lx = s * 0.17f;
        float ly = getHeight() - s * 0.18f;
        return inside(x, y, lx, ly, d * 1.75f);
    }

    private void updateAnalog(float x, float y) {
        float s = Math.min(getWidth(), getHeight());
        float d = s * (largeMode ? 0.10f : 0.085f);
        float cx = mouseMode && mouseTouchOriginValid ? mouseTouchOriginX : s * 0.17f;
        float cy = mouseMode && mouseTouchOriginValid ? mouseTouchOriginY : getHeight() - s * 0.18f;
        float baseR = d * 1.42f;
        float dx = x - cx;
        float dy = y - cy;
        float mag = (float) Math.sqrt(dx * dx + dy * dy);
        float dead = baseR * 0.24f;

        analogX = dx;
        analogY = dy;
        if (mouseMode) {
            if (!mouseMoveTicking) {
                mouseMoveTicking = true;
                postOnAnimation(mouseMoveTick);
            }
            invalidate();
            return;
        }
        boolean up = false, down = false, left = false, right = false;
        if (mag >= dead) {
            float nx = dx / mag;
            float ny = dy / mag;
            final float axisThreshold = 0.38f;
            left = nx <= -axisThreshold;
            right = nx >= axisThreshold;
            up = ny <= -axisThreshold;
            down = ny >= axisThreshold;
        }

        setAnalogDirection(KeyEvent.KEYCODE_DPAD_UP, up, analogUp);
        setAnalogDirection(KeyEvent.KEYCODE_DPAD_DOWN, down, analogDown);
        setAnalogDirection(KeyEvent.KEYCODE_DPAD_LEFT, left, analogLeft);
        setAnalogDirection(KeyEvent.KEYCODE_DPAD_RIGHT, right, analogRight);
        analogUp = up;
        analogDown = down;
        analogLeft = left;
        analogRight = right;
        invalidate();
    }

    private void setAnalogDirection(int key, boolean next, boolean previous) {
        if (next != previous) sendKey(key, next);
    }

    private void releaseAnalog() {
        if (mouseMode) {
            analogX = analogY = 0f;
            mouseTouchOriginValid = false;
            mouseLastFrameNanos = 0L;
            invalidate();
            return;
        }
        if (analogUp) sendKey(KeyEvent.KEYCODE_DPAD_UP, false);
        if (analogDown) sendKey(KeyEvent.KEYCODE_DPAD_DOWN, false);
        if (analogLeft) sendKey(KeyEvent.KEYCODE_DPAD_LEFT, false);
        if (analogRight) sendKey(KeyEvent.KEYCODE_DPAD_RIGHT, false);
        analogUp = analogDown = analogLeft = analogRight = false;
        analogX = analogY = 0f;
        invalidate();
    }

    private void releaseAllInputs() {
        releaseAnalog();
        analogPointerId = -1;
        for (int i = 0; i < activePointers.size(); i++) {
            int value = activePointers.valueAt(i);
            if (mouseMode) handleMouseControlUp(value);
            else sendKey(value, false);
        }
        activePointers.clear();
    }

    private void sendKey(int key, boolean down) {
        if (listener != null) listener.onKey(key, down);
    }

    private void handleMouseControlDown(int pointerId, int key) {
        if (key == KeyEvent.KEYCODE_BUTTON_A) {
            activePointers.put(pointerId, key);
            if (mouseListener != null) mouseListener.onButton(0, true);
        } else if (key == KeyEvent.KEYCODE_BUTTON_B) {
            activePointers.put(pointerId, key);
            if (mouseListener != null) mouseListener.onButton(2, true);
        } else if (key == KeyEvent.KEYCODE_BUTTON_X) {
            if (mouseListener != null) mouseListener.onWheel(-120f);
        } else if (key == KeyEvent.KEYCODE_BUTTON_Y) {
            if (mouseListener != null) mouseListener.onWheel(120f);
        }
    }

    private void handleMouseControlUp(int key) {
        if (key == KeyEvent.KEYCODE_BUTTON_A) {
            if (mouseListener != null) mouseListener.onButton(0, false);
        } else if (key == KeyEvent.KEYCODE_BUTTON_B) {
            if (mouseListener != null) mouseListener.onButton(2, false);
        }
    }

    private void notifyMouseMove() {
        if (!mouseMode || mouseListener == null || getWidth() <= 0 || getHeight() <= 0) return;
        mouseListener.onMove(clamp(mouseCursorX / getWidth(), 0f, 1f),
                clamp(mouseCursorY / getHeight(), 0f, 1f));
    }

    private int hitButtonKey(float x, float y) {
        float w = getWidth(), h = getHeight();
        float s = Math.min(w, h);
        float d = s * (largeMode ? 0.10f : 0.085f);
        float r = d * 0.84f;
        float smallR = d * 0.68f;

        float rx = w - s * 0.19f;
        float ry = h - s * 0.19f;
        final float faceGap = 1.15f;
        if (inside(x, y, rx, ry + d * faceGap, r)) return KeyEvent.KEYCODE_BUTTON_A;
        if (inside(x, y, rx + d * faceGap, ry, r)) return KeyEvent.KEYCODE_BUTTON_B;
        if (inside(x, y, rx - d * faceGap, ry, r)) return KeyEvent.KEYCODE_BUTTON_X;
        if (inside(x, y, rx, ry - d * faceGap, r)) return KeyEvent.KEYCODE_BUTTON_Y;

        float shoulderY = Math.max(smallR + 8f, s * 0.085f);
        if (inside(x, y, w * 0.09f, shoulderY, smallR)) return KeyEvent.KEYCODE_BUTTON_L1;
        if (inside(x, y, w * 0.19f, shoulderY, smallR)) return KeyEvent.KEYCODE_BUTTON_L2;
        if (inside(x, y, w * 0.81f, shoulderY, smallR)) return KeyEvent.KEYCODE_BUTTON_R2;
        if (inside(x, y, w * 0.91f, shoulderY, smallR)) return KeyEvent.KEYCODE_BUTTON_R1;

        float centerY = h - s * 0.075f;
        if (inside(x, y, w * 0.46f, centerY, smallR)) return KeyEvent.KEYCODE_BUTTON_SELECT;
        if (inside(x, y, w * 0.54f, centerY, smallR)) return KeyEvent.KEYCODE_BUTTON_START;
        return 0;
    }

    private int hitMouseButtonKey(float x, float y) {
        float w = getWidth(), h = getHeight();
        float s = Math.min(w, h);
        float d = s * (largeMode ? 0.10f : 0.085f);
        float r = d * 0.84f;
        float rx = w - s * 0.19f;
        float ry = h - s * 0.19f;
        final float faceGap = 1.15f;
        if (inside(x, y, rx, ry + d * faceGap, r)) return KeyEvent.KEYCODE_BUTTON_A;
        if (inside(x, y, rx + d * faceGap, ry, r)) return KeyEvent.KEYCODE_BUTTON_B;
        if (inside(x, y, rx - d * faceGap, ry, r)) return KeyEvent.KEYCODE_BUTTON_X;
        if (inside(x, y, rx, ry - d * faceGap, r)) return KeyEvent.KEYCODE_BUTTON_Y;
        return 0;
    }

    private static float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }

    private static boolean inside(float x, float y, float cx, float cy, float r) {
        float dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= r * r;
    }
}
