# tests/run_check.ps1
# Builds and runs the efmt checks with MinGW g++.
#
#   .\tests\run_check.ps1                             # use tests/include/middleware/etl
#   .\tests\run_check.ps1 -EtlInclude C:\path\to\etl  # (re)point the ETL include
#   .\tests\run_check.ps1 -Bench                      # also run the micro-benchmark
#
# efmt is header-only and includes both <middleware/efmt/...> and
# <middleware/etl/...>, so the compiler needs one include root providing both.
# tests/include/middleware/etl is a junction to the ETL headers; efmt itself
# resolves through tests/include/middleware/efmt.
param(
    [string]$EtlInclude = $env:EFMT_ETL_INCLUDE,
    [string]$Cxx = 'g++',
    [switch]$Bench
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$include = Join-Path $PSScriptRoot 'include'
$etlLink = Join-Path $include 'middleware\etl'
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$efmtLink = Join-Path $include 'middleware\efmt'
if (-not (Test-Path (Join-Path $efmtLink 'core\format.hpp'))) {
    New-Item -ItemType Junction -Path $efmtLink -Target (Join-Path $root 'efmt') | Out-Null
}

if ($EtlInclude) {
    if (Test-Path $etlLink) { cmd /c rmdir "$etlLink" | Out-Null }
    New-Item -ItemType Junction -Path $etlLink -Target $EtlInclude | Out-Null
}
# efmt 核心只用标准库，ETL 只有 elog 集成检查需要
# 通过 junction 里的实际文件判断（比 Test-Path 目录本身更可靠）
$hasEtl = Test-Path (Join-Path $etlLink 'expected.h')
if (-not $hasEtl) {
    Write-Host "note: no ETL at $etlLink - skipping the elog integration steps (-EtlInclude to enable)"
}

$script:failures = 0
$baseArgs = @('-O2', '-Wall', '-Wextra', "-I$include")

function Invoke-EfmtBuild {
    param([string]$Name, [string[]]$Arguments)
    Write-Host "=== $Name ==="
    & $Cxx @Arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $Name"
        $script:failures++
        return $false
    }
    return $true
}

function Invoke-EfmtRun {
    param([string]$Name, [string]$Exe)
    & $Exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $Name"
        $script:failures++
    }
}

foreach ($std in @('c++17', 'c++20')) {
    $exe = Join-Path $out "efmt_check_$std.exe"
    $arguments = @("-std=$std") + $baseArgs +
        @((Join-Path $PSScriptRoot 'efmt_check.cpp'), '-o', $exe)
    if (Invoke-EfmtBuild "host checks ($std)" $arguments) {
        Invoke-EfmtRun "host checks ($std)" $exe
    }
}

$embedded = Join-Path $out 'efmt_embedded.exe'
$embeddedArgs = @('-std=c++17') + $baseArgs +
    @('-DEFMT_ENABLE_HOSTED=0', (Join-Path $PSScriptRoot 'efmt_embedded_build.cpp'), '-o', $embedded)
if (Invoke-EfmtBuild 'embedded configuration (EFMT_ENABLE_HOSTED=0)' $embeddedArgs) {
    Invoke-EfmtRun 'embedded configuration' $embedded
}

Write-Host '=== negative test: mismatched argument count must not compile ==='
# Native stderr lines surface as errors, which must not abort the script here.
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$failLog = & $Cxx @('-std=c++17', '-O2', "-I$include",
    (Join-Path $PSScriptRoot 'efmt_compile_fail.cpp'),
    '-o', (Join-Path $out 'compile_fail.exe')) 2>&1
$failExit = $LASTEXITCODE
$ErrorActionPreference = $previousPreference
if ($failExit -eq 0) {
    Write-Host 'compile-fail case unexpectedly compiled'
    $script:failures++
} elseif (($failLog -join ' ') -notmatch 'Number of arguments does not match format string') {
    Write-Host 'compilation failed for an unexpected reason:'
    Write-Host ($failLog -join [Environment]::NewLine)
    $script:failures++
}

$elog = Join-Path $root 'elog\elog.hpp'
if ((Test-Path $elog) -and $hasEtl) {
    $elogExe = Join-Path $out 'elog_integration.exe'
    $elogArgs = @('-std=c++17') + $baseArgs + @("-I$root") +
        @((Join-Path $PSScriptRoot 'elog_integration.cpp'), '-o', $elogExe)
    if (Invoke-EfmtBuild 'elog integration (consumer of efmt)' $elogArgs) {
        Invoke-EfmtRun 'elog integration' $elogExe
    }
}

# 宿主环境但关闭流接口/ANSI：验证按特性裁剪的构建
if ((Test-Path $elog) -and $hasEtl) {
    $noStreamExe = Join-Path $out 'elog_no_stream.exe'
    $noStreamArgs = @('-std=c++17') + $baseArgs +
        @('-DEFMT_ENABLE_STREAM_API=0', '-DEFMT_ENABLE_ANSI_STYLES=0', "-I$root",
          (Join-Path $PSScriptRoot 'elog_integration.cpp'), '-o', $noStreamExe)
    if (Invoke-EfmtBuild 'no-stream / no-ANSI configuration' $noStreamArgs) {
        Invoke-EfmtRun 'no-stream / no-ANSI configuration' $noStreamExe
    }
}

if ($Bench) {
    $benchExe = Join-Path $out 'efmt_bench.exe'
    $benchArgs = @('-std=c++17') + $baseArgs +
        @((Join-Path $PSScriptRoot 'efmt_bench.cpp'), '-o', $benchExe)
    if (Invoke-EfmtBuild 'micro-benchmark' $benchArgs) {
        & $benchExe
    }
}

if ($script:failures -gt 0) {
    Write-Host "$($script:failures) step(s) failed"
    exit 1
}
Write-Host 'all checks passed'
exit 0
