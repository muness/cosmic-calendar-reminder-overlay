# Akonadi alarm discovery

**Result:** a source review establishes an adapter boundary, not a delivered native Akonadi adapter. Akonadi is not installed in the development environment, and no live service or calendar account was started or changed for this investigation.

## Inspected source

The inspected primary source is KDE's [`akonadi-calendar` repository at commit `4848a675df7c014581efdf7452de56bcb1254685`](https://github.com/KDE/akonadi-calendar/tree/4848a675df7c014581efdf7452de56bcb1254685).

At that revision:

- `reminder-daemon/kalendaralarmclient.h` owns `checkAlarms`, alarm dismissal/suspension, and persisted notifications in-process.
- `src/etmcalendar.h` exposes selected collections, interval alarms, and calendar-change notifications for an application using the relevant KDE APIs.
- `reminder-daemon/org.kde.calendar.Calendar.xml` exports `showIncidenceByUid`; it does not export a generic alarm feed or alarm actions.

These interfaces imply that a data-feed adapter can use an in-process Qt/Akonadi component (or a new upstream-supported API). They do **not** expose the reminder daemon's private native scheduler as a reusable service. The inspected D-Bus interface is insufficient to implement native dismiss/snooze ownership from this project. `KalendarAlarmClient`'s in-process state cannot be borrowed by another process through `ETMCalendar`. No bus interface is invented here, and read-only capability discovery must not activate or start Akonadi.

## Boundary and remaining work

A future **event-feed adapter** would need a supported Qt/Akonadi component and an explicit, stable identity for each occurrence. A proposed identity contract is:

- source/resource ID plus selected collection ID;
- incidence UID plus recurrence-instance key (including the original recurrence date/time for exceptions);
- event start/end in an unambiguous time representation and a revision/change token where the provider exposes one.

The adapter would translate selected events to local-core alerts, process collection/item create, update, and delete notifications, and reconcile after disconnects so canceled or moved events do not remain active. Calendar/collection selection must be explicit.

Because no public KalendarAlarmClient action API was found, a separate adapter cannot claim Kalendar's native persisted dismiss/snooze ownership. The choices are to implement alert/action persistence in the local core or wait for an upstream Kalendarac API. A local-core implementation may duplicate Kalendarac's ordinary reminders for the same calendars. Avoiding those duplicates requires a deliberate user policy to disable or narrow Kalendarac reminders for the selected sources; this project must not silently suppress global reminders. Until that policy and lifecycle are designed and tested, the adapter should remain a feed prototype, not a complete native alarm integration.

The next evidence step is a Qt/Akonadi helper with local lifecycle persistence, tested against a real Akonadi session and explicit duplicate-reminder policy. Alternatively, upstream could expose a supported public action/snooze API. Neither is delivered here.

Current status: **Akonadi source discovery documented; native adapter not delivered or runtime-tested.** The existing CLI can submit synthetic alerts, but that is not an Akonadi bridge.
