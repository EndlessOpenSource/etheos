param (
    [switch]$SkipCMake,
    [switch]$SkipMariaDB,
    [switch]$SkipSQLite
)

Add-Type -Assembly System.IO.Compression.FileSystem

function CheckPathForDll($DllName, $AdditionalPaths) {
    $splitPaths = $env:PATH.Split(';')
    if ($AdditionalPaths) {
        $splitPaths += $AdditionalPaths
    }

    foreach ($path in $splitPaths) {
        if ($path -and (Test-Path $path)) {
            $dllPath = Join-Path $path $DllName
            if (Test-Path $dllPath) {
                Write-Output "Found $DllName in $path"
                return
            }
        }
    }

    Write-Warning "Warning: $DllName not found in `$env:PATH"
}

$CurrentIdentity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $CurrentIdentity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script must be run as administrator since it downloads/installs software dependencies."
    exit -1
}

$DownloadDir = Join-Path $PSScriptRoot 'download'
if (-not (Test-Path $DownloadDir)) {
    New-Item $DownloadDir -ItemType Directory | Out-Null
}

# Use chocolatey to install dependencies
#
if (-not (Get-Command choco)) {
    Set-ExecutionPolicy Bypass -Scope Process -Force
    Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
}

if (-not $SkipCMake) {
    Write-Output "Installing CMake..."
    choco install -y cmake --installargs '"ADD_CMAKE_TO_PATH=System"' | Out-Null

    refreshenv
    if (-not (Get-Command cmake)) {
        Write-Warning "Could not detect cmake.exe after install. Shell may need to be restarted."
    }
}

$MariaDBURL = "https://downloads.mariadb.com/Connectors/c/connector-c-2.3.7/mariadb-connector-c-2.3.7-win32.msi"
if (-not $SkipMariaDB) {
    Write-Output "Installing MariaDB..."
    $DownloadedFile = (Join-Path $DownloadDir "mariadb.msi")
    (New-Object System.Net.WebClient).DownloadFile($MariaDBURL, $DownloadedFile)
    msiexec /I $DownloadedFile /qn

    # Update the path with the new directory
    # This is a fixed default value of the MSI that is downloaded
    $InstallDir="C:\Program Files (x86)\MariaDB\MariaDB Connector C\lib"
    $PATHRegKey = 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment'
    $Old_Path=(Get-ItemProperty -Path $PATHRegKey -Name Path).Path
    if ($Old_Path.IndexOf($InstallDir, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        Set-ItemProperty -Path $PATHRegKey -Name PATH -Value ("$Old_Path;$InstallDir")
    }

    refreshenv
    CheckPathForDll -DllName "libmariadb.dll"
}

$Sqlite3URL = "https://sqlite.org/2019/sqlite-amalgamation-3300100.zip"
if (-not $SkipSQLite) {
    Write-Output "Installing SQLite..."
    choco install -y sqlite --params "/NoTools" | Out-Null

    $LocalSqliteDir = (Join-Path $PSScriptRoot "../sqlite")
    foreach ($dir in @($LocalSqliteDir, "$LocalSqliteDir\bin", "$LocalSqliteDir\src", "$LocalSqliteDir\include")) {
        if (-not (Test-Path $dir)) {
            mkdir $dir | Out-Null
        }
    }
    $LocalSqliteDir = Resolve-Path $LocalSqliteDir

    # This is a guaranteed download location due to OSS Chocolatey restrictions
    $ChocoSqliteInstallDir = "C:\ProgramData\chocolatey\lib\sqlite\tools"
    foreach ($item in ((Get-ChildItem -Force -Recurse $ChocoSqliteInstallDir | Where-Object {$_.Name -like '*.dll'}))) {
        Copy-Item -Force $item.FullName (Join-Path $LocalSqliteDir "bin")
    }

    $DownloadedFile = (Join-Path $DownloadDir "sqlite3.zip")
    try {
        # This throws for some reason but still downloads the file?
        (New-Object System.Net.WebClient).DownloadFile($Sqlite3URL, $DownloadedFile)
        $zip = [IO.Compression.ZipFile]::OpenRead($DownloadedFile)

        foreach ($entry in ($zip.Entries | Where-Object {$_.Name -like '*.h' -or $_.Name -like '*.c'})) {
            $filename = [System.IO.Path]::GetFileName($entry)
            if ($filename -like '*.h') {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$LocalSqliteDir\include\$filename", $true)
            } elseif ($filename -like '*.c') {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$LocalSqliteDir\src\$filename", $true)
            }
        }
    }
    catch {}
    finally {
        if ($zip) {
            $zip.Dispose()
        }
    }

    refreshenv
    CheckPathForDll -DllName "sqlite3.dll" -AdditionalPaths "$LocalSqliteDir\bin"
}

if ((Test-Path $DownloadDir)) {
    Remove-Item $DownloadDir -Recurse -Force
}

if (-not (Get-Command vswhere)) {
    Write-Output "Installing vswhere..."
    choco install -y vswhere | Out-Null

    refreshenv
    if (-not (Get-Command vswhere)) {
        Write-Warning "Could not detect vswhere after install. Shell may need to be restarted."
    }
}
