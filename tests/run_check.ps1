# tests/run_check.ps1
# Builds and runs the efmt checks with MinGW g++.
#
#   .\tests\run_check.ps1                             # use tests/include/middleware/etl
#   .\tests\run_check.ps1 -EtlInclude C:\path\to\etl  # (re)point the ETL include
#   .\tests\run_check.ps1 -Bench                      # also run the micro-benchmark
#   .\tests\run_check.ps1 -Size                       # also report the MCU footprint
#   .\tests\run_check.ps1 -Qemu                       # run the embedded check on QEMU (mps2-an386)
#   .\tests\run_check.ps1 -QemuBench                   # embedded cycle numbers on QEMU (-icount)
param(
    [string]$EtlInclude = $env:EFMT_ETL_INCLUDE,
    [string]$Cxx = 'g++',
    [switch]$Bench,
    [switch]$Size,
    [switch]$Qemu,
    [switch]$QemuBench
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

# 声明即推导（E_FMT_DERIVE，对标 Rust #[derive(Debug)]）
$deriveAutoExe = Join-Path $out 'efmt_derive_auto_check.exe'
$deriveAutoArgs = @('-std=c++17') + $baseArgs +
    @((Join-Path $PSScriptRoot 'efmt_derive_auto_check.cpp'), '-o', $deriveAutoExe)
if (Invoke-EfmtBuild 'E_FMT_DERIVE (declaration derives itself)' $deriveAutoArgs) {
    Invoke-EfmtRun 'E_FMT_DERIVE' $deriveAutoExe
}

$deriveAutoEmbExe = Join-Path $out 'efmt_derive_auto_check_embedded.exe'
$deriveAutoEmbArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_HOSTED=0',
    (Join-Path $PSScriptRoot 'efmt_derive_auto_check.cpp'), '-o', $deriveAutoEmbExe)
if (Invoke-EfmtBuild 'E_FMT_DERIVE (embedded configuration)' $deriveAutoEmbArgs) {
    Invoke-EfmtRun 'E_FMT_DERIVE (embedded)' $deriveAutoEmbExe
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

Invoke-EfmtCompileFail -Name 'E_FMT_DERIVE with an unformattable member' -ExpectedPattern '成员类型没有格式化器' -Arguments @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive.cpp'), '-o', (Join-Path $out 'compile_fail_derive.exe'))

Invoke-EfmtCompileFail -Name 'derive macro outside the type namespace' -ExpectedPattern '请把宏写在' -Arguments @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive_scope.cpp'), '-o', (Join-Path $out 'compile_fail_scope.exe'))

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
# 嵌入式配置（EFMT_ENABLE_HOSTED=0）：elog 的 ETL 字符串/容器支持在 MCU 配置下
# 同样生效（容器由 elog.hpp 默认打开，不受 HOSTED 影响）。
if ((Test-Path $elog) -and $hasEtl) {
    $elogEmbExe = Join-Path $out 'elog_integration_embedded.exe'
    $elogEmbArgs = @('-std=c++17') + $baseArgs + @('-DEFMT_ENABLE_HOSTED=0', "-I$root", (Join-Path $PSScriptRoot 'elog_integration.cpp'), '-o', $elogEmbExe)
    if (Invoke-EfmtBuild 'elog integration (embedded configuration)' $elogEmbArgs) {
        Invoke-EfmtRun 'elog integration (embedded)' $elogEmbExe
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
        @{ Name = 'minimal (no float, 4 args)'; Source = 'efmt_tiny_build.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', '-DEFMT_MAX_FORMAT_ARGS=4') },
        @{ Name = 'derive (built-in float)'; Source = 'efmt_derive_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0') }
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

# -Qemu：把嵌入式配置的行为检查在 QEMU (mps2-an386, Cortex-M4) 里真实跑一遍
# 需要 qemu-system-arm + arm-none-eabi-g++；链接必须用 thumb/v7e-m 的 libgcc
# （-nostdlib 会让 GCC 驱动丢掉 multilib -L，-lgcc 会解析到 A32 libgcc，
#  Thumb 代码调其 64 位除法会指令流错乱 -> 42 被格式化成 80 + HardFault-Lockup）
if ($Qemu) {
    $qemuCmd = Get-Command qemu-system-arm -ErrorAction SilentlyContinue
    $qarmCmd = Get-Command arm-none-eabi-g++ -ErrorAction SilentlyContinue
    if (-not $qemuCmd -or -not $qarmCmd) {
        Write-Host "note: qemu-system-arm or arm-none-eabi-g++ not found - skipping -Qemu"
    } else {
        Write-Host '=== embedded check on QEMU (mps2-an386, Cortex-M4) ==='
        $qsrcDir = Join-Path $PSScriptRoot 'qemu'
        $qo = Join-Path $out 'qemu_check.o'
        $qso = Join-Path $out 'qemu_start.o'
        $qelf = Join-Path $out 'qemu_efmt_check.elf'
        $qflags = @('-std=c++17', '-mthumb', '-mcpu=cortex-m4', '-mfloat-abi=soft',
                    '-ffreestanding', '-fno-exceptions', '-fno-rtti',
                    '-fno-use-cxa-atexit', '-fno-threadsafe-statics', '-fno-builtin',
                    '-DEFMT_ENABLE_HOSTED=0', '-Os')
        & $qarmCmd @qflags @("-I$include") -c (Join-Path $qsrcDir 'qemu_efmt_check.cpp') -o $qo
        $buildOk = ($LASTEXITCODE -eq 0)
        if ($buildOk) {
            & $qarmCmd @('-Os', '-mcpu=cortex-m4', '-mthumb') -c (Join-Path $qsrcDir 'startup.s') -o $qso
            $buildOk = ($LASTEXITCODE -eq 0)
        }
        if ($buildOk) {
            $libgcc = & $qarmCmd -mthumb -mcpu=cortex-m4 -print-libgcc-file-name
            $linkScript = '-Wl,-T,' + (Join-Path $qsrcDir 'link.ld')
            & $qarmCmd -nostartfiles $linkScript '-Wl,--gc-sections' $qo $qso $libgcc -o $qelf
            $buildOk = ($LASTEXITCODE -eq 0)
        }
        if ($buildOk) {
            $serial = Join-Path $out 'qemu_serial.txt'
            Remove-Item $serial -ErrorAction SilentlyContinue
            $p = Start-Process -FilePath $qemuCmd.Source -ArgumentList @('-machine', 'mps2-an386', '-nographic', '-serial', 'stdio', '-monitor', 'none', '-kernel', $qelf) -RedirectStandardOutput $serial -NoNewWindow -PassThru
            $deadline = (Get-Date).AddSeconds(25)
            $ok = $false
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 300
                if (Test-Path $serial) {
                    $text = Get-Content $serial -Raw -ErrorAction SilentlyContinue
                    if ($text -match 'ALL PASS') { $ok = $true; break }
                    if ($text -match 'FAILED') { break }
                }
                if ($p.HasExited) { break }
            }
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            $tail = ''
            if (Test-Path $serial) {
                $tail = ((Get-Content $serial | Select-Object -Last 4) -join ' | ')
            }
            if ($ok) {
                Write-Host "  QEMU embedded check: PASS ($tail)"
            } else {
                Write-Host "  QEMU embedded check: FAILED ($tail)"
                $script:failures++
            }
        } else {
            Write-Host '  QEMU check: build failed'
            $script:failures++
        }
    }
}

# -QemuBench：嵌入式周期数（QEMU mps2-an386 + -icount + SysTick），相对对比可复现
if ($QemuBench) {
    $bqemu = Get-Command qemu-system-arm -ErrorAction SilentlyContinue
    $bqarm = Get-Command arm-none-eabi-g++ -ErrorAction SilentlyContinue
    if (-not $bqemu -or -not $bqarm) {
        Write-Host "note: qemu-system-arm or arm-none-eabi-g++ not found - skipping -QemuBench"
    } else {
        Write-Host '=== embedded cycle bench on QEMU (mps2-an386, -icount) ==='
        $qsrcDir = Join-Path $PSScriptRoot 'qemu'
        $qb = Join-Path $out 'qemu_bench.o'
        $qsb = Join-Path $out 'qemu_bench_start.o'
        $qelfb = Join-Path $out 'qemu_efmt_bench.elf'
        $qflags = @('-std=c++17', '-mthumb', '-mcpu=cortex-m4', '-mfloat-abi=soft',
                    '-ffreestanding', '-fno-exceptions', '-fno-rtti',
                    '-fno-use-cxa-atexit', '-fno-threadsafe-statics', '-fno-builtin',
                    '-DEFMT_ENABLE_HOSTED=0', '-Os')
        & $bqarm @qflags @("-I$include") -c (Join-Path $qsrcDir 'qemu_efmt_bench.cpp') -o $qb
        $buildOk = ($LASTEXITCODE -eq 0)
        if ($buildOk) {
            & $bqarm @('-Os', '-mcpu=cortex-m4', '-mthumb') -c (Join-Path $qsrcDir 'startup.s') -o $qsb
            $buildOk = ($LASTEXITCODE -eq 0)
        }
        if ($buildOk) {
            $libgcc = & $bqarm -mthumb -mcpu=cortex-m4 -print-libgcc-file-name
            $linkScript = '-Wl,-T,' + (Join-Path $qsrcDir 'link.ld')
            & $bqarm -nostartfiles $linkScript '-Wl,--gc-sections' $qb $qsb $libgcc -o $qelfb
            $buildOk = ($LASTEXITCODE -eq 0)
        }
        if ($buildOk) {
            $serial = Join-Path $out 'qemu_bench_serial.txt'
            Remove-Item $serial -ErrorAction SilentlyContinue
            $p = Start-Process -FilePath $bqemu.Source -ArgumentList @('-machine', 'mps2-an386', '-nographic', '-serial', 'stdio', '-monitor', 'none', '-icount', 'shift=0', '-kernel', $qelfb) -RedirectStandardOutput $serial -NoNewWindow -PassThru
            $deadline = (Get-Date).AddSeconds(40)
            $done = $false
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 300
                if (Test-Path $serial) {
                    $text = Get-Content $serial -Raw -ErrorAction SilentlyContinue
                    if ($text -match 'sink=') { $done = $true; break }
                }
                if ($p.HasExited) { break }
            }
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            if ($done) {
                Get-Content $serial | Where-Object { $_ -match '^bench ' } | ForEach-Object { Write-Host ('  ' + $_) }
            } else {
                Write-Host '  QEMU cycle bench: no output (timeout)'
                $script:failures++
            }
        } else {
            Write-Host '  QEMU cycle bench: build failed'
            $script:failures++
        }
    }
}

if ($script:failures -gt 0) {
    Write-Host "$($script:failures) step(s) failed"
    exit 1
}
Write-Host 'all checks passed'
exit 0