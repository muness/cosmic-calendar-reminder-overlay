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

## Local alert core preview

The local core is a separate, private session-D-Bus API. It does not own
`org.freedesktop.Notifications`, and generic alerts work without EDS. Its
SQLite state defaults to `$XDG_STATE_HOME/calendar-reminder-overlay/alerts.sqlite3`.
Set `REMINDER_STATE_HOME` to isolate it. `reminderctl.py` starts the core on
first use, and the sender can exit without losing the alert or its action result.

Build both native binaries and run the isolated lifecycle checks:

```sh
make
make check
```

Submit/update/cancel/status/list and wait for an action result:

```sh
src/reminderctl.py notify timer:tea "Tea is ready" --body "Take the kettle off"
src/reminderctl.py update timer:tea "Tea is ready now"
src/reminderctl.py status timer:tea
src/reminderctl.py wait timer:tea --after-revision 1 --timeout 300
src/reminderctl.py cancel timer:tea
```

Structured JSON input is accepted for `notify` and `update`; the core enforces
selection again at its D-Bus admission boundary when `source_id` and
`calendar_id` are supplied:

```sh
printf '%s\n' '{"id":"hook:build","title":"Build finished","body":"Review the result","destination":"https://example.org","source_id":"eds-local","calendar_id":"backend-id"}' \
  | src/reminderctl.py notify --json
```

`wait` observes the durable result written by the UI (`open`, `dismiss`, or
`snooze`); it does not cancel the alert when the producer exits. Alert IDs must
be namespaced, for example `timer:tea` or `homelab:garage-door`. Actions carry
the revision shown by `status`; an old revision is rejected. Expiry is a Unix
timestamp. Snooze deadline and action result survive core restarts. Snoozed
alerts become active on the first service query after their deadline; session
catch-up is therefore state-driven and does not replay expired alerts.

To show a submitted synthetic alert in the existing large GTK overlay, run:

```sh
src/reminderctl.py notify synthetic:demo "Synthetic reminder" --body "The GTK UI is reusing its large action layout" --url https://example.org --show
```

The Open button records success only after the destination launcher accepts
the URI. A failed launch leaves the alert active. The CLI's `act ... open`
also launches the destination before recording the result.

Synthetic mode does not initialize the EDS registry. The current EDS path is
still the prototype path and has not yet been converted to the TOML selector.

Configuration uses strict, versioned TOML; see [config.example.toml](config.example.toml).
The default path is `$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml`.
`config-check` rejects unknown keys and out-of-range values. Calendar selection
uses `source-id/calendar-id` pairs: include mode with an empty list selects
nothing, exclude mode selects everything except listed pairs, and all mode
requires an empty list. `check-selection` exercises the same matcher without
connecting to EDS; the core reloads the validated policy before each CLI
request and rejects unselected source submissions. Audio fields are validated but report as unavailable, and
calendar listing is pending the next EDS adapter group.
