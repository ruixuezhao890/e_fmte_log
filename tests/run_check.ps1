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
    param([string]$Name, [string]$Exe, [string[]]$ExeArgs = @())
    & $Exe @ExeArgs
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

# —— 前置条件：Prereq 字段用 '+' 组合（etl / elog / eserde / ecli / sandbox）——
$elogHpp = Join-Path $root 'elog\elog.hpp'
$serdeHpp = Join-Path $root 'eserde\serde.hpp'
$cliHpp = Join-Path $root 'ecli\cli.hpp'
$sandboxMain = Join-Path $root 'sandbox\main.cpp'

function Test-CheckPrereq {
    param([string]$Prereq)
    switch -Regex ($Prereq) {
        'etl'     { if (-not $hasEtl) { return $false } }
        'elog'    { if (-not (Test-Path $elogHpp)) { return $false } }
        'eserde'  { if (-not (Test-Path $serdeHpp)) { return $false } }
        'ecli'    { if (-not (Test-Path $cliHpp)) { return $false } }
        'sandbox' { if (-not (Test-Path $sandboxMain)) { return $false } }
    }
    return $true
}

# —— 构建 + 运行检查表（数据驱动）——
# 每条：Name=日志标题 / Exe=产物名 / Source=源文件（纯文件名相对 tests/，绝对路径原样用）/
#       Std=标准（默认 c++17）/ Defines=附加宏 / RootI=加 -I$root / Prereq=前置条件 /
#       RunArgs=运行参数。
# 注意：sandbox 不 -DEFMT_DERIVE_SHOW_TYPE —— 开关默认值只在 efmt/core/format_base.hpp
# 定义一次，沙盒与测试吃同一个默认值，改默认值时两边不会悄悄分叉。
$buildChecks = @(
    @{ Name = 'host checks (c++17)';                        Exe = 'efmt_check_c++17.exe';              Std = 'c++17'; Source = 'efmt_check.cpp' },
    @{ Name = 'host checks (c++20)';                        Exe = 'efmt_check_c++20.exe';              Std = 'c++20'; Source = 'efmt_check.cpp' },

    # 自带浮点引擎：与 libc 的 snprintf 逐字节对拍（随机位模式 + 规范组合）
    @{ Name = 'float engine vs libc printf (differential)'; Exe = 'efmt_float_check.exe';             Defines = @('-DEFMT_USE_LIBC_PRINTF=0'); Source = 'efmt_float_check.cpp' },
    @{ Name = 'embedded configuration (EFMT_ENABLE_HOSTED=0)'; Exe = 'efmt_embedded.exe';             Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'efmt_embedded_build.cpp' },
    @{ Name = 'minimal configuration (no float, 4 args)';   Exe = 'efmt_tiny.exe';                     Defines = @('-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', '-DEFMT_MAX_FORMAT_ARGS=4'); Source = 'efmt_tiny_build.cpp' },

    # 声明即推导（E_FMT_DERIVE，对标 Rust #[derive(Debug)]）
    @{ Name = 'E_FMT_DERIVE (declaration derives itself)';  Exe = 'efmt_derive_auto_check.exe';        Source = 'efmt_derive_auto_check.cpp' },
    @{ Name = 'E_FMT_DERIVE (embedded configuration)';      Exe = 'efmt_derive_auto_check_embedded.exe'; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'efmt_derive_auto_check.cpp' },

    # 自定义类型自动派生（AUTO / FIELDS / ENUM）
    @{ Name = 'derived formatters (AUTO / FIELDS / ENUM)';  Exe = 'efmt_derive_check.exe';             Source = 'efmt_derive_check.cpp' },
    @{ Name = 'derived formatters (embedded configuration)'; Exe = 'efmt_derive_check_embedded.exe';   Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'efmt_derive_check.cpp' },

    # 手册里的示例代码：跑一遍，文档与实现脱节时这里先红
    @{ Name = 'manual examples (docs stay honest)';        Exe = 'efmt_manual_examples.exe';          RootI = $true; Source = 'efmt_manual_examples.cpp' },

    # elog：efmt 的日志层（需要 ETL —— 容器支持）
    @{ Name = 'elog integration (consumer of efmt)';       Exe = 'elog_integration.exe';              RootI = $true; Source = 'elog_integration.cpp'; Prereq = 'etl+elog' },
    # 宿主环境但关闭流接口/ANSI：验证按特性裁剪的构建
    @{ Name = 'no-stream / no-ANSI configuration';         Exe = 'elog_no_stream.exe';                RootI = $true; Defines = @('-DEFMT_ENABLE_STREAM_API=0', '-DEFMT_ENABLE_ANSI_STYLES=0'); Source = 'elog_integration.cpp'; Prereq = 'etl+elog' },
    # 嵌入式配置（EFMT_ENABLE_HOSTED=0）：elog 的 ETL 字符串/容器支持在 MCU 配置下
    # 同样生效（容器由 elog.hpp 默认打开，不受 HOSTED 影响）。
    @{ Name = 'elog integration (embedded configuration)'; Exe = 'elog_integration_embedded.exe';     RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'elog_integration.cpp'; Prereq = 'etl+elog' },

    # eserde：efmt 的编译期反射 / 能力基座（与 elog 同类的外挂层，efmt 本体不认识它）
    @{ Name = 'eserde schema / caps / tags';               Exe = 'eserde_schema_check.exe';           RootI = $true; Source = 'eserde_schema_check.cpp'; Prereq = 'eserde' },
    # 裁剪：-DEFMT_DERIVE_ENABLE_TAGS=0 时不解析标签，其它能力照旧
    @{ Name = 'eserde with tag parsing trimmed off';       Exe = 'eserde_no_tags_check.exe';          RootI = $true; Defines = @('-DEFMT_DERIVE_ENABLE_TAGS=0'); Source = 'eserde_no_tags_check.cpp'; Prereq = 'eserde' },
    # 嵌入式配置：基座不依赖宿主特性
    @{ Name = 'eserde (embedded configuration)';           Exe = 'eserde_schema_check_embedded.exe';   RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'eserde_schema_check.cpp'; Prereq = 'eserde' },
    # JSON 序列化 / 反序列化（宿主 + 嵌入式）
    @{ Name = 'eserde::json (serialize / parse)';          Exe = 'eserde_json_check.exe';             RootI = $true; Source = 'eserde_json_check.cpp'; Prereq = 'eserde' },
    @{ Name = 'eserde::json (embedded configuration)';     Exe = 'eserde_json_check_embedded.exe';    RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'eserde_json_check.cpp'; Prereq = 'eserde' },
    # ETL 类型（etl::string / etl::vector）：需要 ETL 头文件
    @{ Name = 'eserde::json with ETL types';               Exe = 'eserde_json_etl_check.exe';         RootI = $true; Source = 'eserde_json_etl_check.cpp'; Prereq = 'etl+eserde' },
    # CBOR 子集（二进制）：黄金字节取自 RFC 8949 附录 A —— 写出的要逐字节相等，
    # 标准编码器写出的要能读回来（宿主 + 嵌入式）
    @{ Name = 'eserde::cbor (RFC 8949 golden bytes / roundtrip)'; Exe = 'eserde_cbor_check.exe';      RootI = $true; Source = 'eserde_cbor_check.cpp'; Prereq = 'eserde' },
    @{ Name = 'eserde::cbor (embedded configuration)';     Exe = 'eserde_cbor_check_embedded.exe';    RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'eserde_cbor_check.cpp'; Prereq = 'eserde' },
    @{ Name = 'eserde::cbor with ETL types';               Exe = 'eserde_cbor_etl_check.exe';         RootI = $true; Source = 'eserde_cbor_etl_check.cpp'; Prereq = 'etl+eserde' },
    # libs 手册的示例（docs/libs/ESERDE-使用手册.md）：每段示例都有可编译版本，文档与实现脱节时这里先红
    @{ Name = 'eserde manual examples (docs stay honest)'; Exe = 'eserde_manual_examples.exe';        RootI = $true; Source = 'eserde_manual_examples.cpp'; Prereq = 'etl+eserde' },

    # ecli：命令行解析（efmt + eserde 之上的可选层；解析器不认识 argv，只认识 token 表）
    # 宿主：argv 与"一行文本"两条路都走一遍
    @{ Name = 'ecli command line parsing (argv + line text)'; Exe = 'ecli_cli_check.exe';             RootI = $true; Source = 'ecli_cli_check.cpp'; Prereq = 'ecli' },
    # 嵌入式配置：零堆、零异常，帮助文本照常
    @{ Name = 'ecli (embedded configuration)';             Exe = 'ecli_cli_check_embedded.exe';        RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'ecli_cli_check.cpp'; Prereq = 'ecli' },
    # ETL types: fixed-capacity containers must fail loudly, never truncate silently
    @{ Name = 'ecli with ETL types';                       Exe = 'ecli_cli_etl_check.exe';            RootI = $true; Source = 'ecli_cli_etl_check.cpp'; Prereq = 'etl+ecli' },
    # 命令表：多命令 / 子命令分发（每个命令一个自包含 thunk）
    @{ Name = 'ecli command table / subcommands';          Exe = 'ecli_command_check.exe';             RootI = $true; Source = 'ecli_command_check.cpp'; Prereq = 'ecli' },
    @{ Name = 'ecli command table (embedded configuration)'; Exe = 'ecli_command_check_embedded.exe';  RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'ecli_command_check.cpp'; Prereq = 'ecli' },
    # 第一批 / 第二批：别名 / count(-vvv) / delim(值分隔) / trailing / hyphen / optional / 关系约束
    @{ Name = 'ecli aliases / count / delim / trailing / relations'; Exe = 'ecli_cli_extra_check.exe'; RootI = $true; Source = 'ecli_cli_extra_check.cpp'; Prereq = 'ecli' },
    @{ Name = 'ecli extras (embedded configuration)';      Exe = 'ecli_cli_extra_check_embedded.exe';  RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'ecli_cli_extra_check.cpp'; Prereq = 'ecli' },
    # 命令名模式段（matchit 的 extractor 做匹配）：宿主 / 嵌入式 / 关掉模式开关三配置
    @{ Name = 'ecli command name patterns (:param / *rest)'; Exe = 'ecli_pattern_check.exe';          RootI = $true; Source = 'ecli_pattern_check.cpp'; Prereq = 'ecli' },
    @{ Name = 'ecli patterns (embedded configuration)';    Exe = 'ecli_pattern_check_embedded.exe';    RootI = $true; Defines = @('-DEFMT_ENABLE_HOSTED=0'); Source = 'ecli_pattern_check.cpp'; Prereq = 'ecli' },
    @{ Name = 'ecli patterns trimmed off';                 Exe = 'ecli_pattern_check_off.exe';         RootI = $true; Defines = @('-DECLI_ENABLE_PATTERN_COMMANDS=0'); Source = 'ecli_pattern_check.cpp'; Prereq = 'ecli' },
    # libs 手册的示例（docs/libs/MATCHIT-使用手册.md）：不依赖 ETL —— 只要了 matchit + ecli 的命令表
    @{ Name = 'matchit manual examples (docs stay honest)'; Exe = 'matchit_manual_examples.exe';      RootI = $true; Source = 'matchit_manual_examples.cpp'; Prereq = 'ecli' },
    # ecli × elog：命令回复接到 elog 的 sink（需要 ETL —— elog 依赖 etl::array）
    @{ Name = 'ecli x elog (reply sink)';                  Exe = 'ecli_elog_check.exe';                RootI = $true; Source = 'ecli_elog_check.cpp'; Prereq = 'etl+elog+ecli' },
    # libs 手册的示例（docs/libs/ECLI-使用手册.md）：命令行 + 命令表 + 回复通道，全部示例一起编
    @{ Name = 'ecli manual examples (docs stay honest)';   Exe = 'ecli_manual_examples.exe';           RootI = $true; Source = 'ecli_manual_examples.cpp'; Prereq = 'etl+elog+ecli' },

    # sandbox：CLion 试玩工程也是库的消费者 —— 一起编一遍，再跑一遍它的冒烟（--check）。
    # 它曾经在"能力门禁生效"后静默烂掉（没人编它），这一步就是防这个。
    # 注意带 --check：sandbox 不带参数时进的是【交互命令台】（等人敲命令），
    # 那会让测试卡在等输入；--check 把命令表当脚本跑一遍，只看有没有异常。
    @{ Name = 'sandbox console (CLion playground, consumer of everything)'; Exe = 'sandbox_check.exe'; RootI = $true; Source = $sandboxMain; Prereq = 'etl+sandbox'; RunArgs = @('--check') }
)

foreach ($c in $buildChecks) {
    if (-not (Test-CheckPrereq $c.Prereq)) { continue }
    $src = if ([System.IO.Path]::IsPathRooted($c.Source)) { $c.Source } else { Join-Path $PSScriptRoot $c.Source }
    $std = if ($c.Std) { $c.Std } else { 'c++17' }
    $rootInclude = if ($c.RootI) { @("-I$root") } else { @() }
    $exe = Join-Path $out $c.Exe
    $arguments = @("-std=$std") + $baseArgs + $rootInclude + $c.Defines + @($src, '-o', $exe)
    if (Invoke-EfmtBuild $c.Name $arguments) {
        Invoke-EfmtRun $c.Name $exe -ExeArgs $c.RunArgs
    }
}

# —— 编译期负例表：必须编译失败且报错命中 ExpectedPattern ——
# 每条：Name / Args（完整编译参数）/ Pattern（期望错误特征串）/ Prereq（可选）
$failChecks = @(
    @{ Name = 'mismatched argument count'; Pattern = 'Number of arguments does not match format string'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail.cpp'), '-o', (Join-Path $out 'compile_fail.exe')) },
    @{ Name = 'float argument with EFMT_ENABLE_FLOAT=0'; Pattern = 'EFMT_ENABLE_FLOAT=0'; Args = @('-std=c++17', '-O2', "-I$include", '-DEFMT_ENABLE_HOSTED=0', '-DEFMT_ENABLE_FLOAT=0', (Join-Path $PSScriptRoot 'efmt_compile_fail_float.cpp'), '-o', (Join-Path $out 'compile_fail_float.exe')) },
    @{ Name = 'AUTO on a non-aggregate type'; Pattern = 'E_FMT_FORMATTER_AUTO 只能用于聚合体'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_auto.cpp'), '-o', (Join-Path $out 'compile_fail_auto.exe')) },
    @{ Name = 'E_FMT_DERIVE with an unformattable member'; Pattern = '成员类型没有格式化器'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive.cpp'), '-o', (Join-Path $out 'compile_fail_derive.exe')) },
    @{ Name = 'derive macro outside the type namespace'; Pattern = '请把宏写在'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive_scope.cpp'), '-o', (Join-Path $out 'compile_fail_scope.exe')) },
    @{ Name = 'E_FMT_DERIVE with a top-level comma'; Pattern = '第一个参数只能是'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive_commas.cpp'), '-o', (Join-Path $out 'compile_fail_derive_commas.exe')) },
    @{ Name = 'enum handed to E_FMT_DERIVE'; Pattern = 'E_FMT_DERIVE_ENUM'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive_enum.cpp'), '-o', (Join-Path $out 'compile_fail_derive_enum.exe')) },
    @{ Name = 'E_FMT_DERIVE on an empty type'; Pattern = '声明体是空的'; Args = @('-std=c++17', '-O2', "-I$include", (Join-Path $PSScriptRoot 'efmt_compile_fail_derive_empty.cpp'), '-o', (Join-Path $out 'compile_fail_derive_empty.exe')) },

    # 能力门禁：没写 Serialize / Deserialize 就想（反）序列化 —— 必须是编译错误
    @{ Name = 'serialize a type without the Serialize capability'; Pattern = '这个类型不能序列化'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'eserde_compile_fail_caps.cpp'), '-o', (Join-Path $out 'compile_fail_caps.exe')); Prereq = 'eserde' },
    @{ Name = 'deserialize a type without the Deserialize capability'; Pattern = '这个类型不能反序列化'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'eserde_compile_fail_caps_read.cpp'), '-o', (Join-Path $out 'compile_fail_caps_read.exe')); Prereq = 'eserde' },

    # Negative test: a temporary sink would dangle (reply stores a pointer)
    @{ Name = 'temporary elog sink handed to reply_to_sink'; Pattern = 'deleted function'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'ecli_compile_fail_elog_temp.cpp'), '-o', (Join-Path $out 'compile_fail_cli_elog_temp.exe')); Prereq = 'etl+elog+ecli' },
    # Negative tests: duplicate option names / a relation tag pointing at a missing field
    @{ Name = 'duplicate option names'; Pattern = '选项名撞车'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'ecli_compile_fail_names.cpp'), '-o', (Join-Path $out 'compile_fail_cli_names.exe')); Prereq = 'ecli' },
    @{ Name = 'relation tag pointing at a missing field'; Pattern = 'needs / conflicts / unless 的取值必须是本类型里真实存在的字段名或选项名'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'ecli_compile_fail_relation.cpp'), '-o', (Join-Path $out 'compile_fail_cli_relation.exe')); Prereq = 'ecli' },
    # Capability gate: no Cli tag means no command line parsing (must be a compile error)
    @{ Name = 'cli args without the Cli capability'; Pattern = '这个类型不能做命令行参数'; Args = @('-std=c++17', '-O2', "-I$include", "-I$root", (Join-Path $PSScriptRoot 'ecli_compile_fail_caps.cpp'), '-o', (Join-Path $out 'compile_fail_cli_caps.exe')); Prereq = 'ecli' }
)

foreach ($f in $failChecks) {
    if (-not (Test-CheckPrereq $f.Prereq)) { continue }
    Invoke-EfmtCompileFail -Name $f.Name -ExpectedPattern $f.Pattern -Arguments $f.Args
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
        @{ Name = 'derive (built-in float)'; Source = 'efmt_derive_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0') },
        # ecli：同一份源码三条读数，差值就是命令行解析的代价
        @{ Name = 'cli baseline (decl only)'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root", '-DECLI_SIZE_PROBE_OFF=1') },
        @{ Name = 'cli parse'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root") },
        @{ Name = 'cli parse + help/error'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root", '-DECLI_SIZE_PROBE_HELP=1') },
        @{ Name = 'cli command table (2 cmds)'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root", '-DECLI_SIZE_PROBE_TABLE=1') },
        @{ Name = 'cli pattern cmd (matchit)'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root", '-DECLI_SIZE_PROBE_PATTERN=1') },
        @{ Name = 'cli subcommand struct (new)'; Source = 'ecli_size_probe.cpp'; Defines = @('-DEFMT_ENABLE_HOSTED=0', "-I$root", '-DECLI_SIZE_PROBE_SUBCMD=1') }
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

# —— QEMU 公共步骤 ——
# -Qemu / -QemuBench：把嵌入式配置在 QEMU (mps2-an386, Cortex-M4) 里真实跑一遍
# 需要 qemu-system-arm + arm-none-eabi-g++；链接必须用 thumb/v7e-m 的 libgcc
# （-nostdlib 会让 GCC 驱动丢掉 multilib -L，-lgcc 会解析到 A32 libgcc，
#  Thumb 代码调其 64 位除法会指令流错乱 -> 42 被格式化成 80 + HardFault-Lockup）

# 编译 qemu 源（cpp -> .o，startup.s -> .o）并链接成 elf；任一失败返回 $null
function Invoke-QemuBuild {
    param([string]$CxxArm, [string]$SrcBase, [string]$OutBase)
    $qsrcDir = Join-Path $PSScriptRoot 'qemu'
    $qflags = @('-std=c++17', '-mthumb', '-mcpu=cortex-m4', '-mfloat-abi=soft',
                '-ffreestanding', '-fno-exceptions', '-fno-rtti',
                '-fno-use-cxa-atexit', '-fno-threadsafe-statics', '-fno-builtin',
                '-DEFMT_ENABLE_HOSTED=0', '-Os')
    & $CxxArm @qflags @("-I$include") -c (Join-Path $qsrcDir "$SrcBase.cpp") -o (Join-Path $out "$OutBase.o")
    if ($LASTEXITCODE -ne 0) { return $null }
    & $CxxArm @('-Os', '-mcpu=cortex-m4', '-mthumb') -c (Join-Path $qsrcDir 'startup.s') -o (Join-Path $out "$OutBase.start.o")
    if ($LASTEXITCODE -ne 0) { return $null }
    $libgcc = & $CxxArm -mthumb -mcpu=cortex-m4 -print-libgcc-file-name
    $linkScript = '-Wl,-T,' + (Join-Path $qsrcDir 'link.ld')
    $elf = Join-Path $out "$OutBase.elf"
    & $CxxArm -nostartfiles $linkScript '-Wl,--gc-sections' (Join-Path $out "$OutBase.o") (Join-Path $out "$OutBase.start.o") $libgcc -o $elf
    if ($LASTEXITCODE -ne 0) { return $null }
    return $elf
}

# 启动 QEMU、轮询串口输出直到命中 $Pattern（或超时 / FAILED / 进程退出），返回 $true/$false
function Wait-QemuSerial {
    param([string]$Qemu, [string]$Elf, [string]$Serial, [string]$Pattern, [int]$TimeoutSec, [switch]$Icount)
    Remove-Item $Serial -ErrorAction SilentlyContinue
    $qemuArgs = @('-machine', 'mps2-an386', '-nographic', '-serial', 'stdio', '-monitor', 'none')
    if ($Icount) { $qemuArgs += @('-icount', 'shift=0') }
    $qemuArgs += @('-kernel', $Elf)
    $p = Start-Process -FilePath $Qemu -ArgumentList $qemuArgs -RedirectStandardOutput $Serial -NoNewWindow -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    try {
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 300
            if (Test-Path $Serial) {
                $text = Get-Content $Serial -Raw -ErrorAction SilentlyContinue
                if ($text -match $Pattern) { return $true }
                if ($text -match 'FAILED') { break }
            }
            if ($p.HasExited) { break }
        }
    } finally {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    return $false
}

if ($Qemu) {
    $qemuCmd = Get-Command qemu-system-arm -ErrorAction SilentlyContinue
    $qarmCmd = Get-Command arm-none-eabi-g++ -ErrorAction SilentlyContinue
    if (-not $qemuCmd -or -not $qarmCmd) {
        Write-Host "note: qemu-system-arm or arm-none-eabi-g++ not found - skipping -Qemu"
    } else {
        Write-Host '=== embedded check on QEMU (mps2-an386, Cortex-M4) ==='
        $qelf = Invoke-QemuBuild $qarmCmd.Source 'qemu_efmt_check' 'qemu_check'
        if ($qelf) {
            $serial = Join-Path $out 'qemu_serial.txt'
            $ok = Wait-QemuSerial $qemuCmd.Source $qelf $serial 'ALL PASS' 25
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
        $qelfb = Invoke-QemuBuild $bqarm.Source 'qemu_efmt_bench' 'qemu_bench'
        if ($qelfb) {
            $serial = Join-Path $out 'qemu_bench_serial.txt'
            $done = Wait-QemuSerial $bqemu.Source $qelfb $serial 'sink=' 40 -Icount
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