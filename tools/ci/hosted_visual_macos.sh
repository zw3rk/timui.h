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
  printf '\ngui bootstrap:\n'
  launchctl print "gui/$(id -u)" >/dev/null && printf 'yes\n' || printf 'no\n'
  printf '\ntcc screen capture grants:\n'
  sudo -n sqlite3 "/Library/Application Support/com.apple.TCC/TCC.db" \
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
  gui_runner="${out}/run-iterm2-smoke.sh"
  cat > "${gui_runner}" <<EOF
#!/usr/bin/env bash
cd '${root}' || exit 1
printf '\033[8;30;100t'
./build/image_smoke --protocol iterm2 --frames ${gui_frames}
status=\$?
sleep 8
exit \${status}
EOF
  chmod +x "${gui_runner}"

  applescript="${out}/open-iterm2.applescript"
  cat > "${applescript}" <<'EOF'
on run argv
  set scriptPath to item 1 of argv
  set launchCommand to "/bin/bash " & quoted form of scriptPath
  tell application id "com.googlecode.iterm2"
    activate
    set timuiWindow to (create window with default profile command launchCommand)
  end tell
end run
EOF

  osascript "${applescript}" "${gui_runner}" \
    > "${out}/osascript.stdout" \
    2> "${out}/osascript.stderr"
  status=$?
  printf '%s\n' "${status}" > "${out}/osascript.status"
  note "- osascript iTerm2 launch: exit ${status}"

  sleep 12
  if [ -x /usr/sbin/screencapture ]; then
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
