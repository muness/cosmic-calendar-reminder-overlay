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
  export PATH="$fakebin:$PATH"
  export SYSTEMCTL_LOG="$tmp/$1-systemctl.log"
  export FAKE_ENABLED="$tmp/$1-enabled"
  export FAKE_ACTIVE="$tmp/$1-active"
  mkdir -p "$HOME" "$XDG_CONFIG_HOME/calendar-reminder-overlay"
  : >"$SYSTEMCTL_LOG"
  rm -f "$FAKE_ENABLED" "$FAKE_ACTIVE"
}
contains() { grep -Fq -- "$2" "$1"; }
not_contains() { ! grep -Fq -- "$2" "$1"; }

# Fresh staging must not contact systemd and must seed a no-calendars config.
new_home fresh
"$root/install.sh" >"$tmp/fresh.out"
[[ ! -s "$SYSTEMCTL_LOG" ]]
grep -Fq 'calendars = []' "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
"$HOME/.local/bin/reminderctl" --config "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml" config-check \
  | grep -Fq '"valid": true'
bash -n "$HOME/.local/bin/calendar-reminder-overlay-service"
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

# An already active install is restarted after its files and config are refreshed.
new_home restart
printf '%s' 'calendar-reminder-overlay.service' >"$FAKE_ACTIVE"
"$root/install.sh" --activate >"$tmp/restart.out"
contains "$SYSTEMCTL_LOG" 'restart calendar-reminder-overlay.service'

# Uninstall removes owned payload/unit but keeps config, state and legacy binary.
new_home uninstall
"$root/install.sh" >"$tmp/uninstall-stage.out"
config="$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml"
state="$XDG_DATA_HOME/calendar-reminder-overlay/state/keep.db"
legacy="$HOME/.local/bin/evolution-dunst-reminders"
mkdir -p "$(dirname "$state")" "$HOME/.local/bin"
printf 'keep' >"$state"; printf 'legacy' >"$legacy"
"$root/uninstall.sh" >"$tmp/uninstall.out"
[[ -f "$config" && -f "$state" && -f "$legacy" ]]
[[ ! -e "$XDG_CONFIG_HOME/systemd/user/calendar-reminder-overlay.service" ]]
[[ ! -e "$XDG_DATA_HOME/calendar-reminder-overlay/build/reminder-core" ]]
not_contains "$SYSTEMCTL_LOG" 'disable --now evolution-dunst-reminders.service'
contains "$tmp/uninstall.out" 'Kept user configuration and state'

# Purge is explicit and removes only this application's known config/state.
new_home purge
"$root/install.sh" >"$tmp/purge-stage.out"
state="${HOME}/.local/state/calendar-reminder-overlay/alerts.sqlite3"
mkdir -p "$(dirname "$state")"
printf 'state' >"$state"
"$root/uninstall.sh" --purge >"$tmp/purge.out"
[[ ! -e "$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml" ]]
[[ ! -e "$state" ]]
contains "$tmp/purge.out" 'Purged its config and state directory'

printf 'install-smoke: fresh stage, config preservation, fresh activation, legacy migration, restart, and uninstall passed\n'
