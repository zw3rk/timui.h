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

function Count-EscP {
  param([string]$Path)
  if (-not (Test-Path $Path)) {
    return
  }
  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $count = 0
  for ($i = 0; $i -lt ($bytes.Length - 1); $i++) {
    if ($bytes[$i] -eq 27 -and $bytes[$i + 1] -eq 80) {
      $count++
    }
  }
  Set-Content -Path (Join-Path $Out "sixel-dcs-count.txt") -Value $count -Encoding ascii
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
  Invoke-Captured $Name { & $Bash --noprofile --norc -lc $Command } | Out-Null
}

Add-Evidence "## Build and stream diagnostics"
Invoke-Msys "image-smoke-build" "cd '$MsysRoot' && PATH=/usr/bin:/bin:`$PATH make build/image_smoke CC=/usr/bin/gcc POSIX_CFLAGS='-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700'"
Invoke-Msys "conpty-smoke" "cd '$MsysRoot' && OS=Windows_NT PATH=/ucrt64/bin:/usr/bin:/bin:`$PATH make smoke-conpty-win32 CONPTY_WIN_CC=/ucrt64/bin/gcc"
Invoke-Msys "sixel-diagnostic" "cd '$MsysRoot' && mkdir -p '$MsysOut' && if [ -x ./build/image_smoke ] && command -v script >/dev/null 2>&1; then script -q -c './build/image_smoke --protocol sixel --frames 1' '$MsysOut/sixel.typescript'; elif [ ! -x ./build/image_smoke ]; then echo image-smoke-missing > '$MsysOut/sixel-diagnostic.skip'; else echo script-not-available > '$MsysOut/sixel-diagnostic.skip'; fi"
Count-EscP (Join-Path $Out "sixel.typescript")

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
  Start-Sleep -Seconds 12
  Get-Process WindowsTerminal,OpenConsole,cmd,bash -ErrorAction SilentlyContinue |
    Format-List * > (Join-Path $Out "terminal-processes-24s.txt") 2>&1
  Save-ScreenCapture "windows-terminal-sixel-24s.png"
} else {
  Add-Evidence "- Windows Terminal: wt.exe unavailable"
}

Add-Evidence ""
Add-Evidence "## Outcome"
Add-Evidence "- ConPTY smoke counts only if conpty-smoke.stdout contains PASS conpty smoke: observed TIMUI_CONPTY_SMOKE."
Add-Evidence "- cmd-sanity.png only proves hosted Windows screenshot mechanics. It is not image-protocol evidence."
Add-Evidence "- Sixel visual evidence counts only if a windows-terminal-sixel-*.png visibly shows the live image smoke in Windows Terminal with visible image tiles, not placeholders."
Add-Evidence "- The sixel typescript and DCS count are diagnostics only; they do not replace a visual screenshot or recording."

exit 0
