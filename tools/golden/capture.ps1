<#
.SYNOPSIS
    Golden-output capture: everything CB's engine produces from the regen sources and the feature bundles, written to
    D:\CB-tmp\hky-split-golden\<Label>\ so two captures (before / after a refactor) can be byte-compared (compare.ps1).

.DESCRIPTION
    1. Every <Name>.hky\ folder under CB_REGEN_SOURCE: `BehaviorConverter --regen-master`, then `hky-tool unpack` of
       the result -> regen\<Name>\.
    2. Every features\<Name>.hky\ folder: `hky-tool assemble`, then unpack -> features\<Name>\.
    3. All of those bundles as one Data folder's load order: `hky-tool serve-dump` -> served\ (every unit's served
       bytes, the projected adsf, the composed asdsf).
    4. The loose vanilla meshes under -VanillaData (read only): `--hky-roundtrip --all-kinds` (the kept differing
       trees -> hky-roundtrip\) and `--metatext-roundtrip` on its adsf / asdsf / speeddata.
    Every input is only read; TMP/TEMP point into <Label>.work\ so the regen's scratch stays under -Root too.
    Tool logs land in logs\ (raw, with timings); gates\ holds each log with the run's own paths and timings masked, plus
    every step's exit code. compare.ps1 skips logs\ by default.

    Packed .hky files are not bit-reproducible across toolchains, so they stay in <Label>.work\ and only their
    unpacked trees are captured.

.PARAMETER Label
    The capture's name (e.g. before, after).
.PARAMETER BuildDir
    The build tree holding bin\RelWithDebInfo\{hky-tool,BehaviorConverter}.exe (default: <repo>\build\release).
.PARAMETER Root
    Where captures go (default D:\CB-tmp\hky-split-golden).
#>
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$BuildDir,
    [string]$Root = 'D:\CB-tmp\hky-split-golden',
    [string]$VanillaData = 'D:\Projects\Common\Skyrim Source\Data'
)
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\release' }
$bin       = Join-Path $BuildDir 'bin\RelWithDebInfo'
$hkyTool   = Join-Path $bin 'hky-tool.exe'
$converter = Join-Path $bin 'BehaviorConverter.exe'
$schema    = Join-Path $repo 'schema'
$table     = Join-Path $schema 'metadata\injectables.json'
foreach ($p in @($hkyTool, $converter, $schema, $table)) { if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" } }
if (-not $env:CB_REGEN_SOURCE -or -not (Test-Path -LiteralPath $env:CB_REGEN_SOURCE)) { throw 'CB_REGEN_SOURCE is not set or missing' }
# The core cap: the offline tools size their pools from CB_MAX_THREADS.
if (-not $env:CB_MAX_THREADS) { $env:CB_MAX_THREADS = '6' }

$out  = Join-Path $Root $Label
$work = Join-Path $Root "$Label.work"
foreach ($d in @($out, $work)) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force } }
$logs    = Join-Path $out 'logs'
$gates   = Join-Path $out 'gates'
$plugins = Join-Path $work 'data\community_behaviors\plugins'
New-Item -ItemType Directory -Force -Path $logs, $gates, $plugins | Out-Null
# The regen's temp work and drift dumps go under the capture too: nothing is written outside $Root.
$tmp = Join-Path $work 'tmp'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:TMP = $tmp; $env:TEMP = $tmp
$noInput = Join-Path $work 'stdin.empty'
[System.IO.File]::WriteAllBytes($noInput, @())

$exitCodes = [System.Collections.Generic.List[string]]::new()
$utf8 = New-Object System.Text.UTF8Encoding($false)

# Runs one tool step: raw output -> logs\<name>.log, masked output -> gates\<name>.txt, exit code recorded.
function Invoke-Step([string]$Name, [string]$Exe, [string[]]$ToolArgs) {
    Write-Host "== $Name"
    $log = Join-Path $logs "$Name.log"
    $p = Start-Process -FilePath $Exe -ArgumentList ($ToolArgs | ForEach-Object { '"' + $_ + '"' }) -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" -RedirectStandardInput $noInput
    $text = [System.IO.File]::ReadAllText($log)
    $err  = [System.IO.File]::ReadAllText("$log.err")
    if ($err) { $text += "`n--- stderr ---`n" + $err }
    Remove-Item -LiteralPath "$log.err"
    $masked = $text.Replace($work, '<WORK>').Replace($out, '<OUT>').Replace($repo, '<REPO>').Replace($env:CB_REGEN_SOURCE, '<REGEN>')
    $masked = $masked.Replace([System.IO.Path]::GetTempPath().TrimEnd('\'), '<TEMP>')
    $masked = [regex]::Replace($masked, '_\d+(?=[\\/])', '_<PID>')                               # cb_regen_<Name>_<pid>
    $masked = [regex]::Replace($masked, '\d+(\.\d+)?\s*(ms|s|sec|seconds)\b', '<T>')
    [System.IO.File]::WriteAllText((Join-Path $gates "$Name.txt"), $masked, $utf8)
    $exitCodes.Add("$Name $($p.ExitCode)")
    Write-Host "   exit $($p.ExitCode)"
    return $p.ExitCode
}

# 1. Derived bundles.
$skyrimHky = $null
foreach ($src in Get-ChildItem -LiteralPath $env:CB_REGEN_SOURCE -Directory | Where-Object { $_.Name -like '*.hky' } | Sort-Object Name) {
    $hky = Join-Path $plugins $src.Name
    Invoke-Step "regen-$($src.BaseName)" $converter @('--regen-master', $src.FullName, $hky) | Out-Null
    if (Test-Path -LiteralPath $hky) {
        Invoke-Step "unpack-regen-$($src.BaseName)" $hkyTool @('unpack', $hky, '-o', (Join-Path $out "regen\$($src.BaseName)")) | Out-Null
    }
    if ($src.Name -eq 'Skyrim.hky') { $skyrimHky = $hky; $skyrimSrc = $src.FullName }
}
if (-not $skyrimHky) { throw 'CB_REGEN_SOURCE holds no Skyrim.hky folder' }

# 2. Feature bundles.
foreach ($f in Get-ChildItem -LiteralPath (Join-Path $repo 'features') -Directory | Where-Object { $_.Name -like '*.hky' } | Sort-Object Name) {
    $hky = Join-Path $plugins $f.Name
    Invoke-Step "assemble-$($f.BaseName)" $hkyTool @('assemble', $f.FullName, $table, $schema, '-o', $hky) | Out-Null
    if (Test-Path -LiteralPath $hky) {
        Invoke-Step "unpack-feature-$($f.BaseName)" $hkyTool @('unpack', $hky, '-o', (Join-Path $out "features\$($f.BaseName)")) | Out-Null
    }
}

# 3. What the whole load order serves.
Invoke-Step 'serve-dump' $hkyTool @('serve-dump', $schema, '--data', (Join-Path $work 'data'), '-o', (Join-Path $out 'served')) | Out-Null

# 4. Vanilla round trips.
$vanilla = $VanillaData
Invoke-Step 'hky-roundtrip' $converter @('--hky-roundtrip', (Join-Path $vanilla 'meshes'), (Join-Path $out 'hky-roundtrip'), $skyrimHky, '--all-kinds') | Out-Null
Invoke-Step 'metatext-roundtrip' $converter @('--metatext-roundtrip',
    (Join-Path $vanilla 'meshes\animationdatasinglefile.txt'),
    (Join-Path $vanilla 'meshes\animationsetdatasinglefile.txt'),
    (Join-Path $vanilla 'meshes\speeddatasinglefile.txt')) | Out-Null

[System.IO.File]::WriteAllLines((Join-Path $gates '_exit-codes.txt'), $exitCodes, $utf8)
$files = Get-ChildItem -LiteralPath $out -Recurse -File
$bytes = ($files | Measure-Object Length -Sum).Sum
Write-Host ("capture '{0}': {1} file(s), {2:N1} MB -> {3}" -f $Label, $files.Count, ($bytes / 1MB), $out)
$exitCodes | ForEach-Object { Write-Host "  $_" }
