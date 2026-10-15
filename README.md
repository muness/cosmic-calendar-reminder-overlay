# Calendar Reminder Overlay

A local GTK reminder surface with a separate alert core and `reminderctl` CLI. The core owns local alert state and its session D-Bus API; the renderer presents alerts submitted to that core. Calendar support uses Evolution Data Server (EDS) when explicitly enabled and selected in the TOML configuration.

This repository is an active implementation. A previous COSMIC setup was accepted by its user, but the current integration branch has not been verified as a packaged install or across desktop environments. See [compatibility](docs/compatibility.md) for the evidence boundary.

## Build and install

Build dependencies include a C compiler, `make`, GTK4, GTK4 Layer Shell, Evolution Data Server, GIO, SQLite, and JSON-GLib. Runtime needs `/usr/bin/python3` with PyGObject's Gio and GLib bindings. Audio playback is optional and requires PipeWire's `pw-play` when enabled in config. `make` builds the core and renderer; `make check` runs the local core lifecycle checks.

```sh
make
make check
./install.sh
```

`install.sh` builds and stages the files under `$XDG_DATA_HOME/calendar-reminder-overlay` (default: `~/.local/share`), creates `reminderctl` and `reminder-diagnostics` commands in `~/.local/bin`, and installs the user service file. It does not enable or start the service. The first configuration is created at `$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml` (default: `~/.config`) with an empty calendar selection. Existing configuration is validated before installed files are replaced.

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

To explicitly show a synthetic alert in the GTK renderer, use `--show`:

```sh
reminderctl notify synthetic:demo "Synthetic reminder" \
  --body "A demo alert" --url https://example.org --show
```

The demo does not enroll a calendar or run automatically during installation. `reminder-diagnostics` reports installed paths, config validation, renderer capability evidence when supported, any already-running core's alerts and EDS freshness/errors when exposed, service state, and recent service warnings. It does not start the service or play audio; its core queries disable D-Bus auto-start, and a missing capability or diagnostics method is reported as unavailable.

## Uninstall

```sh
./uninstall.sh
```

This removes the owned service unit, launchers, and installed binaries while preserving configuration and alert state. Use `./uninstall.sh --purge` only when you also want to remove this application's config and state. Legacy executables and notification services are not removed.

## Desktop compatibility

The GTK Layer Shell renderer targets compositors that implement its overlay layer. The COSMIC legacy user acceptance does not establish that this branch works there. Hyprland and KDE Plasma Wayland have not been tested in this work. An optional GNOME Shell helper prototype is in [gnome-extension](gnome-extension/README.md); it is not installed or enabled by the installer and has not been tested in a GNOME session. Details and limits are in [docs/compatibility.md](docs/compatibility.md).

## Akonadi discovery

A source-based investigation found that KDE's reminder daemon owns alarm dismissal, snooze, persistence, and scheduling in-process; its inspected D-Bus interface does not expose a generic alarm feed or actions. The report records the exact adapter boundary and why native Akonadi support is not claimed: [docs/akonadi-discovery.md](docs/akonadi-discovery.md).
