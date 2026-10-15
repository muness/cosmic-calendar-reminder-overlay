#!/usr/bin/env bash
set -euo pipefail
: "${HOME:?HOME must be set}"

usage() { printf 'Usage: %s [--activate]\n' "${0##*/}"; }
activate=false
while (($#)); do
  case "$1" in
    --activate) activate=true ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
  shift
done

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
config_home="${XDG_CONFIG_HOME:-${HOME:?HOME must be set}/.config}"
bin_dir="${HOME}/.local/bin"
install_root="${data_home}/calendar-reminder-overlay"
unit_dir="${config_home}/systemd/user"
unit_file="${unit_dir}/calendar-reminder-overlay.service"
legacy_unit="evolution-dunst-reminders.service"

command -v make >/dev/null || { echo 'make is required' >&2; exit 1; }
make -C "$root" all
for artifact in build/reminder-core build/calendar-reminder-overlay src/reminderctl.py; do
  [[ -f "$root/$artifact" ]] || { echo "Build did not produce $artifact" >&2; exit 1; }
done

config_file="${config_home}/calendar-reminder-overlay/config.toml"
candidate_config="$(mktemp)"
trap 'rm -f "$candidate_config"' EXIT
if [[ -e "$config_file" ]]; then
  config_to_validate="$config_file"
else
  cat >"$candidate_config" <<'CONFIG'
version = 1

[selection]
mode = "include"
calendars = []
CONFIG
  chmod 0600 "$candidate_config"
  config_to_validate="$candidate_config"
fi

# Validate the source helper and the exact config that would be installed before
# changing the installed payload, launchers, service unit, or legacy service.
if ! /usr/bin/python3 "$root/src/reminderctl.py" --config "$config_to_validate" config-check >/dev/null; then
  echo "Configuration validation failed: $config_file" >&2
  exit 1
fi

mkdir -p "$install_root/build" "$install_root/src" "$bin_dir" "$unit_dir" "$(dirname -- "$config_file")"
install -m 0755 "$root/build/reminder-core" "$install_root/build/reminder-core"
install -m 0755 "$root/build/calendar-reminder-overlay" "$install_root/build/calendar-reminder-overlay"
install -m 0644 "$root/src/reminderctl.py" "$install_root/src/reminderctl.py"
install -m 0644 "$root/config.example.toml" "$install_root/config.example.toml"
# Do not replace a config that appeared after validation (for example, a
# concurrent first-run); source it into place only when no config exists.
if [[ ! -e "$config_file" ]]; then
  install -m 0600 "$candidate_config" "$config_file"
fi

cat >"${bin_dir}/calendar-reminder-overlay-service" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
config_home="${XDG_CONFIG_HOME:-${HOME:?HOME must be set}/.config}"
root="${data_home}/calendar-reminder-overlay"
exec "${root}/build/reminder-core" --headless \
  --config "${config_home}/calendar-reminder-overlay/config.toml" \
  --helper "${root}/src/reminderctl.py" \
  --overlay "${root}/build/calendar-reminder-overlay"
LAUNCHER
chmod 0755 "${bin_dir}/calendar-reminder-overlay-service"

cat >"${bin_dir}/reminderctl" <<'CLI'
#!/usr/bin/env bash
set -euo pipefail
data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
exec /usr/bin/python3 "${data_home}/calendar-reminder-overlay/src/reminderctl.py" \
  --core "${data_home}/calendar-reminder-overlay/build/reminder-core" "$@"
CLI
chmod 0755 "${bin_dir}/reminderctl"

cat >"${bin_dir}/reminder-diagnostics" <<'DIAG'
#!/usr/bin/env bash
set -euo pipefail
config_home="${XDG_CONFIG_HOME:-${HOME:?HOME must be set}/.config}"
data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
state_home="${XDG_STATE_HOME:-${HOME:?HOME must be set}/.local/state}"
root="${data_home}/calendar-reminder-overlay"
config="${config_home}/calendar-reminder-overlay/config.toml"
printf 'Install root: %s\nConfig: %s\nState DB: %s/calendar-reminder-overlay/alerts.sqlite3\n' \
  "$root" "$config" "$state_home"
for path in "$root/build/reminder-core" "$root/build/calendar-reminder-overlay" "$root/src/reminderctl.py" "$config" "$state_home/calendar-reminder-overlay/alerts.sqlite3"; do
  if [[ -e "$path" ]]; then printf 'Present: %s\n' "$path"; else printf 'Missing: %s\n' "$path"; fi
done
echo 'Configuration validation:'
/usr/bin/python3 "$root/src/reminderctl.py" --config "$config" config-check || true
echo 'Renderer capability report:'
if capabilities="$("$root/build/calendar-reminder-overlay" --capabilities 2>/dev/null)"; then
  printf '%s\n' "$capabilities"
else
  echo 'Unavailable: installed renderer does not expose --capabilities.'
fi
echo 'Core alerts/state (runtime evidence; calls cannot auto-start services):'
if ! command -v busctl >/dev/null; then
  echo 'Unavailable: busctl is not installed.'
else
  if owner="$(busctl --user --no-pager status org.muness.CalendarReminderOverlay.Core 2>/dev/null)"; then
    printf '%s\n' "$owner" | sed -n '1,5p'
  else
    echo 'No running core owner detected.'
  fi
  if ! busctl --user --auto-start=no call org.muness.CalendarReminderOverlay.Core \
    /org/muness/CalendarReminderOverlay/Core \
    org.muness.CalendarReminderOverlay.Core1 List 2>/dev/null; then
    echo 'Core alert list unavailable (core is stopped or this version lacks List).'
  fi
  echo 'EDS source freshness/errors (core runtime diagnostics):'
  if ! busctl --user --auto-start=no call org.muness.CalendarReminderOverlay.Core \
    /org/muness/CalendarReminderOverlay/Core \
    org.muness.CalendarReminderOverlay.Core1 Diagnostics 2>/dev/null; then
    echo 'Unavailable: core is stopped or does not expose Diagnostics.'
  fi
fi
if command -v systemctl >/dev/null; then
  printf 'User service state: '
  systemctl --user is-active calendar-reminder-overlay.service 2>/dev/null || true
fi
if command -v journalctl >/dev/null; then
  echo 'Recent service diagnostics (warnings/errors):'
  journalctl --user -u calendar-reminder-overlay.service -p warning --no-pager -n 20 2>/dev/null || true
fi
echo 'Capability notes: renderer capabilities describe detected process support; runtime output above is separate evidence.'
echo 'Calendar listing and playback are not probed here; no service is started and no audio is played.'
DIAG
chmod 0755 "${bin_dir}/reminder-diagnostics"

install -m 0644 "$root/systemd/calendar-reminder-overlay.service" "$unit_file"

if [[ "$activate" == true ]]; then
  command -v systemctl >/dev/null || { echo 'systemctl is required for --activate' >&2; exit 1; }
  systemctl --user daemon-reload
  # Migrate the specifically named legacy unit only after replacement files and
  # configuration have been installed and validated. Never touch notification
  # daemons or the legacy executable itself.
  if systemctl --user is-enabled --quiet "$legacy_unit" 2>/dev/null; then
    systemctl --user disable --now "$legacy_unit"
  elif systemctl --user is-active --quiet "$legacy_unit" 2>/dev/null; then
    systemctl --user stop "$legacy_unit"
  fi
  systemctl --user enable calendar-reminder-overlay.service
  if ! systemctl --user is-active --quiet graphical-session.target 2>/dev/null; then
    echo 'Enabled for the next graphical session; not started outside a graphical session.'
  elif systemctl --user is-active --quiet calendar-reminder-overlay.service 2>/dev/null; then
    systemctl --user restart calendar-reminder-overlay.service
  else
    systemctl --user start calendar-reminder-overlay.service
  fi
fi

printf 'Installed files under %s\n' "$install_root"
printf 'Configuration: %s\n' "$config_file"
if [[ "$activate" != true ]]; then
  echo 'Staged only. Review configuration, then run install.sh --activate to enable the user service.'
fi
