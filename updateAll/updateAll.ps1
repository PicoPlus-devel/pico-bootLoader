<#
.SYNOPSIS
    Installs arcade ROM sets on the SD card.

.DESCRIPTION
    Takes MAME ROM zips from one folder, the way MiSTer keeps them in
    /games/mame, and extracts each set into the folder on the SD card where its
    application looks for it. The sets, their zips and their folders are
    defined in updateAll.json next to this script; a new core is added there,
    not here.

    A zip that is not in the folder is downloaded into it first, from the
    databases listed in updateAll.json or given with -Database; without -Zips,
    into a temporary folder that is removed afterwards. A database uses the
    format of the MiSTer Downloader: a JSON file, optionally zipped, whose
    "files" map gives each zip a "url" (or a "base_files_url" to prefix), a
    "size" and an MD5 "hash". No database is configured by default. A set that
    is already complete on the card needs no zip, so nothing is downloaded for
    it.

    Only the files listed for a set are extracted. They are found by size and
    CRC32 rather than by name, as the applications themselves do, so merged,
    split and non-merged sets all work, and they are written under the names
    from the configuration. Files already on the card with the right contents
    are left alone, and nothing on the card is ever deleted.

    The exit code is 0 when every set that was found is complete on the card,
    and 1 otherwise.

.PARAMETER Zips
    Folder with MAME zips you already have, for example phoenix.zip.
    Downloaded zips are kept here as well. Default: download into a temporary
    folder that is removed afterwards.

.PARAMETER Sd
    Root of the SD card, for example E:\. Any existing folder will do.

.PARAMETER Core
    Install only these cores, by the IDs that -List shows. Default: every core
    in the configuration.

.PARAMETER Database
    Download databases to use in addition to those in the configuration: URLs
    or files, separated with commas.

.PARAMETER NoDownload
    Use only the zips already in the zip folder.

.PARAMETER Config
    Configuration file. Default: updateAll.json next to this script.

.PARAMETER DryRun
    Show what would be downloaded and written without doing it.

.PARAMETER List
    List the configured cores and databases and exit.

.EXAMPLE
    .\updateAll.ps1 -Zips D:\MAME\roms -Sd E:\

.EXAMPLE
    .\updateAll.ps1 -Sd E:\ -Database https://example.org/arcade_roms_db.json.zip

.EXAMPLE
    .\updateAll.ps1 -Zips D:\MAME\roms -Sd E:\ -Core phoenix,mooncresta -DryRun

.EXAMPLE
    .\updateAll.ps1 -List
#>
#Requires -Version 5.1
# (Placed after the help block: in front of it, Get-Help no longer finds it.)
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(ParameterSetName = 'Install')]
    [string] $Zips,

    [Parameter(ParameterSetName = 'Install', Mandatory = $true)]
    [string] $Sd,

    [Parameter(ParameterSetName = 'Install')]
    [string[]] $Core,

    # Not -Db: that is already an alias of the common parameter -Debug.
    [string[]] $Database,

    [Parameter(ParameterSetName = 'Install')]
    [switch] $NoDownload,

    [Parameter(ParameterSetName = 'Install')]
    [switch] $DryRun,

    [Parameter(ParameterSetName = 'List', Mandatory = $true)]
    [switch] $List,

    [string] $Config
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # the progress bar slows Invoke-WebRequest down badly

# Not a parameter default: Windows PowerShell 5.1 has no $PSScriptRoot yet
# when those are evaluated.
if (-not $Config) { $Config = Join-Path $PSScriptRoot 'updateAll.json' }

if ($PSVersionTable.PSEdition -ne 'Core') {
    # Windows PowerShell 5.1 does not offer TLS 1.2 by default.
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not ('UpdateAll.Crc32' -as [type])) {
    Add-Type -TypeDefinition @'
namespace UpdateAll
{
    public static class Crc32
    {
        static readonly uint[] Table = MakeTable();

        static uint[] MakeTable()
        {
            uint[] t = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }

        public static uint Compute(byte[] data)
        {
            uint c = 0xFFFFFFFFu;
            foreach (byte b in data)
                c = Table[(c ^ b) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }
    }
}
'@
}

$script:UserAgent = 'updateAll (pico-bootLoader)'
$script:DownloadAttempts = 3
$script:TimeoutSec = 60

# PowerShell 7 takes the CRC from the zip directory. Windows PowerShell 5.1
# (.NET Framework) does not expose it, so there the entries are read instead.
$script:HasEntryCrc = $null -ne [System.IO.Compression.ZipArchiveEntry].GetProperty('Crc32')

function Test-BadName([string] $Name) {
    return (-not $Name) -or $Name -eq '.' -or $Name -eq '..' -or
        $Name.IndexOfAny([char[]]'/\:*?"<>|') -ge 0
}

function Test-Url([string] $Source) {
    return $Source -match '^https?://'
}

function Get-Field($Object, [string] $Name) {
    $p = $Object.PSObject.Properties[$Name]
    if ($p) { return $p.Value }
    return $null
}

function Read-Config([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "configuration not found: $Path"
    }
    try {
        $cfg = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        throw "cannot read ${Path}: $($_.Exception.Message)"
    }
    if ($null -eq (Get-Field $cfg 'cores') -or @(Get-Field $cfg 'cores').Count -eq 0) {
        throw "${Path}: no cores defined"
    }

    $ids = @{}
    $defs = foreach ($d in @(Get-Field $cfg 'cores')) {
        $id = [string](Get-Field $d 'id')
        foreach ($key in 'id', 'name', 'zips', 'destination', 'files') {
            if ($null -eq (Get-Field $d $key)) { throw "${Path}: core '$id' has no '$key'" }
        }
        if ($ids.ContainsKey($id)) { throw "${Path}: core '$id' is defined twice" }
        $ids[$id] = $true

        $dest = ([string](Get-Field $d 'destination')).Replace('\', '/').Trim('/')
        $parts = @($dest.Split('/'))
        if (-not $dest -or @($parts | Where-Object { Test-BadName $_ }).Count -gt 0) {
            throw "${Path}: core '$id' has an invalid destination '$(Get-Field $d 'destination')'"
        }

        $zipNames = @(Get-Field $d 'zips')
        $fileDefs = @(Get-Field $d 'files')
        if ($zipNames.Count -eq 0 -or $fileDefs.Count -eq 0) {
            throw "${Path}: core '$id' needs at least one zip and one file"
        }
        if (@($zipNames | Where-Object { Test-BadName $_ }).Count -gt 0) {
            throw "${Path}: core '$id' has an invalid zip name"
        }
        $files = foreach ($f in $fileDefs) {
            $name = [string](Get-Field $f 'name')
            if (Test-BadName $name) { throw "${Path}: core '$id' has an invalid file name '$name'" }
            $size = Get-Field $f 'size'
            $crcText = [string](Get-Field $f 'crc')
            if ($null -eq $size -or $size -isnot [ValueType] -or -not $crcText) {
                throw "${Path}: core '$id', file '$name' needs a numeric 'size' and a hexadecimal 'crc'"
            }
            try {
                $crc = [Convert]::ToUInt32($crcText, 16)
            } catch {
                throw "${Path}: core '$id', file '$name' needs a numeric 'size' and a hexadecimal 'crc'"
            }
            [pscustomobject]@{ Name = $name; Size = [long]$size; Crc = $crc }
        }

        [pscustomobject]@{
            Id        = $id
            Name      = [string](Get-Field $d 'name')
            Set       = [string](Get-Field $d 'set')
            Zips      = $zipNames
            DestParts = $parts
            DestShown = '/' + $dest
            Files     = @($files)
        }
    }

    # Relative database paths are relative to the configuration file.
    $configDir = Split-Path -Parent ((Resolve-Path -LiteralPath $Path).ProviderPath)
    $sources = @(Get-Field $cfg 'databases' | Where-Object { $null -ne $_ })
    if (@($sources | Where-Object { $_ -isnot [string] }).Count -gt 0) {
        throw "${Path}: 'databases' must be a list of URLs or paths"
    }
    $sources = @($sources | ForEach-Object {
        if (Test-Url $_) { $_ } else { Join-Path $configDir $_ }
    })

    return [pscustomobject]@{ Cores = @($defs); Databases = $sources }
}

function Resolve-Folder([string] $Path, [string] $What) {
    $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $item -or -not $item.PSIsContainer) { throw "${What} not found: $Path" }
    return $item.FullName
}

# ---------------------------------------------------------------------------
# Databases and downloads
# ---------------------------------------------------------------------------

function Get-HttpStatus($ErrorRecord) {
    try {
        $response = $ErrorRecord.Exception.Response
        if ($null -ne $response) { return [int]$response.StatusCode }
    } catch { }
    return 0
}

function Save-Url([string] $Url, [string] $Path) {
    Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing -UserAgent $script:UserAgent `
        -TimeoutSec $script:TimeoutSec
}

function Read-Database([string] $Source, [string[]] $WantedZips) {
    if (Test-Url $Source) {
        $tmp = [System.IO.Path]::GetTempFileName()
        try {
            Save-Url $Source $tmp
            $bytes = [System.IO.File]::ReadAllBytes($tmp)
        } finally {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    } else {
        $bytes = [System.IO.File]::ReadAllBytes($Source)
    }

    if ($bytes.Length -ge 4 -and $bytes[0] -eq 0x50 -and $bytes[1] -eq 0x4B -and
            $bytes[2] -eq 3 -and $bytes[3] -eq 4) {
        # A zipped database, as MiSTer publishes them.
        $zip = New-Object System.IO.Compression.ZipArchive((New-Object System.IO.MemoryStream(, $bytes)))
        try {
            $entry = $zip.Entries | Where-Object { $_.Name -like '*.json' } | Select-Object -First 1
            if (-not $entry) { throw 'the zip holds no .json file' }
            $reader = New-Object System.IO.StreamReader($entry.Open(), [System.Text.Encoding]::UTF8)
            try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally {
            $zip.Dispose()
        }
    } else {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)
    }

    $parsed = $text | ConvertFrom-Json
    $files = Get-Field $parsed 'files'
    if ($files -isnot [System.Management.Automation.PSCustomObject]) { throw "no 'files' map" }

    # Only the zips some core asks for are indexed; a MiSTer database lists
    # many thousands of files.
    $wanted = @{}
    foreach ($w in $WantedZips) { $wanted[$w.ToLowerInvariant()] = $true }
    $byZip = @{}
    foreach ($p in $files.PSObject.Properties) {
        $path = $p.Name.TrimStart('|')   # MiSTer marks paths for external storage with '|'
        $leaf = $path.Substring($path.LastIndexOf('/') + 1).ToLowerInvariant()
        if (-not $wanted.ContainsKey($leaf) -or $p.Value -isnot [System.Management.Automation.PSCustomObject]) {
            continue
        }
        if (-not $byZip.ContainsKey($leaf)) { $byZip[$leaf] = New-Object System.Collections.ArrayList }
        [void]$byZip[$leaf].Add([pscustomobject]@{ Path = $path; Entry = $p.Value })
    }

    $id = [string](Get-Field $parsed 'db_id')
    return [pscustomobject]@{
        Id    = $(if ($id) { $id } else { $Source })
        Base  = [string](Get-Field $parsed 'base_files_url')
        ByZip = $byZip
    }
}

function Find-InDatabases([string] $ZipName) {
    if ($null -eq $script:DbLoaded) {
        $script:DbLoaded = @()
        foreach ($src in $script:DbSources) {
            try {
                $script:DbLoaded += Read-Database $src $script:WantedZips
            } catch {
                $msg = "database ${src}: $($_.Exception.Message)"
                $script:DbErrors += $msg
                Write-Host "ERROR: $msg" -ForegroundColor Red
            }
        }
    }
    foreach ($loaded in $script:DbLoaded) {
        $key = $ZipName.ToLowerInvariant()
        if (-not $loaded.ByZip.ContainsKey($key)) { continue }
        # A MiSTer database can hold the same zip name under games/mame and
        # games/hbmame; the MAME one is the set the applications expect.
        $hits = $loaded.ByZip[$key] | Sort-Object @{ Expression = {
            $parts = $_.Path -split '/'
            -not ($parts.Count -ge 2 -and $parts[-2] -eq 'mame')
        } }
        foreach ($hit in $hits) {
            $url = [string](Get-Field $hit.Entry 'url')
            if (-not $url -and $loaded.Base) {
                $url = $loaded.Base + ((($hit.Path -split '/') | ForEach-Object {
                    [Uri]::EscapeDataString($_) }) -join '/')
            }
            if ($url) {
                return [pscustomobject]@{
                    Db = $loaded; Url = $url
                    Size = Get-Field $hit.Entry 'size'; Md5 = [string](Get-Field $hit.Entry 'hash')
                }
            }
        }
    }
    return $null
}

function Invoke-Download([string] $Url, [string] $Dest, $Size, [string] $Md5) {
    $tmp = "$Dest.part"
    $reason = 'no attempt made'
    for ($attempt = 0; $attempt -lt $script:DownloadAttempts; $attempt++) {
        if ($attempt) { Start-Sleep -Seconds (2 * $attempt) }
        try {
            Save-Url $Url $tmp
            $got = (Get-Item -LiteralPath $tmp).Length
            if ($null -ne $Size -and $got -ne [long]$Size) {
                throw "received $got bytes, the database says $Size"
            }
            if ($Md5 -and (Get-FileHash -LiteralPath $tmp -Algorithm MD5).Hash -ne $Md5) {
                throw 'the MD5 does not match the database'
            }
            try {
                [System.IO.Compression.ZipFile]::OpenRead($tmp).Dispose()
            } catch {
                throw 'the file received is not a zip'
            }
            if ([System.IO.File]::Exists($Dest)) { [System.IO.File]::Delete($Dest) }
            [System.IO.File]::Move($tmp, $Dest)
            return $got
        } catch {
            $status = Get-HttpStatus $_
            if ($status) { $reason = "HTTP $status" } else { $reason = $_.Exception.Message }
            if ($status -ge 400 -and $status -lt 500) { break }   # retrying will not help
        } finally {
            if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
        }
    }
    throw $reason
}

function Get-MissingZips($Def, [string] $ZipDir, [System.Collections.ArrayList] $ZipListing, [bool] $Preview) {
    $out = [pscustomobject]@{ Notes = @(); Errors = @(); Pending = $false }
    foreach ($zipName in $Def.Zips) {
        if (@($ZipListing | Where-Object { $_ -eq $zipName }).Count -gt 0) { continue }
        $hit = Find-InDatabases $zipName
        if (-not $hit) { continue }
        if ($Preview) {
            $out.Notes += "would download $zipName from $($hit.Db.Id)"
            $out.Pending = $true
            continue
        }
        try {
            [void][System.IO.Directory]::CreateDirectory($ZipDir)
            $got = Invoke-Download $hit.Url (Join-Path $ZipDir $zipName) $hit.Size $hit.Md5
        } catch {
            $out.Errors += "download of $zipName from $($hit.Db.Id) failed: $($_.Exception.Message)"
            continue
        }
        [void]$ZipListing.Add($zipName)
        $out.Notes += "downloaded $zipName ($([math]::Floor(($got + 1023) / 1024)) KB) from $($hit.Db.Id)"
    }
    return $out
}

# ---------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------

function Get-EntryBytes([System.IO.Compression.ZipArchiveEntry] $Entry) {
    $stream = $Entry.Open()
    try {
        $buffer = New-Object System.IO.MemoryStream
        $stream.CopyTo($buffer)
        return , $buffer.ToArray()
    } finally {
        $stream.Dispose()
    }
}

function Test-Current([string] $Path, $File) {
    try {
        if (-not [System.IO.File]::Exists($Path)) { return $false }
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        return $bytes.Length -eq $File.Size -and [UpdateAll.Crc32]::Compute($bytes) -eq $File.Crc
    } catch {
        return $false
    }
}

function Install-Core($Def, [string] $ZipDir, $ZipListing, [string] $SdRoot, [bool] $Preview) {
    $res = [pscustomobject]@{
        Written = 0; Current = 0; Missing = @(); Errors = @(); Skipped = $null; Used = @()
    }

    $paths = @(foreach ($zipName in $Def.Zips) {
        $hit = $ZipListing | Where-Object { $_ -eq $zipName } | Select-Object -First 1
        if ($hit) { Join-Path $ZipDir $hit }
    })
    if ($paths.Count -eq 0) {
        $res.Skipped = ($Def.Zips -join ' or ') + ' not found'
        return $res
    }
    $res.Used = @($paths | ForEach-Object { Split-Path -Leaf $_ })

    # Only entries of a size the set needs are looked at, which keeps reading
    # them cheap where the CRC is not in the zip directory.
    $wantedSizes = @{}
    foreach ($f in $Def.Files) { $wantedSizes[[long]$f.Size] = $true }

    $archives = New-Object System.Collections.ArrayList
    try {
        $index = @{}
        $byName = @{}
        $unreadable = New-Object System.Collections.ArrayList
        foreach ($p in $paths) {
            $zipLeaf = Split-Path -Leaf $p
            try {
                $zip = [System.IO.Compression.ZipFile]::OpenRead($p)
            } catch {
                $res.Errors += "cannot open ${zipLeaf}: $($_.Exception.InnerException.Message)"
                continue
            }
            [void]$archives.Add($zip)
            foreach ($entry in $zip.Entries) {
                if (-not $entry.Name) { continue }
                $nameKey = $entry.Name.ToLowerInvariant()
                if (-not $byName.ContainsKey($nameKey)) { $byName[$nameKey] = New-Object System.Collections.ArrayList }
                [void]$byName[$nameKey].Add([pscustomobject]@{ Zip = $zipLeaf; Entry = $entry })
                if (-not $wantedSizes.ContainsKey([long]$entry.Length)) { continue }
                try {
                    if ($script:HasEntryCrc) {
                        $crc = [uint32]$entry.Crc32
                    } else {
                        $crc = [UpdateAll.Crc32]::Compute((Get-EntryBytes $entry))
                    }
                } catch {
                    [void]$unreadable.Add("${zipLeaf}:$($entry.FullName)")
                    continue
                }
                $key = "$([long]$entry.Length):$crc"
                if (-not $index.ContainsKey($key)) { $index[$key] = New-Object System.Collections.ArrayList }
                [void]$index[$key].Add([pscustomobject]@{ Zip = $zipLeaf; Entry = $entry })
            }
        }

        $dest = $SdRoot
        foreach ($part in $Def.DestParts) { $dest = Join-Path $dest $part }

        foreach ($f in $Def.Files) {
            $target = Join-Path $dest $f.Name
            if (Test-Current $target $f) {
                $res.Current++
                continue
            }

            # The root of the zip before clone subfolders, and the expected
            # name before any other file with the same contents.
            $rank = @(
                @{ Expression = { $_.Entry.FullName.IndexOfAny([char[]]'/\') -ge 0 } },
                @{ Expression = { $_.Entry.Name -ne $f.Name } }
            )
            $key = "$([long]$f.Size):$($f.Crc)"
            if (-not $index.ContainsKey($key)) {
                $nameKey = $f.Name.ToLowerInvariant()
                if ($byName.ContainsKey($nameKey)) {
                    $other = $byName[$nameKey] | Sort-Object $rank | Select-Object -First 1
                    $res.Errors += "$($other.Zip):$($other.Entry.FullName) is not the expected file " +
                        '(damaged, or from a different version of the set)'
                } else {
                    $res.Missing += $f.Name
                }
                continue
            }
            $best = $index[$key] | Sort-Object $rank | Select-Object -First 1
            $source = "$($best.Zip):$($best.Entry.FullName)"

            if ($Preview) {
                $res.Written++
                continue
            }
            try {
                $data = Get-EntryBytes $best.Entry
            } catch {
                $res.Errors += "${source}: $($_.Exception.Message)"
                continue
            }
            if ($data.Length -ne $f.Size -or [UpdateAll.Crc32]::Compute($data) -ne $f.Crc) {
                $res.Errors += "${source}: contents do not match the size and CRC"
                continue
            }
            try {
                [void][System.IO.Directory]::CreateDirectory($dest)
                $tmp = "$target.tmp"
                [System.IO.File]::WriteAllBytes($tmp, $data)
                if ([System.IO.File]::Exists($target)) { [System.IO.File]::Delete($target) }
                [System.IO.File]::Move($tmp, $target)
            } catch {
                $res.Errors += "cannot write ${target}: $($_.Exception.InnerException.Message)"
                continue
            }
            $res.Written++
        }

        if ($res.Missing.Count -gt 0 -and $unreadable.Count -gt 0) {
            $res.Errors += "unreadable entries in the zip: $($unreadable -join ', ')"
        }
    } finally {
        foreach ($zip in $archives) { $zip.Dispose() }
    }
    return $res
}

function Get-FilesToInstall($Def, [string] $SdRoot) {
    $dest = $SdRoot
    foreach ($part in $Def.DestParts) { $dest = Join-Path $dest $part }
    return @($Def.Files | Where-Object { -not (Test-Current (Join-Path $dest $_.Name) $_) }).Count
}

$exitCode = 1
$tempDir = $null
try {
    $cfg = Read-Config $Config
    $defs = @($cfg.Cores)
    $script:DbSources = @($cfg.Databases)
    # -File passes "a,b" as one string, so split on commas here as well.
    foreach ($d in @($Database | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
            Where-Object { $_ })) {
        if (Test-Url $d) { $script:DbSources += $d }
        else { $script:DbSources += $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($d) }
    }

    if ($List) {
        Write-Host ('{0,-12} {1,-14} {2,-14} {3,-26} {4}' -f 'ID', 'Name', 'Zip', 'SD card folder', 'Set')
        foreach ($d in $defs) {
            Write-Host ('{0,-12} {1,-14} {2,-14} {3,-26} {4}, {5} files' -f
                $d.Id, $d.Name, ($d.Zips -join ', '), $d.DestShown, $d.Set, $d.Files.Count)
        }
        Write-Host ''
        if ($script:DbSources.Count -gt 0) {
            Write-Host 'Databases:'
            foreach ($src in $script:DbSources) { Write-Host "  $src" }
        } else {
            Write-Host 'Databases: none configured, so missing zips are not downloaded'
        }
        exit 0
    }

    $sdRoot = Resolve-Folder $Sd 'SD card root'
    $mayDownload = $script:DbSources.Count -gt 0 -and -not $NoDownload
    if (-not $Zips -and -not $mayDownload) {
        throw 'nothing to install from: give a zip folder with -Zips, or configure a database to download from'
    }
    if ($Zips) {
        $zipDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Zips)
        if (-not [System.IO.Directory]::Exists($zipDir) -and -not $mayDownload) {
            throw "zip folder not found: $Zips"
        }
    }

    $requested = @()
    if ($Core) {
        # -File passes "a,b" as one string, so split on commas here as well.
        $requested = @($Core | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
            Where-Object { $_ })
        $known = @($defs | ForEach-Object { $_.Id })
        $unknown = @($requested | Where-Object { $known -notcontains $_ })
        if ($unknown.Count -gt 0) {
            throw "unknown core: $($unknown -join ', '). Configured: $($known -join ', ')"
        }
        $defs = @($defs | Where-Object { $requested -contains $_.Id })
    }

    if ($Zips) { Write-Host "Zips from:    $zipDir" } else { Write-Host 'Zips from:    downloads only' }
    Write-Host "SD card:      $sdRoot"
    if ($mayDownload) {
        Write-Host "Databases:    $($script:DbSources.Count), used for zips that are not in the zip folder"
    }
    if ($DryRun) { Write-Host 'Dry run:      nothing is downloaded or written' }
    Write-Host ''

    if (-not $Zips) {
        # Without -Zips, downloads go to a temporary folder that is removed again.
        $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ('updateAll-' + [guid]::NewGuid().ToString('N'))
        [void][System.IO.Directory]::CreateDirectory($tempDir)
        $zipDir = $tempDir
    }

    $zipListing = New-Object System.Collections.ArrayList
    if ([System.IO.Directory]::Exists($zipDir)) {
        foreach ($file in Get-ChildItem -LiteralPath $zipDir -File) { [void]$zipListing.Add($file.Name) }
    }
    if (-not $mayDownload) { $script:DbSources = @() }
    $script:DbLoaded = $null
    $script:DbErrors = @()
    $script:WantedZips = @($defs | ForEach-Object { $_.Zips })

    $complete = 0; $failed = 0; $skipped = 0; $toDownload = 0
    foreach ($d in $defs) {
        # A set that is complete on the card needs no zip, so nothing is
        # opened or downloaded for it.
        if ((Get-FilesToInstall $d $sdRoot) -eq 0) {
            $complete++
            Write-Host "$($d.Name): $($d.DestShown)"
            Write-Host "  $($d.Files.Count) up to date"
            continue
        }

        $fetch = Get-MissingZips $d $zipDir $zipListing $DryRun.IsPresent
        $res = Install-Core $d $zipDir $zipListing $sdRoot $DryRun.IsPresent
        $res.Errors = @($fetch.Errors) + @($res.Errors)

        if ($res.Skipped -and -not $fetch.Pending -and $fetch.Errors.Count -eq 0) {
            $skipped++
            $where = ''
            if ($null -ne $script:DbLoaded -and @($script:DbLoaded).Count -gt 0) { $where = ', and not in any database' }
            Write-Host "$($d.Name): skipped, $($res.Skipped)$where"
            continue
        }

        $zipsShown = if ($res.Used.Count -gt 0) { $res.Used -join ', ' } else { $d.Zips -join ', ' }
        Write-Host "$($d.Name): $zipsShown -> $($d.DestShown)"
        foreach ($note in $fetch.Notes) { Write-Host "  $note" }
        if ($res.Skipped) {
            foreach ($err in $res.Errors) { Write-Host "  ERROR: $err" -ForegroundColor Red }
            if ($fetch.Pending) { $toDownload++ } else { $failed++ }
            continue
        }

        $parts = @()
        if ($res.Written) { $parts += "$($res.Written) $(if ($DryRun) { 'to write' } else { 'written' })" }
        if ($res.Current) { $parts += "$($res.Current) up to date" }
        if ($parts.Count -gt 0) { Write-Host "  $($parts -join ', ')" }
        if ($res.Missing.Count -gt 0) {
            Write-Host ("  INCOMPLETE: {0} of {1} files not in the zip: {2}" -f
                $res.Missing.Count, $d.Files.Count, ($res.Missing -join ', ')) -ForegroundColor Yellow
        }
        foreach ($err in $res.Errors) { Write-Host "  ERROR: $err" -ForegroundColor Red }

        if ($res.Missing.Count -eq 0 -and $res.Errors.Count -eq 0) { $complete++ } else { $failed++ }
    }

    Write-Host ''
    $summary = "$complete complete, $failed incomplete, $skipped skipped"
    if ($toDownload) { $summary += ", $toDownload to download" }
    Write-Host $summary

    $requestedSkipped = $requested.Count -gt 0 -and $skipped -gt 0
    if (($complete -gt 0 -or $toDownload -gt 0) -and $failed -eq 0 -and -not $requestedSkipped -and
            $script:DbErrors.Count -eq 0) {
        $exitCode = 0
    }
} catch {
    [Console]::Error.WriteLine("ERROR: $($_.Exception.Message)")
    $exitCode = 1
} finally {
    if ($tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue }
}
exit $exitCode
