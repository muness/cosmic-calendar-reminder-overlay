#!/usr/bin/python3
"""Omarchy presentation adapter. Calendar ownership stays in reminder-core."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

BUS = 'org.muness.CalendarReminderOverlay.Core'
PATH = '/org/muness/CalendarReminderOverlay/Core'
IFACE = BUS + '1'


def call(method, signature=None, args=()):
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    result = connection.call_sync(BUS, PATH, IFACE, method,
                                  GLib.Variant(signature, args) if signature else None,
                                  None, Gio.DBusCallFlags.NO_AUTO_START, 1500, None)
    return result.unpack()


def project(alerts, diagnostics, enabled, now=None):
    """Expose reminder data only: this is not a second calendar/agenda engine."""
    now = int(time.time()) if now is None else now
    pending = []
    active = 0
    for alert in alerts:
        state = alert.get('state')
        if state not in ('active', 'scheduled', 'snoozed'):
            continue
        if state == 'active':
            active += 1
        start = int(alert.get('event_start') or 0)
        # The core owns eligibility: an after-event alarm can still be pending
        # after event_end. Do not silently apply another scheduling policy here.
        pending.append({'id': alert['id'], 'title': str(alert.get('title') or 'Untitled reminder'),
                        'event_start': start, 'state': state,
                        'calendar': bool(alert.get('source_id') and alert.get('calendar_id'))})
    pending.sort(key=lambda item: (item['state'] != 'active', item['event_start'] or now, item['title']))
    calendars = [c for c in diagnostics.get('calendars', []) if c.get('selected')]
    problems = sum(bool(c.get('error')) or c.get('state') in ('error', 'stale') for c in calendars)
    return {'available': True, 'enabled': bool(enabled), 'active_count': active,
            'items': pending[:6], 'selected_calendars': len(calendars),
            'eds_enabled': bool(diagnostics.get('eds_enabled')),
            'calendar_problems': problems, 'registry_state': diagnostics.get('registry_state', 'unknown'),
            'updated_at': now, 'error': None}


def snapshot():
    try:
        enabled = call('GetPresentation')[0]
        alerts = json.loads(call('List')[0])
        diagnostics = json.loads(call('Diagnostics')[0])
        return project(alerts, diagnostics, enabled)
    except (GLib.Error, ValueError, KeyError, TypeError):
        # Never report a stopped/unresponsive service as an intentional off state.
        return {'available': False, 'enabled': None, 'items': [], 'active_count': 0,
                'error': 'Reminder service is unavailable. Start it or try again.'}


def watch():
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    pending = [0]

    def emit():
        pending[0] = 0
        print(json.dumps(snapshot(), ensure_ascii=False), flush=True)
        return GLib.SOURCE_REMOVE

    def changed(*_):
        if not pending[0]:
            pending[0] = GLib.timeout_add(120, emit)

    connection.signal_subscribe(BUS, IFACE, None, PATH, None, Gio.DBusSignalFlags.NONE, changed)
    connection.signal_subscribe('org.freedesktop.DBus', 'org.freedesktop.DBus', 'NameOwnerChanged',
                                '/org/freedesktop/DBus', BUS, Gio.DBusSignalFlags.NONE, changed)
    GLib.timeout_add_seconds(30, lambda: (changed(), GLib.SOURCE_CONTINUE)[1])
    emit()
    GLib.MainLoop().run()


def action(name):
    home = Path.home()
    calendar_launcher = home / '.local/bin/reminder-calendar'
    commands = {
        'start': ['/usr/bin/systemctl', '--user', 'start', 'calendar-reminder-overlay.service'],
        'settings': [str(home / '.local/bin/reminder-settings')],
        'reload': [str(home / '.local/bin/reminderctl'), 'reload'],
        'preview': [str(home / '.local/bin/reminder-demo')],
        'calendar': [str(calendar_launcher)] if os.access(calendar_launcher, os.X_OK) else ['/usr/bin/evolution', '-c', 'calendar'],
    }
    if name in ('enable', 'disable'):
        if not call('SetPresentation', '(b)', (name == 'enable',))[0]:
            raise RuntimeError('The service could not save the reminder setting.')
    elif name in ('settings', 'preview', 'calendar'):
        # These launch a long-lived window or isolated preview. They do not
        # inherit the shell's output pipe or hold the widget in a busy state.
        subprocess.Popen(commands[name], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif name == 'reload':
        call('GetPresentation')  # Do not let reminderctl implicitly start a stopped core.
        result = subprocess.run(commands[name], capture_output=True, text=True, timeout=10)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or 'Settings could not be applied. Check the configuration.')
    else:
        subprocess.run(commands[name], check=True, capture_output=True, text=True, timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['status', 'watch', 'enable', 'disable', 'start', 'settings', 'reload', 'preview', 'calendar'])
    args = parser.parse_args()
    if args.command == 'watch':
        watch()
    elif args.command == 'status':
        print(json.dumps(snapshot(), ensure_ascii=False))
    else:
        try:
            action(args.command)
            print(json.dumps({'ok': True}))
        except (GLib.Error, OSError, RuntimeError, subprocess.SubprocessError) as error:
            print(json.dumps({'ok': False, 'error': str(error)}))
            raise SystemExit(1)


if __name__ == '__main__':
    main()
