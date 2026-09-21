# tests/run_check.ps1
# Builds and runs the efmt checks with MinGW g++.
#
#   .\tests\run_check.ps1                             # use tests/include/middleware/etl
#   .\tests\run_check.ps1 -EtlInclude C:\path\to\etl  # (re)point the ETL include
#   .\tests\run_check.ps1 -Bench                      # also run the micro-benchmark
#   .\tests\run_check.ps1 -Size                       # also report the MCU footprint
param(
    [string]$EtlInclude = $env:EFMT_ETL_INCLUDE,
    [string]$Cxx = 'g++',
    [switch]$Bench,
    [switch]$Size
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
# 注意：删 junction 只能用不带 /s 的 rmdir（cmd /c rmdir "路径"）。
# rmdir /s 会跟着 junction 进到目标目录里，把目标目录的内容删掉。
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

function Invoke-EfmtCompileFail {
    param([string]$Name, [string[]]$Arguments, [string]$ExpectedPattern)
    Write-Host "=== negative test: $Name ==="
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $log = & $Cxx @Arguments 2>&1
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference

    if ($exit -eq 0) {
        Write-Host "FAILED: $Name compiled but must not"
        $script:failures++
    } elseif (($log -join ' ') -notmatch $ExpectedPattern) {
        Write-Host "FAILED: $Name failed for an unexpected reason:"
        Write-Host ($log -join [Environment]::NewLine)
        $script:failures++
    }
}
foreach ($std in @('c++17', 'c++20')) {
    $exe = Join-Path $out "efmt_check_$std.exe"
    $arguments = @("-std=$std") + $baseArgs + @((Join-Path $PSScriptRoot 'efmt_check.cpp'), '-o', $exe)
    if (Invoke-EfmtBuild "host checks ($std)" $arguments) {
        Invoke-EfmtRun "host checks ($std)" $exe
    }
}

# 自带浮点引擎：与 libc 的 snprintf 逐字节对拍（随机位模式 + 规范组合）
$floatExe = Join-Path $out 'efmt_float_check.exe'
$floatArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_USE_LIBC_PRINTF=0', (Join-Path $PSScriptRoot 'efmt_float_check.cpp'), '-o', $floatExe)
if (Invoke-EfmtBuild 'float engine vs libc printf (differential)' $floatArgs) {
    Invoke-EfmtRun 'float engine vs libc printf' $floatExe
}

$embedded = Join-Path $out 'efmt_embedded.exe'
$embeddedArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_HOSTED=0', (Join-Path $PSScriptRoot 'efmt_embedded_build.cpp'), '-o', $embedded)
if (Invoke-EfmtBuild 'embedded configuration (EFMT_ENABLE_HOSTED=0)' $embeddedArgs) {
    Invoke-EfmtRun 'embedded configuration' $embedded
}

$tiny = Join-Path $out 'efmt_tiny.exe'
$tinyArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', '-DEFMT_MAX_FORMAT_ARGS=4', (Join-Path $PSScriptRoot 'efmt_tiny_build.cpp'), '-o', $tiny)
if (Invoke-EfmtBuild 'minimal configuration (no float, 4 args)' $tinyArgs) {
    Invoke-EfmtRun 'minimal configuration' $tiny
}

# 自定义类型自动派生（AUTO / FIELDS / ENUM）
$deriveExe = Join-Path $out 'efmt_derive_check.exe'
$deriveArgs = @('-std=c++17') + $baseArgs +
    @((Join-Path $PSScriptRoot 'efmt_derive_check.cpp'), '-o', $deriveExe)
if (Invoke-EfmtBuild 'derived formatters (AUTO / FIELDS / ENUM)' $deriveArgs) {
    Invoke-EfmtRun 'derived formatters' $deriveExe
}

$deriveEmbExe = Join-Path $out 'efmt_derive_check_embedded.exe'
$deriveEmbArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_HOSTED=0', (Join-Path $PSScriptRoot 'efmt_derive_check.cpp'), '-o', $deriveEmbExe)
if (Invoke-EfmtBuild 'derived formatters (embedded configuration)' $deriveEmbArgs) {
    Invoke-EfmtRun 'derived formatters (embedded)' $deriveEmbExe
}

# 手册里的示例代码：跑一遍，文档与实现脱节时这里先红
$manualExe = Join-Path $out 'efmt_manual_examples.exe'
$manualArgs = @('-std=c++17') + $baseArgs +
    @((Join-Path $PSScriptRoot 'efmt_manual_examples.cpp'), '-o', $manualExe)
if (Invoke-EfmtBuild 'manual examples (docs stay honest)' $manualArgs) {
    Invoke-EfmtRun 'manual examples' $manualExe
}

Invoke-EfmtCompileFail -Name 'mismatched argument count' -ExpectedPattern 'Number of arguments does not match format string' -Arguments @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail.cpp'), '-o', (Join-Path $out 'compile_fail.exe'))

Invoke-EfmtCompileFail -Name 'float argument with EFMT_ENABLE_FLOAT=0' -ExpectedPattern 'EFMT_ENABLE_FLOAT=0' -Arguments @('-std=c++17', '-O2', "-I$include", '-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', (Join-Path $PSScriptRoot 'efmt_compile_fail_float.cpp'), '-o', (Join-Path $out 'compile_fail_float.exe'))

Invoke-EfmtCompileFail -Name 'AUTO on a non-aggregate type' -ExpectedPattern 'E_FMT_FORMATTER_AUTO 只能用于聚合体' -Arguments @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_auto.cpp'), '-o', (Join-Path $out 'compile_fail_auto.exe'))

$elog = Join-Path $root 'elog\elog.hpp'
if ((Test-Path $elog) -and $hasEtl) {
    $elogExe = Join-Path $out 'elog_integration.exe'
    $elogArgs = @('-std=c++17') + $baseArgs + @("-I$root") + @((Join-Path $PSScriptRoot 'elog_integration.cpp'), '-o', $elogExe)
    if (Invoke-EfmtBuild 'elog integration (consumer of efmt)' $elogArgs) {
        Invoke-EfmtRun 'elog integration' $elogExe
    }
}

# 宿主环境但关闭流接口/ANSI：验证按特性裁剪的构建
if ((Test-Path $elog) -and $hasEtl) {
    $noStreamExe = Join-Path $out 'elog_no_stream.exe'
    $noStreamArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_STREAM_API=0', '-DEFMT_ENABLE_ANSI_STYLES=0', "-I$root", (Join-Path $PSScriptRoot 'elog_integration.cpp'), '-o', $noStreamExe)
    if (Invoke-EfmtBuild 'no-stream / no-ANSI configuration' $noStreamArgs) {
        Invoke-EfmtRun 'no-stream / no-ANSI configuration' $noStreamExe
    }
}
if ($Bench) {
    # 宿主默认走 libc 浮点；再加一轮自带引擎，方便对比两种实现的耗时
    $variants = @(
        @{ Name = 'micro-benchmark (libc float)'; Value = 1 },
        @{ Name = 'micro-benchmark (built-in float)'; Value = 0 }
    )
    foreach ($variant in $variants) {
        $benchExe = Join-Path $out "efmt_bench_$($variant.Value).exe"
        $benchArgs = @('-std=c++17') + $baseArgs + @("-DEFMT_USE_LIBC_PRINTF=$($variant.Value)", (Join-Path $PSScriptRoot 'efmt_bench.cpp'), '-o', $benchExe)
        if (Invoke-EfmtBuild $variant.Name $benchArgs) {
            & $benchExe
        }
    }
}
# 交叉编译体积报告（需要 arm-none-eabi-g++ / xtensa-esp32-elf-g++，缺失就跳过）
if ($Size) {
    $targets = @(
        @{ Name = 'cortex-m4 (newlib-nano)'; Cxx = 'arm-none-eabi-g++'; SizeTool = 'arm-none-eabi-size'; Flags = @('-mcpu=cortex-m4', '-mthumb', '--specs=nano.specs', '--specs=nosys.specs') },
        @{ Name = 'esp32 (xtensa)'; Cxx = 'xtensa-esp32-elf-g++'; SizeTool = 'xtensa-esp32-elf-size'; Flags = @('-mlongcalls', '-DEFMT_ENABLE_HOSTED=0') }
    )
    $templates = @(
        @{ Name = 'default (built-in float)'; Source = 'efmt_embedded_build.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0') },
        @{ Name = 'minimal (no float, 4 args)'; Source = 'efmt_tiny_build.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', '-DEFMT_MAX_FORMAT_ARGS=4') }
    )
    foreach ($target in $targets) {
        $compiler = Get-Command $target.Cxx -ErrorAction SilentlyContinue
        if (-not $compiler) {
            Write-Host "note: $($target.Cxx) not found - skipping $($target.Name)"
            continue
        }
        Write-Host "=== footprint: $($target.Name) (-Os, whole program, --gc-sections) ==="
        foreach ($template in $templates) {
            $tag = $template.Name -replace '[^A-Za-z0-9]', '_'
            $elf = Join-Path $out "size_$tag.elf"
            $sizeArgs = @('-Os', '-std=c++17', '-ffunction-sections', '-fdata-sections', '-fno-exceptions', '-fno-rtti') + $target.Flags + @("-I$include") + $template.Defines + @((Join-Path $PSScriptRoot $template.Source), '-Wl,--gc-sections', '-o', $elf)
            & $target.Cxx @sizeArgs
            if ($LASTEXITCODE -eq 0) {
                $line = (& $target.SizeTool $elf | Select-Object -Skip 1) -join ' '
                Write-Host ("  {0,-30} {1}" -f $template.Name, ($line -replace '\s+', ' '))
            } else {
                Write-Host "  $($template.Name): build failed"
                $script:failures++
            }
        }
    }
}

if ($script:failures -gt 0) {
    Write-Host "$($script:failures) step(s) failed"
    exit 1
}
Write-Host 'all checks passed'
exit 0
