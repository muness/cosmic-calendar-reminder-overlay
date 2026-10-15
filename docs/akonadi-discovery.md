# Akonadi alarm discovery

**Result:** a source review establishes an adapter boundary, not a delivered native Akonadi adapter. Akonadi is not installed in the development environment, and no live service or calendar account was started or changed for this investigation.

## Inspected source

The inspected primary source is KDE's [`akonadi-calendar` repository at commit `4848a675df7c014581efdf7452de56bcb1254685`](https://github.com/KDE/akonadi-calendar/tree/4848a675df7c014581efdf7452de56bcb1254685).

At that revision:

- `reminder-daemon/kalendaralarmclient.h` owns `checkAlarms`, alarm dismissal/suspension, and persisted notifications in-process.
- `src/etmcalendar.h` exposes selected collections, interval alarms, and calendar-change notifications for an application using the relevant KDE APIs.
- `reminder-daemon/org.kde.calendar.Calendar.xml` exports `showIncidenceByUid`; it does not export a generic alarm feed or alarm actions.

These interfaces imply that a native adapter needs an in-process Qt/Akonadi component (or a new upstream-supported API). The inspected D-Bus interface is insufficient to safely implement dismiss/snooze ownership from this project. No bus interface is invented here, and read-only capability discovery must not activate or start Akonadi.

## Boundary and remaining work

A future adapter should consume selected collections and alarm/occurrence data through the appropriate KDE libraries, preserve the scheduler's native dismiss/snooze semantics, and translate resulting alerts to the local core. It needs a supported build environment with Qt/Akonadi dependencies and tests against a real Akonadi session before native support can be claimed.

Current status: **Akonadi source discovery documented; native adapter not delivered or runtime-tested.** The existing CLI can submit synthetic alerts, but that is not an Akonadi bridge.
