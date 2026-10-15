# Calendar Reminder Overlay

Huge calendar reminders for Linux, with buttons you can actually click: **Open meeting**, **Dismiss**, and **Snooze**. The overlay covers your screen and stays until you act. Ordinary desktop notifications keep using your existing notification service.

Early alpha. Installed and used on COSMIC Wayland; other desktops are [unverified](docs/compatibility.md). Calendar accounts come from Evolution Data Server (EDS). You can also send reminders from scripts without connecting a calendar.

## Try the overlay first

You need a Linux graphical session, a C compiler, `make`, `pkg-config`, and development libraries for GTK4, GTK4 Layer Shell, Evolution Data Server (`libecal-2.0`), GIO, SQLite, and JSON-GLib. Runtime needs Python 3.11+ at `/usr/bin/python3` with PyGObject (Gio and GLib), plus `busctl` and `dbus-run-session`. Automatic startup uses a systemd user session. Optional sound uses PipeWire's `pw-play`.

From this checkout:

```sh
./install.sh
~/.local/bin/reminder-demo
```

Installation builds the app and stages it in your home directory. It creates a configuration with no calendars selected; it does not start the background service. The installer reports missing dependencies before replacing installed files.

The preview shows a **Preview reminder — no calendar event** overlay with real buttons. Click **Dismiss** to finish, or **Snooze** to see it return; keep the terminal open while snoozed. Press Ctrl+C in that terminal to end the preview. It uses temporary state and a private D-Bus session, stays silent, and creates no calendar event. Replay it whenever you want. To try the Open meeting button:

```sh
~/.local/bin/reminder-demo --url https://example.org
```

The supplied link opens only when you click **Open meeting**. Keyboard use: click the background, then Tab to a button and press Enter. Typing as an alert arrives cannot activate its buttons before Tab navigation.

Add `~/.local/bin` to your PATH if these commands are not found without the full path. You can skip the preview and continue below.

## Connect your calendars

First, connect your account in your desktop's Online Accounts or Evolution, and confirm that events appear in Evolution's Calendar view. This app reads calendars already available through EDS; it does not implement Google or Microsoft sign-in. Provider support depends on your EDS account setup. KDE's Akonadi accounts are [not connected yet](docs/akonadi-discovery.md).

### Choose calendars

```sh
~/.local/bin/reminderctl calendars
```

Copy a selection key from that list. To print a minimal configuration for one calendar, replace the example key below with your actual key:

```sh
~/.local/bin/reminderctl calendars --snippet 'eds-local/YOUR-CALENDAR-ID'
```

Edit `~/.config/calendar-reminder-overlay/config.toml` (or `$XDG_CONFIG_HOME/calendar-reminder-overlay/config.toml`). A minimal setup is:

```toml
version = 1

[selection]
mode = "include"
calendars = ["eds-local/YOUR-CALENDAR-ID"] # replace with a key from the list

[calendar]
eds_enabled = true
```

List multiple keys to select multiple calendars. An empty include list selects none. Use `mode = "all"` only if you want every enabled EDS calendar, including holiday calendars. Installing again preserves your configuration.

### Start reminders

From the checkout, in your graphical session:

```sh
~/.local/bin/reminderctl config-check
./install.sh --activate
~/.local/bin/reminder-diagnostics
```

Look for **User service: active** and per-calendar fetch status in diagnostics. Calendar synchronization can take time; an active service alone does not prove the provider has synced. Existing calendar alarms are used when available; fallback reminders use a five-minute lead by default. [Configuration options](config.example.toml) cover snooze choices, selected outputs, theme, fallback behavior, per-calendar overrides, and optional audio. Sound is off by default.

Activation enables startup with your graphical session and starts or restarts the service. From a text session it enables startup for the next graphical login. It replaces the specifically named legacy `evolution-dunst-reminders.service` if present. It does not replace Dunst, SwayNC, or your ordinary notification daemon.

## Send a reminder from a script

With the service active:

```sh
~/.local/bin/reminderctl notify timer:tea 'Tea is ready' \
  --body 'Take the kettle off' --show
```

`--show` requests the overlay. Omit it to submit state without explicitly opening a renderer. A generic reminder has no event start time, so **Until event starts** is unavailable. Supply `--url https://example.org` to offer an Open meeting button.

Reuse an ID to address the same reminder; choose a fresh ID for a separate one. Inspect or cancel it with:

```sh
~/.local/bin/reminderctl status timer:tea
~/.local/bin/reminderctl cancel timer:tea
```

`reminderctl --help` lists update, action, and wait commands for integrations.

## Change settings or stop reminders

After editing TOML, validate it and explicitly reload the running service:

```sh
~/.local/bin/reminderctl config-check
~/.local/bin/reminderctl reload
```

To stop the service for now, then start it again:

```sh
systemctl --user stop calendar-reminder-overlay.service
systemctl --user start calendar-reminder-overlay.service
```

To turn off automatic startup as well:

```sh
systemctl --user disable --now calendar-reminder-overlay.service
```

These are service controls, not a timed pause: the service does not monitor calendars while stopped. CLI commands that need the core can start it again. A small [panel control for turning reminders on/off](docs/applet-control.md) is proposed; calendar browsing stays in the existing calendar applet. It is not included yet.

## Troubleshooting

| What you see | Next step |
|---|---|
| Build or dependency error | Install the named development libraries and Python bindings, then rerun `./install.sh`. |
| Preview cannot open a display | Run it from a terminal inside your graphical desktop. |
| No calendars listed | Check that events appear in Evolution; reconnect or sync the account there first. |
| No calendar reminders | Check `eds_enabled`, selection keys, and per-calendar status in `reminder-diagnostics`. |
| Calendar status is stale or error | Check the account connection and the service journal below. The last successful snapshot is retained. |
| Command not found | Use `~/.local/bin/<command>` or add that directory to PATH. |

```sh
~/.local/bin/reminder-diagnostics
journalctl --user -u calendar-reminder-overlay.service -n 50 --no-pager
```

Diagnostics does not start the service or set up accounts. Its normal output summarizes alerts; `reminder-diagnostics --verbose` includes event contents and service warnings. Review verbose output and journals for private meeting details before sharing them.

## Update or uninstall

From an updated checkout, rerun `./install.sh --activate`. This rebuilds, preserves configuration and state, and restarts the service with the new version. `./install.sh` alone stages files without restarting an already-running service.

```sh
./uninstall.sh
```

Uninstall preserves configuration and alert state. `./uninstall.sh --purge` also removes those files. It leaves unrelated notifications and legacy executables alone.

## Development and desktop support

`make` builds the core and GTK renderer; `make check` runs the automated checks. The core owns persistent alert state and a session D-Bus API; the CLI and renderer consume it.

COSMIC has local user acceptance for the installed alpha. Multi-monitor behavior, other compositors, and all account/provider combinations are not established by that result. Hyprland/Omarchy and KDE Plasma Wayland are unverified. The optional GNOME Shell helper is experimental and not installed automatically. See [compatibility](docs/compatibility.md), [GNOME helper](gnome-extension/README.md), and [Akonadi discovery](docs/akonadi-discovery.md).
