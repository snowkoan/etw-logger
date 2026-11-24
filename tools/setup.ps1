<#
.SYNOPSIS
Sets the ETW provider name in etw_logger.h, regenerates the matching provider GUID, and sets the
same name in watch.cmd.

.DESCRIPTION
Updates the files passed with -Header and -Watch, normally your app's copies. With neither, it
updates include\etw_logger.h and tools\watch.cmd in this repository.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -ProviderName Contoso.MyApp -Header D:\src\MyApp\etw_logger.h -Watch D:\src\MyApp\watch.cmd
Updates your app's copies of etw_logger.h and watch.cmd.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -ProviderName Contoso.MyApp
Updates the copies in this repository.
#>
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string] $ProviderName,

    # A copy of etw_logger.h to update.
    [string] $Header,

    # A copy of watch.cmd to update.
    [string] $Watch
)

$ErrorActionPreference = 'Stop'

if (-not $Header -and -not $Watch)
{
    $Header = Join-Path $PSScriptRoot '..\include\etw_logger.h'
    $Watch = Join-Path $PSScriptRoot 'watch.cmd'
}

# .NET EventSource derives its GUID from the name with the standard ETW hash; that's the GUID
# tools look up when you enable a provider as "*Name".
$source = [System.Diagnostics.Tracing.EventSource]::new($ProviderName)
$guid = $source.Guid
$source.Dispose()

# {0x2fd875c5,0x68ac,...,{0x5b,0xc7,...}} -> (0x2fd875c5, 0x68ac, ..., 0x5b, 0xc7, ...)
$cGuid = '(' + ($guid.ToString('X') -replace '[{}]', '' -replace ',', ', ') + ')'

function Get-EditedText([string] $File, [System.Collections.IDictionary] $Edits)
{
    $text = [IO.File]::ReadAllText($File)
    foreach ($edit in $Edits.GetEnumerator())
    {
        $count = [regex]::Matches($text, $edit.Key).Count
        if ($count -ne 1)
        {
            throw "Expected exactly one match for '$($edit.Key)' in $File but found $count."
        }
        $text = [regex]::Replace($text, $edit.Key, $edit.Value)
    }
    $text
}

# Compute every edit before writing anything, so a mismatch leaves all files untouched.
$updates = [ordered]@{}
if ($Header)
{
    $file = (Resolve-Path -LiteralPath $Header).ProviderPath
    $updates[$file] = Get-EditedText $file ([ordered]@{
        '(?m)^#define ETWLOG_PROVIDER_NAME "[^"]*"'   = "#define ETWLOG_PROVIDER_NAME `"$ProviderName`""
        '(?m)^// \{[0-9a-fA-F-]{36}\}'                = "// {$guid}"
        # Keeps whatever comes between the name and the value, such as a line continuation.
        '(?m)^(#define ETWLOG_PROVIDER_GUID\s*\\?\s*)\([^)]*\)' = "`${1}$cGuid"
    })
}
if ($Watch)
{
    $file = (Resolve-Path -LiteralPath $Watch).ProviderPath
    $updates[$file] = Get-EditedText $file ([ordered]@{
        '(?m)^set "PROVIDER=[^"]*"' = "set `"PROVIDER=$ProviderName`""
    })
}

foreach ($update in $updates.GetEnumerator())
{
    [IO.File]::WriteAllText($update.Key, $update.Value)
    Write-Host "Updated $($update.Key)"
}

Write-Host "Provider: $ProviderName"
Write-Host "GUID:     {$guid}"
if (-not $Watch)
{
    Write-Host "watch.cmd was not updated. Pass -Watch, or run: watch.cmd $ProviderName"
}
