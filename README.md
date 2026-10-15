# COSMIC Calendar Reminder Overlay

A fullscreen, clickable calendar reminder overlay for COSMIC, built around Evolution Data Server calendars. It uses GTK4 Layer Shell's overlay layer so alerts can stay huge and visible above normal windows. Reminders include **Open meeting**, **Dismiss**, **Snooze 1 min**, **Snooze 5 min**, and **Until start** actions.

The service listens for Evolution calendar alarms and schedules a configurable fallback reminder for events without alarms (default: 5 minutes before start). Meeting links are detected in event URLs, descriptions, Google Meet conference properties, and supported conferencing URLs.

## Requirements

On Arch Linux, install the runtime and build dependencies:

```sh
sudo pacman -S gtk4 gtk4-layer-shell evolution-data-server
```

The application uses `libecal` from Evolution Data Server. A Wayland session and a compositor that supports the layer-shell overlay layer are required for the true always-on-top overlay behavior. Without layer-shell support, the service falls back to a fullscreen GTK window.

## Build and install

```sh
./install.sh
```

The installer builds to `~/.local/bin/calendar-reminder-overlay`, installs the user systemd unit, and enables/starts it. To uninstall, stop/disable the service and remove the binary and unit.

Set the fallback lead time in `~/.config/evolution-calendar-reminder-overlay.conf`:

```ini
[reminders]
fallback-lead-minutes=5
```

Accepted values are 1–120 minutes.

## Try the visual mock

```sh
./demo/overlay-mock.py
```

It opens a fullscreen mock with clickable controls. The controls demonstrate their UI handlers; the mock does not read calendars, launch a meeting URL, or change calendar data. On systems where the Python GTK4 Layer Shell binding loads after Wayland libraries, run it as:

```sh
LD_PRELOAD=/usr/lib/libgtk4-layer-shell.so ./demo/overlay-mock.py
```

## Other desktop notifications

`systemd/swaync.service` is an optional SwayNotificationCenter user unit for ordinary notifications. The calendar overlay does not use Dunst. Install/enable SwayNotificationCenter separately if desired.

## Current status

This is a personal project in active development, tested on COSMIC Wayland with Evolution calendars. The service currently uses Evolution's local calendar APIs and has not been packaged for distribution.

## Reuse and portability research

See [the problem-space and reuse audit](docs/attention-overlay-research.md) for the verified component capabilities, existing alternatives, source/display boundaries, and proposed CLI/TOML configuration. The proposed features in that report are not yet implemented.

## Epic and implementation plan

[The epic](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/1) tracks the selected top-level capability issues. [The planning record](docs/attention-overlay-plan.md) preserves the Aim, independent problem frames, selected S&T steps, dependencies, evidence gates and deferred alternatives.
