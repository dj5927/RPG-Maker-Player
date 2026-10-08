$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$apk = Join-Path $root 'app\build\outputs\apk\debug\app-debug.apk'
$src = Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\MkxpPlayerActivity.java'
$runner = Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\RunnerCoordinator.java'
$gradle = Join-Path $root 'app\build.gradle'
$aapt = Join-Path $root '.tooling\android-sdk\build-tools\35.0.0\aapt.exe'

if (!(Test-Path $apk)) { throw 'APK missing' }
if (!(Test-Path $src)) { throw 'MkxpPlayerActivity missing' }
if (!(Test-Path $runner)) { throw 'RunnerCoordinator missing' }
if (!(Test-Path $aapt)) { throw 'aapt missing' }

$source = Get-Content $src -Raw
foreach ($needle in @('mkxp-z-187','mkxp-z-193','mkxp-z-modern','ruby187','ruby193','ruby')) {
    if (-not $source.Contains($needle)) { throw "Selectable Ruby runtime missing: $needle" }
}
if ($source -notmatch 'win32_wrap\.rb') {
    throw 'win32_wrap preload missing'
}
if ($source -notmatch 'mkxp_wrap\.rb') {
    throw 'mkxp_wrap preload missing'
}
if ($source -notmatch 'ruby_classic_wrap\.rb') {
    throw 'ruby_classic_wrap preload missing'
}
if ($source -notmatch 'kgl2_wrap\.rb') {
    throw 'kgl2_wrap preload missing'
}
if ($source -notmatch 'fix_volume\.rb') {
    throw 'fix_volume preload missing'
}
if ($source -notmatch 'new File\(compatCommon, "win32_wrap\.rb"\)') {
    throw 'absolute compat preload construction missing'
}
if ($source -notmatch 'JSONObject' -or $source -notmatch '\.json') {
    throw 'MKXP JSON config bridge missing'
}
if ($source -match '\.conf') {
    throw 'Legacy A008 key=value config path still present'
}
if ($source -notmatch 'protected boolean shouldCheckNativeSdlVersion\(\)') {
    throw 'MKXP SDL version compatibility hook missing'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($apk)
try {
    $names = @($zip.Entries | ForEach-Object FullName)
    foreach ($name in @(
        'lib/arm64-v8a/libmkxp-z-187.so',
        'lib/arm64-v8a/libmkxp-z-193.so',
        'lib/arm64-v8a/libmkxp-z-31.so',
        'lib/arm64-v8a/libmkxp-z-modern.so',
        'lib/arm64-v8a/libprobe-ruby18.so',
        'lib/arm64-v8a/libprobe-ruby19.so',
        'lib/arm64-v8a/libprobe-ruby31.so',
        'lib/arm64-v8a/libruby.so',
        'lib/arm64-v8a/libruby187.so',
        'lib/arm64-v8a/libruby193.so',
        'lib/arm64-v8a/libSDL2_image.so',
        'lib/arm64-v8a/libSDL2_sound.so',
        'lib/arm64-v8a/libSDL2_ttf.so',
        'lib/arm64-v8a/libopenal.so',
        'lib/arm64-v8a/libfluidsynth.so',
        'lib/arm64-v8a/libsndfile.so',
        'lib/arm64-v8a/liboboe.so',
        'lib/arm64-v8a/libogg.so',
        'lib/arm64-v8a/libvorbis.so',
        'lib/arm64-v8a/libvorbisenc.so',
        'lib/arm64-v8a/libFLAC.so',
        'lib/arm64-v8a/libopus.so',
        'lib/arm64-v8a/libc++_shared.so',
        'lib/arm64-v8a/libSDL2.so',
        'lib/arm64-v8a/libeasyrpg_libretro.so',
        'lib/arm64-v8a/librpgmp_libretro_host.so'
    )) {
        if ($names -notcontains $name) { throw "APK missing $name" }
    }

    foreach ($legacy in @(
        'lib/arm64-v8a/libfluidlite.so',
        'lib/arm64-v8a/libmkxp18.so',
        'lib/arm64-v8a/libmkxp19.so',
        'lib/arm64-v8a/libmkxp30.so'
    )) {
        if ($names -contains $legacy) { throw "Legacy JoiPlay runtime still packaged: $legacy" }
    }

    foreach ($abi in @('armeabi-v7a','x86','x86_64')) {
        if ($names -notcontains "lib/$abi/libeasyrpg_libretro.so" -or
            $names -notcontains "lib/$abi/librpgmp_libretro_host.so") {
            throw "EasyRPG libretro ABI missing: $abi"
        }
        if ($names -contains "lib/$abi/libmkxp-z-187.so" -or $names -contains "lib/$abi/libmkxp-z-193.so") {
            throw "Self-built MKXP must remain ARM64-only: $abi"
        }
    }

    if ($names | Where-Object { $_ -match 'libeasyrpg_android\.so$|libgamebrowser\.so$' }) {
        throw 'Legacy EasyRPG Android Player native libraries still packaged'
    }
} finally {
    $zip.Dispose()
}

$runnerSource = Get-Content $runner -Raw
if ($runnerSource -notmatch 'arm64-v8a' -or $runnerSource -notmatch 'Build\.SUPPORTED_ABIS') {
    throw 'ARM64-only MKXP runtime guard missing'
}

$gradleSource = Get-Content $gradle -Raw
if ($gradleSource -match 'abiFilters') {
    throw 'Global ABI filter must not restrict launcher APK'
}
if ($gradleSource -notmatch 'versionCode 139') {
    throw 'A139 versionCode missing'
}
$modernDebugWriter = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\util\debugwriter.h') -Raw
foreach ($needle in @('RPGMP_GAME_LOG','[MKXP-MODERN]','std::fopen')) {
    if (-not $modernDebugWriter.Contains($needle)) { throw "Modern native log marker missing: $needle" }
}
$heuristics = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\RubyRuntimeHeuristics.java') -Raw
foreach ($needle in @('archive=rvdata2','ruby31-signature','syntax=kwargs','syntax=keyword-params','deep-cache=')) {
    if (-not $heuristics.Contains($needle)) { throw "A133 Ruby AUTO heuristic marker missing: $needle" }
}
if ($runnerSource.Contains('if (session.index > 0)') -and $runnerSource.Contains('saveRubyFallbackRuntime')) { throw 'AUTO success runtime persistence regression' }
foreach ($needle in @('RubyFallbackSession','RUBY_FALLBACK_STARTUP_TIMEOUT_MS','rubySuccessMarker','saveRubyFallbackRuntime','Ruby 1.8 / 1.9 / 3.1 모두 초기 구동에 실패')) {
    if (-not $runnerSource.Contains($needle)) { throw "A135 Ruby AUTO fallback marker missing: $needle" }
}
$launcherSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\LauncherActivity.java') -Raw
$launcherViewSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\LauncherView.java') -Raw
$uiTextSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\UiText.java') -Raw
foreach ($needle in @('resetTouchInteractionState','launch guard self-heal: stale process marker cleared')) {
    if (-not ($launcherSource.Contains($needle) -or $launcherViewSource.Contains($needle))) { throw "A139 launch-input self-heal missing: $needle" }
}
foreach ($needle in @('UiText','language(Context context)','return "ko"','return "ja"','return "en"')) {
    if (-not $uiTextSource.Contains($needle)) { throw "A139 i18n marker missing: $needle" }
}
$playerSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\MkxpPlayerActivity.java') -Raw
foreach ($needle in @('showRubyLoadingOverlay','rubyLoadingWatcher','RPGMP_RUBY_FALLBACK_SUCCESS_MARKER')) {
    if (-not $playerSource.Contains($needle)) { throw "A137 Ruby loading overlay missing: $needle" }
}
$launcherViewSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\LauncherView.java') -Raw
if (-not $launcherViewSource.Contains('drawBusyOverlay')) { throw 'A137 launcher loading overlay missing' }
$modernGraphics = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\graphics-binding.cpp') -Raw
foreach ($needle in @('RPGMP_RUBY_FALLBACK_SUCCESS_MARKER','graphics-update')) {
    if (-not $modernGraphics.Contains($needle)) { throw "A135 Ruby fallback success marker missing: $needle" }
}
$modernBinding = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\binding-mri.cpp') -Raw
foreach ($needle in @('RPGMP_RUBY_FALLBACK_FAIL_MARKER','[RPGMP-RUBY-FALLBACK-FAIL]','ruby-exception')) {
    if (-not $modernBinding.Contains($needle)) { throw "A134 Ruby fallback exception marker missing: $needle" }
}
$modernMain = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\main.cpp') -Raw
foreach ($needle in @('library constructor reached','MAIN-BEGIN','RPGMP-MODERN-CRASH','RPGMP-MODERN-CRASH-ENTER','rpgmpInstallCrashSignalHandlers','rpgmpModernLibraryBase',' offset=')) {
    if (-not $modernMain.Contains($needle)) { throw "Modern crash diagnostic marker missing: $needle" }
}
foreach ($needle in @('rpgmpCrashRubyLine','rpgmpSetCrashRubyLine',' ruby_line=')) {
    if (-not $modernMain.Contains($needle)) { throw "A112 Ruby line crash marker missing: $needle" }
}
foreach ($needle in @('rpgmpCrashRubyPath','rpgmpSetCrashRubyPath',' ruby_path=')) {
    if (-not $modernMain.Contains($needle)) { throw "A114 Ruby path crash marker missing: $needle" }
}
$modernGraphics = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\graphics-binding.cpp') -Raw
foreach ($needle in @('RPGMP_RUBY_FALLBACK_SUCCESS_MARKER','graphics-update')) {
    if (-not $modernGraphics.Contains($needle)) { throw "A135 Ruby fallback success marker missing: $needle" }
}
$modernBinding = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\binding-mri.cpp') -Raw
foreach ($needle in @('rpgmpRegisterRubyStaticExtensions','rpgmpEnableRubyLineTrace','rpgmpDisableRubyLineTrace','Init_wait();','rb_provide("io/wait.so")','rpgmpDataDirectoryValue')) {
    if ($modernBinding.Contains($needle)) { throw "A123 removed Ruby workaround still present: $needle" }
}
foreach ($needle in @('[RPGMP-ZLIB] require-ok','[RPGMP-DATADIR] android-clean path=','[RPGMP-DATADIR] android-clean-return')) {
    if (-not $modernBinding.Contains($needle)) { throw "A123 clean Ruby runtime marker missing: $needle" }
}
foreach ($needle in @('safeFault','crashTid','rpgmpRgssTid',' rgss_tid=')) {
    if (-not $modernMain.Contains($needle)) { throw "A122 safe crash thread/fault marker missing: $needle" }
}
foreach ($needle in @('[SCRIPT-HEAD] line=','scriptCount == 1 && i == 0')) {
    if (-not $modernBinding.Contains($needle)) { throw "A113 Reborn script-head marker missing: $needle" }
}
$modernFluid = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\audio\fluid-fun.cpp') -Raw
if (-not $modernFluid.Contains('SDL error:')) {
    throw 'A112 FluidSynth SDL loader error marker missing'
}
if (-not $modernMain.Contains('#if !defined(WORKDIR_CURRENT) && !defined(__ANDROID__)')) {
    throw 'A104 Android workdir auto-skip guard missing'
}
if (-not $modernMain.Contains('ANDROID-WORKDIR-AUTO-SKIP')) {
    throw 'A104 Android workdir skip marker missing'
}
foreach ($needle in @('GL-INIT-RETURN','OPENAL-CONTEXT-BEGIN','OPENAL-CONTEXT-END','OPENAL-MAKECURRENT-END','SHAREDSTATE-BEGIN','SHAREDSTATE-END','VSYNC-BEGIN','VSYNC-END')) {
    if (-not $modernMain.Contains($needle)) { throw "A105 init-stage marker missing: $needle" }
}
if (-not $modernMain.Contains('alcCreateContext(threadData->alcDev, nullptr)')) {
    throw 'A105 Android OpenAL context compatibility path missing'
}
$modernFs = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\filesystem\filesystem.cpp') -Raw
foreach ($needle in @('PHYSFS_AndroidInit','SDL_AndroidGetJNIEnv','PHYSFS-ANDROID-INIT-BEGIN','mountAPKAssets')) {
    if (-not $modernFs.Contains($needle)) { throw "A106 Android PhysFS marker missing: $needle" }
}
$modernShared = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\sharedstate.cpp') -Raw
foreach ($needle in @('fileSystem.mountAPKAssets()','config.gameFolder.c_str()','FILESYSTEM-ANDROID-MOUNT-BEGIN','FILESYSTEM-ANDROID-MOUNT-END')) {
    if (-not $modernShared.Contains($needle)) { throw "A106 Android filesystem mount marker missing: $needle" }
}
$modernConfig = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\config.cpp') -Raw
foreach ($needle in @('GAMEINI-ABS=','GAMEINI-TITLE-CP949-FALLBACK','gameFolder + "/" + execName + ".ini"','gameFolder + "/UserData/AppData"')) {
    if (-not $modernConfig.Contains($needle)) { throw "A107 Game.ini compatibility marker missing: $needle" }
}
foreach ($needle in @('mkxp_fs::createDirectories(customDataPath.c_str())','DATADIR-READY=','DATADIR-CREATE-FAILED=')) {
    if (-not $modernConfig.Contains($needle)) { throw "A122 Android data directory creation marker missing: $needle" }
}
$modernFsImpl = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\src\filesystem\filesystemImpl.cpp') -Raw
if (-not $modernFsImpl.Contains('filesystemImpl::createDirectories')) {
    throw 'A122 filesystem createDirectories helper missing'
}
foreach ($needle in @('BASE-CONFIG-ARG=','androidConfigArg','readConfFile(androidConfigArg.c_str())','skipAndroidConfigArg')) {
    if (-not $modernConfig.Contains($needle)) { throw "A108 Android argv JSON config bridge missing: $needle" }
}
foreach ($needle in @('CWD-GAMEFOLDER-SET=','mkxp_fs::setCurrentDirectory(gameFolder.c_str())')) {
    if (-not $modernConfig.Contains($needle)) { throw "A109 Android CWD compatibility marker missing: $needle" }
}
$mkxpActivity = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\MkxpPlayerActivity.java') -Raw
foreach ($needle in @('SCREEN_ORIENTATION_SENSOR_LANDSCAPE','setOrientationBis','ORIENTATION_LOCK landscape')) {
    if (-not $mkxpActivity.Contains($needle)) { throw "A109 landscape lock marker missing: $needle" }
}
foreach ($needle in @('ensureMkxpSoundFont','TimGM6mb.sf2','midiSoundFont')) {
    if (-not $mkxpActivity.Contains($needle)) { throw "A110 MKXP MIDI marker missing: $needle" }
}
if (-not $mkxpActivity.Contains('root.put("fontHeightReporting", 1);')) {
    throw 'A131 fontHeightReporting compatibility setting missing'
}
if (-not $mkxpActivity.Contains('boolean modernRubyRuntime =')) {
    throw 'A132 modern Ruby runtime preload guard missing'
}
if ($mkxpActivity -notmatch 'if \(modernRubyRuntime\) \{\s*preloads\.put\(new File\(compatCommon, "ruby31_legacy_wrap\.rb"\)\.getAbsolutePath\(\)\);\s*\}') {
    throw 'A132 ruby31_legacy_wrap must be preloaded only for Ruby 3.1/modern runtime'
}
if (([regex]::Matches($mkxpActivity, 'ruby31_legacy_wrap\.rb')).Count -ne 1) {
    throw 'A132 ruby31_legacy_wrap preload reference must exist exactly once'
}
foreach ($needle in @('GeneralUser_GS_v1.471.sf2','falling back to TimGM6mb')) {
    if (-not $mkxpActivity.Contains($needle)) { throw "A114 GeneralUser GS marker missing: $needle" }
}
$modernMk = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z.mk') -Raw
if (-not $modernMk.Contains('libfluidsynth.so')) {
    throw 'A110 Android FluidSynth soname missing'
}
$modernGraphics = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\graphics-binding.cpp') -Raw
foreach ($needle in @('RPGMP_RUBY_FALLBACK_SUCCESS_MARKER','graphics-update')) {
    if (-not $modernGraphics.Contains($needle)) { throw "A135 Ruby fallback success marker missing: $needle" }
}
$modernBinding = Get-Content (Join-Path $root 'work\mkxp-z-android-modern\app\jni\mkxp-z\binding\binding-mri.cpp') -Raw
foreach ($needle in @('[PRELOAD-BEGIN]','[GAME-SCRIPT-LOOP-BEGIN]','[SCRIPT-BEGIN]','[SCRIPT-END]','rpgmpSetCrashContext')) {
    if (-not $modernBinding.Contains($needle)) { throw "Modern script trace marker missing: $needle" }
}
$modernCore = Join-Path $root 'app\src\main\jniLibs\arm64-v8a\libmkxp-z-modern.so'
if (!(Test-Path $modernCore)) {
    throw 'Modern mkxp-z runtime missing'
}
if (-not $source.Contains('runtime=modern core=mkxp-z-2.4.2 git=37a04d1 ruby=3.1')) {
    throw 'Ruby 3.1 modern runtime marker missing'
}
if (-not $source.Contains('"mkxp-z-31".equals(runtimeLibrary) || "mkxp-z-modern".equals(runtimeLibrary)')) {
    throw 'Modern Ruby 3.1 stdlib loadpath bridge missing'
}
if (!(Test-Path (Join-Path $root 'app\src\main\assets\rgss_compat\ruby31lib\set.rb'))) {
    throw 'Ruby 3.1 stdlib asset set.rb missing'
}
if (!(Test-Path (Join-Path $root 'app\src\main\assets\rgss_compat\ruby31lib\uri.rb'))) {
    throw 'Ruby 3.1 stdlib asset uri.rb missing'
}
if (!(Test-Path (Join-Path $root 'app\src\main\assets\rgss_compat\common\ruby31_legacy_wrap.rb'))) {
    throw 'Ruby 3.1 legacy compat preload missing'
}
$ruby31Compat = Get-Content (Join-Path $root 'app\src\main\assets\rgss_compat\common\ruby31_legacy_wrap.rb') -Raw
foreach ($needle in @('__rpgmp_original_require','bypass=already-loaded','bypass=already-initialized')) {
    if ($ruby31Compat.Contains($needle)) { throw "A123 removed require workaround still present: $needle" }
}
foreach ($needle in @('__rpgmp_original_eval','[RPGMP-EVAL-BEGIN]','[RPGMP-EVAL-END]','[RPGMP-EVAL-ERROR]')) {
    if ($ruby31Compat.Contains($needle)) { throw "A124 context-breaking eval wrapper still present: $needle" }
}
foreach ($needle in @('__rpgmp_original_open','[RPGMP-SCRIPT-OPEN]')) {
    if (-not $ruby31Compat.Contains($needle)) { throw "A125 script-open trace marker missing: $needle" }
}
foreach ($needle in @('[RPGMP-MODERN-CRASH-CTX-BEGIN]','[RPGMP-MODERN-CRASH-PC]','[RPGMP-MODERN-CRASH-REGS]','BASE modern=')) {
    if (-not $modernMain.Contains($needle)) { throw "A125 native crash context marker missing: $needle" }
}
foreach ($needle in @('SDL_CreateThreadWithStackSize(rgssThreadFun','rpgmpRgssStackSize = 8u * 1024u * 1024u','RGSS-THREAD-STACK=')) {
    if (-not $modernMain.Contains($needle)) { throw "A127 RGSS stack fix marker missing: $needle" }
}
$localeSource = Get-Content -LiteralPath (Join-Path $root 'work\mkxp-z-android-modern\app\jni\ruby\localeinit.c') -Raw
foreach ($needle in @('#if defined __ANDROID__','return rb_usascii_str_new_cstr("UTF-8");','return ENCINDEX_UTF_8;','idx = ENCINDEX_UTF_8;')) {
    if (-not $localeSource.Contains($needle)) { throw "A130 Android UTF-8 locale/filesystem fix missing: $needle" }
}
$transcodeSource = Get-Content -LiteralPath (Join-Path $root 'work\mkxp-z-android-modern\app\jni\ruby\transcode.c') -Raw
if (-not $transcodeSource.Contains('const VALUE fn = rb_str_new(0, total_len);')) {
    throw 'A130 transcode.c upstream allocation restore missing'
}
if (-not $modernBinding.Contains('default_external=#{Encoding.default_external.name} filesystem=#{Encoding.find(''filesystem'').name} locale=#{Encoding.find(''locale'').name} cwd_enc=#{Dir.pwd.encoding.name}')) {
    throw 'A130 runtime encoding-state marker missing'
}
foreach ($needle in @('RGSS-STACK-RANGE low=','[RPGMP-MODERN-CRASH-BT] depth=',' stack_low=',' stack_high=')) {
    if (-not $modernMain.Contains($needle)) { throw "A128 native stack trace marker missing: $needle" }
}
foreach ($needle in @('native_stack_remaining','[RPGMP-REQUIRE-STATE]','gem_original_require')) {
    if (-not $modernBinding.Contains($needle)) { throw "A128 require-state diagnostic marker missing: $needle" }
}
foreach ($needle in @('rpgmpReinstallCrashSignalHandlersAfterRuby','CRASH-HANDLER-REINSTALLED-AFTER-RUBY','SA_SIGINFO | SA_RESETHAND')) {
    if (-not $modernMain.Contains($needle)) { throw "A126 post-Ruby crash handler marker missing: $needle" }
}
if (-not $modernBinding.Contains('rpgmpReinstallCrashSignalHandlersAfterRuby();')) {
    throw 'A126 post-Ruby crash handler reinstall call missing'
}
foreach ($needle in @('[RPGMP-RUBY-BASE] base=','dladdr(reinterpret_cast<void *>(&ruby_init)')) {
    if (-not $modernBinding.Contains($needle)) { throw "A125 Ruby base marker missing: $needle" }
}
$fontSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\display\font.cpp') -Raw
foreach ($needle in @('std::string mtoolForceName;','fontFamilyKey','fontFilenameAlias','registerAlias(fileAlias)')) {
    if (-not $fontSource.Contains($needle)) { throw "MKXP font compatibility marker missing: $needle" }
}
if ($fontSource.Contains('mtoolForceName = "Unifont Smooth"')) {
    throw 'Legacy forced MTool font override still active'
}
$nativeBuildSource = Get-Content (Join-Path $root 'tools\build_selfbuilt_mkxp_arm64.sh') -Raw
if (-not $nativeBuildSource.Contains('src/display/font.cpp')) {
    throw 'Native rebuild script does not sync authoritative font.cpp'
}
if (-not $nativeBuildSource.Contains('src/display/bitmap.cpp')) {
    throw 'Native rebuild script does not sync authoritative bitmap.cpp'
}
if (-not $nativeBuildSource.Contains('src/util/debugwriter.h')) {
    throw 'Native rebuild script does not sync authoritative debugwriter.h'
}
$debugWriter = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\util\debugwriter.h') -Raw
foreach ($needle in @('RPGMP_GAME_LOG','[MKXP-NATIVE]','std::fopen')) {
    if (-not $debugWriter.Contains($needle)) { throw "Native log capture marker missing: $needle" }
}
if (-not $source.Contains('Os.setenv("RPGMP_GAME_LOG"')) {
    throw 'MkxpPlayerActivity native log environment missing'
}
$bitmapSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\display\bitmap.cpp') -Raw
foreach ($needle in @('rpgmpSelectTextFont','TTF_GlyphIsProvided','RPGMP font glyph fallback')) {
    if (-not $bitmapSource.Contains($needle)) { throw "MKXP missing-glyph fallback marker missing: $needle" }
}

$badging = (& $aapt dump badging $apk) -join "`n"
foreach ($abi in @('arm64-v8a','armeabi-v7a','x86','x86_64')) {
    if ($badging -notmatch [regex]::Escape($abi)) { throw "APK native-code missing $abi" }
}

$manifest = (& $aapt dump xmltree $apk AndroidManifest.xml) -join "`n"
foreach ($needle in @(
    'com.dj5927.rpgmakerplayer.MkxpPlayerActivity',
    'com.dj5927.rpgmakerplayer.MvMzPlayerActivity',
    'com.dj5927.rpgmakerplayer.LibretroEasyRpgActivity',
    'android:extractNativeLibs'
)) {
    if ($manifest -notmatch [regex]::Escape($needle)) { throw "Manifest missing $needle" }
}
if ($manifest -match [regex]::Escape('org.easyrpg.player.player.EasyRpgPlayerActivity')) {
    throw 'Legacy EasyRPG Player Activity still packaged'
}
foreach ($processName in @(':mkxp',':mvmz',':easyrpg')) {
    if ($manifest -notmatch [regex]::Escape($processName)) { throw "Dedicated game process missing: $processName" }
}

foreach ($bad in @(
    'android.permission.READ_EXTERNAL_STORAGE',
    'android.permission.WRITE_EXTERNAL_STORAGE'
)) {
    if ($manifest -match [regex]::Escape($bad)) { throw "Forbidden broad storage permission present: $bad" }
}

Write-Output 'ANDROID_MKXP_RUNTIME_SMOKE_PASS'

$bindingSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\binding\binding-mri.cpp') -Raw
foreach ($needle in @('mriConsolePrint','mriConsoleP','[RUBY-PRINT]','[PRELOAD-BEGIN]','[PRELOAD-END]')) {
    if (-not $bindingSource.Contains($needle)) { throw "Ruby 3 diagnostics marker missing: $needle" }
}
foreach ($needle in @('[GAME-SCRIPT-LOOP-BEGIN]','[SCRIPT-BEGIN]','[SCRIPT-END]','legacy MTool loading status 5 skipped')) {
    if (-not $bindingSource.Contains($needle)) { throw "Game script trace marker missing: $needle" }
}
if ($bindingSource.Contains('MtoolProc::notifyLoadingStatus(5);')) {
    throw 'Legacy MTool loading status 5 JNI callback still active'
}
$mainSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\main.cpp') -Raw
foreach ($needle in @('rpgmpInstallCrashSignalHandlers','[RPGMP-CRASH] signal=','rpgmpSetCrashContext')) {
    if (-not $mainSource.Contains($needle)) { throw "Native crash capture marker missing: $needle" }
}
if (-not $bindingSource.Contains('rpgmpSetCrashContext(i, scriptName)')) {
    throw 'Per-script native crash context missing'
}
$graphicsBinding = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\binding\graphics-binding.cpp') -Raw
if ($graphicsBinding.Contains('MtoolProc::staticCall')) {
    throw 'Legacy MTool per-frame Graphics.update hook still active'
}
$sharedStateSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\sharedstate.cpp') -Raw
if ($sharedStateSource.Contains('MtoolProc::notifyLoadingStatus')) {
    throw 'Legacy MTool SharedState loading hook still active'
}
$mainSource = Get-Content (Join-Path $root 'vendor\mkxp-z-android-mtool-reference\app\jni\mkxp-z\src\main.cpp') -Raw
if ($mainSource.Contains('MtoolProc::initJNI') -or $mainSource.Contains('MtoolProc::notifyLoadingStatus')) {
    throw 'Legacy MTool main/JNI hooks still active'
}
$nativeBuildSource = Get-Content (Join-Path $root 'tools\build_selfbuilt_mkxp_arm64.sh') -Raw
if (-not $nativeBuildSource.Contains('src/main.cpp')) {
    throw 'Native rebuild script does not sync authoritative main.cpp'
}
if (-not $nativeBuildSource.Contains('binding/graphics-binding.cpp')) {
    throw 'Native rebuild script does not sync authoritative graphics-binding.cpp'
}
$gameLogSource = Get-Content (Join-Path $root 'app\src\main\java\com\dj5927\rpgmakerplayer\GameLog.java') -Raw
foreach ($needle in @('getParentFile()','"_log"','logRoot=')) {
    if (-not $gameLogSource.Contains($needle)) { throw "Central game log marker missing: $needle" }
}
$cicpoffsPatch = Get-Content (Join-Path $root 'tools\patch_mkxpz_cicpoffs_pre_eval.py') -Raw
foreach ($needle in @('RAPI_FULL <= 193','RPGMP_ANDROID_CICPOFFS_SKIP_RUBY31')) {
    if (-not $cicpoffsPatch.Contains($needle)) { throw "Ruby31 cicpoffs guard missing: $needle" }
}

