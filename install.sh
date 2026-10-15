#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bin_dir="${HOME}/.local/bin"
unit_dir="${HOME}/.config/systemd/user"

mkdir -p "${bin_dir}" "${unit_dir}"
cc -Wall -Wextra -O2 "${root}/src/calendar-reminder-overlay.c" \
  -o "${bin_dir}/calendar-reminder-overlay" \
  $(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 libecal-2.0)

sed "s|%h/.local/bin/evolution-dunst-reminders|%h/.local/bin/calendar-reminder-overlay|" \
  "${root}/systemd/calendar-reminder-overlay.service" \
  > "${unit_dir}/calendar-reminder-overlay.service"
systemctl --user daemon-reload
systemctl --user enable --now calendar-reminder-overlay.service
