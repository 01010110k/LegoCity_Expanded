<#
    publish_github.ps1 - publish a release built by make_release.py to GitHub (Notes\70-release.md).

    DRY RUN BY DEFAULT: it only prints the commands it would run. Nothing is created, pushed or uploaded
    unless you pass -Go. The repository is created PRIVATE unless you pass -Public.

        .\publish_github.ps1 -Repo MOD_NAME                      # print the plan
        .\publish_github.ps1 -Repo MOD_NAME -Public              # print the plan for a public repo
        .\publish_github.ps1 -Repo MOD_NAME -Go                  # really do it (private repo)
        .\publish_github.ps1 -Repo 01010110k/LegoCity_Expanded -Existing -Go   # push into the existing public repo

    Needs: gh (GitHub CLI) logged in, git. Reads D:\release_out\repo and D:\release_out\LCUMod-<version>.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [string]$Version = "0.1.0",
    [string]$Title = "LegoCity Expanded",
    [string]$OutRoot = "D:\release_out",
    [switch]$Public,
    [switch]$Existing,        # the repo already exists on GitHub (owner/name): add it as origin instead of creating it
    [switch]$Go
)
$ErrorActionPreference = 'Stop'
$repoDir = Join-Path $OutRoot "repo"
$relDir  = Join-Path $OutRoot "LCUMod-$Version"
$tag     = "v$Version"
foreach ($p in @($repoDir, $relDir, (Join-Path $relDir "SHA256SUMS.txt"), (Join-Path $relDir "RELEASE_NOTES.md"))) {
    if (-not (Test-Path $p)) { throw "missing: $p (run make_release.py first)" }
}
# every asset in the release folder except the notes (the notes become the release text); each must be < 2 GiB
$assets = Get-ChildItem $relDir -File | Where-Object { $_.Name -ne "RELEASE_NOTES.md" } | Sort-Object Name
foreach ($a in $assets) { if ($a.Length -ge 2GB) { throw "asset over 2 GiB: $($a.Name)" } }
# verify the checksums before anything leaves this PC
foreach ($line in Get-Content (Join-Path $relDir "SHA256SUMS.txt")) {
    $h, $n = $line -split '  ', 2
    if ($n -eq "SHA256SUMS.txt") { continue }
    $got = (Get-FileHash (Join-Path $relDir $n) -Algorithm SHA256).Hash.ToLower()
    if ($got -ne $h) { throw "checksum mismatch: $n" }
}
$visibility = if ($Public) { "--public" } else { "--private" }
$plan = @(
    $(if ($Existing) { @{ cwd = $repoDir; cmd = "git"; args = @("remote", "add", "origin", "https://github.com/$Repo.git") } }
      else { @{ cwd = $repoDir; cmd = "gh"; args = @("repo", "create", $Repo, $visibility, "--source", $repoDir, "--remote", "origin", "--description", "A free fan-made mod for LEGO City Undercover (PC)") } }),
    @{ cwd = $repoDir; cmd = "git"; args = @("push", "-u", "origin", "main") },
    @{ cwd = $repoDir; cmd = "git"; args = @("tag", $tag) },
    @{ cwd = $repoDir; cmd = "git"; args = @("push", "origin", $tag) },
    @{ cwd = $repoDir; cmd = "gh"; args = @("release", "create", $tag, "--repo", $Repo, "--title", "$Title $Version", "--notes-file", (Join-Path $relDir "RELEASE_NOTES.md"), "--draft") + ($assets | ForEach-Object { $_.FullName }) }
)
Write-Host ("Mode: " + $(if ($Go) { "GO - commands will run" } else { "DRY RUN - nothing will run (pass -Go to publish)" }))
Write-Host ("Repository visibility: " + $(if ($Public) { "PUBLIC" } else { "private" }) + "; the release is created as a DRAFT")
Write-Host ""
foreach ($s in $plan) {
    $shown = ($s.args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    Write-Host "[$($s.cwd)] $($s.cmd) $shown"
}
Write-Host ""
Write-Host "Assets:"
foreach ($a in $assets) { Write-Host ("  {0,-28} {1,10:N1} MB" -f $a.Name, ($a.Length / 1MB)) }
if (-not $Go) { return }
foreach ($s in $plan) {
    Push-Location $s.cwd
    try {
        & $s.cmd @($s.args)
        if ($LASTEXITCODE -ne 0) { throw "failed: $($s.cmd) $($s.args -join ' ')" }
    } finally { Pop-Location }
}
Write-Host "Published as a draft release: review it on GitHub, then publish it there."
