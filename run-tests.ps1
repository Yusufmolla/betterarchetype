param (
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$cmakeCommand = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
$cmakePath = if ($null -ne $cmakeCommand) { $cmakeCommand.Source } else { $null }

if ($null -eq $cmakePath) {
    $kitwareCMakePath = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'

    if (Test-Path -LiteralPath $kitwareCMakePath) {
        $cmakePath = $kitwareCMakePath
    }
}

if ($null -eq $cmakePath) {
    throw 'CMake was not found. Add it to PATH or install Kitware CMake.'
}

$buildDirectory = Join-Path $PSScriptRoot 'cmake-build-tests-msvc'

& $cmakePath `
    -S $PSScriptRoot `
    -B $buildDirectory `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    -DBETTER_ARCHETYPE_BUILD_TESTS=ON

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmakePath --build $buildDirectory --config $Configuration --target BetterArchetypeTests --parallel

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildDirectory -C $Configuration --output-on-failure
exit $LASTEXITCODE
