package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.os.Build;

import java.util.Locale;

public final class UiText {
    private UiText() {}

    public static String language(Context context) {
        Locale locale;
        if (Build.VERSION.SDK_INT >= 24) {
            locale = context.getResources().getConfiguration().getLocales().get(0);
        } else {
            locale = context.getResources().getConfiguration().locale;
        }
        String lang = locale == null ? "en" : locale.getLanguage();
        if ("ko".equalsIgnoreCase(lang)) return "ko";
        if ("ja".equalsIgnoreCase(lang)) return "ja";
        return "en";
    }

    public static String t(Context context, String ko) {
        if (ko == null) return "";
        String lang = language(context);
        if ("ko".equals(lang)) return ko;
        boolean ja = "ja".equals(lang);
        switch (ko) {
            case "홈": return ja ? "ホーム" : "Home";
            case "라이브러리": return ja ? "ライブラリ" : "Library";
            case "설정": return ja ? "設定" : "Settings";
            case "최근 플레이": return ja ? "最近プレイ" : "Recent Play";
            case "내 라이브러리": return ja ? "マイライブラリ" : "My Library";
            case "게임을 실행하면 최근 플레이에 표시됩니다.": return ja ? "ゲームを起動すると最近プレイに表示されます。" : "Played games will appear here.";
            case "선택한 플랫폼에 게임이 없습니다.": return ja ? "選択したプラットフォームにゲームがありません。" : "No games for the selected platform.";
            case "전체": return ja ? "すべて" : "All";
            case "플랫폼": return ja ? "プラットフォーム" : "Platform";
            case "뒤로": return ja ? "戻る" : "Back";
            case "선택": return ja ? "選択" : "Select";
            case "게임 폴더": return ja ? "ゲームフォルダー" : "Game Folder";
            case "라이브러리 재검색": return ja ? "ライブラリ再スキャン" : "Rescan Library";
            case "엔진별 설정": return ja ? "エンジン設定" : "Engine Settings";
            case "지원 엔진": return ja ? "対応エンジン" : "Supported Engines";
            case "선택한 폴더 전체 자동 스캔": return ja ? "選択フォルダー全体を自動スキャン" : "Automatically scan the selected folder";
            case "게임별 설정이 없을 경우 사용되는 기본값 설정": return ja ? "ゲーム別設定がない場合に使う既定値" : "Defaults used when no per-game setting exists";
            case "게임 실행 준비중": return ja ? "ゲーム起動準備中" : "Preparing game launch";
            case "게임에 알맞는 루비 탐색중": return ja ? "ゲームに合うRubyを検索中" : "Finding the best Ruby runtime";
            case "게임 설정": return ja ? "ゲーム設定" : "Game Settings";
            case "게임 이름": return ja ? "ゲーム名" : "Game Name";
            case "로케일": return ja ? "ロケール" : "Locale";
            case "Ruby 런타임": return "Ruby Runtime";
            case "렌더링 모드": return ja ? "レンダリングモード" : "Rendering Mode";
            case "RGSS 마우스 호환": return ja ? "RGSS マウス互換" : "RGSS Mouse Compatibility";
            case "가상패드 표시": return ja ? "仮想パッドを表示" : "Show Virtual Gamepad";
            case "가상패드 크게 표시": return ja ? "仮想パッドを大きく表示" : "Large Virtual Gamepad";
            case "패드 키 설정": return ja ? "ゲームパッド設定" : "Gamepad Mapping";
            case "바로 실행": return ja ? "今すぐ起動" : "Launch Now";
            case "닫기": return ja ? "閉じる" : "Close";
            case "저장": return ja ? "保存" : "Save";
            case "저장 후 닫기": return ja ? "保存して閉じる" : "Save & Close";
            case "엔진 기본값 사용": return ja ? "エンジン既定値を使用" : "Use Engine Default";
            case "앱 기본값 복원": return ja ? "アプリ既定値に戻す" : "Restore App Defaults";
            case "자동 (정밀판별 + fallback)": return ja ? "自動（詳細判定 + フォールバック）" : "Auto (Deep Detect + Fallback)";
            case "자동 (정밀)": return ja ? "自動（詳細）" : "Auto (Deep)";
            case "자동 (권장)": return ja ? "自動（推奨）" : "Auto (Recommended)";
            case "자동": return ja ? "自動" : "Auto";
            case "호환 패치": return ja ? "互換パッチ" : "Compatibility Patch";
            case "마우스 스크립트 패스": return ja ? "マウススクリプトをパス" : "Bypass Mouse Script";
            case "원본 사용": return ja ? "オリジナルを使用" : "Use Original";
            case "원본 유지": return ja ? "オリジナルを維持" : "Keep Original";
            case "앱 기본 키맵": return ja ? "アプリ既定キーマップ" : "App Default Keymap";
            case "EasyRPG 설정": return ja ? "EasyRPG 設定" : "EasyRPG Settings";
            case "Ruby 정밀 검사": return ja ? "Ruby 詳細スキャン" : "Ruby Deep Scan";
            case "호환성 정밀 검사": return ja ? "互換性詳細スキャン" : "Compatibility Deep Scan";
            case "가상패드 마우스 모드": return ja ? "仮想パッド マウスモード" : "Virtual Pad Mouse Mode";
            case "왼쪽 아날로그=커서 · 좌클릭/우클릭 · 휠 위/아래": return ja ? "左スティック=カーソル · 左/右クリック · ホイール上下" : "Left stick=cursor · left/right click · wheel up/down";
            case "설정을 저장했습니다.": return ja ? "設定を保存しました。" : "Settings saved.";
            case "검색된 게임이 없습니다": return ja ? "ゲームが見つかりません" : "No games found";
            case "게임 폴더를 선택하세요": return ja ? "ゲームフォルダーを選択してください" : "Select a game folder";
            case "선택되지 않음": return ja ? "未選択" : "Not selected";
            case "게임 검색 중…": return ja ? "ゲームを検索中…" : "Scanning games…";
            case "게임 목록을 읽는 중…": return ja ? "ゲーム一覧を読み込み中…" : "Reading game list…";
            case "게임 종료 중…": return ja ? "ゲーム終了処理中…" : "Closing game…";
            case "게임 종료 완료": return ja ? "ゲームを終了しました" : "Game closed";
            case "세이브 동기화 중…": return ja ? "セーブ同期中…" : "Syncing saves…";
            case "게임 프로세스 정리 중…": return ja ? "ゲームプロセスを整理中…" : "Cleaning up game process…";
            case "예": return ja ? "はい" : "Yes";
            case "아니오": return ja ? "いいえ" : "No";
            case "게임 종료": return ja ? "ゲーム終了" : "Exit Game";
            case "게임을 종료하시겠습니까?": return ja ? "ゲームを終了しますか？" : "Exit the game?";
            case "확인": return ja ? "確認" : "OK";
            case "한국어": return ja ? "韓国語" : "Korean";
            case "일본어": return ja ? "日本語" : "Japanese";
            case "영어": return ja ? "英語" : "English";
           default:
                if (ko.endsWith(" 로딩중")) {
                    String prefix = ko.substring(0, ko.length() - " 로딩중".length());
                    return prefix + (ja ? " 読み込み中" : " Loading");
                }
                if (ko.endsWith(" 기본 설정")) {
                    String prefix = ko.substring(0, ko.length() - " 기본 설정".length());
                    return prefix + (ja ? " 既定設定" : " Defaults");
                }
                if (ko.endsWith(" · 패드 키 설정")) {
                    String prefix = ko.substring(0, ko.length() - " · 패드 키 설정".length());
                    return prefix + (ja ? " · ゲームパッド設定" : " · Gamepad Mapping");
                }                if (ko.endsWith(" · 패드 키 기본 설정")) {
                    String prefix = ko.substring(0, ko.length() - " · 패드 키 기본 설정".length());
                    return prefix + (ja ? " · ゲームパッド既定設定" : " · Default Gamepad Mapping");
                }
                return ko;
        }
    }

    public static String[] array(Context context, String... values) {
        String[] out = new String[values.length];
        for (int i = 0; i < values.length; i++) out[i] = t(context, values[i]);
        return out;
    }
}
