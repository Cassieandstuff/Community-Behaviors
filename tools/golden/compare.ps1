<#
.SYNOPSIS
    Recursive byte diff of two golden captures (capture.ps1). Exit 0 = identical, 1 = any difference.

.PARAMETER A
    A capture folder, or a label under -Root.
.PARAMETER B
    The other.
.PARAMETER Exclude
    Top-level folders to skip (default: logs, whose raw tool output carries timings and paths; gates\ holds it masked).
.PARAMETER Max
    How many differences to list (default 50; all are counted).
#>
param(
    [Parameter(Mandatory = $true)][string]$A,
    [Parameter(Mandatory = $true)][string]$B,
    [string[]]$Exclude = @('logs'),
    [string]$Root = 'D:\CB-tmp\hky-split-golden',
    [int]$Max = 50
)
$ErrorActionPreference = 'Stop'

function Resolve-Capture([string]$x) {
    if (Test-Path -LiteralPath $x -PathType Container) { return (Resolve-Path -LiteralPath $x).Path }
    $p = Join-Path $Root $x
    if (Test-Path -LiteralPath $p -PathType Container) { return (Resolve-Path -LiteralPath $p).Path }
    throw "no capture: $x"
}

# Relative path -> full path; the key is the exact relative spelling (case kept: a rename by case is a difference).
function Get-Tree([string]$dir) {
    $map = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::Ordinal)
    foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Force) {
        $rel = $f.FullName.Substring($dir.Length).TrimStart('\')
        if ($Exclude -contains $rel.Split('\')[0]) { continue }
        $map[$rel] = $f.FullName
    }
    return $map
}

Add-Type -TypeDefinition @'
public static class GoldenBytes {
    public static bool Same(string x, string y) {
        var fx = new System.IO.FileInfo(x); var fy = new System.IO.FileInfo(y);
        if (fx.Length != fy.Length) return false;
        using (var sx = fx.OpenRead()) using (var sy = fy.OpenRead()) {
            var bx = new byte[1 << 20]; var by = new byte[1 << 20];
            while (true) {
                int nx = Fill(sx, bx), ny = Fill(sy, by);
                if (nx != ny) return false;
                if (nx == 0) return true;
                for (int i = 0; i < nx; ++i) if (bx[i] != by[i]) return false;
            }
        }
    }
    static int Fill(System.IO.Stream s, byte[] b) {
        int n = 0, r;
        while (n < b.Length && (r = s.Read(b, n, b.Length - n)) > 0) n += r;
        return n;
    }
}
'@
function Test-SameBytes([string]$x, [string]$y) { return [GoldenBytes]::Same($x, $y) }

$da = Resolve-Capture $A
$db = Resolve-Capture $B
$ta = Get-Tree $da
$tb = Get-Tree $db
$diffs = [System.Collections.Generic.List[string]]::new()
foreach ($k in ($ta.Keys | Sort-Object)) {
    if (-not $tb.ContainsKey($k)) { $diffs.Add("only in A: $k"); continue }
    if (-not (Test-SameBytes $ta[$k] $tb[$k])) { $diffs.Add("differs:   $k") }
}
foreach ($k in ($tb.Keys | Sort-Object)) { if (-not $ta.ContainsKey($k)) { $diffs.Add("only in B: $k") } }

$diffs | Select-Object -First $Max | ForEach-Object { Write-Host $_ }
if ($diffs.Count -gt $Max) { Write-Host "... and $($diffs.Count - $Max) more" }
Write-Host ("compare: {0} file(s) in A, {1} in B, {2} difference(s) (excluded: {3})" -f $ta.Count, $tb.Count, $diffs.Count, ($Exclude -join ', '))
if ($diffs.Count) { exit 1 }
exit 0
