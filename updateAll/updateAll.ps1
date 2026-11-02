<#
.SYNOPSIS
    Installs arcade ROM sets and game data on the SD card.

.DESCRIPTION
    Takes MAME ROM zips from one folder, the way MiSTer keeps them in
    /games/mame, and extracts each set into the folder on the SD card where its
    application looks for it. The sets, their zips and their folders are
    defined in updateAll.json next to this script; a new core is added there,
    not here.

    The "data" list of updateAll.json adds the shareware and demo data of the
    ported games. Each entry is downloaded from its own URL, as a zip or as the
    one file to install; its files may go into subfolders, and zips inside the
    download that it names under "archives" are searched as well. An entry
    with "uf2" turns the downloaded file into a UF2 image for the loader to
    flash.

    The "bios" list adds the BIOS files that the emulators need, in the same
    form as "data". The Windows program shows them as one set, and -Core bios
    selects them together. A BIOS file may be built from its download rather
    than taken from it as is: "offset" cuts it out of a download that is one
    file, and "parts" joins entries of a zip.

    A zip that is not in the folder is downloaded into it first, from the
    databases listed in updateAll.json or given with -Database; without -Zips,
    into a temporary folder that is removed afterwards. A database uses the
    format of the MiSTer Downloader: a JSON file, optionally zipped, whose
    "files" map gives each zip a "url" (or a "base_files_url" to prefix), a
    "size" and an MD5 "hash". A set that is already complete on the card needs
    no zip, so nothing is downloaded for it.

    When updateAll.json has a "config_url", the configuration is first read
    from there, so that sets added since this copy was made are installed too.
    If it cannot be read, the local file is used.

    Only the files listed for a set are extracted. They are found by size and
    CRC32 rather than by name, as the applications themselves do, so merged,
    split and non-merged sets all work, and they are written under the names
    from the configuration. Files already on the card with the right contents
    are left alone, and nothing on the card is ever deleted.

    The exit code is 0 when every set that was found is complete on the card,
    and 1 otherwise.

.PARAMETER Zips
    Folder with MAME zips you already have, for example phoenix.zip.
    Downloaded zips and game data are kept here as well. Default: download
    into a temporary folder that is removed afterwards.

.PARAMETER Sd
    Root of the SD card, for example E:\. Any existing folder will do.

.PARAMETER Core
    Install only these cores, by the IDs that -List shows; bios selects every
    BIOS file. Default: every core in the configuration.

.PARAMETER Database
    Download databases to use in addition to those in the configuration: URLs
    or files, separated with commas.

.PARAMETER NoDownload
    Use only the files already in the zip folder.

.PARAMETER Config
    Configuration file. Default: updateAll.json next to this script. When it
    has a config_url, the configuration is read from there, and from this file
    only if that fails.

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

# A type of its own, so that a session that already holds Crc32 still gets it.
if (-not ('UpdateAll.Uf2' -as [type])) {
    Add-Type -TypeDefinition @'
namespace UpdateAll
{
    public static class Uf2
    {
        // data as a UF2 image at address with this family ID, as picotool's
        // "uf2 convert -t bin" writes it: 256 bytes in each 512-byte block,
        // the last one padded with zeros.
        public static byte[] FromBinary(byte[] data, uint address, uint family)
        {
            int n = (data.Length + 255) / 256;
            byte[] image = new byte[n * 512];
            for (int i = 0; i < n; i++)
            {
                int at = i * 512;
                uint[] head = { 0x0A324655u, 0x9E5D5157u, 0x00002000u, unchecked(address + (uint)(256 * i)),
                                256u, (uint)i, (uint)n, family };
                for (int k = 0; k < 8; k++)
                    Put(image, at + 4 * k, head[k]);
                System.Array.Copy(data, 256 * i, image, at + 32, System.Math.Min(256, data.Length - 256 * i));
                Put(image, at + 508, 0x0AB16F30u);
            }
            return image;
        }

        static void Put(byte[] b, int at, uint v)
        {
            b[at] = (byte)v;
            b[at + 1] = (byte)(v >> 8);
            b[at + 2] = (byte)(v >> 16);
            b[at + 3] = (byte)(v >> 24);
        }
    }
}
'@
}

$script:UserAgent = 'updateAll (pico-bootLoader)'
$script:DownloadAttempts = 3
$script:TimeoutSec = 60
$script:ConfigTimeoutSec = 15

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
        $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    } catch {
        throw "cannot read ${Path}: $($_.Exception.Message)"
    }
    # Relative database paths are relative to the configuration file.
    $configDir = Split-Path -Parent ((Resolve-Path -LiteralPath $Path).ProviderPath)
    return ConvertFrom-Config $text $Path $configDir
}

# The name a downloaded file is kept under in the zip folder: the last part of
# the URL's path, unescaped.
function Get-UrlFileName([string] $Url) {
    $path = ($Url -replace '^[^:/?#]+://[^/?#]*', '') -replace '[?#].*$', ''
    return [Uri]::UnescapeDataString($path.Substring($path.LastIndexOf('/') + 1))
}

# One entry of "cores", "data" or "bios", as Kind says; a BIOS entry has the
# form of game data.
function ConvertFrom-Core($d, [string] $Kind, [string] $Path, [hashtable] $Ids) {
    $IsData = $Kind -ne 'cores'
    $id = [string](Get-Field $d 'id')
    foreach ($key in 'id', 'name', $(if ($IsData) { 'url' } else { 'zips' }), 'destination', 'files') {
        if ($null -eq (Get-Field $d $key)) { throw "${Path}: core '$id' has no '$key'" }
    }
    if ($id -eq 'bios') { throw "${Path}: core '$id' has a reserved id" }
    if ($Ids.ContainsKey($id)) { throw "${Path}: core '$id' is defined twice" }
    $Ids[$id] = $true

    $url = $null
    $archives = @()
    $uf2 = $null
    if ($IsData) {
        # The file is kept in the zip folder under the last part of the URL.
        $url = Get-Field $d 'url'
        $zipName = ''
        if ($url -is [string] -and (Test-Url $url)) { $zipName = Get-UrlFileName $url }
        if (Test-BadName $zipName) { throw "${Path}: core '$id' has an invalid url '$url'" }
        # Not Get-Field: returning a list of one unrolls it into its item.
        $p = $d.PSObject.Properties['archives']
        if ($p) {
            $archives = $p.Value
            if ($archives -isnot [array] -or
                    @($archives | Where-Object { $_ -isnot [string] -or (Test-BadName $_) }).Count -gt 0) {
                throw "${Path}: core '$id' has an invalid archive name"
            }
        }
        $p = $d.PSObject.Properties['uf2']
        if ($p -and $null -ne $p.Value) {
            try {
                $uf2 = [pscustomobject]@{
                    Address = [Convert]::ToUInt32([string](Get-Field $p.Value 'address'), 16)
                    Family  = [Convert]::ToUInt32([string](Get-Field $p.Value 'family'), 16)
                }
            } catch {
                throw "${Path}: core '$id' needs a hexadecimal 'address' and 'family' in 'uf2'"
            }
        }
    }

    $dest = ([string](Get-Field $d 'destination')).Replace('\', '/').Trim('/')
    $parts = @($dest.Split('/'))
    if (-not $dest -or @($parts | Where-Object { Test-BadName $_ }).Count -gt 0) {
        throw "${Path}: core '$id' has an invalid destination '$(Get-Field $d 'destination')'"
    }

    if ($IsData) { $zipNames = @($zipName) } else { $zipNames = @(Get-Field $d 'zips') }
    $fileDefs = @(Get-Field $d 'files')
    if ($zipNames.Count -eq 0 -or $fileDefs.Count -eq 0) {
        throw "${Path}: core '$id' needs at least one zip and one file"
    }
    if (@($zipNames | Where-Object { Test-BadName $_ }).Count -gt 0) {
        throw "${Path}: core '$id' has an invalid zip name"
    }
    $files = foreach ($f in $fileDefs) {
        $name = [string](Get-Field $f 'name')
        # Game data may go into subfolders of its destination; ROM sets may
        # not, as older copies of updateAll would reject that.
        if ($IsData) { $nameParts = @($name.Split('/')) } else { $nameParts = @($name) }
        if (@($nameParts | Where-Object { Test-BadName $_ }).Count -gt 0) {
            throw "${Path}: core '$id' has an invalid file name '$name'"
        }
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
        # A BIOS file may be part of a download that is one file, or the
        # entries of a zip joined; older copies do not know either.
        $offset = $null
        $join = @()
        $p = $f.PSObject.Properties['offset']
        if ($Kind -eq 'bios' -and $p) {
            try {
                if ($p.Value -isnot [string]) { throw 'not a string' }
                $offset = [Convert]::ToUInt32($p.Value, 16)
            } catch {
                throw "${Path}: core '$id', file '$name' needs a hexadecimal 'offset'"
            }
        }
        # Not Get-Field: returning a list of one unrolls it into its item.
        $p = $f.PSObject.Properties['parts']
        if ($Kind -eq 'bios' -and $p) {
            $join = $p.Value
            if ($join -isnot [array] -or $join.Count -eq 0 -or
                    @($join | Where-Object { $_ -isnot [string] -or (Test-BadName $_) }).Count -gt 0) {
                throw "${Path}: core '$id', file '$name' has an invalid 'parts' list"
            }
        }
        [pscustomobject]@{ Name = $name; Size = [long]$size; Crc = $crc; Offset = $offset; Parts = @($join) }
    }
    $sliced = @($files | Where-Object { $null -ne $_.Offset }).Count -gt 0
    $joined = @($files | Where-Object { $_.Parts.Count -gt 0 }).Count -gt 0
    if ([int]$sliced + [int]$joined + [int]($null -ne $uf2) -gt 1) {
        throw "${Path}: core '$id' cannot combine 'offset', 'parts' and 'uf2'"
    }

    return [pscustomobject]@{
        Id        = $id
        Name      = [string](Get-Field $d 'name')
        Set       = [string](Get-Field $d 'set')
        Zips      = $zipNames
        DestParts = $parts
        DestShown = '/' + $dest
        Files     = @($files)
        Url       = $url
        Archives  = @($archives)
        Uf2       = $uf2
        Bios      = $Kind -eq 'bios'
        Sliced    = $sliced
    }
}

# Read-Config of a configuration that is already read; Path names it in
# messages, and relative database paths are made relative to ConfigDir.
function ConvertFrom-Config([string] $Text, [string] $Path, [string] $ConfigDir) {
    try {
        $cfg = $Text | ConvertFrom-Json
    } catch {
        throw "cannot read ${Path}: $($_.Exception.Message)"
    }
    if ($null -eq (Get-Field $cfg 'cores') -or @(Get-Field $cfg 'cores').Count -eq 0) {
        throw "${Path}: no cores defined"
    }
    # "data" holds game data that comes from one URL rather than from a MAME
    # zip in the databases. It is a key of its own because copies of updateAll
    # that predate it read this file too, and skip keys they do not know.
    $data = @()
    $p = $cfg.PSObject.Properties['data']
    if ($p) {
        $data = $p.Value
        if ($data -isnot [array]) { throw "${Path}: 'data' must be a list" }
    }
    # "bios" holds the BIOS files, in the form of game data; a key of its own
    # for the same reason, and so that they can be selected as one set.
    $bios = @()
    $p = $cfg.PSObject.Properties['bios']
    if ($p) {
        $bios = $p.Value
        if ($bios -isnot [array]) { throw "${Path}: 'bios' must be a list" }
    }

    $ids = @{}
    $defs = @(foreach ($d in @(Get-Field $cfg 'cores')) { ConvertFrom-Core $d 'cores' $Path $ids }) +
            @(foreach ($d in $data) { ConvertFrom-Core $d 'data' $Path $ids }) +
            @(foreach ($d in $bios) { ConvertFrom-Core $d 'bios' $Path $ids })

    $sources = @(Get-Field $cfg 'databases' | Where-Object { $null -ne $_ })
    if (@($sources | Where-Object { $_ -isnot [string] }).Count -gt 0) {
        throw "${Path}: 'databases' must be a list of URLs or paths"
    }
    $sources = @($sources | ForEach-Object {
        if (Test-Url $_) { $_ } else { Join-Path $ConfigDir $_ }
    })

    # Not Get-Field: returning a list of one unrolls it into its item.
    $configUrl = $null
    $p = $cfg.PSObject.Properties['config_url']
    if ($p) { $configUrl = $p.Value }
    if ($configUrl -is [string] -and $configUrl -eq '') {
        $configUrl = $null
    } elseif ($null -ne $configUrl -and ($configUrl -isnot [string] -or -not (Test-Url $configUrl))) {
        throw "${Path}: 'config_url' must be an http or https URL"
    }

    return [pscustomobject]@{ Cores = @($defs); Databases = $sources; ConfigUrl = $configUrl }
}

# The configuration at Url, as Read-Config returns it. Its own config_url is
# not followed.
function Read-ConfigUrl([string] $Url, [string] $ConfigDir) {
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        try {
            Save-Url $Url $tmp $script:ConfigTimeoutSec
        } catch {
            $status = Get-HttpStatus $_
            if ($status) { $reason = "HTTP $status" } else { $reason = $_.Exception.Message }
            throw "cannot read ${Url}: $reason"
        }
        $text = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::UTF8)
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
    return ConvertFrom-Config $text $Url $ConfigDir
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

function Save-Url([string] $Url, [string] $Path, [int] $TimeoutSec = $script:TimeoutSec) {
    # -OutFile is a wildcard pattern in Windows PowerShell 5.1, and escaping it
    # puts the escapes into the name, so a path with [ or ] in it is written
    # under a plain temporary name first.
    $out = $Path
    if ([Management.Automation.WildcardPattern]::ContainsWildcardCharacters($Path)) {
        $out = Join-Path ([System.IO.Path]::GetTempPath()) ('updateAll-' + [guid]::NewGuid().ToString('N') + '.part')
    }
    try {
        Invoke-WebRequest -Uri $Url -OutFile $out -UseBasicParsing -UserAgent $script:UserAgent `
            -TimeoutSec $TimeoutSec
        if ($out -ne $Path) {
            if ([System.IO.File]::Exists($Path)) { [System.IO.File]::Delete($Path) }
            [System.IO.File]::Move($out, $Path)
        }
    } finally {
        if ($out -ne $Path -and [System.IO.File]::Exists($out)) { [System.IO.File]::Delete($out) }
    }
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

function Test-Zip([string] $Path) {
    try {
        [System.IO.Compression.ZipFile]::OpenRead($Path).Dispose()
        return $true
    } catch {
        return $false
    }
}

function Invoke-Download([string] $Url, [string] $Dest, $Size, [string] $Md5, [bool] $NeedZip = $true) {
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
            if ($NeedZip -and -not (Test-Zip $tmp)) { throw 'the file received is not a zip' }
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

function Get-Uf2Note([string] $Name, [uint32] $Address, [bool] $Preview) {
    $verb = if ($Preview) { 'would convert' } else { 'converted' }
    return ('{0} {1} to a UF2 image at 0x{2:X8}' -f $verb, $Name, $Address)
}

# Downloads the core's zips that are not in ZipDir, from the databases, or from
# the core's own URL when UrlDownloads is set.
function Get-MissingZips($Def, [string] $ZipDir, [System.Collections.ArrayList] $ZipListing, [bool] $Preview,
                         [bool] $UrlDownloads) {
    $out = [pscustomobject]@{ Notes = @(); Errors = @(); Pending = $false }
    foreach ($zipName in $Def.Zips) {
        if (@($ZipListing | Where-Object { $_ -eq $zipName }).Count -gt 0) { continue }
        if ($Def.Url) {
            if (-not $UrlDownloads) { continue }
            $hit = [pscustomobject]@{ Url = $Def.Url; Size = $null; Md5 = '' }
            $source = ([Uri]$Def.Url).Host.ToLowerInvariant()
        } else {
            $hit = Find-InDatabases $zipName
            if (-not $hit) { continue }
            $source = $hit.Db.Id
        }
        if ($Preview) {
            $out.Notes += "would download $zipName from $source"
            if ($Def.Uf2) { $out.Notes += Get-Uf2Note $zipName $Def.Uf2.Address $true }
            $out.Pending = $true
            continue
        }
        try {
            [void][System.IO.Directory]::CreateDirectory($ZipDir)
            $got = Invoke-Download $hit.Url (Join-Path $ZipDir $zipName) $hit.Size $hit.Md5 (-not $Def.Url)
        } catch {
            $out.Errors += "download of $zipName from $source failed: $($_.Exception.Message)"
            continue
        }
        [void]$ZipListing.Add($zipName)
        $out.Notes += "downloaded $zipName ($([math]::Floor(($got + 1023) / 1024)) KB) from $source"
    }
    return $out
}

# ---------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------

function Get-EntryBytes($Entry) {
    if ($Entry -isnot [System.IO.Compression.ZipArchiveEntry]) {
        return , $Entry.Data    # a plain file, see Open-Archives
    }
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

# The innermost message of an exception: what .NET says went wrong.
function Get-InnerMessage([System.Exception] $Exception) {
    while ($Exception.InnerException) { $Exception = $Exception.InnerException }
    return $Exception.Message
}

# A file held in memory, standing for an entry of a zip.
function New-PlainEntry([string] $Name, [byte[]] $Bytes) {
    return [pscustomobject]@{
        Name = $Name; FullName = $Name; Length = [long]$Bytes.Length
        Crc32 = [UpdateAll.Crc32]::Compute($Bytes); Data = $Bytes
    }
}

# The archives to search in the file at Path, as @{ Label; Entries }: the zip
# itself, or the file alone when game data is not a zip or is to become a UF2
# image, plus the zips inside it that the core names in "archives". Opened zips
# are added to Opened, to be closed by the caller; problems go to Res.Errors, and
# a conversion to Res.Notes.
function Open-Archives($Def, [string] $Path, $Res, [System.Collections.ArrayList] $Opened, [bool] $Preview) {
    $leaf = Split-Path -Leaf $Path
    if ($Def.Url -and ($Def.Uf2 -or $Def.Sliced -or -not (Test-Zip $Path))) {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($Path)
        } catch {
            $Res.Errors += "cannot open ${leaf}: $(Get-InnerMessage $_.Exception)"
            return
        }
        if ($Def.Uf2) {
            $bytes = [UpdateAll.Uf2]::FromBinary($bytes, $Def.Uf2.Address, $Def.Uf2.Family)
            $Res.Notes += Get-Uf2Note $leaf $Def.Uf2.Address $Preview
        }
        $entries = @(New-PlainEntry $leaf $bytes)
        # A BIOS file with an offset is that part of the download, under its
        # own name.
        foreach ($f in $Def.Files) {
            if ($null -eq $f.Offset) { continue }
            $from = [Math]::Min([long]$f.Offset, [long]$bytes.Length)
            $n = [Math]::Min([long]$bytes.Length - $from, [Math]::Max([long]$f.Size, [long]0))
            $part = New-Object byte[] $n
            [Array]::Copy($bytes, $from, $part, 0, $n)
            $entries += New-PlainEntry $f.Name.Substring($f.Name.LastIndexOf('/') + 1) $part
        }
        return [pscustomobject]@{ Label = $leaf; Entries = $entries }
    }
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    } catch {
        $Res.Errors += "cannot open ${leaf}: $($_.Exception.InnerException.Message)"
        return
    }
    [void]$Opened.Add($zip)
    [pscustomobject]@{ Label = $leaf; Entries = @($zip.Entries) }
    foreach ($inner in $Def.Archives) {
        $found = $zip.Entries | Where-Object { $_.Name -eq $inner } | Select-Object -First 1
        if (-not $found) {
            $Res.Errors += "$leaf holds no $inner"
            continue
        }
        $label = "${leaf}:$($found.FullName)"
        try {
            $bytes = Get-EntryBytes $found
            $innerZip = New-Object System.IO.Compression.ZipArchive((New-Object System.IO.MemoryStream(, $bytes)))
        } catch {
            $Res.Errors += "cannot open ${label}: $(Get-InnerMessage $_.Exception)"
            continue
        }
        [void]$Opened.Add($innerZip)
        [pscustomobject]@{ Label = $label; Entries = @($innerZip.Entries) }
    }
}

# The BIOS file File, made of the entries that its "parts" name, joined in that
# order, as an archive holding just that file; nothing when a part is not there
# or cannot be read, which goes to Res.Errors.
function Join-Parts($File, $Arcs, $Res) {
    $data = New-Object System.IO.MemoryStream
    foreach ($part in $File.Parts) {
        $found = @(foreach ($arc in $Arcs) {
            foreach ($entry in $arc.Entries) {
                if ($entry.Name -and $entry.Name -eq $part) { [pscustomobject]@{ Zip = $arc.Label; Entry = $entry } }
            }
        })
        if ($found.Count -eq 0) {
            $Res.Errors += "$($Arcs[0].Label) holds no $part"
            return
        }
        # The root of the zip before its subfolders.
        $best = @(@($found | Where-Object { $_.Entry.FullName.IndexOfAny([char[]]'/\') -lt 0 }) + $found)[0]
        try {
            $bytes = Get-EntryBytes $best.Entry
        } catch {
            $Res.Errors += "$($best.Zip):$($best.Entry.FullName): $($_.Exception.Message)"
            return
        }
        $data.Write($bytes, 0, $bytes.Length)
    }
    $name = $File.Name.Substring($File.Name.LastIndexOf('/') + 1)
    return [pscustomobject]@{ Label = $Arcs[0].Label; Entries = @(New-PlainEntry $name $data.ToArray()) }
}

function Install-Core($Def, [string] $ZipDir, $ZipListing, [string] $SdRoot, [bool] $Preview) {
    $res = [pscustomobject]@{
        Written = 0; Current = 0; Missing = @(); Errors = @(); Notes = @(); Skipped = $null; Used = @()
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
        $firstLabel = $null
        $arcs = @(foreach ($p in $paths) { Open-Archives $Def $p $res $archives $Preview })
        $opened = $arcs
        foreach ($f in $Def.Files) {
            if ($f.Parts.Count -gt 0 -and $opened.Count -gt 0) {
                $joined = Join-Parts $f $opened $res
                if ($joined) { $arcs += $joined }
            }
        }
        foreach ($arc in $arcs) {
            $zipLeaf = $arc.Label
            if ($null -eq $firstLabel) { $firstLabel = $zipLeaf }
            foreach ($entry in $arc.Entries) {
                if (-not $entry.Name) { continue }
                $nameKey = $entry.Name.ToLowerInvariant()
                if (-not $byName.ContainsKey($nameKey)) { $byName[$nameKey] = New-Object System.Collections.ArrayList }
                [void]$byName[$nameKey].Add([pscustomobject]@{ Zip = $zipLeaf; Entry = $entry })
                if (-not $wantedSizes.ContainsKey([long]$entry.Length)) { continue }
                try {
                    if ($script:HasEntryCrc -or $entry -isnot [System.IO.Compression.ZipArchiveEntry]) {
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
            $target = Get-TargetPath $dest $f.Name
            $wanted = $f.Name.Substring($f.Name.LastIndexOf('/') + 1)
            if (Test-Current $target $f) {
                $res.Current++
                continue
            }

            # The root of the zip before clone subfolders, and the expected
            # name before any other file with the same contents.
            $rank = @(
                @{ Expression = { $_.Entry.FullName.IndexOfAny([char[]]'/\') -ge 0 } },
                @{ Expression = { $_.Entry.Name -ne $wanted } }
            )
            $key = "$([long]$f.Size):$($f.Crc)"
            if (-not $index.ContainsKey($key)) {
                $nameKey = $wanted.ToLowerInvariant()
                if ($Def.Uf2 -and $null -ne $firstLabel) {
                    $res.Errors += "the UF2 image of $firstLabel is not the expected file " +
                        '(damaged, or from a different version)'
                } elseif ($byName.ContainsKey($nameKey)) {
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
                [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target))
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

# Where a file goes: game data may name a subfolder ("baseq2/pak0.pak").
function Get-TargetPath([string] $Dest, [string] $Name) {
    return Join-Path $Dest $Name.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function Get-FilesToInstall($Def, [string] $SdRoot) {
    $dest = $SdRoot
    foreach ($part in $Def.DestParts) { $dest = Join-Path $dest $part }
    return @($Def.Files | Where-Object { -not (Test-Current (Get-TargetPath $dest $_.Name) $_) }).Count
}

$exitCode = 1
$tempDir = $null
try {
    $cfg = Read-Config $Config
    # The configuration at config_url replaces the local one; the local one
    # is kept when that cannot be read.
    $setsFrom = $null
    if ($cfg.ConfigUrl) {
        $url = $cfg.ConfigUrl
        $localPath = (Resolve-Path -LiteralPath $Config).ProviderPath
        try {
            $cfg = Read-ConfigUrl $url (Split-Path -Parent $localPath)
            $setsFrom = $url
        } catch {
            $setsFrom = "$localPath ($($_.Exception.Message))"
        }
    }
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
        if ($setsFrom) { Write-Host "Sets from:    $setsFrom" }
        if ($script:DbSources.Count -gt 0) {
            Write-Host 'Databases:'
            foreach ($src in $script:DbSources) { Write-Host "  $src" }
        } else {
            Write-Host 'Databases: none configured, so missing zips are not downloaded'
        }
        exit 0
    }

    $sdRoot = Resolve-Folder $Sd 'SD card root'
    # Game data has its own URL, so it can be downloaded without a database.
    $useDatabases = $script:DbSources.Count -gt 0 -and -not $NoDownload
    $urlDownloads = -not $NoDownload
    $mayDownload = $useDatabases -or ($urlDownloads -and @($defs | Where-Object { $_.Url }).Count -gt 0)
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
        $anyBios = @($defs | Where-Object { $_.Bios }).Count -gt 0
        $unknown = @($requested | Where-Object { $known -notcontains $_ -and -not ($anyBios -and $_ -eq 'bios') })
        if ($unknown.Count -gt 0) {
            throw "unknown core: $($unknown -join ', '). Configured: $($known -join ', ')"
        }
        $defs = @($defs | Where-Object { $requested -contains $_.Id -or ($_.Bios -and $requested -contains 'bios') })
    }

    if ($setsFrom) { Write-Host "Sets from:    $setsFrom" }
    if ($Zips) { Write-Host "Zips from:    $zipDir" } else { Write-Host 'Zips from:    downloads only' }
    Write-Host "SD card:      $sdRoot"
    if ($useDatabases) {
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
    if (-not $useDatabases) { $script:DbSources = @() }
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

        $fetch = Get-MissingZips $d $zipDir $zipListing $DryRun.IsPresent $urlDownloads
        $res = Install-Core $d $zipDir $zipListing $sdRoot $DryRun.IsPresent
        $res.Errors = @($fetch.Errors) + @($res.Errors)

        if ($res.Skipped -and -not $fetch.Pending -and $fetch.Errors.Count -eq 0) {
            $skipped++
            $where = ''
            if ($null -ne $script:DbLoaded -and @($script:DbLoaded).Count -gt 0 -and -not $d.Url) {
                $where = ', and not in any database'
            }
            Write-Host "$($d.Name): skipped, $($res.Skipped)$where"
            continue
        }

        $zipsShown = if ($res.Used.Count -gt 0) { $res.Used -join ', ' } else { $d.Zips -join ', ' }
        Write-Host "$($d.Name): $zipsShown -> $($d.DestShown)"
        foreach ($note in @($fetch.Notes) + @($res.Notes)) { Write-Host "  $note" }
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
