# Omarchy calendar reminders

A Quickshell bar widget backed by the existing Calendar Reminder Overlay service. It projects EDS calendar status and pending reminders over session D-Bus; it does not maintain another account store or scheduler. The existing GTK Layer Shell renderer supplies the actionable overlay, including snooze, dismiss, and meeting links.

## Install and remove

From the repository root, use `./install.sh --with-omarchy` for the shared payload plus widget. Add `--activate` only when you want the shared service enabled and started/restarted. To add or update only the widget, run:

```sh
/usr/bin/python3 adapters/omarchy/install.py
```

Requires Omarchy's plugin-capable Quickshell shell and system Python with PyGObject. `OMARCHY_PATH` defaults to `/usr/share/omarchy`. The installer handles both the upstream Color singleton and this machine's ThemeColor compatibility rename. The plugin is placed in `$XDG_CONFIG_HOME/omarchy/plugins/muness.calendar-reminders` (default `~/.config`), before the clock unless already placed elsewhere. Shell configuration and prior plugin payloads are backed up under `calendar-reminder-overlay/adapter-backups` in that same config directory. Hot reload is attempted; otherwise it loads on the next shell start.

Remove just the widget, preserving calendar configuration and the shared service:

```sh
/usr/bin/python3 adapters/omarchy/install.py --uninstall
```

The repository's `uninstall.sh` also removes this widget when removing the shared application.

## Behavior

Click the bell for active reminders and upcoming events with alarms, selected-calendar count, and sync problems. Native calendar alarms appear as upcoming events before their alarms fire; their timing and dismissal remain owned by Evolution Data Server. Disabled reminders and an unavailable service have distinct states. Turn reminders on/off, start a stopped service, open the calendar (Evolution by default, or `~/.local/bin/reminder-calendar` when executable), edit settings, apply settings, or launch a silent isolated preview. Presentation off changes the shared service setting, so it applies across desktop adapters. Event titles render as literal text; event bodies and meeting URLs are not sent to the bar. Actions use argument arrays, not shell interpolation.

The adapter watches core changes and ownership, reconnects after service loss, and refreshes every 30 seconds. Read-only status never starts the core via D-Bus activation. Calendar policy, alarm eligibility, and durable state stay in the core; the widget lists at most six pending reminders.

## Execution and review evidence — 2026-10-21

Aim: reuse the calendar reminder overlay in Omarchy through a native adapter and widget while preserving the working COSMIC integration. Delivered: shared calendar integration, native themed widget, overlay controls and preview, reversible installer. Review: aligned; no core scheduling or COSMIC behavior changed.

| Risk | Status and evidence | Wrong shortcut rejected |
|---|---|---|
| Existing calendar state changes during tests | Retired: private D-Bus, temporary state/config, silent synthetic reminders; live service left running | Testing by dismissing real calendar alarms |
| Widget duplicates or replaces unrelated settings | Retired: repeat installation and removal tests preserve unrelated layout and calendar configuration | Replacing shell.json with a template |
| Stopped service looks like reminders are disabled | Retired: owner loss, restart, and presentation tests plus rendered states | Treating every D-Bus error as off |
| Legitimate late alarms disappear | Retired: projection test keeps pending reminders after event end | Filtering by event_end in the UI |
| Event text executes or becomes markup | Retired: literal-title projection test and Text.PlainText inspection; argv-only actions | Building shell commands from titles |
| Overlay fails outside COSMIC | Retired locally: actual Hyprland layer mapped; snooze/return/dismiss passed; fullscreen stacking observed in isolated Sway | Inferring compositor support from a successful build |
| Every monitor, lock, and suspend scenario works | Accepted boundary: broader session testing requires user/device coverage | Generalizing one-session evidence |

Validation: `make check`; private-bus `tests/omarchy-adapter.py`; isolated rendered `tests/omarchy-ui.py`; live private-bus `tests/omarchy-native.py`; installer smoke test. UI scripts require their documented runtime dependencies and a compatible local shell; inspect them before running. Native test briefly displays a synthetic overlay in the current session. No automated pointer-click coverage is claimed. Human verification remaining: comfort and readability during normal daily use, plus monitor/lock/suspend combinations.
