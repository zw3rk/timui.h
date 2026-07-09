#!/usr/bin/env bash
set -u

frames="${1:-120}"
case "${frames}" in
  ""|*[!0-9]*) frames=120 ;;
esac
if [ "${frames}" -lt 1 ]; then
  frames=120
fi
gui_frames="${frames}"
if [ "${gui_frames}" -lt 900 ]; then
  gui_frames=900
fi

root="$(pwd)"
out="${root}/artifacts/hosted-visual/macos"
evidence="${out}/evidence.md"
mkdir -p "${out}"
: > "${evidence}"

note() {
  printf '%s\n' "$*" >> "${evidence}"
}

capture() {
  name="$1"
  shift
  "$@" > "${out}/${name}.stdout" 2> "${out}/${name}.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/${name}.status"
  note "- ${name}: exit ${status}"
  return "${status}"
}

note "# macOS Hosted Visual Probe"
note ""
note "## Metadata"
note "- Commit: $(git rev-parse HEAD 2>/dev/null || printf unknown)"
note "- Date UTC: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
note "- Runner OS: ${RUNNER_OS:-unknown}"
note "- Frames: ${frames}"
note "- GUI frames: ${gui_frames}"
note "- TERM: ${TERM:-unset}"
note "- TERM_PROGRAM: ${TERM_PROGRAM:-unset}"
note "- Nix action outcome: ${TIMUI_HOSTED_NIX_STATUS:-unknown}"
note ""

{
  printf 'sw_vers:\n'
  sw_vers || true
  printf '\nuname:\n'
  uname -a || true
  printf '\nwhoami:\n'
  whoami || true
  printf '\nconsole user:\n'
  stat -f 'user=%Su uid=%u' /dev/console || true
  printf '\nwindowserver:\n'
  pgrep -lf WindowServer || true
  printf '\niterm2 app:\n'
  ls -ld /Applications/iTerm.app /Applications/iTerm2.app 2>/dev/null || true
  printf '\nbrew:\n'
  command -v brew || true
  printf '\nopen:\n'
  command -v open || true
  printf '\nscreencapture:\n'
  command -v screencapture || true
  printf '\nprocess:\n'
  ps -p $$ -o pid,ppid,comm,args || true
  printf '\nbash:\n'
  command -v bash || true
  /bin/bash --version 2>/dev/null | head -1 || true
  printf '\ngui bootstrap:\n'
  launchctl print "gui/$(id -u)" >/dev/null && printf 'yes\n' || printf 'no\n'
  printf '\ntcc system screen capture grants:\n'
  sudo -n sqlite3 "/Library/Application Support/com.apple.TCC/TCC.db" \
    "SELECT service,client,client_type,auth_value,auth_reason FROM access WHERE service='kTCCServiceScreenCapture';" \
    || true
  printf '\ntcc user screen capture grants:\n'
  sqlite3 "${HOME}/Library/Application Support/com.apple.TCC/TCC.db" \
    "SELECT service,client,client_type,auth_value,auth_reason FROM access WHERE service='kTCCServiceScreenCapture';" \
    || true
} > "${out}/metadata.txt" 2>&1

note "## Build and stream diagnostics"
if command -v nix >/dev/null 2>&1 && [ "${TIMUI_HOSTED_NIX_STATUS:-success}" != "failure" ]; then
  note "- Build path: nix develop -c make"
  capture image-smoke-build nix develop -c make build/image_smoke || true
else
  note "- Build path: native make fallback (Nix unavailable or install failed)"
  capture image-smoke-build make build/image_smoke CC="${CC:-cc}" || true
fi

diag_runner="${out}/run-iterm2-diagnostic.sh"
cat > "${diag_runner}" <<EOF
#!/usr/bin/env bash
cd '${root}' || exit 1
./build/image_smoke --protocol iterm2 --frames 1
EOF
chmod +x "${diag_runner}"

if command -v script >/dev/null 2>&1; then
  script -q "${out}/iterm2.typescript" "${diag_runner}" \
    > "${out}/iterm2-diagnostic.stdout" \
    2> "${out}/iterm2-diagnostic.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/iterm2-diagnostic.status"
  note "- iterm2-diagnostic: exit ${status}"
else
  note "- iterm2-diagnostic: skipped, script(1) unavailable"
  printf 'script(1) unavailable\n' > "${out}/iterm2-diagnostic.skip"
fi

if [ -f "${out}/iterm2.typescript" ]; then
  grep -ao '1337;File=' "${out}/iterm2.typescript" | wc -l | tr -d ' ' \
    > "${out}/osc1337-count.txt" || true
fi

note ""
note "## macOS screenshot sanity"
terminal_runner="${out}/run-terminal-sanity.command"
cat > "${terminal_runner}" <<'EOF'
#!/usr/bin/env bash
printf '\033[8;24;100t'
clear
printf '\nTIMUI_HOSTED_SCREENSHOT_SANITY\n\n'
i=0
while [ "${i}" -lt 14 ]; do
  printf 'visible terminal capture sanity line %02d\n' "${i}"
  i=$((i + 1))
done
sleep 45
EOF
chmod +x "${terminal_runner}"

if command -v open >/dev/null 2>&1; then
  open -na Terminal "${terminal_runner}" \
    > "${out}/open-terminal-sanity.stdout" \
    2> "${out}/open-terminal-sanity.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/open-terminal-sanity.status"
  note "- Terminal.app launch sanity: exit ${status}"
  sleep 8
  if [ -x /usr/sbin/screencapture ]; then
    /usr/sbin/screencapture -x -D 1 "${out}/terminal-sanity.png" \
      > "${out}/terminal-sanity-screencapture.stdout" \
      2> "${out}/terminal-sanity-screencapture.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/terminal-sanity-screencapture.status"
    note "- Terminal.app screenshot sanity: exit ${status}"
    if [ -f "${out}/terminal-sanity.png" ]; then
      sips -g pixelWidth -g pixelHeight "${out}/terminal-sanity.png" \
        > "${out}/terminal-sanity.info" 2>&1 || true
    fi
  fi
else
  note "- Terminal.app launch sanity: skipped, open(1) unavailable"
fi

note ""
note "## iTerm2 GUI attempt"
iterm_app="/Applications/iTerm.app"
if [ ! -d "${iterm_app}" ] && [ -d "/Applications/iTerm2.app" ]; then
  iterm_app="/Applications/iTerm2.app"
fi

if [ ! -d "${iterm_app}" ] && command -v brew >/dev/null 2>&1; then
  capture brew-install-iterm2 brew install --cask iterm2 || true
fi

if [ ! -d "${iterm_app}" ] && [ -d "/Applications/iTerm2.app" ]; then
  iterm_app="/Applications/iTerm2.app"
fi

if [ -d "${iterm_app}" ]; then
  note "- iTerm2 app: ${iterm_app}"
  console_uid="$(stat -f '%u' /dev/console 2>/dev/null || id -u)"
  lsregister="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
  {
    printf 'app: %s\n\n' "${iterm_app}"
    printf 'quarantine before:\n'
    xattr -p com.apple.quarantine "${iterm_app}" 2>&1 || true
    if [ -x "${iterm_app}/Contents/MacOS/iTerm2" ]; then
      printf '\nexecutable quarantine before:\n'
      xattr -p com.apple.quarantine "${iterm_app}/Contents/MacOS/iTerm2" 2>&1 || true
    fi
    printf '\nrecursive quarantine removal:\n'
    xattr -dr com.apple.quarantine "${iterm_app}" 2>&1 || true
    printf '\nquarantine after:\n'
    xattr -p com.apple.quarantine "${iterm_app}" 2>&1 || true
    if [ -x "${iterm_app}/Contents/MacOS/iTerm2" ]; then
      printf '\nexecutable quarantine after:\n'
      xattr -p com.apple.quarantine "${iterm_app}/Contents/MacOS/iTerm2" 2>&1 || true
    fi
    printf '\nspctl assessment:\n'
    spctl --assess --verbose "${iterm_app}" 2>&1 || true
    printf '\nlsregister -f:\n'
    if [ -x "${lsregister}" ]; then
      "${lsregister}" -f "${iterm_app}" || true
    fi
  } > "${out}/iterm2-first-launch.stdout" 2> "${out}/iterm2-first-launch.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/iterm2-first-launch.status"
  note "- iTerm2 first-launch remediation: exit ${status}"

  {
    printf 'app: %s\n\n' "${iterm_app}"
    if [ -f "${iterm_app}/Contents/Info.plist" ]; then
      printf 'Info.plist selected keys:\n'
      plutil -p "${iterm_app}/Contents/Info.plist" \
        | grep -E 'CFBundleIdentifier|CFBundleName|CFBundleExecutable|CFBundleShortVersionString|CFBundleVersion' \
        || true
    fi
    printf '\nmdls bundle id:\n'
    mdls -name kMDItemCFBundleIdentifier "${iterm_app}" || true
    printf '\nAppleScript ids:\n'
    osascript -e 'id of application "iTerm"' || true
    osascript -e 'id of application "iTerm2"' || true
    osascript -e 'application id "com.googlecode.iterm2" is running' || true
    printf '\nlsregister:\n'
    if [ -x "${lsregister}" ]; then
      "${lsregister}" -f "${iterm_app}" || true
    fi
  } > "${out}/iterm2-app.txt" 2>&1

  gui_runner="${out}/run-iterm2-smoke.sh"
  cat > "${gui_runner}" <<EOF
#!/usr/bin/env bash
cd '${root}' || exit 1
./build/image_smoke --protocol iterm2 --frames ${gui_frames}
status=\$?
sleep 8
exit \${status}
EOF
  chmod +x "${gui_runner}"

  note ""
  note "## iTerm2 Python API screenshot attempt"
  capture iterm2-defaults-enable-api defaults write com.googlecode.iterm2 EnableAPIServer -bool true || true
  capture iterm2-defaults-allow-inline-display defaults write com.googlecode.iterm2 NoSyncSuppressDownloadConfirmation -bool true || true
  capture iterm2-defaults-allow-inline-display-selection defaults write com.googlecode.iterm2 NoSyncSuppressDownloadConfirmation_selection -int 0 || true
  capture iterm2-defaults-allow-big-download defaults write com.googlecode.iterm2 NoSyncAllowBigDownload -bool true || true
  capture iterm2-defaults-allow-big-download-selection defaults write com.googlecode.iterm2 NoSyncAllowBigDownload_selection -int 0 || true

  {
    noauth="${HOME}/Library/Application Support/iTerm2/disable-automation-auth"
    mkdir -p "$(dirname "${noauth}")"
    hex="$(/usr/bin/python3 - "${noauth}" <<'PY'
import pathlib
import sys
print(str(pathlib.Path(sys.argv[1]).expanduser()).encode("utf-8").hex())
PY
)"
    printf '%s %s' "${hex}" "61DF88DC-3423-4823-B725-22570E01C027" | sudo tee "${noauth}" >/dev/null
    sudo chown root:wheel "${noauth}"
    sudo chmod 0644 "${noauth}"
    ls -l "${noauth}"
    stat -f 'owner=%u size=%z' "${noauth}"
  } > "${out}/iterm2-api-auth.stdout" 2> "${out}/iterm2-api-auth.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/iterm2-api-auth.status"
  note "- iTerm2 API no-auth marker: exit ${status}"

  api_venv="${RUNNER_TEMP:-/tmp}/timui-iterm2-api-venv"
  rm -rf "${api_venv}"
  capture iterm2-api-venv python3 -m venv "${api_venv}" || true
  api_python="${api_venv}/bin/python3"
  if [ -x "${api_python}" ]; then
    capture iterm2-api-pip "${api_python}" -m pip install --upgrade pip iterm2 || true
    capture iterm2-api-pyobjc "${api_python}" -m pip install --upgrade pyobjc-framework-Cocoa || true
    api_src="${RUNNER_TEMP:-/tmp}/timui-iterm2-api-src"
    {
      rm -rf "${api_src}"
      git clone --depth=1 --filter=blob:none --sparse \
        https://github.com/gnachman/iTerm2.git "${api_src}"
      (
        cd "${api_src}" || exit 1
        git sparse-checkout set api/library/python/iterm2/iterm2
        git rev-parse HEAD
      ) > "${out}/iterm2-api-source-commit.txt"
      site_pkgs="$("${api_python}" - <<'PY'
import site
print(site.getsitepackages()[0])
PY
)"
      rm -rf "${site_pkgs}/iterm2"
      cp -R "${api_src}/api/library/python/iterm2/iterm2" "${site_pkgs}/iterm2"
      "${api_python}" - <<'PY'
import iterm2
print(getattr(iterm2, "__version__", "unknown"))
print("has_async_screenshot=%s" % hasattr(iterm2.Session, "async_screenshot"))
assert hasattr(iterm2.Session, "async_screenshot")
PY
    } > "${out}/iterm2-api-overlay.stdout" 2> "${out}/iterm2-api-overlay.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/iterm2-api-overlay.status"
    note "- iTerm2 API upstream overlay: exit ${status}"
  else
    note "- iTerm2 API pip install: skipped, venv python unavailable"
    printf 'venv python unavailable\n' > "${out}/iterm2-api-pip.skip"
  fi

  {
    launchctl asuser "${console_uid}" /usr/bin/open -b com.googlecode.iterm2 ||
      launchctl asuser "${console_uid}" /usr/bin/open -a iTerm ||
      launchctl asuser "${console_uid}" /usr/bin/open "${iterm_app}" ||
      open -b com.googlecode.iterm2 ||
      open -a iTerm ||
      open "${iterm_app}"
  } > "${out}/iterm2-open.stdout" 2> "${out}/iterm2-open.stderr" &
  open_pid=$!
  open_status=""
  open_waited=0
  while kill -0 "${open_pid}" 2>/dev/null; do
    if [ "${open_waited}" -ge 15 ]; then
      kill "${open_pid}" 2>/dev/null || true
      wait "${open_pid}" 2>/dev/null || true
      open_status=124
      break
    fi
    sleep 1
    open_waited=$((open_waited + 1))
  done
  if [ -z "${open_status}" ]; then
    wait "${open_pid}"
    open_status=$?
  fi
  printf '%s\n' "${open_status}" > "${out}/iterm2-open.status"
  note "- iTerm2 open: exit ${open_status}"
  pgrep -lf 'iTerm|iTerm2' > "${out}/iterm2-processes-after-open.txt" 2>&1 || true
  if [ "${open_status}" != "0" ]; then
    {
      iterm_exe="${iterm_app}/Contents/MacOS/iTerm2"
      if [ ! -x "${iterm_exe}" ]; then
        printf 'missing executable: %s\n' "${iterm_exe}"
        exit 127
      fi
      printf 'launching: %s\n' "${iterm_exe}"
      launchctl asuser "${console_uid}" /usr/bin/nohup "${iterm_exe}" \
        > "${out}/iterm2-direct-launch.process.stdout" \
        2> "${out}/iterm2-direct-launch.process.stderr" &
      direct_pid=$!
      printf '%s\n' "${direct_pid}" > "${out}/iterm2-direct-launch.pid"
      sleep 4
      ps -p "${direct_pid}" -o pid,ppid,comm,args || true
      printf '\nprocesses:\n'
      pgrep -lf 'iTerm|iTerm2' || true
    } > "${out}/iterm2-direct-launch.stdout" 2> "${out}/iterm2-direct-launch.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/iterm2-direct-launch.status"
    note "- iTerm2 direct executable launch: exit ${status}"
  else
    note "- iTerm2 direct executable launch: skipped, open succeeded"
    printf 'open succeeded\n' > "${out}/iterm2-direct-launch.skip"
  fi
  sleep 6

  api_script="${out}/iterm2-api-capture.py"
  cat > "${api_script}" <<'PY'
import hashlib
import json
import os
import pathlib
import signal
import shlex
import struct
import sys
import time

import iterm2


def prelaunch_iterm():
    try:
        import AppKit
        import Foundation
        bundle = "com.googlecode.iterm2"
        running = AppKit.NSRunningApplication.runningApplicationsWithBundleIdentifier_(bundle)
        if running:
            return "already running"
        app_path = os.environ.get("IT2_APP_PATH")
        if app_path:
            url = Foundation.NSURL.fileURLWithPath_(app_path)
            ok = AppKit.NSWorkspace.sharedWorkspace().openURL_(url)
            return "openURL(%s)=%s" % (app_path, ok)
        ok = AppKit.NSWorkspace.sharedWorkspace().launchApplication_("iTerm")
        return "launchApplication(iTerm)=%s" % ok
    except Exception as exc:
        return repr(exc)


def png_size(data):
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    return struct.unpack(">II", data[16:24])


def screen_text(contents):
    return "\n".join(contents.line(i).string
                     for i in range(contents.number_of_lines))


async def main(connection):
    out = pathlib.Path(os.environ["TIMUI_HOSTED_OUT"])
    runner = os.environ["TIMUI_HOSTED_GUI_RUNNER"]
    info = {
        "python": sys.version,
        "iterm2_module_version": getattr(iterm2, "__version__", "unknown"),
        "has_async_screenshot": False,
        "runner": runner,
    }
    info_path = out / "iterm2-api-session.json"
    try:
        app = await iterm2.async_get_app(connection)
        try:
            await app.async_activate()
        except Exception as exc:
            info["app_activate_error"] = repr(exc)

        command = "/bin/bash " + shlex.quote(runner)
        window = await iterm2.Window.async_create(connection, command=command)
        if window is None:
            raise RuntimeError("iTerm2 returned no window")
        info["window_id"] = window.window_id
        try:
            await window.async_activate()
        except Exception as exc:
            info["window_activate_error"] = repr(exc)

        tab = window.current_tab or (window.tabs[0] if window.tabs else None)
        if tab is None:
            raise RuntimeError("iTerm2 window has no tab")
        session = tab.current_session or (tab.sessions[0] if tab.sessions else None)
        if session is None:
            raise RuntimeError("iTerm2 tab has no session")
        info["session_id"] = session.session_id
        try:
            await session.async_activate()
        except Exception as exc:
            info["session_activate_error"] = repr(exc)

        matched = False
        text = ""
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            contents = await session.async_get_screen_contents()
            text = screen_text(contents)
            if "active: iterm2" in text and "plain png" in text:
                matched = True
                break
            await asyncio.sleep(0.25)
        (out / "iterm2-api-session.txt").write_text(text, encoding="utf-8")
        info["screen_text_matched"] = matched
        info["has_async_screenshot"] = hasattr(session, "async_screenshot")
        if not info["has_async_screenshot"]:
            raise RuntimeError("installed iterm2 module lacks Session.async_screenshot")

        png = await session.async_screenshot()
        width, height = png_size(png)
        (out / "iterm2-api-session.png").write_bytes(png)
        info["png_width"] = width
        info["png_height"] = height
        info["png_bytes"] = len(png)
        info["png_sha256"] = hashlib.sha256(png).hexdigest()
        info["accepted_by_script"] = matched and width > 0 and height > 0
    finally:
        info_path.write_text(json.dumps(info, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")


import asyncio

def api_timeout(_signum, _frame):
    raise TimeoutError("timed out waiting for iTerm2 Python API")


signal.signal(signal.SIGALRM, api_timeout)
signal.alarm(int(os.environ.get("TIMUI_HOSTED_API_TIMEOUT", "90")))
try:
    out_dir = pathlib.Path(os.environ.get("TIMUI_HOSTED_OUT", "."))
    (out_dir / "iterm2-api-prelaunch.txt").write_text(prelaunch_iterm() + "\n",
                                                      encoding="utf-8")
    iterm2.run_until_complete(main, True)
finally:
    signal.alarm(0)
PY

  if [ -x "${api_python}" ]; then
    TIMUI_HOSTED_OUT="${out}" \
      TIMUI_HOSTED_GUI_RUNNER="${gui_runner}" \
      TIMUI_HOSTED_API_TIMEOUT=90 \
      IT2_APP_PATH="${iterm_app}" \
      "${api_python}" "${api_script}" \
      > "${out}/iterm2-api-capture.stdout" \
      2> "${out}/iterm2-api-capture.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/iterm2-api-capture.status"
    note "- iTerm2 Python API screenshot: exit ${status}"
    if [ -f "${out}/iterm2-api-session.png" ]; then
      sips -g pixelWidth -g pixelHeight "${out}/iterm2-api-session.png" \
        > "${out}/iterm2-api-session.info" 2>&1 || true
    fi
  else
    note "- iTerm2 Python API screenshot: skipped, venv python unavailable"
    printf 'venv python unavailable\n' > "${out}/iterm2-api-capture.skip"
  fi

  note ""
  note "## iTerm2 OS screenshot diagnostics"

  if command -v swift >/dev/null 2>&1; then
    swift_probe="${out}/dump-iterm2-windows.swift"
    cat > "${swift_probe}" <<'SWIFT'
import CoreGraphics
import Foundation

func intValue(_ value: Any?) -> Int {
    if let number = value as? NSNumber {
        return number.intValue
    }
    return 0
}

let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                         kCGNullWindowID) as? [[String: Any]] ?? []
var emittedEnv = false
for window in windows {
    let owner = window[kCGWindowOwnerName as String] as? String ?? ""
    if !owner.localizedCaseInsensitiveContains("iterm") {
        continue
    }
    let id = intValue(window[kCGWindowNumber as String])
    let bounds = window[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let x = intValue(bounds["X"])
    let y = intValue(bounds["Y"])
    let w = intValue(bounds["Width"])
    let h = intValue(bounds["Height"])
    if !emittedEnv {
        print("WINDOW_ID=\(id)")
        print("WINDOW_RECT=\(x),\(y),\(w),\(h)")
        emittedEnv = true
    }
    print("window id=\(id) owner=\(owner) rect=\(x),\(y),\(w),\(h)")
}
SWIFT
    swift "${swift_probe}" \
      > "${out}/iterm2-window-list.txt" \
      2> "${out}/iterm2-window-list.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/iterm2-window-list.status"
    note "- iTerm2 window list: exit ${status}"
  else
    note "- iTerm2 window list: skipped, swift unavailable"
    printf 'swift unavailable\n' > "${out}/iterm2-window-list.skip"
  fi

  sleep 12
  if [ -x /usr/sbin/screencapture ]; then
    window_id="$(awk -F= '/^WINDOW_ID=/{print $2; exit}' "${out}/iterm2-window-list.txt" 2>/dev/null || true)"
    window_rect="$(awk -F= '/^WINDOW_RECT=/{print $2; exit}' "${out}/iterm2-window-list.txt" 2>/dev/null || true)"
    if [ -n "${window_id}" ]; then
      /usr/sbin/screencapture -x -l "${window_id}" "${out}/iterm2-window-12s.png" \
        > "${out}/screencapture-window-12s.stdout" \
        2> "${out}/screencapture-window-12s.stderr"
      status=$?
      printf '%s\n' "${status}" > "${out}/screencapture-window-12s.status"
      note "- screencapture iTerm2 window after 12s: exit ${status}"
      if [ -f "${out}/iterm2-window-12s.png" ]; then
        sips -g pixelWidth -g pixelHeight "${out}/iterm2-window-12s.png" \
          > "${out}/iterm2-window-12s.info" 2>&1 || true
      fi
    else
      note "- screencapture iTerm2 window after 12s: skipped, no window id"
      printf 'no iTerm2 window id\n' > "${out}/screencapture-window-12s.skip"
    fi

    if [ -n "${window_rect}" ]; then
      /usr/sbin/screencapture -x -R "${window_rect}" "${out}/iterm2-region-12s.png" \
        > "${out}/screencapture-region-12s.stdout" \
        2> "${out}/screencapture-region-12s.stderr"
      status=$?
      printf '%s\n' "${status}" > "${out}/screencapture-region-12s.status"
      note "- screencapture iTerm2 region after 12s: exit ${status}"
      if [ -f "${out}/iterm2-region-12s.png" ]; then
        sips -g pixelWidth -g pixelHeight "${out}/iterm2-region-12s.png" \
          > "${out}/iterm2-region-12s.info" 2>&1 || true
      fi
    else
      note "- screencapture iTerm2 region after 12s: skipped, no window rect"
      printf 'no iTerm2 window rect\n' > "${out}/screencapture-region-12s.skip"
    fi

    /usr/sbin/screencapture -x -D 1 "${out}/iterm2-screen-12s.png" \
      > "${out}/screencapture-12s.stdout" \
      2> "${out}/screencapture-12s.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/screencapture-12s.status"
    note "- screencapture after 12s: exit ${status}"
    if [ -f "${out}/iterm2-screen-12s.png" ]; then
      sips -g pixelWidth -g pixelHeight "${out}/iterm2-screen-12s.png" \
        > "${out}/iterm2-screen-12s.info" 2>&1 || true
    fi

    sleep 12
    /usr/sbin/screencapture -x -D 1 "${out}/iterm2-screen-24s.png" \
      > "${out}/screencapture-24s.stdout" \
      2> "${out}/screencapture-24s.stderr"
    status=$?
    printf '%s\n' "${status}" > "${out}/screencapture-24s.status"
    note "- screencapture after 24s: exit ${status}"
    if [ -f "${out}/iterm2-screen-24s.png" ]; then
      sips -g pixelWidth -g pixelHeight "${out}/iterm2-screen-24s.png" \
        > "${out}/iterm2-screen-24s.info" 2>&1 || true
    fi
  else
    note "- screencapture: skipped, /usr/sbin/screencapture unavailable"
  fi
else
  note "- iTerm2 app: unavailable after brew attempt"
fi

note ""
note "## Outcome"
note "- Accepted: manual inspection required."
note "- terminal-sanity.png only proves hosted macOS screenshot mechanics. It is not image-protocol evidence."
note "- Count this as iTerm2 live visual evidence only if an iterm2-screen-*.png visibly shows the timui image smoke with PNG-backed image tiles, not placeholders."
note "- The typescript and OSC 1337 count are diagnostics only; they do not replace a visual screenshot or recording."

exit 0
