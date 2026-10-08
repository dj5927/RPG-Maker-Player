param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'
$mkxpRoot = Join-Path $ProjectRoot 'vendor\android-mkxp-reference\app\src\main\jni\mkxp'
$source = Join-Path $mkxpRoot 'binding-mri\binding-mri.cpp'
$patch = Join-Path $ProjectRoot 'patches\android-mkxp-pre-eval-cicpoffs.patch'

if (-not (Test-Path $source)) { throw "android-mkxp source missing: $source" }
if (-not (Test-Path $patch)) { throw "patch missing: $patch" }

$text = Get-Content -Raw -Path $source
if ($text.Contains('RPGMP_ANDROID_CICPOFFS_PRE_EVAL')) {
    Write-Output 'Android MKXP pre-eval compatibility patch already applied.'
    exit 0
}

& git -C $mkxpRoot apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'git apply --check failed' }
& git -C $mkxpRoot apply $patch
if ($LASTEXITCODE -ne 0) { throw 'git apply failed' }

Write-Output 'Android MKXP pre-eval compatibility patch applied.'
