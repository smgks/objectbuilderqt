# Builds one CMake target with the Qt MinGW toolchain. Called by the projects
# in ObjectBuilderQt.sln (JetBrains Rider / Visual Studio "Makefile" projects).
#
#   cmake-build.ps1 -Config Release|Debug [-Target name] [-Rebuild] [-Clean]
#
# Release builds into build\, Debug into build-debug\. The first build of a
# folder runs the CMake configure step. Override the tool locations with the
# QT_DIR / QT_TOOLS environment variables.
param(
    [string]$Config = "Release",
    [string]$Target = "all",
    [switch]$Rebuild,
    [switch]$Clean
)

# "Continue": compiler warnings arrive on stderr and must not stop the script.
$ErrorActionPreference = "Continue"
$qtDir = if ($env:QT_DIR) { $env:QT_DIR } else { "C:\Qt\6.12.0\mingw_64" }
$qtTools = if ($env:QT_TOOLS) { $env:QT_TOOLS } else { "C:\Qt\Tools" }
$env:PATH = "$qtTools\mingw1310_64\bin;$qtTools\Ninja;$qtTools\CMake_64\bin;$qtDir\bin;$env:PATH"

$source = Split-Path -Parent $PSScriptRoot
$build = if ($Config -ieq "Release") { Join-Path $source "build" } else { Join-Path $source "build-debug" }

# Rider/MSBuild builds the solution's projects in parallel; they all share one
# build folder, so only one ninja may run in it at a time.
$mutexName = "Global\ObjectBuilderQt-" + ($build -replace '[\\:/ ]', '_')
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
[void]$mutex.WaitOne()

# GCC prints "file:line:col: error: text"; MSBuild/Rider link "file(line,col): error : text".
function Invoke-Tool([string[]]$arguments) {
    & cmake @arguments 2>&1 | ForEach-Object {
        Write-Host ("$_" -replace '^(.+?):(\d+):(\d+): (fatal error|error|warning|note): ', '$1($2,$3): $4 : ')
    }
    return $LASTEXITCODE
}

try {
    if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
        $code = Invoke-Tool @("-S", $source, "-B", $build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=$Config",
            "-DCMAKE_PREFIX_PATH=$qtDir",
            "-DCMAKE_CXX_COMPILER=$qtTools/mingw1310_64/bin/c++.exe",
            "-DCMAKE_MAKE_PROGRAM=$qtTools/Ninja/ninja.exe")
        if ($code -ne 0) { exit $code }
    }
    if ($Clean) {
        exit (Invoke-Tool @("--build", $build, "--target", "clean"))
    }
    $arguments = @("--build", $build, "--target", $Target)
    if ($Rebuild) { $arguments += "--clean-first" }
    exit (Invoke-Tool $arguments)
}
finally {
    $mutex.ReleaseMutex()
}
