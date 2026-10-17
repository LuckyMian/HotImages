# Creates a junction (directory symbolic link) at HotImageDemo/Plugins/HotImages
# pointing at the HotImages plugin in this repository.
#
# UE looks for project plugins only under <Project>/Plugins, so this makes the
# demo self-contained without duplicating the plugin sources.

$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = (Get-Location).Path }

$PluginSource = Join-Path $RepoRoot 'HotImages'
$PluginLink   = Join-Path $RepoRoot 'HotImageDemo\Plugins\HotImages'

if (-not (Test-Path -LiteralPath (Join-Path $PluginSource 'HotImages.uplugin'))) {
    throw "Plugin not found at '$PluginSource'. Run this script from the repository root."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PluginLink) | Out-Null

if (Test-Path -LiteralPath $PluginLink) {
    $item = Get-Item -LiteralPath $PluginLink -Force
    if ($item.LinkType -eq 'Junction') {
        Write-Host "Junction already exists: $PluginLink"
        exit 0
    }
    throw "A file/folder already exists at '$PluginLink' that is not a junction. Remove it first."
}

New-Item -ItemType Junction -Path $PluginLink -Target $PluginSource | Out-Null
Write-Host "Created junction: $PluginLink -> $PluginSource"
