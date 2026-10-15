# Calendar Reminder Overlay

A local GTK reminder surface with a separate alert core and `reminderctl` CLI. The core owns local alert state and its session D-Bus API; the renderer presents alerts submitted to that core. Calendar support uses Evolution Data Server (EDS) when explicitly enabled and selected in the TOML configuration.

This repository is an active implementation. A previous COSMIC setup was accepted by its user, but the current integration branch has not been verified as a packaged install or across desktop environments. See [compatibility](docs/compatibility.md) for the evidence boundary.

## Quick start

After building and staging once with `make && ./install.sh`, try the preview in your graphical session before choosing calendars. It shows a real overlay alert titled **“Preview reminder — no calendar event”** using a temporary config, state database, and private D-Bus session. It creates no calendar event, changes no user config or state, and always runs silently. Success means the overlay appears with its Dismiss and Snooze buttons; after Dismiss, the terminal reports that the preview finished. Run `reminder-demo` again any time for a fresh preview, or skip it. A supplied `--url` is offered only through Open in the alert.

```sh
reminder-demo
reminder-setup-help  # optional: explicitly list local EDS calendars and copy selection keys
${EDITOR:-vi} "${XDG_CONFIG_HOME:-$HOME/.config}/calendar-reminder-overlay/config.toml"
reminderctl config-check
./install.sh --activate
reminder-diagnostics
```

`reminder-setup-help` only reads calendars already visible to EDS; it does not create accounts or edit the config. Uncomment only the desired calendar keys in its TOML template. Leave `[calendar].eds_enabled = false` for a preview-only setup. The initial config selects no calendars, and installing or previewing does not take over Dunst, SwayNC, or another ordinary notification service. The preview uses the actual GTK alert buttons; keep its terminal open after Snooze so the isolated core can show the alert again when that snooze expires. Its private D-Bus may produce accessibility or portal warnings; those are not hidden, and desktop/session handoff still needs real-session validation.

## Build and install

Build dependencies include a C compiler, `make`, GTK4, GTK4 Layer Shell, Evolution Data Server, GIO, SQLite, and JSON-GLib. Runtime needs `/usr/bin/python3` with PyGObject's Gio and GLib bindings. Audio playback is optional and requires PipeWire's `pw-play` when enabled in config. `make` builds the core and renderer; `make check` runs the local core lifecycle checks.

```sh
make
make check
./install.sh
```

`install.sh` builds and stages the files under `$XDG_DATA_HOME/calendar-reminder-overlay` (default: `~/.local/share`), creates `reminderctl`, `reminder-demo`, `reminder-setup-help`, and `reminder-diagnostics` commands in `~/.local/bin`, and installs the user service file. It does not enable or start the service. The first configuration is created at `$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml` (default: `~/.config`) with an empty calendar selection. Existing configuration is validated before installed files are replaced.

Review the configuration, then explicitly activate the service:

```sh
${EDITOR:-vi} "${XDG_CONFIG_HOME:-$HOME/.config}/calendar-reminder-overlay/config.toml"
./install.sh --activate
```

On activation, the installer enables `calendar-reminder-overlay.service` for `graphical-session.target` and starts or restarts it when that target is active. If activated from a text session, it leaves the unit enabled for the next graphical session without starting it. The unit stops when the graphical session ends. If the old `evolution-dunst-reminders.service` is enabled or active, it disables/stops that legacy unit after validating the new files and config. It does not change Dunst, SwayNC, or another notification daemon. The old executable is left in place for manual review.

Configuration uses strict versioned TOML; see [config.example.toml](config.example.toml). Set `[calendar].eds_enabled = true` to connect to EDS, and select calendars using `source-id/calendar-id` strings in `[selection]`. Include mode with an empty list selects nothing. `reminderctl config-check` validates configuration without starting the service. `reminderctl calendars` lists configured EDS calendars read-only.

## CLI and synthetic demonstration

The CLI can submit and manage local alerts. For example:

```sh
reminderctl notify timer:tea "Tea is ready" --body "Take the kettle off"
reminderctl status timer:tea
reminderctl wait timer:tea --after-revision 1 --timeout 300
reminderctl cancel timer:tea
```

For an isolated GTK preview with fresh IDs and real alert actions, use:

```sh
reminder-demo
# Optional link; it opens only after choosing Open in the alert.
reminder-demo --url 'https://example.org'
```

`reminder-demo` does not enroll a calendar or run automatically during installation. `reminder-diagnostics` reports installed paths, config validation, EDS selection state, renderer capability evidence, and the already-running core's alert and per-source freshness/errors when exposed. It never initializes account discovery or auto-starts the service; run `reminder-setup-help` explicitly to list existing EDS calendars. A stale/error source points to the account/provider and service journal. A headless renderer-capability failure needs a rerun in the target graphical session and does not by itself prove display incompatibility.

## Uninstall

```sh
./uninstall.sh
```

This removes the owned service unit, launchers, and installed binaries while preserving configuration and alert state. Use `./uninstall.sh --purge` only when you also want to remove this application's config and state. Legacy executables and notification services are not removed.

## Desktop compatibility

The GTK Layer Shell renderer targets compositors that implement its overlay layer. The COSMIC legacy user acceptance does not establish that this branch works there. Hyprland and KDE Plasma Wayland have not been tested in this work. An optional GNOME Shell helper prototype is in [gnome-extension](gnome-extension/README.md); it is not installed or enabled by the installer and has not been tested in a GNOME session. Details and limits are in [docs/compatibility.md](docs/compatibility.md).

## Akonadi discovery

A source-based investigation found that KDE's reminder daemon owns alarm dismissal, snooze, persistence, and scheduling in-process; its inspected D-Bus interface does not expose a generic alarm feed or actions. The report records the exact adapter boundary and why native Akonadi support is not claimed: [docs/akonadi-discovery.md](docs/akonadi-discovery.md).
