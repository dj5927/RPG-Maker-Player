#!/usr/bin/env bash
# RPG Maker Player - SteamOS Desktop Mode registration helper
# This script ONLY registers shortcuts/artwork. It never launches the player.

set -u

ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
STATE_DIR="$HOME/.local/share/rpgmakerplayer"
LOG_FILE="$STATE_DIR/register.log"
STEAM_WRAPPER="$STATE_DIR/steam-launch.sh"
STEAM_MARKER="$STATE_DIR/steam_registered_path.txt"
APPS_DIR="$HOME/.local/share/applications"
APP_DESKTOP="$APPS_DIR/rpg-maker-player.desktop"
ICON_SRC="$ROOT/steam_art/ICON.PNG"
ICON_DST="$HOME/.local/share/icons/rpg-maker-player.png"
STEAM_ART_DIR="$ROOT/steam_art"

mkdir -p "$STATE_DIR" 2>/dev/null || true
: > "$LOG_FILE" 2>/dev/null || true

HAS_DISPLAY=0
[ -n "${DISPLAY:-}" ] && HAS_DISPLAY=1
[ -n "${WAYLAND_DISPLAY:-}" ] && HAS_DISPLAY=1

log() {
    printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || date)" "$1" >> "$LOG_FILE" 2>/dev/null || true
}

show_info() {
    if [ "$HAS_DISPLAY" = "1" ] && command -v kdialog >/dev/null 2>&1; then
        kdialog --title "RPG Maker Player" --msgbox "$1" >/dev/null 2>&1 || true
    elif [ "$HAS_DISPLAY" = "1" ] && command -v zenity >/dev/null 2>&1; then
        zenity --info --title="RPG Maker Player" --text="$1" --width=440 2>/dev/null || true
    else
        printf '%b\n' "$1"
    fi
}

show_error() {
    if [ "$HAS_DISPLAY" = "1" ] && command -v kdialog >/dev/null 2>&1; then
        kdialog --title "RPG Maker Player" --error "$1" >/dev/null 2>&1 || true
    elif [ "$HAS_DISPLAY" = "1" ] && command -v zenity >/dev/null 2>&1; then
        zenity --error --title="RPG Maker Player" --text="$1" --width=520 2>/dev/null || true
    else
        printf '[ERROR] %b\n' "$1" >&2
    fi
}

ensure_permissions() {
    log "setting executable permissions"
    chmod +x "$ROOT/run.sh" "$ROOT/MKXP_Launcher" "$ROOT/register_steamos.sh" 2>/dev/null || true
    chmod +x "$ROOT/RPG Maker Player.desktop" 2>/dev/null || true

    if [ -d "$ROOT/runtime" ]; then
        find "$ROOT/runtime" -type f \( \
            -name '*.sh' -o \
            -name 'mkxp-z' -o \
            -name 'ruby' -o \
            -name 'easyrpg-player' -o \
            -name 'nw' -o \
            -name 'cicpoffs' -o \
            -name 'chrome_crashpad_handler' -o \
            -name 'nacl_helper' -o \
            -name 'nacl_helper_bootstrap' \
        \) -exec chmod +x {} + 2>/dev/null || true
    fi

    if [ ! -x "$ROOT/MKXP_Launcher" ]; then
        log "MKXP_Launcher remains non-executable"
        return 1
    fi
    return 0
}

desktop_dir() {
    local d=""
    if command -v xdg-user-dir >/dev/null 2>&1; then
        d="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    fi
    [ -z "$d" ] && d="$HOME/Desktop"
    printf '%s\n' "$d"
}

ensure_launcher_shortcuts() {
    mkdir -p "$STATE_DIR" "$APPS_DIR" "$HOME/.local/share/icons" 2>/dev/null || return 1

    if [ ! -f "$ICON_SRC" ]; then
        log "missing desktop icon: $ICON_SRC"
        return 1
    fi
    cp -f "$ICON_SRC" "$ICON_DST" 2>/dev/null || return 1

    {
        printf '%s\n' '#!/usr/bin/env bash'
        printf 'ROOT=%q\n' "$ROOT"
        cat <<'EOF'
cd "$ROOT" || exit 1
exec /usr/bin/env bash "$ROOT/run.sh"
EOF
    } > "$STEAM_WRAPPER"
    chmod +x "$STEAM_WRAPPER" 2>/dev/null || true
    [ -x "$STEAM_WRAPPER" ] || return 1

    cat > "$APP_DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=RPG Maker Player
Comment=RPG Maker Player
Exec=$STEAM_WRAPPER
Path=$ROOT
Icon=$ICON_DST
Terminal=false
Categories=Game;
StartupNotify=true
EOF
    chmod +x "$APP_DESKTOP" 2>/dev/null || true

    local d
    d="$(desktop_dir)"
    mkdir -p "$d" 2>/dev/null || true
    if [ -d "$d" ] && [ -w "$d" ]; then
        local desk="$d/RPG Maker Player.desktop"
        cat > "$desk" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=RPG Maker Player
Comment=RPG Maker Player
Exec=$STEAM_WRAPPER
Path=$ROOT
Icon=$ICON_DST
Terminal=false
Categories=Game;
StartupNotify=true
EOF
        chmod +x "$desk" 2>/dev/null || true
        if command -v gio >/dev/null 2>&1; then
            gio set "$desk" metadata::trusted true >/dev/null 2>&1 || true
        fi
        log "desktop shortcut=$desk"
    else
        log "desktop directory unavailable: $d"
        return 1
    fi

    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database -q "$APPS_DIR" 2>/dev/null || true
    return 0
}

steam_has_shortcut() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - <<'PY' >/dev/null 2>&1
import struct
from pathlib import Path

home = Path.home()
roots = [
    home / ".steam" / "steam" / "userdata",
    home / ".steam" / "root" / "userdata",
    home / ".local" / "share" / "Steam" / "userdata",
]

def read_cstr(data, pos):
    end = data.find(b"\0", pos)
    if end < 0:
        raise ValueError
    return data[pos:end].decode("utf-8", errors="replace"), end + 1

def parse_obj(data, pos=0, stop=False):
    out = {}
    while pos < len(data):
        typ = data[pos]
        pos += 1
        if typ == 0x08:
            if stop:
                return out, pos
            continue
        key, pos = read_cstr(data, pos)
        if typ == 0x00:
            val, pos = parse_obj(data, pos, True)
        elif typ == 0x01:
            val, pos = read_cstr(data, pos)
        elif typ == 0x02:
            val = struct.unpack_from("<i", data, pos)[0]
            pos += 4
        elif typ == 0x03:
            val = struct.unpack_from("<f", data, pos)[0]
            pos += 4
        elif typ == 0x07:
            val = struct.unpack_from("<Q", data, pos)[0]
            pos += 8
        else:
            raise ValueError
        out[key] = val
    return out, pos

def ci(d, name):
    for k, v in d.items():
        if str(k).lower() == name.lower():
            return v
    return None

def matched(vdf):
    try:
        root, _ = parse_obj(vdf.read_bytes())
    except Exception:
        return False
    shortcuts = root.get("shortcuts") or root.get("Shortcuts") or root
    if not isinstance(shortcuts, dict):
        return False
    for item in shortcuts.values():
        if not isinstance(item, dict):
            continue
        name = str(ci(item, "appname") or "")
        exe = str(ci(item, "exe") or "")
        start = str(ci(item, "startdir") or "")
        hay = " ".join((name, exe, start)).lower()
        if name.strip().lower() == "rpg maker player" or "rpg-maker-player.desktop" in hay or "rpgmakerplayer/steam-launch.sh" in hay:
            return True
    return False

for base in roots:
    if not base.is_dir():
        continue
    for vdf in base.glob("*/config/shortcuts.vdf"):
        if matched(vdf):
            raise SystemExit(0)
raise SystemExit(1)
PY
}

request_steam_registration() {
    if steam_has_shortcut; then
        log "Steam shortcut already exists"
        return 0
    fi

    local requested=0
    if command -v steamos-add-to-steam >/dev/null 2>&1; then
        log "registering via steamos-add-to-steam"
        if steamos-add-to-steam "$APP_DESKTOP" >> "$LOG_FILE" 2>&1; then
            requested=1
        fi
    fi

    if [ "$requested" = "0" ] && command -v steam >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
        log "registering via steam://addnonsteamgame fallback"
        local encoded
        encoded="$(python3 - "$APP_DESKTOP" <<'PY' 2>/dev/null
import sys, urllib.parse
print(urllib.parse.quote(sys.argv[1], safe=''))
PY
)"
        if [ -n "$encoded" ]; then
            touch /tmp/addnonsteamgamefile 2>/dev/null || true
            steam "steam://addnonsteamgame/$encoded" >> "$LOG_FILE" 2>&1 &
            requested=1
        fi
    fi

    [ "$requested" = "1" ] || return 1

    local i
    for i in $(seq 1 40); do
        if steam_has_shortcut; then
            printf '%s\n' "$ROOT" > "$STEAM_MARKER" 2>/dev/null || true
            log "Steam shortcut registration confirmed"
            return 0
        fi
        sleep 0.5
    done

    if command -v steam >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
        local encoded_retry
        encoded_retry="$(python3 - "$APP_DESKTOP" <<'PY' 2>/dev/null
import sys, urllib.parse
print(urllib.parse.quote(sys.argv[1], safe=''))
PY
)"
        if [ -n "$encoded_retry" ]; then
            log "Steam shortcut still missing; retrying via steam URL"
            touch /tmp/addnonsteamgamefile 2>/dev/null || true
            steam "steam://addnonsteamgame/$encoded_retry" >> "$LOG_FILE" 2>&1 &
        fi
    fi

    for i in $(seq 1 80); do
        if steam_has_shortcut; then
            printf '%s\n' "$ROOT" > "$STEAM_MARKER" 2>/dev/null || true
            log "Steam shortcut registration confirmed after retry"
            return 0
        fi
        sleep 0.5
    done

    log "Steam shortcut was not found after registration request"
    return 1
}

apply_steam_artwork() {
    [ -d "$STEAM_ART_DIR" ] || return 1
    command -v python3 >/dev/null 2>&1 || return 1

    python3 - "$STEAM_ART_DIR" >> "$LOG_FILE" 2>&1 <<'PY'
import shutil
import struct
import sys
import time
from pathlib import Path

art_dir = Path(sys.argv[1])
home = Path.home()
roots = [
    home / ".steam" / "steam" / "userdata",
    home / ".steam" / "root" / "userdata",
    home / ".local" / "share" / "Steam" / "userdata",
]

def read_cstr(data, pos):
    end = data.find(b"\0", pos)
    if end < 0:
        raise ValueError("unterminated string")
    return data[pos:end].decode("utf-8", errors="replace"), end + 1

def parse_obj(data, pos=0, stop=False):
    out = {}
    while pos < len(data):
        typ = data[pos]
        pos += 1
        if typ == 0x08:
            if stop:
                return out, pos
            continue
        key, pos = read_cstr(data, pos)
        if typ == 0x00:
            val, pos = parse_obj(data, pos, True)
        elif typ == 0x01:
            val, pos = read_cstr(data, pos)
        elif typ == 0x02:
            val = struct.unpack_from("<i", data, pos)[0]
            pos += 4
        elif typ == 0x03:
            val = struct.unpack_from("<f", data, pos)[0]
            pos += 4
        elif typ == 0x07:
            val = struct.unpack_from("<Q", data, pos)[0]
            pos += 8
        else:
            raise ValueError(f"unsupported VDF type {typ:#x}")
        out[key] = val
    return out, pos

def get_ci(d, name):
    for k, v in d.items():
        if str(k).lower() == name.lower():
            return v
    return None

def appids(vdf):
    try:
        root, _ = parse_obj(vdf.read_bytes())
    except Exception:
        return []
    shortcuts = root.get("shortcuts") or root.get("Shortcuts") or root
    if not isinstance(shortcuts, dict):
        return []
    out = []
    for item in shortcuts.values():
        if not isinstance(item, dict):
            continue
        name = str(get_ci(item, "appname") or "")
        exe = str(get_ci(item, "exe") or "")
        start = str(get_ci(item, "startdir") or "")
        appid = get_ci(item, "appid")
        hay = " ".join((name, exe, start)).lower()
        if not (name.strip().lower() == "rpg maker player" or "rpg-maker-player.desktop" in hay or "rpgmakerplayer/steam-launch.sh" in hay):
            continue
        if isinstance(appid, int):
            out.append(appid & 0xFFFFFFFF)
    return out

seen = set()
for _ in range(80):
    vdfs = []
    for base in roots:
        if not base.is_dir():
            continue
        for p in base.glob("*/config/shortcuts.vdf"):
            try:
                seen.add(p.resolve())
            except Exception:
                seen.add(p)
    for p in list(seen):
        if p.exists():
            vdfs.append(p)

    applied = 0
    for vdf in vdfs:
        ids = appids(vdf)
        if not ids:
            continue
        grid = vdf.parent / "grid"
        grid.mkdir(parents=True, exist_ok=True)
        for appid in ids:
            mapping = {
                "600.png": f"{appid}p.png",
                "920.png": f"{appid}.png",
                "1920.png": f"{appid}_hero.png",
                "logo.png": f"{appid}_logo.png",
                "ICON.PNG": f"{appid}_icon.png",
            }
            for src_name, dst_name in mapping.items():
                src = art_dir / src_name
                if not src.is_file():
                    raise FileNotFoundError(src)
                stem = Path(dst_name).stem
                for ext in (".jpg", ".jpeg", ".png"):
                    old = grid / f"{stem}{ext}"
                    if old.exists() and old.name != dst_name:
                        try:
                            old.unlink()
                        except OSError:
                            pass
                dst = grid / dst_name
                if not dst.exists() or src.read_bytes() != dst.read_bytes():
                    shutil.copy2(src, dst)
            print(f"artwork appid={appid} grid={grid}")
            applied += 1
    if applied:
        raise SystemExit(0)
    time.sleep(0.5)

print("artwork appid lookup timed out")
raise SystemExit(2)
PY
}

main() {
    log "=== RPG Maker Player SteamOS registration ==="
    log "root=$ROOT"

    for required in "$ROOT/run.sh" "$ROOT/MKXP_Launcher" "$ICON_SRC" \
                    "$STEAM_ART_DIR/600.png" "$STEAM_ART_DIR/920.png" \
                    "$STEAM_ART_DIR/1920.png" "$STEAM_ART_DIR/logo.png"; do
        if [ ! -f "$required" ]; then
            show_error "필수 파일을 찾을 수 없습니다.\n\n$required\n\n로그: $LOG_FILE"
            exit 10
        fi
    done

    if ! ensure_permissions; then
        show_error "실행 권한을 자동으로 설정하지 못했습니다.\n\n로그: $LOG_FILE"
        exit 11
    fi

    if ! ensure_launcher_shortcuts; then
        show_error "바탕화면 바로가기를 만들지 못했습니다.\n\n로그: $LOG_FILE"
        exit 12
    fi

    if ! request_steam_registration; then
        show_error "Steam Gaming Mode 등록을 완료하지 못했습니다.\n\nDesktop Mode에서 Steam이 실행 중인지 확인한 뒤 다시 실행해주세요.\n\n로그: $LOG_FILE"
        exit 13
    fi

    if ! apply_steam_artwork; then
        show_error "Steam 등록은 되었지만 썸네일 자동 적용을 완료하지 못했습니다.\n\nSteam을 Desktop Mode에서 실행한 상태로 다시 한 번 등록 파일을 실행해주세요.\n\n로그: $LOG_FILE"
        exit 14
    fi

    log "registration and artwork completed"
    show_info "$(printf '게이밍모드에 등록되었습니다.\n게이밍모드에서 실행해주세요')"
    exit 0
}

main "$@"

