#!/usr/bin/env bash
set -euo pipefail
purge=false
while (($#)); do
  case "$1" in
    --purge) purge=true ;;
    -h|--help) echo 'Usage: uninstall.sh [--purge]'; exit 0 ;;
    *) echo 'Usage: uninstall.sh [--purge]' >&2; exit 2 ;;
  esac
  shift
done

config_home="${XDG_CONFIG_HOME:-${HOME:?HOME must be set}/.config}"
data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
state_home="${XDG_STATE_HOME:-${HOME}/.local/state}"
unit="${config_home}/systemd/user/calendar-reminder-overlay.service"
if command -v systemctl >/dev/null; then
  systemctl --user disable --now calendar-reminder-overlay.service >/dev/null 2>&1 || true
fi
rm -f "$unit" \
  "${HOME}/.local/bin/calendar-reminder-overlay-service" \
  "${HOME}/.local/bin/reminderctl" \
  "${HOME}/.local/bin/reminder-diagnostics" \
  "${data_home}/calendar-reminder-overlay/build/reminder-core" \
  "${data_home}/calendar-reminder-overlay/build/calendar-reminder-overlay" \
  "${data_home}/calendar-reminder-overlay/src/reminderctl.py" \
  "${data_home}/calendar-reminder-overlay/config.example.toml"
rmdir "${data_home}/calendar-reminder-overlay/build" \
  "${data_home}/calendar-reminder-overlay/src" \
  "${data_home}/calendar-reminder-overlay" 2>/dev/null || true
if [[ "$purge" == true ]]; then
  rm -f "${config_home}/calendar-reminder-overlay/config.toml"
  rmdir "${config_home}/calendar-reminder-overlay" 2>/dev/null || true
  rm -rf "${state_home}/calendar-reminder-overlay" "${data_home}/calendar-reminder-overlay"
fi
if command -v systemctl >/dev/null; then systemctl --user daemon-reload >/dev/null 2>&1 || true; fi
echo 'Removed calendar-reminder-overlay files and its user unit.'
if [[ "$purge" == true ]]; then
  echo 'Purged its config and state directory.'
else
  echo 'Kept user configuration and state. Use --purge to remove them.'
fi
echo 'Legacy executables, legacy units, and notification daemons were left untouched.'
