$releaseDir = "dist/AiAssistantAvatar_Release"
$zipPath = "dist/AiAssistantAvatar_Release.zip"

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

# 1. 実行ファイルのコピー
Copy-Item "build/AiAssistantAvatar.exe" -Destination "$releaseDir/" -Force
Copy-Item "build/AvatarSkinBuilder.exe" -Destination "$releaseDir/" -Force
if (Test-Path "build/CommunityObserver.exe") {
    Copy-Item "build/CommunityObserver.exe" -Destination "$releaseDir/" -Force
}

# 2. windeployqt による Qt6 依存ファイル・プラグインの一括自動展開
$windeployqt = "C:\Qt\6.10.1\mingw_64\bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --no-compiler-runtime "$releaseDir/AiAssistantAvatar.exe"
} else {
    Write-Error "windeployqt.exe not found at $windeployqt"
}

# 3. MinGW ランタイム DLL のコピー
$mingwBinPaths = @("C:\Qt\Tools\mingw1310_64\bin", "C:\tmp\mingw64\bin")
$mingwDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")

foreach ($dll in $mingwDlls) {
    foreach ($binPath in $mingwBinPaths) {
        $src = Join-Path $binPath $dll
        if (Test-Path $src) {
            Copy-Item $src -Destination "$releaseDir/" -Force
            break
        }
    }
}

# 4. アセット・テキスト設定ファイルのコピー
$configReleaseDir = "$releaseDir/Config"
New-Item -ItemType Directory -Force -Path $configReleaseDir | Out-Null
if (Test-Path "blacklist.txt") { Copy-Item "blacklist.txt" -Destination "$configReleaseDir/" -Force }
if (Test-Path "whitelist.txt") { Copy-Item "whitelist.txt" -Destination "$configReleaseDir/" -Force }
if (Test-Path "local_settings.json.sample") { Copy-Item "local_settings.json.sample" -Destination "$releaseDir/" -Force }
Copy-Item "README.md" -Destination "$releaseDir/" -Force
if (Test-Path "pic") {
    Copy-Item -Recurse -Force "pic" -Destination "$releaseDir/"
}
if (Test-Path "knowledge") {
    Copy-Item -Recurse -Force "knowledge" -Destination "$releaseDir/"
}


# 5. ZIP 圧縮パッケージの作成
Compress-Archive -Path "$releaseDir\*" -DestinationPath $zipPath -Force

Write-Host "Full Release package successfully created at $zipPath with all Qt6 & MinGW DLLs!"
