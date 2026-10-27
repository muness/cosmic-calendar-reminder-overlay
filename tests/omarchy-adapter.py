#!/usr/bin/python3
"""Private-bus adapter checks: no real calendars, settings or notifications."""
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(sys.argv[1]).resolve()


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


adapter_path = root / 'adapters/omarchy/adapter.py'
adapter = load('adapter', adapter_path)
installer = load('adapter_install', root / 'adapters/omarchy/install.py')

# Finished alerts are absent, but a pending after-event alarm stays visible.
fixtures = [dict(id='gone', title='Past', state='dismissed', event_start=10),
            dict(id='followup', title='After-event alarm', state='scheduled', event_start=10, event_end=20),
            dict(id='future', title='<b>Literal title</b>', state='scheduled', event_start=200,
                 source_id='fixture', calendar_id='selected'),
            dict(id='active', title='Now', state='active')]
view = adapter.project(fixtures, {'eds_enabled': True, 'registry_state': 'ready',
                                 'calendars': [{'selected': False, 'state': 'error'},
                                               {'selected': True, 'state': 'fresh', 'name': 'Work', 'account': 'Account'}]}, False, now=100)
assert [item['id'] for item in view['items']] == ['active', 'followup', 'future']
assert view['available'] and view['enabled'] is False
assert view['calendar_problems'] == 0 and view['selected_calendars'] == 1
assert view['calendar_names'] == ['Work — Account']
assert view['items'][2]['title'] == '<b>Literal title</b>'
view = adapter.project(fixtures, {'calendars': []}, True, now=100, events=[
    {'id': 'future', 'title': 'Duplicate', 'event_start': 200},
    {'id': 'native', 'title': 'Work meeting', 'event_start': 150}])
assert [item['id'] for item in view['items']] == ['active', 'followup', 'native', 'future']
assert view['items'][2]['title'] == 'Work meeting'
view = adapter.project(fixtures, {'calendars': []}, True, now=100, declined={'future', 'followup'})
assert [item['id'] for item in view['items']] == ['active']



with tempfile.TemporaryDirectory(prefix='omarchy-reminder-test-') as directory:
    tmp = Path(directory)
    config_home = tmp / 'config space'
    shell = config_home / 'omarchy/shell.json'
    shell.parent.mkdir(parents=True)
    original = {'version': 1, 'idle': {'lock': 999}, 'bar': {'layout': {
        'left': [{'id': 'custom.widget', 'setting': 'preserve'}], 'center': [],
        'right': [{'id': 'omarchy.clock'}]}}}
    shell.write_text(json.dumps(original))
    runtime = tmp / 'runtime'
    (runtime / 'shell/Commons').mkdir(parents=True)
    (runtime / 'shell/Commons/ThemeColor.qml').touch()
    policy = config_home / 'calendar-reminder-overlay/config.toml'
    policy.parent.mkdir()
    policy.write_text('private calendar policy sentinel')
    installer.install(config_home, runtime, reload=False)
    installer.install(config_home, runtime, reload=False)
    result = json.loads(shell.read_text())
    assert result['idle'] == original['idle']
    assert result['bar']['layout']['left'] == original['bar']['layout']['left']
    assert sum(entry['id'] == installer.PLUGIN for entry in result['bar']['layout']['right']) == 1
    shutil.rmtree(runtime)  # Removal must still work after uninstalling Omarchy.
    installer.install(config_home, runtime, remove=True, reload=False)
    assert json.loads(shell.read_text())['bar'] == original['bar']
    assert policy.read_text() == 'private calendar policy sentinel'

    assert adapter.snapshot()['available'] is False, 'Private bus must not start a service'
    watcher = subprocess.Popen(['/usr/bin/python3', str(adapter_path), 'watch'], stdout=subprocess.PIPE, text=True)
    core = None

    def wait_state(predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if select.select([watcher.stdout], [], [], max(.01, deadline - time.monotonic()))[0]:
                value = json.loads(watcher.stdout.readline())
                if predicate(value):
                    return value
        raise AssertionError('Watcher did not report expected state')

    try:
        wait_state(lambda s: not s['available'])
        config = tmp / 'core.toml'
        config.write_text('version=1\n[selection]\nmode="all"\ncalendars=[]\n[calendar]\neds_enabled=false\n[audio]\nenabled=false\n')
        stub = tmp / 'renderer'
        stub.write_text('#!/bin/sh\nexec sleep 60\n')
        stub.chmod(0o755)
        core = subprocess.Popen([str(root / 'build/reminder-core'), '--headless', '--state', str(tmp / 'state.sqlite3'),
                                 '--config', str(config), '--helper', str(root / 'src/reminderctl.py'),
                                 '--overlay', str(stub)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_state(lambda s: s['available'])
        assert adapter.call('RefreshCalendars')[0] is False, 'Disabled integration must reject refresh'
        title = '<b>Literal</b> $(touch /tmp/never-run-reminder-adapter)'
        adapter.call('Submit', '(sssssst)', ('fixture', 'selected', 'fixture:adapter', title, '', '', 0))
        state = wait_state(lambda s: bool(s['items']))
        assert state['items'][0]['title'] == title
        result = subprocess.run(['/usr/bin/python3', str(adapter_path), 'disable'], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout
        state = wait_state(lambda s: s['available'] and s['enabled'] is False)
        assert len(state['items']) == 1, 'Disabling presentation must not dismiss alerts'
        adapter.call('SetPresentation', '(b)', (True,))
        wait_state(lambda s: s['available'] and s['enabled'] is True)
        core.terminate(); core.wait(timeout=5); core = None
        wait_state(lambda s: not s['available'] and s['enabled'] is None)
        core = subprocess.Popen([str(root / 'build/reminder-core'), '--headless', '--state', str(tmp / 'state.sqlite3'),
                                 '--config', str(config), '--helper', str(root / 'src/reminderctl.py'),
                                 '--overlay', str(stub)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        state = wait_state(lambda s: s['available'] and s['enabled'] is True)
        assert len(state['items']) == 1
        assert not Path('/tmp/never-run-reminder-adapter').exists()
    finally:
        watcher.terminate(); watcher.wait(timeout=5)
        if core:
            core.terminate(); core.wait(timeout=5)

print('Omarchy adapter: projection, install/remove, live signals, off/unavailable, and inert text checks passed')
