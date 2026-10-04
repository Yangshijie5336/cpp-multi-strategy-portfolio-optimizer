param(
    [string]$InputFile = "data.xls",
    [string]$OutputFile = "mvo.exe",
    [string]$VsDevCmd = ""
)

$sourceDir = (Resolve-Path $PSScriptRoot).Path
$buildScript = Join-Path $sourceDir "cmake_build.bat"
if (-not (Test-Path -LiteralPath $buildScript)) {
    throw "找不到 cmake_build.bat"
}

# The current target is a C++20 CMake project. It links Eigen, HiGHS, NLopt
# and FreeXL, so the old single-file cl.exe command is no longer sufficient.
# cmake_build.bat initializes the x64 MSVC environment and deploys the FreeXL
# runtime DLLs beside the executable.
& cmd.exe /d /c "`"$buildScript`""
if ($LASTEXITCODE -ne 0) {
    throw "CMake/MSVC 编译失败，退出码: $LASTEXITCODE"
}

$builtExe = Join-Path $sourceDir "build\mvo.exe"
if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "构建未生成 build\mvo.exe"
}

$requestedExe = Join-Path $sourceDir $OutputFile
if ([IO.Path]::GetFullPath($requestedExe) -ne [IO.Path]::GetFullPath($builtExe)) {
    Copy-Item -LiteralPath $builtExe -Destination $requestedExe -Force
    Get-ChildItem -LiteralPath (Join-Path $sourceDir "build") -Filter *.dll |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path (Split-Path $requestedExe) $_.Name) -Force
        }
}

& $builtExe $InputFile
if ($LASTEXITCODE -ne 0) {
    throw "程序运行失败，退出码: $LASTEXITCODE"
}
