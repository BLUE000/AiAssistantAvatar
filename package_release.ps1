Write-Host "============================================================"
Write-Host "  Re-configuring and Building in Release Mode..."
Write-Host "============================================================"
& cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
& cmake --build build --config Release

# 0. TrustChain 検証の実行
Write-Host "============================================================"
Write-Host "  TrustChain Verification for Release Packaging"
Write-Host "============================================================"

$tcCommit = ""
$rawCommit = git rev-parse HEAD
if ($rawCommit) { $tcCommit = $rawCommit.Trim() }

$tcBranch = ""
$rawBranch = git rev-parse --abbrev-ref HEAD
if ($rawBranch) { $tcBranch = $rawBranch.Trim() }

$rawStatus = git status --porcelain
$tcIsDirty = -not [string]::IsNullOrWhiteSpace($rawStatus)
$tcVerified = $false
$tcStatus = "UNKNOWN"

if ($tcIsDirty) {
    $tcStatus = "CUSTOMIZED_DIRTY"
    Write-Warning "[TrustChain] WARNING: Working tree has uncommitted changes (dirty). Build is marked as CUSTOMIZED."
} else {
    $remoteCheck = git ls-remote origin $tcBranch
    if ($remoteCheck -and ($remoteCheck -match $tcCommit)) {
        $tcStatus = "PASSED"
        $tcVerified = $true
        Write-Host "[TrustChain] Origin verification PASSED: Commit $tcCommit exists on remote $tcBranch." -ForegroundColor Green
    } else {
        $tcStatus = "CUSTOMIZED_UNPUSHED"
        Write-Warning "[TrustChain] WARNING: Commit $tcCommit not found on remote $tcBranch (unpushed). Build is marked as CUSTOMIZED."
    }
}

$releaseDir = "dist/AiAssistantAvatar_Release"
$zipPath = "dist/AiAssistantAvatar_Release.zip"

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

# 1. 実行ファイルのコピー
Copy-Item "build/AiAssistantAvatar.exe" -Destination "$releaseDir/" -Force
Copy-Item "build/AvatarSkinBuilder.exe" -Destination "$releaseDir/" -Force

$toolsDir = "$releaseDir/tools"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
if (Test-Path "build/CommunityObserver.exe") {
    Copy-Item "build/CommunityObserver.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/WebSearcher.exe") {
    Copy-Item "build/WebSearcher.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/TwitchIntroGenerator.exe") {
    Copy-Item "build/TwitchIntroGenerator.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/GeminiChatter.exe") {
    Copy-Item "build/GeminiChatter.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/MistralChatter.exe") {
    Copy-Item "build/MistralChatter.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/GroqChatter.exe") {
    Copy-Item "build/GroqChatter.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/SakuraChatter.exe") {
    Copy-Item "build/SakuraChatter.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/HuggingFaceChatter.exe") {
    Copy-Item "build/HuggingFaceChatter.exe" -Destination "$toolsDir/" -Force
}
if (Test-Path "build/OpenRouterChatter.exe") {
    Copy-Item "build/OpenRouterChatter.exe" -Destination "$toolsDir/" -Force
}

# tools 配下の CLI ツール用 qt.conf を生成 (F-47)
Set-Content -Path "$toolsDir/qt.conf" -Value "[Paths]`nPlugins = .." -Encoding ASCII


# 2. windeployqt による Qt6 依存ファイル・プラグインの一括自動展開
$windeployqt = "C:\Qt\6.10.1\mingw_64\bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --no-compiler-runtime "$releaseDir/AiAssistantAvatar.exe"
} else {
    Write-Error "windeployqt.exe not found at $windeployqt"
}

# 3. MinGW ランタイム DLL および独自 DLL (libTransCipher.dll) のコピー
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

$transCipherDll = "Lib/TransCipher/bin/libTransCipher.dll"
if (-not (Test-Path $transCipherDll)) {
    $transCipherDll = "Lib/TransCipher/lib/libTransCipher.dll"
}
if (Test-Path $transCipherDll) {
    Copy-Item $transCipherDll -Destination "$releaseDir/" -Force
} else {
    Write-Warning "libTransCipher.dll not found in Lib/TransCipher!"
}

# 4. アセット・テキスト設定ファイルのコピー
$configReleaseDir = "$releaseDir/Config"
New-Item -ItemType Directory -Force -Path $configReleaseDir | Out-Null
if (Test-Path "blacklist.txt") { Copy-Item "blacklist.txt" -Destination "$configReleaseDir/" -Force }
if (Test-Path "whitelist.txt") { Copy-Item "whitelist.txt" -Destination "$configReleaseDir/" -Force }
if (Test-Path "Config/local_settings.json.sample") { Copy-Item "Config/local_settings.json.sample" -Destination "$configReleaseDir/" -Force }
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

# 6. リリースサマリ JSON の出力
Start-Sleep -Milliseconds 500
$zipItem = Get-Item "dist\AiAssistantAvatar_Release.zip"
$zipItem.Refresh()
$zipSize = $zipItem.Length
$tcCustomized = if ($tcIsDirty) { $true } else { $false }

$summaryObj = [PSCustomObject]@{
    timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss.ffffff")
    package_file = $zipPath
    package_size_bytes = $zipSize
    trustchain = [PSCustomObject]@{
        verified = $tcVerified
        status = $tcStatus
        commit = $tcCommit
        branch = $tcBranch
        is_customized = $tcCustomized
    }
}
$jsonString = $summaryObj | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText((Join-Path (Get-Location) "dist/release_summary.json"), $jsonString, [System.Text.Encoding]::UTF8)

Write-Host "Full Release package successfully created at $zipPath with all Qt6 and MinGW DLLs!"
Write-Host "TrustChain Status: $tcStatus | Summary: dist/release_summary.json"
