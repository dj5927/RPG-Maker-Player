# RPG Maker Player 1.2

## 한국어

### 1.1 → 1.2 업데이트 내역
- **슬립/복귀 안정성 개선**: 게임 실행 중에도 RPG Maker Player frontend surface를 유지하도록 실행 구조를 정리해 SteamOS 슬립 복귀 후 게임 실행/종료 및 런처 복귀 안정성을 개선했습니다.
- **중복 실행 방지**: single-instance 보호를 추가해 슬립 복귀나 반복 실행 시 런처가 중복으로 뜨는 문제를 방지했습니다.
- **런처 전력 사용 개선**: 화면이 바뀌지 않을 때 불필요한 전체 redraw를 줄이고 idle polling 주기를 낮춰 대기 중 CPU/전력 사용량을 크게 줄였습니다.
- **게임 종료키 단순화**: 게임 실행 중 **Start + Select를 1.5초 동안 계속 누르면 팝업 없이 현재 게임을 바로 종료**하고 기존 런처로 복귀합니다. 1.5초 전에 버튼을 놓으면 종료되지 않습니다.
- **엔진별 종료키 통일**: XP/VX/VX Ace의 mkxp-z, MV/MZ의 NW.js hook, EasyRPG, WOLF/Proton 및 부모 런처 fallback 입력까지 모두 같은 1.5초 hold 기준을 사용합니다.
- **프로세스 종료 안정화**: 게임 process group 단위 종료와 2.5초 SIGKILL fallback을 유지해 Proton/Wine helper 등 남는 자식 프로세스를 더 안정적으로 정리합니다.
- **업데이트 안전 패키지**: v1.2 전체 배포본은 기존 `config/launcher.json`, `config/catalogs`, cache/log 내용, `game/gamelist.json`을 포함하지 않아 기존 사용자 설정을 덮어쓰지 않습니다.

### 설치 / 업데이트
1. `RPG_Maker_Player_SteamOS_WINDOWS_SAFE.tar.gz`를 다운로드합니다.
2. 기존 설치를 업데이트할 경우 압축 안의 `rpg maker player` 폴더를 기존 폴더 위에 병합/덮어쓰기 하면 됩니다.
3. 사용자 설정과 게임 목록 메타데이터는 v1.2 패키지에서 제외되어 보존됩니다.

### SHA-256
`4FE2609064EC5C0BC628B62066C9687D35FAD178C4C6C349CE4647D0E5C7B658`

---

## English

### Changes from 1.1 to 1.2
- **Improved suspend/resume reliability** by keeping the RPG Maker Player frontend surface mapped while a game is running, making post-suspend game launches, exits, and launcher returns more reliable on SteamOS.
- **Added single-instance protection** to prevent duplicate launcher processes after resume or repeated launches.
- **Reduced launcher idle CPU/power usage** by avoiding unnecessary full redraws and using slower idle polling when the UI is unchanged.
- **Simplified in-game exit handling**: hold **Start + Select continuously for 1.5 seconds** to exit the current game directly with no popup and return to the existing launcher. Releasing either button before 1.5 seconds cancels the action.
- **Unified the exit hold across engines**: mkxp-z for XP/VX/VX Ace, NW.js hooks for MV/MZ, EasyRPG, WOLF/Proton, and the launcher fallback input path all use the same 1.5-second hold.
- **Improved child-process cleanup** with process-group termination and the existing 2.5-second SIGKILL fallback for stubborn Proton/Wine helper processes.
- **Update-safe full package**: v1.2 does not include `config/launcher.json`, `config/catalogs`, cache/log contents, or `game/gamelist.json`, so existing user state is not overwritten.

### Install / update
1. Download `RPG_Maker_Player_SteamOS_WINDOWS_SAFE.tar.gz`.
2. For an existing installation, merge/overwrite the included `rpg maker player` folder onto your current installation.
3. User configuration and game-list metadata are preserved by the v1.2 package.

### SHA-256
`4FE2609064EC5C0BC628B62066C9687D35FAD178C4C6C349CE4647D0E5C7B658`
