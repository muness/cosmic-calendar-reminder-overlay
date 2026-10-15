#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fakebin="$tmp/fakebin"
mkdir -p "$fakebin"
cat >"$fakebin/systemctl" <<'FAKE'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == --user ]] || exit 91
shift
printf '%q ' "$@" >>"${SYSTEMCTL_LOG:?}"
printf '\n' >>"${SYSTEMCTL_LOG:?}"
verb="$1"; shift
case "$verb" in
  is-enabled)
    [[ "$1" == --quiet ]] && shift
    [[ -f "${FAKE_ENABLED:-}" && "$(cat "$FAKE_ENABLED")" == "$1" ]]
    ;;
  is-active)
    [[ "$1" == --quiet ]] && shift
    if [[ "$1" == graphical-session.target ]]; then
      [[ -f "${FAKE_GRAPHICAL:-}" ]]
      exit
    fi
    [[ -f "${FAKE_ACTIVE:-}" && "$(cat "$FAKE_ACTIVE")" == "$1" ]]
    ;;
  enable) printf '%s' "$1" >"${FAKE_ENABLED:?}" ;;
  disable)
    rm -f "${FAKE_ENABLED:?}" "${FAKE_ACTIVE:?}"
    ;;
  start|restart) printf '%s' "$1" >"${FAKE_ACTIVE:?}" ;;
  stop) rm -f "${FAKE_ACTIVE:?}" ;;
  daemon-reload) ;;
  *) echo "unexpected systemctl command: $verb $*" >&2; exit 92 ;;
esac
FAKE
chmod +x "$fakebin/systemctl"

new_home() {
  export HOME="$tmp/$1 home"
  export XDG_CONFIG_HOME="$HOME/config root"
  export XDG_DATA_HOME="$HOME/data root"
  export XDG_STATE_HOME="$HOME/state root"
  export PATH="$fakebin:$PATH"
  export SYSTEMCTL_LOG="$tmp/$1-systemctl.log"
  export FAKE_ENABLED="$tmp/$1-enabled"
  export FAKE_ACTIVE="$tmp/$1-active"
  export FAKE_GRAPHICAL="$tmp/$1-graphical"
  mkdir -p "$HOME" "$XDG_CONFIG_HOME/calendar-reminder-overlay"
  : >"$SYSTEMCTL_LOG"
  : >"$FAKE_GRAPHICAL"
  rm -f "$FAKE_ENABLED" "$FAKE_ACTIVE"
}
contains() { grep -Fq -- "$2" "$1"; }
not_contains() { ! grep -Fq -- "$2" "$1"; }

# Invalid config must fail before replacing installed files or stopping legacy service.
new_home invalid
install_root="$XDG_DATA_HOME/calendar-reminder-overlay"
mkdir -p "$install_root/build" "$install_root/src" "$XDG_CONFIG_HOME/systemd/user" "$HOME/.local/bin"
printf 'old core payload\n' >"$install_root/build/reminder-core"
printf 'old renderer payload\n' >"$install_root/build/calendar-reminder-overlay"
printf 'old helper payload\n' >"$install_root/src/reminderctl.py"
printf 'old service launcher\n' >"$HOME/.local/bin/calendar-reminder-overlay-service"
printf 'old user unit\n' >"$XDG_CONFIG_HOME/systemd/user/calendar-reminder-overlay.service"
printf 'version = 999\n' >"$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
printf '%s' 'evolution-dunst-reminders.service' >"$FAKE_ENABLED"
if "$root/install.sh" --activate >"$tmp/invalid.out" 2>&1; then
  echo 'installer unexpectedly accepted invalid config' >&2
  exit 1
fi
grep -Fq 'old core payload' "$install_root/build/reminder-core"
grep -Fq 'old renderer payload' "$install_root/build/calendar-reminder-overlay"
grep -Fq 'old helper payload' "$install_root/src/reminderctl.py"
grep -Fq 'old service launcher' "$HOME/.local/bin/calendar-reminder-overlay-service"
grep -Fq 'old user unit' "$XDG_CONFIG_HOME/systemd/user/calendar-reminder-overlay.service"
[[ ! -s "$SYSTEMCTL_LOG" ]]

# Fresh staging must not contact systemd and must seed a no-calendars config.
new_home fresh
"$root/install.sh" >"$tmp/fresh.out"
[[ ! -s "$SYSTEMCTL_LOG" ]]
grep -Fq 'calendars = []' "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
"$HOME/.local/bin/reminderctl" --config "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml" config-check \
  | grep -Fq '"valid": true'
for command in reminder-demo reminder-diagnostics; do
  [[ -x "$HOME/.local/bin/$command" ]]
done
cp -p "$HOME/.local/bin/reminderctl" "$tmp/reminderctl-real"
renderer="$XDG_DATA_HOME/calendar-reminder-overlay/build/calendar-reminder-overlay"
cp -p "$renderer" "$tmp/renderer-real"
cat >"$renderer" <<'RENDERER'
#!/usr/bin/env bash
case "${1:-}" in
  --capabilities)
    echo '{"version":1,"renderer":"gtk4","layer_shell_supported":true,"monitors":[{"connector":"test","scale_factor":1,"width":1000,"height":700}]}'
    ;;
  --watch) exec sleep 30 ;;
  *) exit 3 ;;
esac
RENDERER
chmod 0755 "$renderer"
cat >"$HOME/.local/bin/reminderctl" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
  if [[ "$argument" == wait ]]; then
    echo '{"action":"dismiss","action_revision":1}'
    exit 0
  fi
done
/usr/bin/python3 -c 'import json,sys; open(sys.argv[1], "a").write(json.dumps(sys.argv[2:])+"\n")' "${DEMO_LOG:?}" "$@"
echo '{"id":"preview:captured","revision":1,"accepted":true}'
STUB
chmod 0755 "$HOME/.local/bin/reminderctl"
export DEMO_LOG="$tmp/demo-args.jsonl"
config="$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
cp "$config" "$tmp/config.before-demo"
"$HOME/.local/bin/reminder-demo" --help >"$tmp/demo-help.out"
[[ ! -s "$DEMO_LOG" ]]
"$HOME/.local/bin/reminder-demo" >"$tmp/demo-one.out"
"$HOME/.local/bin/reminder-demo" --url 'https://example.test/open' >"$tmp/demo-two.out"
cmp "$tmp/config.before-demo" "$config"
[[ ! -e "$XDG_STATE_HOME/calendar-reminder-overlay/alerts.sqlite3" ]]
grep -Fq 'Preview reminder — no calendar event' "$tmp/demo-help.out"
grep -Fq 'Starting synthetic preview preview:' "$tmp/demo-one.out"
/usr/bin/python3 - "$DEMO_LOG" "$config" <<'PY'
import json
import pathlib
import re
import sys
rows = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
assert len(rows) == 2, rows
ids = []
for args in rows:
    assert args.count("notify") == 1, args
    index = args.index("notify")
    alert_id, title = args[index + 1:index + 3]
    assert re.fullmatch(r"preview:[0-9a-f]{32}", alert_id), alert_id
    assert title == "Preview reminder — no calendar event", title
    assert "--show" not in args, args
    assert "--open" not in args, args
    ids.append(alert_id)
assert ids[0] != ids[1], ids
assert "--url" in rows[1] and "https://example.test/open" in rows[1], rows[1]
assert "--url" not in rows[0], rows[0]
PY
mv "$tmp/reminderctl-real" "$HOME/.local/bin/reminderctl"
mv "$tmp/renderer-real" "$renderer"
bash -n "$HOME/.local/bin/calendar-reminder-overlay-service"
unit="$XDG_CONFIG_HOME/systemd/user/calendar-reminder-overlay.service"
grep -Fq 'PartOf=graphical-session.target' "$unit"
grep -Fq 'WantedBy=graphical-session.target' "$unit"
not_contains "$unit" 'WantedBy=default.target'
# A subsequent install preserves an explicitly chosen calendar selection.
sed -i 's#calendars = \[\]#calendars = ["work/selected"]#' "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
"$root/install.sh" >"$tmp/staged-again.out"
grep -Fq 'calendars = ["work/selected"]' "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
contains "$tmp/fresh.out" 'Staged only'

# Fresh activation starts the new service and does not probe/disable legacy unit.
new_home fresh-activate
"$root/install.sh" --activate >"$tmp/activate.out"
contains "$SYSTEMCTL_LOG" 'start calendar-reminder-overlay.service'
not_contains "$SYSTEMCTL_LOG" 'disable --now evolution-dunst-reminders.service'
not_contains "$SYSTEMCTL_LOG" 'stop evolution-dunst-reminders.service'

# Explicit legacy migration disables only that unit after replacement staging.
new_home migrate
printf '%s' 'evolution-dunst-reminders.service' >"$FAKE_ENABLED"
"$root/install.sh" --activate >"$tmp/migrate.out"
contains "$SYSTEMCTL_LOG" 'disable --now evolution-dunst-reminders.service'
contains "$SYSTEMCTL_LOG" 'start calendar-reminder-overlay.service'
not_contains "$SYSTEMCTL_LOG" 'swaync.service'
not_contains "$SYSTEMCTL_LOG" 'dunst.service'

# An active but disabled legacy unit is stopped without touching notification services.
new_home migrate-active
printf '%s' 'evolution-dunst-reminders.service' >"$FAKE_ACTIVE"
"$root/install.sh" --activate >"$tmp/migrate-active.out"
contains "$SYSTEMCTL_LOG" 'stop evolution-dunst-reminders.service'
not_contains "$SYSTEMCTL_LOG" 'disable --now evolution-dunst-reminders.service'
not_contains "$SYSTEMCTL_LOG" 'dunst.service'

# An already active install is restarted after its files and config are refreshed.
new_home restart
printf '%s' 'calendar-reminder-overlay.service' >"$FAKE_ACTIVE"
"$root/install.sh" --activate >"$tmp/restart.out"
contains "$SYSTEMCTL_LOG" 'restart calendar-reminder-overlay.service'

# Headless activation enables for the next graphical session without starting now.
new_home headless
rm -f "$FAKE_GRAPHICAL"
"$root/install.sh" --activate >"$tmp/headless.out"
contains "$SYSTEMCTL_LOG" 'enable calendar-reminder-overlay.service'
not_contains "$SYSTEMCTL_LOG" 'start calendar-reminder-overlay.service'
contains "$tmp/headless.out" 'not started outside a graphical session'

# Uninstall removes owned payload/unit but keeps config, state and legacy binary.
new_home uninstall
"$root/install.sh" >"$tmp/uninstall-stage.out"
config="$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
state="$XDG_DATA_HOME/calendar-reminder-overlay/state/keep.db"
legacy="$HOME/.local/bin/evolution-dunst-reminders"
unknown_install="$XDG_DATA_HOME/calendar-reminder-overlay/user-notes.txt"
mkdir -p "$(dirname "$state")" "$HOME/.local/bin"
printf 'keep' >"$state"; printf 'legacy' >"$legacy"; printf 'keep' >"$unknown_install"
"$root/uninstall.sh" >"$tmp/uninstall.out"
[[ -f "$config" && -f "$state" && -f "$legacy" && -f "$unknown_install" ]]
[[ ! -e "$XDG_CONFIG_HOME/systemd/user/calendar-reminder-overlay.service" ]]
[[ ! -e "$XDG_DATA_HOME/calendar-reminder-overlay/build/reminder-core" ]]
[[ ! -e "$HOME/.local/bin/reminder-demo" && ! -e "$HOME/.local/bin/reminder-diagnostics" ]]
not_contains "$SYSTEMCTL_LOG" 'disable --now evolution-dunst-reminders.service'
contains "$tmp/uninstall.out" 'Kept user configuration and state'

# Purge is explicit and removes only this application's known config/state.
new_home purge
"$root/install.sh" >"$tmp/purge-stage.out"
state="$XDG_STATE_HOME/calendar-reminder-overlay/alerts.sqlite3"
mkdir -p "$(dirname "$state")"
printf 'state' >"$state"
printf 'keep' >"$(dirname "$state")/user-notes.txt"
"$root/uninstall.sh" --purge >"$tmp/purge.out"
[[ ! -e "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml" ]]
[[ ! -e "$state" ]]
[[ -f "$(dirname "$state")/user-notes.txt" ]]
contains "$tmp/purge.out" 'Purged its config and state directory'

# Read-only diagnostics must not start the core through D-Bus auto activation.
new_home diagnostics
"$root/install.sh" >"$tmp/diagnostics-stage.out"
# This literal child script intentionally inherits HOME and XDG paths at runtime.
# shellcheck disable=SC2016
dbus-run-session -- bash -euo pipefail -c '
  unset DISPLAY WAYLAND_DISPLAY
  "$HOME/.local/bin/reminder-diagnostics" >"$HOME/diagnostics.out"
  grep -Fq "Core owner: stopped or unavailable" "$HOME/diagnostics.out"
  grep -Fq "EDS integration: disabled" "$HOME/diagnostics.out"
  grep -Fq "No calendars are selected" "$HOME/diagnostics.out"
  grep -Fq "reminderctl calendars" "$HOME/diagnostics.out"
  [[ ! -e "$XDG_STATE_HOME/calendar-reminder-overlay/alerts.sqlite3" ]]
  ! busctl --user --no-pager status org.muness.CalendarReminderOverlay.Core >/dev/null 2>&1
'

# Exercise the installed service launcher, helper, state location, and CLI against
# a private session bus. EDS stays disabled, so no calendar service or UI is used.
new_home startup
"$root/install.sh" >"$tmp/startup-stage.out"
cat >"$tmp/startup-smoke.sh" <<'SMOKE'
#!/usr/bin/env bash
set -euo pipefail
  "$HOME/.local/bin/calendar-reminder-overlay-service" >"$HOME/core.log" 2>&1 &
  core_pid=$!
  cleanup() { kill "$core_pid" 2>/dev/null || true; wait "$core_pid" 2>/dev/null || true; }
  trap cleanup EXIT
  ready=false
  for _ in {1..50}; do
    if busctl --user --no-pager status org.muness.CalendarReminderOverlay.Core >/dev/null 2>&1; then
      ready=true
      break
    fi
    sleep 0.1
  done
  [[ "$ready" == true ]]
  "$HOME/.local/bin/reminderctl" notify install:smoke "Installed launch works" --body "Private D-Bus smoke"
  "$HOME/.local/bin/reminderctl" status install:smoke | grep -Fq "Installed launch works"
  [[ -f "$XDG_STATE_HOME/calendar-reminder-overlay/alerts.sqlite3" ]]
SMOKE
dbus-run-session -- bash "$tmp/startup-smoke.sh"

printf 'install-smoke: validation-before-mutation, path/config preservation, service activation/migration, uninstall, and installed launcher lifecycle passed\n'
