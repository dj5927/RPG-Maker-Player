# RPG Maker Player 1.3

## 한국어

### 주요 변경점

- **설정 → 업데이트** 추가
  - 현재 버전 표시
  - 사용자가 `업데이트 확인`을 눌렀을 때만 GitHub 확인
  - 새 버전 번호 표시 후 다운로드/설치 확인
  - OTA 패키지 SHA-256 검증 후 설치
  - 실행 시 자동 업데이트 확인 없음
- 게임 실행 중 **Select + X**로 SteamOS 가상키보드 열기
  - 열기 전용입니다. 닫기는 SteamOS 기본 키보드 조작을 사용합니다.
- **Start + Select 1.5초 유지**로 실행 중 게임을 팝업 없이 직접 종료
- XP / VX / VX Ace / MV / MZ 게임별 **Proton 호환 실행** 지원
  - native 실행이 맞지 않는 게임에서 Proton fallback 사용 가능
  - Proton Experimental/GE-Proton 및 게임별 환경 프리셋 지원
- XP / VX / VX Ace native 호환 강화
  - Ruby 1.8 / 1.9 / 3.1 자동 판별/수동 선택/정밀 스캔
  - 공통 Win32API/DLL 경로 정규화와 호환 스텁 확대
  - rpgmakermlinux-cicpoffs/Kawariki 기반 조건부 RGSS 플러그인 patch/port 레이어
  - Ruby 1.9 설치경로 공백 `LD_PRELOAD` 문제 수정
- EasyRPG 개선
  - `.zip` / `.easyrpg` 단독 패키지 감지
  - CP949 / CP932 / CP1252 계열 인코딩 프로필
  - 한국어 EasyRPG `RPG_RT.ini [EasyRPG] Encoding=949` 처리
- WOLF RPG Editor Proton 호환 개선
  - 창모드/Gamescope 호환
  - Config.exe 실행 경로
  - Proton 런타임 선택
- 공통 폰트 fallback, 게임별 로케일(한국어/일본어/영어), 게임별 패드 매핑 및 런타임 설정 개선
- SteamOS 스타일 Home/Library/Settings UI, 최근 플레이, 그리드/리스트, 엔진 필터, 정렬, 라이브 검색, 긴 제목 스크롤, 커스텀 썸네일 실시간 갱신 개선
- 슬립/복귀, 단일 인스턴스, child process 정리 및 idle CPU/전력 사용 개선 유지

### 다운로드

- **풀버전:** `RPG_Maker_Player_SteamOS_V1.3.tar.gz`
- **v1.0 이상 누적 업데이트:** `RPG_Maker_Player_SteamOS_Update_V1.3.tar.gz`

업데이트판은 launcher 설정, catalogs, `game/gamelist.json`, cache/log 및 사용자 게임 데이터는 덮어쓰지 않습니다.

### 설치

새 설치는 풀버전을 압축 해제한 뒤 `RPG Maker Player.desktop`을 Desktop Mode에서 실행해 Steam에 등록합니다.

기존 v1.0~v1.2 사용자는 업데이트판을 기존 `rpg maker player` 폴더 위에 덮어쓸 수 있습니다. v1.3부터는 이후 버전을 런처의 **설정 → 업데이트 → 업데이트 확인**에서 직접 확인/설치할 수 있습니다.

---

## English

### Highlights

- Added **Settings → Update**
  - shows the installed version
  - contacts GitHub only when **Check for updates** is selected
  - shows the available version and asks before download/install
  - verifies the OTA package with SHA-256 before installation
  - no automatic update check at launcher startup
- In-game **Select + X** opens the SteamOS on-screen keyboard
  - open-only; use the normal SteamOS keyboard controls to close it
- Hold **Start + Select for 1.5 seconds** to terminate the running game directly without a popup
- Added per-game **Proton compatibility mode** for XP / VX / VX Ace / MV / MZ
  - useful as a fallback when native execution is not compatible enough
  - Proton Experimental/GE-Proton and per-game environment presets are supported
- Expanded native XP / VX / VX Ace compatibility
  - Ruby 1.8 / 1.9 / 3.1 auto detection, manual override and deep scan
  - broader Win32API/DLL path normalization and compatibility stubs
  - conditional RGSS plugin patch/port layer based on rpgmakermlinux-cicpoffs/Kawariki work
  - fixed Ruby 1.9 `LD_PRELOAD` failures when the install path contains spaces
- Improved EasyRPG support
  - standalone `.zip` / `.easyrpg` detection
  - CP949 / CP932 / CP1252-family encoding profiles
  - Korean EasyRPG `RPG_RT.ini [EasyRPG] Encoding=949` handling
- Improved WOLF RPG Editor Proton compatibility
  - windowed/Gamescope compatibility
  - Config.exe path
  - Proton runtime selection
- Improved shared font fallback, per-game Korean/Japanese/English locale profiles, controller mapping and runtime settings
- Continued SteamOS-style Home/Library/Settings UI improvements: recent play, grid/list views, engine filters, sorting, live search, long-title scrolling and live custom-thumbnail reload
- Retains suspend/resume reliability, single-instance protection, child-process cleanup and lower idle CPU/power use

### Downloads

- **Full package:** `RPG_Maker_Player_SteamOS_V1.3.tar.gz`
- **Cumulative update for v1.0+:** `RPG_Maker_Player_SteamOS_Update_V1.3.tar.gz`

The update package does not replace launcher configuration, catalogs, `game/gamelist.json`, cache/log data, or user game content.

### Installation

For a fresh install, extract the full package and run `RPG Maker Player.desktop` once in Desktop Mode to register it with Steam.

Existing v1.0-v1.2 installations can overlay the cumulative update package on the existing `rpg maker player` folder. Starting with v1.3, future releases can be checked and installed from **Settings → Update → Check for updates**.
