# Panel control: pause reminders and see what's next

Proposal, not an installed feature.

## Interaction

A panel button opens a compact popup. The panel icon shows whether reminders are enabled or paused; the popup spells out the state so color is not the only cue.

- **Reminders on**: a switch turns presentation off without disconnecting calendars.
- **Paused until 14:30**: an explicit Resume button, plus choices for 15 minutes, 1 hour, or until manually resumed. Show a confirmed pause time only after the core accepts it.
- **Coming up**: the next few events from selected calendars, ordered by start time, with local time, title, and calendar name. Offer Open meeting only when a valid link exists.
- **Calendar settings** and **Show demo reminder** are secondary actions.

Keep Dismiss and Snooze on the huge reminder itself. Pause is a global presentation control, not an action on one event.

## State and recovery

While paused, keep calendar synchronization and scheduling alive. Stop audio and hide the current presentation without dismissing its event or changing a provider alarm. On resume, show still-actionable reminders; skip occurrences that have ended. Snoozed reminders keep their due time. Never change the user's ordinary notification settings.

Use a persisted pause deadline in the core, not a timer in the applet. A separate applet or panel crash must not lose the pause. “Until resumed” survives a core restart. A timed pause expires according to wall time, including across suspend. Explain what happens when the display or session is locked; do not present over the lock screen.

The popup needs distinct states:

| State | What the user sees |
|---|---|
| No calendars selected | “Choose calendars to see upcoming events” with Calendar settings. |
| Calendars loading | “Syncing calendars…”; keep any last-known agenda visible. |
| No upcoming events | “Nothing else scheduled today” only when a successful fetch covers that range. |
| Source stale | Last-known events plus the last successful sync time and a connection warning. |
| Core stopped | “Reminder service is stopped” with an explicit Start action; do not claim it is merely paused. |
| Pause request failed | Keep the previous switch state and offer Retry. |
| No meeting link | Show event details; omit the Open meeting action. |

## Reuse and implementation boundary

COSMIC documents native panel applets built with libcosmic, an icon button, and a popup. Start from the [official applet template](https://github.com/pop-os/cosmic-applet-template) and follow the [panel applet guide](https://pop-os.github.io/libcosmic-book/panel-applets.html). The applet should be a small client of this core's session D-Bus API.

The existing [COSMIC calendar applet](https://github.com/Hasmolam/cosmic-ext-applet-calendar) already offers an agenda with Google Calendar and CalDAV synchronization. It is a useful integration candidate, but it has its own account/sync model. We have not verified a public agenda API to consume from it. Avoid making users configure the same accounts twice. Linking to or collaborating with that project remains an option; it is not an existing integration.

The current core already has selected-calendar policy, alert actions, source freshness, and persistent recurrence identities. It does **not** yet expose the needed global pause or complete agenda API. Alert List is not an agenda: it includes dismissed/expired reminders and can omit events that have not produced an alert. The recurrence index stores identities and times, not all event display fields.

Add two small capabilities to the core before the applet:

1. A presentation policy API for getting/setting pause state, with a change signal and matching `reminderctl pause`, `resume`, and `status` controls. Keep calendar enablement separate from presentation pause.
2. A bounded upcoming-events query over selected, successfully fetched occurrences, including title, calendar label, start/end, all-day status, meeting link, and freshness. Return coverage and errors explicitly. Publish changes so the applet can update without repeatedly fetching providers.

Use the same core capabilities for a future KDE tray or other shell client. Only the panel UI should depend on COSMIC. No extra calendar scheduler, provider authentication, or applet-owned alert database is needed.

## First useful increment

Ship pause/resume plus next events in one popup. Defer calendar editing, a month grid, and provider setup to the existing configuration/account tools. Validate panel placement, popup sizing, service loss/reconnect, pause expiry, and selected-calendar consistency in a real COSMIC session before marking it supported.
