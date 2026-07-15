param(
  [int]$Frames = 120
)

if ($Frames -lt 1) {
  $Frames = 120
}
$GuiFrames = $Frames
if ($GuiFrames -lt 900) {
  $GuiFrames = 900
}

$Root = (Get-Location).Path
$Out = Join-Path $Root "artifacts\hosted-visual\windows"
$Evidence = Join-Path $Out "evidence.md"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Set-Content -Path $Evidence -Value "# Windows Hosted Visual Probe" -Encoding utf8

function Add-Evidence {
  param([string]$Line = "")
  Add-Content -Path $Evidence -Value $Line -Encoding utf8
}

function Invoke-Captured {
  param(
    [string]$Name,
    [scriptblock]$Block
  )

  $stdout = Join-Path $Out "$Name.stdout"
  $stderr = Join-Path $Out "$Name.stderr"
  $statusPath = Join-Path $Out "$Name.status"
  $global:LASTEXITCODE = 0
  try {
    & $Block 1> $stdout 2> $stderr
    if ($null -eq $global:LASTEXITCODE) {
      $status = 0
    } else {
      $status = [int]$global:LASTEXITCODE
    }
  } catch {
    $_ | Out-File -FilePath $stderr -Append -Encoding utf8
    $status = 1
  }
  Set-Content -Path $statusPath -Value $status -Encoding ascii
  Add-Evidence "- ${Name}: exit ${status}"
  return $status
}

function Read-TextOrEmpty {
  param([string]$Path)
  if (Test-Path $Path) {
    return [System.IO.File]::ReadAllText($Path)
  }
  return ""
}

function Write-ConptyAcceptance {
  param(
    [int]$Status,
    [string]$Command,
    [string]$Compiler
  )

  $stdoutName = "conpty-smoke.stdout"
  $stderrName = "conpty-smoke.stderr"
  $statusName = "conpty-smoke.status"
  $stdoutPath = Join-Path $Out $stdoutName
  $stderrPath = Join-Path $Out $stderrName
  $statusPath = Join-Path $Out $statusName
  $passNeedle = "PASS conpty smoke: observed TIMUI_CONPTY_SMOKE"
  $stdoutText = Read-TextOrEmpty $stdoutPath
  $stderrText = Read-TextOrEmpty $stderrPath
  $passTokenPresent = $stdoutText.Contains($passNeedle)
  $accepted = ($Status -eq 0 -and $passTokenPresent)
  $windowsVersion = (Read-TextOrEmpty (Join-Path $Out "windows-version.txt")).Trim()
  $queryUser = (Read-TextOrEmpty (Join-Path $Out "query-user.txt")).Trim()
  $qwinsta = (Read-TextOrEmpty (Join-Path $Out "qwinsta.txt")).Trim()
  $wtVersion = (Read-TextOrEmpty (Join-Path $Out "wt-version-command.stdout")).Trim()
  if (-not $wtVersion) {
    $wtVersion = "unknown"
  }

  Set-Content -Path (Join-Path $Out "conpty-smoke.command.txt") -Value $Command -Encoding utf8
  Set-Content -Path (Join-Path $Out "conpty-smoke.meta.txt") -Encoding utf8 -Value @(
    "commit=$commit",
    "runner_os=$($env:RUNNER_OS)",
    "os_env=$($env:OS)",
    "shell=PowerShell $($PSVersionTable.PSVersion)",
    "msys2_location=$Msys2Location",
    "msys2_bash=$Bash",
    "msys2_root=$MsysRoot",
    "compiler=$Compiler",
    "windows_version=$windowsVersion",
    "windows_terminal_version=$wtVersion",
    "status=$Status",
    "passTokenPresent=$passTokenPresent",
    "accepted=$accepted"
  )

  [ordered]@{
    commit = $commit
    runnerOS = $env:RUNNER_OS
    osEnv = $env:OS
    shell = "PowerShell $($PSVersionTable.PSVersion)"
    msys2Location = $Msys2Location
    msys2Bash = "$Bash"
    msys2Root = $MsysRoot
    compiler = $Compiler
    command = $Command
    status = $Status
    passTokenPresent = $passTokenPresent
    accepted = $accepted
    stdout = $stdoutName
    stderr = $stderrName
    statusFile = $statusName
    stdoutBytes = if (Test-Path $stdoutPath) { (Get-Item $stdoutPath).Length } else { 0 }
    stderrBytes = if (Test-Path $stderrPath) { (Get-Item $stderrPath).Length } else { 0 }
    stdoutExcerpt = if ($stdoutText.Length -gt 2048) { $stdoutText.Substring(0, 2048) } else { $stdoutText }
    stderrExcerpt = if ($stderrText.Length -gt 2048) { $stderrText.Substring(0, 2048) } else { $stderrText }
    windowsVersion = $windowsVersion
    windowsTerminalVersion = $wtVersion
    queryUser = $queryUser
    qwinsta = $qwinsta
  } | ConvertTo-Json -Depth 4 |
    Set-Content -Path (Join-Path $Out "conpty-acceptance.json") -Encoding utf8

  Add-Evidence "- ConPTY acceptance manifest: conpty-acceptance.json"
  Add-Evidence "- ConPTY PASS token present: $passTokenPresent"
  Add-Evidence "- ConPTY accepted: $accepted"
}

function Write-DcsMetrics {
  param(
    [string]$Path,
    [string]$Name
  )
  if (-not (Test-Path $Path)) {
    return
  }
  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $count = 0
  $metrics = New-Object System.Collections.Generic.List[string]
  for ($i = 0; $i -lt ($bytes.Length - 1); $i++) {
    if ($bytes[$i] -eq 27 -and $bytes[$i + 1] -eq 80) {
      $count++
      $start = $i + 2
      $end = $start
      while ($end -lt ($bytes.Length - 1)) {
        if ($bytes[$end] -eq 27 -and $bytes[$end + 1] -eq 92) {
          break
        }
        $end++
      }
      $len = $end - $start
      if ($len -gt 0) {
        $take = [Math]::Min($len, 4096)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes, $start, $take)
        $m = [regex]::Match($ascii, '"1;1;([0-9]+);([0-9]+)')
        if ($m.Success) {
          $metrics.Add("dcs[$count] raster=$($m.Groups[1].Value)x$($m.Groups[2].Value) bytes=$len")
        } else {
          $metrics.Add("dcs[$count] raster=not-found bytes=$len")
        }
      }
    }
  }
  Set-Content -Path (Join-Path $Out "${Name}-dcs-count.txt") -Value $count -Encoding ascii
  if ($Name -eq "timui-sixel") {
    Set-Content -Path (Join-Path $Out "sixel-dcs-count.txt") -Value $count -Encoding ascii
  }
  if ($metrics.Count -gt 0) {
    Set-Content -Path (Join-Path $Out "${Name}-dcs-metrics.txt") -Value $metrics -Encoding ascii
  }
}

function Save-ScreenCapture {
  param([string]$FileName)

  $screenshot = Join-Path $Out $FileName
  try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    "bounds=$($bounds.Width)x$($bounds.Height)+$($bounds.X)+$($bounds.Y)" |
      Set-Content -Path (Join-Path $Out "${FileName}.bounds.txt") -Encoding ascii
    if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) {
      $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
      $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
      $graphics.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bitmap.Size)
      $bitmap.Save($screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
      $graphics.Dispose()
      $bitmap.Dispose()
      Set-Content -Path (Join-Path $Out "${FileName}.status") -Value 0 -Encoding ascii
      Add-Evidence "- screen capture ${FileName}: wrote PNG"
    } else {
      Set-Content -Path (Join-Path $Out "${FileName}.status") -Value 2 -Encoding ascii
      Add-Evidence "- screen capture ${FileName}: skipped, virtual screen bounds were empty"
    }
  } catch {
    $_ | Out-File -FilePath (Join-Path $Out "${FileName}.stderr") -Encoding utf8
    Set-Content -Path (Join-Path $Out "${FileName}.status") -Value 1 -Encoding ascii
    Add-Evidence "- screen capture ${FileName}: failed"
  }
}

Add-Evidence ""
Add-Evidence "## Metadata"
$commit = (& git rev-parse HEAD 2>$null)
if (-not $commit) {
  $commit = "unknown"
}
Add-Evidence "- Commit: $commit"
Add-Evidence "- Date UTC: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
Add-Evidence "- Runner OS: $($env:RUNNER_OS)"
Add-Evidence "- Frames: $Frames"
Add-Evidence "- GUI frames: $GuiFrames"
Add-Evidence "- OS env: $($env:OS)"
Add-Evidence "- Shell: PowerShell $($PSVersionTable.PSVersion)"
Add-Evidence "- Windows build path: MSYS2 make is used here because the flake has Linux/Darwin systems only."
Add-Evidence "- Image smoke compiler: MSYS /usr/bin/gcc for POSIX headers."
Add-Evidence "- ConPTY compiler: UCRT64 /ucrt64/bin/gcc for native Win32 APIs."
Add-Evidence ""

cmd.exe /c ver > (Join-Path $Out "windows-version.txt") 2>&1
cmd.exe /c "query user || ver" > (Join-Path $Out "query-user.txt") 2>&1
cmd.exe /c "qwinsta || ver" > (Join-Path $Out "qwinsta.txt") 2>&1
Get-Command wt.exe -ErrorAction SilentlyContinue |
  Format-List * > (Join-Path $Out "wt-command.txt") 2>&1
Get-AppxPackage -Name Microsoft.WindowsTerminal* |
  Format-List * > (Join-Path $Out "wt-appx-package.txt") 2>&1
Get-ChildItem -Path "$env:LOCALAPPDATA\Packages" -Filter "Microsoft.WindowsTerminal*" -ErrorAction SilentlyContinue |
  Format-List * > (Join-Path $Out "wt-package-dirs.txt") 2>&1

$Msys2Location = $env:TIMUI_MSYS2_LOCATION
if (-not $Msys2Location) {
  $Msys2Location = "C:\msys64"
}
$MsysUsrBin = Join-Path $Msys2Location "usr\bin"
$MsysUcrtBin = Join-Path $Msys2Location "ucrt64\bin"

$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
$env:PATH = "$MsysUcrtBin;$MsysUsrBin;$env:PATH"

$Bash = @(
  (Join-Path $MsysUsrBin "bash.exe"),
  (Join-Path $MsysUcrtBin "bash.exe"),
  "C:\msys64\usr\bin\bash.exe",
  "C:\msys64\ucrt64\bin\bash.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $Bash) {
  Add-Evidence "- MSYS2 bash: unavailable"
  Add-Evidence ""
  Add-Evidence "## Outcome"
  Add-Evidence "- Accepted: no; MSYS2 bash was unavailable."
  exit 0
}

function Invoke-BashLastLine {
  param([string]$Command)
  $lines = & $Bash --noprofile --norc -lc $Command 2>> (Join-Path $Out "bash-bootstrap.stderr")
  $last = $lines | Where-Object { $_ -and $_.Trim().Length -gt 0 } | Select-Object -Last 1
  if ($last) {
    return $last.Trim()
  }
  return ""
}

$MsysRoot = Invoke-BashLastLine "cygpath -u '$Root'"
$MsysOut = Invoke-BashLastLine "cygpath -u '$Out'"
Add-Evidence "- MSYS2 location: $Msys2Location"
Add-Evidence "- MSYS2 bash: $Bash"
Add-Evidence "- MSYS2 root: $MsysRoot"
Add-Evidence ""

& $Bash --noprofile --norc -lc "command -v gcc; command -v /ucrt64/bin/gcc; ls -l /usr/bin/gcc /ucrt64/bin/gcc 2>/dev/null || true" `
  > (Join-Path $Out "msys2-toolchain.txt") 2>&1

function Invoke-Msys {
  param(
    [string]$Name,
    [string]$Command
  )
  return (Invoke-Captured $Name { & $Bash --noprofile --norc -lc $Command })
}

Add-Evidence "## Build and stream diagnostics"
$ImageSmokeCommand = "cd '$MsysRoot' && PATH=/usr/bin:/bin:`$PATH make build/image_smoke CC=/usr/bin/gcc POSIX_CFLAGS='-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700'"
$ConptyCommand = "cd '$MsysRoot' && OS=Windows_NT PATH=/ucrt64/bin:/usr/bin:/bin:`$PATH make smoke-conpty-win32 CONPTY_WIN_CC=/ucrt64/bin/gcc"
$SixelDiagnosticCommand = "cd '$MsysRoot' && mkdir -p '$MsysOut' && if [ -x ./build/image_smoke ] && command -v script >/dev/null 2>&1; then script -q -c './build/image_smoke --protocol sixel --frames 1' '$MsysOut/sixel.typescript'; elif [ ! -x ./build/image_smoke ]; then echo image-smoke-missing > '$MsysOut/sixel-diagnostic.skip'; else echo script-not-available > '$MsysOut/sixel-diagnostic.skip'; fi"
$ImageSmokeStatus = Invoke-Msys "image-smoke-build" $ImageSmokeCommand
$ConptyStatus = Invoke-Msys "conpty-smoke" $ConptyCommand
Write-ConptyAcceptance -Status $ConptyStatus -Command $ConptyCommand -Compiler "/ucrt64/bin/gcc"
$SixelDiagnosticStatus = Invoke-Msys "sixel-diagnostic" $SixelDiagnosticCommand
Write-DcsMetrics (Join-Path $Out "sixel.typescript") "timui-sixel"

$DirectFixture = Join-Path $Out "direct-sixel-fixture.sh"
$DirectFixtureLines = @(
  '#!/usr/bin/env bash',
  'set -euo pipefail',
  'sleep_s="${1:-0}"',
  "printf '\033[2J\033[H'",
  "printf 'TIMUI_DIRECT_SIXEL_FIXTURE\n\n'",
  "printf 'If Windows Terminal renders Sixel, a red block should appear below.\n'",
  "printf 'This fixture bypasses timui and writes one known-good Sixel DCS.\n'",
  "printf '\033[7;5H'",
  "printf '\033P0;1;0q`"1;1;180;72#1;2;100;0;0#1'",
  'for b in $(seq 1 12); do',
  "  printf '!180~'",
  '  if [ "$b" -lt 12 ]; then printf "-"; fi',
  'done',
  "printf '\033\\'",
  'if [ "$sleep_s" -gt 0 ]; then sleep "$sleep_s"; fi'
)
[System.IO.File]::WriteAllText($DirectFixture, ($DirectFixtureLines -join "`n") + "`n", [System.Text.Encoding]::ASCII)
$DirectSixelStatus = Invoke-Msys "direct-sixel-diagnostic" "bash '$MsysOut/direct-sixel-fixture.sh' 0 > '$MsysOut/direct.sixel'"
Write-DcsMetrics (Join-Path $Out "direct.sixel") "direct-sixel"

Add-Evidence ""
Add-Evidence "## Windows screenshot sanity"
$SanityCmd = Join-Path $Out "capture-sanity.cmd"
$SanityLines = @(
  "@echo off",
  "title timui-hosted-capture-sanity",
  "mode con: cols=100 lines=30",
  "cls",
  "echo TIMUI_HOSTED_SCREENSHOT_SANITY",
  "echo.",
  "for /L %%I in (0,1,13) do echo visible console capture sanity line %%I",
  "%SystemRoot%\System32\timeout.exe /t 45 /nobreak >nul"
)
[System.IO.File]::WriteAllText($SanityCmd, ($SanityLines -join "`r`n") + "`r`n", [System.Text.Encoding]::ASCII)
try {
  Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$SanityCmd`"") -WindowStyle Normal
  Add-Evidence "- cmd.exe launch sanity: started"
} catch {
  $_ | Out-File -FilePath (Join-Path $Out "cmd-sanity-launch.stderr") -Encoding utf8
  Add-Evidence "- cmd.exe launch sanity: failed"
}
Start-Sleep -Seconds 8
Save-ScreenCapture "cmd-sanity.png"

Add-Evidence ""
Add-Evidence "## Windows Terminal GUI attempt"
$Wt = Get-Command wt.exe -ErrorAction SilentlyContinue
if ($Wt) {
  $wtVersion = Join-Path $Out "wt-version.stdout"
  $Wt.Source | Set-Content -Path $wtVersion -Encoding utf8
  Get-Item $Wt.Source | Format-List * | Out-File -FilePath $wtVersion -Append -Encoding utf8
  Set-Content -Path (Join-Path $Out "wt-version.status") -Value 0 -Encoding ascii
  Add-Evidence "- wt-version: captured file metadata"
  Invoke-Captured "wt-version-command" { & $Wt.Source --version } | Out-Null
  Write-ConptyAcceptance -Status $ConptyStatus -Command $ConptyCommand -Compiler "/ucrt64/bin/gcc"

  Add-Evidence ""
  Add-Evidence "### Direct Sixel control"
  $DirectCmd = Join-Path $Out "run-direct-sixel.cmd"
  $DirectLines = @(
    "@echo off",
    "title timui-direct-sixel-fixture",
    "mode con: cols=120 lines=40",
    "set MSYSTEM=UCRT64",
    "set CHERE_INVOKING=1",
    "set PATH=$MsysUsrBin;$MsysUcrtBin;%PATH%",
    "`"$Bash`" --noprofile --norc -lc `"bash '$MsysOut/direct-sixel-fixture.sh' 45`"",
    "%SystemRoot%\System32\timeout.exe /t 10 /nobreak >nul"
  )
  [System.IO.File]::WriteAllText($DirectCmd, ($DirectLines -join "`r`n") + "`r`n", [System.Text.Encoding]::ASCII)

  try {
    $directWtArgs = @(
      "--window",
      "new",
      "--size",
      "120,40",
      "new-tab",
      "--title",
      "timui-direct-sixel-fixture",
      "cmd.exe",
      "/k",
      $DirectCmd
    )
    Start-Process -FilePath $Wt.Source -ArgumentList $directWtArgs -WindowStyle Normal
    Add-Evidence "- Direct Sixel Windows Terminal launch: started"
  } catch {
    $_ | Out-File -FilePath (Join-Path $Out "wt-direct-launch.stderr") -Encoding utf8
    Add-Evidence "- Direct Sixel Windows Terminal launch: failed"
  }
  Start-Sleep -Seconds 12
  Get-Process WindowsTerminal,OpenConsole,cmd,bash -ErrorAction SilentlyContinue |
    Format-List * > (Join-Path $Out "terminal-processes-direct-12s.txt") 2>&1
  Save-ScreenCapture "windows-terminal-direct-sixel-12s.png"
  Stop-Process -Name WindowsTerminal,OpenConsole,cmd,bash -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 3

  Add-Evidence ""
  Add-Evidence "### timui Sixel smoke"
  $RunCmd = Join-Path $Out "run-sixel-smoke.cmd"
  $RunLines = @(
    "@echo off",
    "title timui-sixel-smoke",
    "mode con: cols=120 lines=40",
    "set MSYSTEM=UCRT64",
    "set CHERE_INVOKING=1",
    "set PATH=$MsysUsrBin;$MsysUcrtBin;%PATH%",
    "`"$Bash`" --noprofile --norc -lc `"cd '$MsysRoot' && export TERM=xterm-256color && printf '\033[8;30;100t' && ./build/image_smoke --protocol sixel --frames $GuiFrames; sleep 8`"",
    "%SystemRoot%\System32\timeout.exe /t 10 /nobreak >nul"
  )
  [System.IO.File]::WriteAllText($RunCmd, ($RunLines -join "`r`n") + "`r`n", [System.Text.Encoding]::ASCII)

  try {
    $wtArgs = @(
      "--window",
      "new",
      "--size",
      "120,40",
      "new-tab",
      "--title",
      "timui-sixel-smoke",
      "cmd.exe",
      "/k",
      $RunCmd
    )
    Start-Process -FilePath $Wt.Source -ArgumentList $wtArgs -WindowStyle Normal
    Add-Evidence "- Windows Terminal launch: started"
  } catch {
    $_ | Out-File -FilePath (Join-Path $Out "wt-launch.stderr") -Encoding utf8
    Add-Evidence "- Windows Terminal launch: failed"
  }

  Start-Sleep -Seconds 12
  Get-Process WindowsTerminal,OpenConsole,cmd,bash -ErrorAction SilentlyContinue |
    Format-List * > (Join-Path $Out "terminal-processes-12s.txt") 2>&1
  Save-ScreenCapture "windows-terminal-sixel-12s.png"
} else {
  Add-Evidence "- Windows Terminal: wt.exe unavailable"
}

Add-Evidence ""
Add-Evidence "## Outcome"
Add-Evidence "- ConPTY smoke counts only if conpty-smoke.stdout contains PASS conpty smoke: observed TIMUI_CONPTY_SMOKE."
Add-Evidence "- cmd-sanity.png only proves hosted Windows screenshot mechanics. It is not image-protocol evidence."
Add-Evidence "- The direct Sixel screenshot is a control: it proves whether hosted Windows Terminal renders Sixel at all, independent of timui."
Add-Evidence "- Sixel visual evidence counts only if a windows-terminal-sixel-*.png visibly shows the live image smoke in Windows Terminal with visible image tiles, not placeholders."
Add-Evidence "- The sixel typescript, direct.sixel, DCS counts, and DCS raster metrics are diagnostics only; they do not replace a visual screenshot or recording."

exit 0
