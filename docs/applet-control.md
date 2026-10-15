# Reminder controls in the panel

Proposal, not an installed feature. Scope narrowed after user feedback: the existing COSMIC calendar applet already supplies the calendar and agenda. This project needs a reminder control, not another event browser.

## Small popup

- **Reminders on/off**, with a readable state label and a switch.
- **Show demo reminder** to check presentation.
- **Settings** to reach configuration.

No month grid, upcoming-events list, calendar editing, or provider account setup. Timed pause can be added later; it is not needed for the first control.

Turning reminders off should suppress overlay presentation and audio while keeping calendar synchronization alive. It must not dismiss provider alarms, remove events, or change ordinary desktop notifications. Persist the state in the shared core, and expose the same control through the CLI. The applet reflects the confirmed core state and reports service-unavailable or failed-update states honestly.

A native COSMIC applet can use the [official template](https://github.com/pop-os/cosmic-applet-template) and [panel popup pattern](https://pop-os.github.io/libcosmic-book/panel-applets.html). It should call the shared core over session D-Bus. The core does not yet have a global presentation enable/disable API; stopping the service is not equivalent because it also stops synchronization.

## Follow existing calendar selection

The [COSMIC calendar applet's EDS backend](https://github.com/Hasmolam/cosmic-ext-applet-calendar/blob/main/src/event/eds.rs) discovers enabled sources and excludes those with `Selected=false`. It already uses the same EDS account infrastructure as Evolution. An earlier proposal incorrectly described a separate account/sync model; no extra account setup is needed for its EDS backend.

Our source currently checks EDS source enablement and its own TOML selection, but does not check the Calendar extension's selected flag. Enabled and selected are different: a connected calendar may be unchecked in Evolution.

The intended default is to follow Evolution's checked calendars using [ESourceSelectable.get_selected](https://gnome.pages.gitlab.gnome.org/evolution-data-server/libedataserver/method.SourceSelectable.get_selected.html), with explicit TOML overrides available for users who want a different reminder selection. React to source changes so checking/unchecking a calendar updates eligibility, withdraws excluded presentations, and stops their audio without dismissing provider alarms. Apply the same selection decision to native alarms, fallback scans, pending fetch results, and calendar diagnostics. This behavior is proposed, not yet implemented.

## First useful increment

Implement shared-core enable/disable controls and desktop calendar-selection following. Then add the small panel popup. Keep the existing calendar applet responsible for browsing events. Reuse the control API for other desktop clients later.
