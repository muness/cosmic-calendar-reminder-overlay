#!/usr/bin/python3
"""Opt-in live Hyprland preview on a private D-Bus and temporary alert state."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location('adapter', root / 'adapters/omarchy/adapter.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
if adapter.snapshot()['available']:
    raise SystemExit('Refusing to run on an existing reminder bus. Use dbus-run-session.')


def wait_for(predicate, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.1)
    raise AssertionError('Native preview did not reach the expected state')


with tempfile.TemporaryDirectory(prefix='omarchy-native-reminder-') as directory:
    tmp = Path(directory)
    config = tmp / 'fixture.toml'
    config.write_text('version=1\n[selection]\nmode="all"\ncalendars=[]\n[calendar]\neds_enabled=false\n[audio]\nenabled=false\n')
    core = renderer = None
    try:
        core = subprocess.Popen([str(root / 'build/reminder-core'), '--headless', '--state', str(tmp / 'alerts.sqlite3'),
                                 '--config', str(config), '--helper', str(root / 'src/reminderctl.py'),
                                 '--overlay', str(root / 'build/calendar-reminder-overlay')],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_for(lambda: adapter.snapshot()['available'])
        revision = adapter.call('Submit', '(sssssst)', ('', '', 'fixture:native',
            'Preview reminder — no calendar event',
            'Omarchy adapter check. This silent preview will snooze, return, and dismiss automatically.', '', 0))[0]
        renderer = subprocess.Popen([str(root / 'build/calendar-reminder-overlay'), '--watch'],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_for(lambda: 'cosmic-calendar-reminder-overlay' in subprocess.check_output(['hyprctl', 'layers', '-j'], text=True))
        print('Native Hyprland overlay layer mapped.', flush=True)
        time.sleep(1)
        assert adapter.call('Act', '(stss)', ('fixture:native', revision, 'snooze-until', str(int(time.time()) + 3)))[0] == 'accepted'
        assert json.loads(adapter.call('Status', '(s)', ('fixture:native',))[0])['state'] == 'snoozed'
        wait_for(lambda: json.loads(adapter.call('Status', '(s)', ('fixture:native',))[0])['state'] == 'active')
        revision = adapter.call('Get', '(s)', ('fixture:native',))[4]
        assert adapter.call('Act', '(stss)', ('fixture:native', revision, 'dismiss', ''))[0] == 'accepted'
        assert json.loads(adapter.call('Status', '(s)', ('fixture:native',))[0])['state'] == 'dismissed'
        print('Native overlay mapping, snooze/return, and dismiss passed; no real calendar state used.')
    finally:
        for process in (renderer, core):
            if process and process.poll() is None:
                process.terminate(); process.wait(timeout=5)
