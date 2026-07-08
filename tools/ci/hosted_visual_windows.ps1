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
Get-Command wt.exe -ErrorAction SilentlyContinue |
  Format-List * > (Join-Path $Out "wt-command.txt") 2>&1

$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"

$Bash = @(
  "C:\msys64\usr\bin\bash.exe",
  "C:\msys64\ucrt64\bin\bash.exe",
  "C:\msys64\mingw64\bin\bash.exe"
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
Add-Evidence "- MSYS2 bash: $Bash"
Add-Evidence "- MSYS2 root: $MsysRoot"
Add-Evidence ""

function Invoke-Msys {
  param(
    [string]$Name,
    [string]$Command
  )
  Invoke-Captured $Name { & $Bash --noprofile --norc -lc $Command } | Out-Null
}

Add-Evidence "## Build and stream diagnostics"
Invoke-Msys "image-smoke-build" "cd '$MsysRoot' && make build/image_smoke CC=/usr/bin/gcc"
Invoke-Msys "conpty-smoke" "cd '$MsysRoot' && OS=Windows_NT make smoke-conpty-win32 CONPTY_WIN_CC=/ucrt64/bin/gcc"
Invoke-Msys "sixel-diagnostic" "cd '$MsysRoot' && mkdir -p '$MsysOut' && if command -v script >/dev/null 2>&1; then script -q -c './build/image_smoke --protocol sixel --frames 1' '$MsysOut/sixel.typescript'; else echo script-not-available > '$MsysOut/sixel-diagnostic.skip'; fi"
Count-EscP (Join-Path $Out "sixel.typescript")

Add-Evidence ""
Add-Evidence "## Windows Terminal GUI attempt"
$Wt = Get-Command wt.exe -ErrorAction SilentlyContinue
if ($Wt) {
  $wtVersion = Join-Path $Out "wt-version.stdout"
  $Wt.Source | Set-Content -Path $wtVersion -Encoding utf8
  Get-Item $Wt.Source | Format-List * | Out-File -FilePath $wtVersion -Append -Encoding utf8
  Set-Content -Path (Join-Path $Out "wt-version.status") -Value 0 -Encoding ascii
  Add-Evidence "- wt-version: captured file metadata"
  $RunScript = Join-Path $Out "run-sixel-smoke.sh"
  $RunLines = @(
    "#!/usr/bin/env bash",
    "cd '$MsysRoot' || exit 1",
    "export TERM=xterm-256color",
    "printf '\033[8;30;100t'",
    "./build/image_smoke --protocol sixel --frames $GuiFrames",
    "sleep 8"
  )
  [System.IO.File]::WriteAllText($RunScript, ($RunLines -join "`n") + "`n", [System.Text.Encoding]::ASCII)
  $MsysRunScript = Invoke-BashLastLine "cygpath -u '$RunScript'"

  try {
    $wtArgs = @(
      "new-tab",
      "--title",
      "timui-sixel-smoke",
      $Bash,
      "--noprofile",
      "--norc",
      "-lc",
      "`"bash '$MsysRunScript'`""
    )
    Start-Process -FilePath $Wt.Source -ArgumentList $wtArgs
    Add-Evidence "- Windows Terminal launch: started"
  } catch {
    $_ | Out-File -FilePath (Join-Path $Out "wt-launch.stderr") -Encoding utf8
    Add-Evidence "- Windows Terminal launch: failed"
  }

  Start-Sleep -Seconds 12
  $Screenshot = Join-Path $Out "windows-terminal-sixel.png"
  try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    "bounds=$($bounds.Width)x$($bounds.Height)+$($bounds.X)+$($bounds.Y)" |
      Set-Content -Path (Join-Path $Out "screen-bounds.txt") -Encoding ascii
    if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) {
      $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
      $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
      $graphics.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bounds.Size)
      $bitmap.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
      $graphics.Dispose()
      $bitmap.Dispose()
      Add-Evidence "- screen capture: wrote windows-terminal-sixel.png"
    } else {
      Add-Evidence "- screen capture: skipped, primary screen bounds were empty"
    }
  } catch {
    $_ | Out-File -FilePath (Join-Path $Out "screen-capture.stderr") -Encoding utf8
    Add-Evidence "- screen capture: failed"
  }
} else {
  Add-Evidence "- Windows Terminal: wt.exe unavailable"
}

Add-Evidence ""
Add-Evidence "## Outcome"
Add-Evidence "- ConPTY smoke counts only if conpty-smoke.stdout contains PASS conpty smoke: observed TIMUI_CONPTY_SMOKE."
Add-Evidence "- Sixel visual evidence counts only if windows-terminal-sixel.png visibly shows the live image smoke in Windows Terminal with visible image tiles, not placeholders."
Add-Evidence "- The sixel typescript and DCS count are diagnostics only; they do not replace a visual screenshot or recording."

exit 0
