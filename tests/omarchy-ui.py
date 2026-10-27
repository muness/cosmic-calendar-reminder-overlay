#!/usr/bin/python3
"""Headless Omarchy UI + fullscreen overlay acceptance on a private bus/home."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def inside(root):
    spec = importlib.util.spec_from_file_location('adapter', root / 'adapters/omarchy/adapter.py')
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    home = Path.home()
    config = home / 'fixture.toml'
    config.write_text('version=1\n[selection]\nmode="all"\ncalendars=[]\n[calendar]\neds_enabled=false\n[audio]\nenabled=false\n')
    stub = home / 'renderer-stub'
    stub.write_text('#!/bin/sh\nexec sleep 60\n'); stub.chmod(0o755)
    core = shell = foot = renderer = None

    def start_core(renderer, name):
        process = subprocess.Popen([str(root / 'build/reminder-core'), '--headless', '--state', str(home / name),
                                    '--config', str(config), '--helper', str(root / 'src/reminderctl.py'),
                                    '--overlay', str(renderer)])
        for _ in range(40):
            if adapter.snapshot()['available']:
                return process
            time.sleep(.1)
        raise RuntimeError('Fixture core failed to start')

    def screenshot(name):
        subprocess.run(['grim', str(home / name)], check=True)

    try:
        subprocess.run(['dbus-update-activation-environment', 'WAYLAND_DISPLAY', 'DISPLAY', 'XDG_CURRENT_DESKTOP'], check=True)
        core = start_core(stub, 'widget.sqlite3')
        adapter.call('Submit', '(sssssst)', ('fixture', 'selected', 'fixture:widget', 'Preview: planning review',
                                           'Synthetic calendar reminder; no real event.', '', 0))
        shell = subprocess.Popen(['quickshell', '--no-color', '-p', '/usr/share/omarchy/shell'])
        time.sleep(3)
        subprocess.run(['omarchy-shell', 'shell', 'summon', 'muness.calendar-reminders', '{}'], check=True)
        time.sleep(1)
        screenshot('widget-ready.png')
        adapter.call('SetPresentation', '(b)', (False,)); time.sleep(.5)
        screenshot('widget-off.png')
        core.terminate(); core.wait(timeout=5); core = None
        time.sleep(.5)
        screenshot('widget-unavailable.png')
        shell.terminate(); shell.wait(timeout=5); shell = None
        foot = subprocess.Popen(['foot', '--app-id=overlay-test', '/bin/sh', '-c', 'printf "Fullscreen app behind reminder\\n"; sleep 30'])
        time.sleep(.5)
        subprocess.run(['swaymsg', '[app_id="overlay-test"]', 'fullscreen', 'enable'], check=True)
        core = start_core(root / 'build/calendar-reminder-overlay', 'overlay.sqlite3')
        revision = adapter.call('Submit', '(sssssst)', ('', '', 'fixture:overlay', 'Preview reminder — no calendar event',
                                                       'This overlay must appear above the fullscreen app. Snooze and dismiss use the shared core.', '', 0))[0]
        renderer = subprocess.Popen([str(root / 'build/calendar-reminder-overlay'), '--watch'])
        time.sleep(1)
        screenshot('overlay-fullscreen.png')
        result = adapter.call('Act', '(stss)', ('fixture:overlay', revision, 'snooze', '1'))[0]
        assert result == 'accepted', result
        assert json.loads(adapter.call('Status', '(s)', ('fixture:overlay',))[0])['state'] == 'snoozed'
        time.sleep(.5)
        screenshot('overlay-snoozed.png')
        revision = adapter.call('Submit', '(sssssst)', ('', '', 'fixture:dismiss', 'Preview: dismiss check', '', '', 0))[0]
        time.sleep(.5)
        result = adapter.call('Act', '(stss)', ('fixture:dismiss', revision, 'dismiss', ''))[0]
        assert result == 'accepted', result
        assert json.loads(adapter.call('Status', '(s)', ('fixture:dismiss',))[0])['state'] == 'dismissed'
        print('OVERLAY_ACTIONS_PASSED', flush=True)
    finally:
        for process in (renderer, shell, core, foot):
            if process and process.poll() is None:
                process.terminate(); process.wait(timeout=5)
        subprocess.run(['swaymsg', 'exit'], check=False)


def main(root):
    trial = Path(tempfile.mkdtemp(prefix='omarchy-reminder-ui-'))
    home, runtime = trial / 'home', trial / 'run'
    home.mkdir(); runtime.mkdir(mode=0o700)
    for relative in ('.config/omarchy', '.local/state/omarchy'):
        target = home / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(Path.home() / relative, target, symlinks=True)
    subprocess.run(['/usr/bin/python3', str(root / 'adapters/omarchy/install.py'), '--no-reload', '--config-home', str(home / '.config')], check=True)
    (home / 'sway.conf').write_text('output * mode 1280x800\nexec /usr/bin/python3 ' + str(root / 'tests/omarchy-ui.py') + ' --inside ' + str(root) + '\n')
    env = os.environ.copy()
    env.update(OMARCHY_PATH='/usr/share/omarchy', PATH='/usr/share/omarchy/bin:' + env['PATH'],
               QT_QPA_PLATFORM='wayland', QT_QUICK_BACKEND='software', XDG_CURRENT_DESKTOP='sway',
               WLR_BACKENDS='headless', WLR_RENDERER='pixman', WLR_LIBINPUT_NO_DEVICES='1')
    for key in ('DBUS_SESSION_BUS_ADDRESS', 'WAYLAND_DISPLAY', 'DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE'):
        env.pop(key, None)
    command = ['timeout', '25', 'bwrap', '--ro-bind', '/', '/', '--bind', str(home), str(Path.home()),
               '--bind', str(runtime), env['XDG_RUNTIME_DIR'], '--tmpfs', '/tmp', '--tmpfs', '/run/dbus',
               '--proc', '/proc', '--dev', '/dev', '--unshare-pid', '--unshare-net', '--die-with-parent',
               'dbus-run-session', '--', 'sway', '-c', str(Path.home() / 'sway.conf')]
    result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (trial / 'startup.log').write_text(result.stdout)
    print('UI evidence:', trial)
    bad = [line for line in result.stdout.splitlines() if any(token in line for token in
           ('TypeError:', 'ReferenceError:', 'Failed to load', 'caused by @', 'plugin load failed', 'Traceback'))]
    if bad:
        print('\n'.join(bad))
    assert result.returncode == 0, result.returncode
    assert 'OVERLAY_ACTIONS_PASSED' in result.stdout
    assert not bad
    for name in ('widget-ready', 'widget-off', 'widget-unavailable', 'overlay-fullscreen', 'overlay-snoozed'):
        assert (home / (name + '.png')).is_file(), name


if __name__ == '__main__':
    if sys.argv[1] == '--inside':
        inside(Path(sys.argv[2]))
    else:
        main(Path(sys.argv[1]).resolve())
