#!/usr/bin/env bash
set -euo pipefail
: "${HOME:?HOME must be set}"

usage() { printf 'Usage: %s [--activate] [--with-applet]\n' "${0##*/}"; }
activate=false
with_applet=false
while (($#)); do
  case "$1" in
    --activate) activate=true ;;
    --with-applet) with_applet=true ;;
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

# Fail before touching installed files, units, or an existing service.
missing=()
for tool in make pkg-config busctl dbus-run-session; do
  command -v "$tool" >/dev/null || missing+=("$tool")
done
command -v "${CC:-cc}" >/dev/null || missing+=("${CC:-cc} (C compiler)")
if [[ "$with_applet" == true ]]; then
  command -v cargo >/dev/null || missing+=("cargo (optional COSMIC applet)")
fi
if [[ "$activate" == true ]]; then
  command -v systemctl >/dev/null || missing+=("systemctl (user service activation)")
fi
if ((${#missing[@]})); then
  printf 'Missing required command: %s\n' "${missing[@]}" >&2
  echo 'Install the missing tools, then rerun install.sh. See README.md for dependencies.' >&2
  exit 1
fi
if ! pkg-config --exists gtk4 gtk4-layer-shell-0 libecal-2.0 gio-2.0 sqlite3 json-glib-1.0; then
  echo 'Missing development libraries. pkg-config reports:' >&2
  pkg-config --print-errors --exists gtk4 gtk4-layer-shell-0 libecal-2.0 gio-2.0 sqlite3 json-glib-1.0 >&2 || true
  echo 'Install the named development packages, then rerun install.sh.' >&2
  exit 1
fi
if ! /usr/bin/python3 -c 'import tomllib; from gi.repository import Gio, GLib' >/dev/null 2>&1; then
  echo '/usr/bin/python3 needs Python 3.11+ with tomllib and PyGObject (Gio/GLib). Install those dependencies, then rerun install.sh.' >&2
  exit 1
fi
make -C "$root" all
if [[ "$with_applet" == true ]]; then
  cargo build --release --locked --manifest-path "$root/applet/Cargo.toml"
  applet_target_dir="$(cargo metadata --format-version 1 --no-deps --manifest-path "$root/applet/Cargo.toml" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["target_directory"])')"
  applet_binary="$applet_target_dir/release/cosmic-ext-applet-reminders"
  [[ -x "$applet_binary" ]] || { echo "Applet build did not produce $applet_binary" >&2; exit 1; }
fi
for artifact in build/reminder-core build/calendar-reminder-overlay src/reminderctl.py scripts/reminder-demo scripts/reminder-diagnostics scripts/reminder-settings; do
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

install -m 0755 "$root/scripts/reminder-demo" "${bin_dir}/reminder-demo"
install -m 0755 "$root/scripts/reminder-diagnostics" "${bin_dir}/reminder-diagnostics"
install -m 0755 "$root/scripts/reminder-settings" "${bin_dir}/reminder-settings"
if [[ "$with_applet" == true ]]; then
  install -m 0755 "$applet_binary" "${bin_dir}/cosmic-ext-applet-reminders"
  mkdir -p "$data_home/applications"
  /usr/bin/python3 - "$root/applet/data/com.muness.CalendarReminderControl.desktop" "$data_home/applications/com.muness.CalendarReminderControl.desktop" "$bin_dir/cosmic-ext-applet-reminders" <<'DESKTOP'
import pathlib, sys
source, target, executable = sys.argv[1:]
# Desktop Exec quoting, with literal field-code and reserved-character escaping.
escaped = executable.replace("%", "%%")
for char in ("\\", '"', "`", "$"):
    escaped = escaped.replace(char, "\\" + char)
pathlib.Path(target).write_text(pathlib.Path(source).read_text().replace("Exec=cosmic-ext-applet-reminders", 'Exec="' + escaped + '"'))
DESKTOP
  echo 'COSMIC applet installed. Add Reminder Controls in Settings → Desktop → Panel → Applets.'
fi

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
  echo 'Files staged; no service was started or restarted.'
  printf 'Try the overlay: %s/reminder-demo\n' "$bin_dir"
  printf 'Choose calendars: %s/reminderctl calendars\n' "$bin_dir"
  echo 'Then edit the configuration above, validate with reminderctl config-check, and run ./install.sh --activate.'
else
  printf 'Check calendar sync: %s/reminder-diagnostics\n' "$bin_dir"
fi
