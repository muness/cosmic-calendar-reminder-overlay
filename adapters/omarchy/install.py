#!/usr/bin/python3
"""Install/remove the Omarchy UI without changing shared calendar policy or state."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PLUGIN = 'muness.calendar-reminders'


def update_layout(config, remove=False):
    # Preserve placement/settings if already installed, including a moved widget.
    sections = config.setdefault('bar', {}).setdefault('layout', {})
    present = False
    for section in ('left', 'center', 'right'):
        entries = sections.setdefault(section, [])
        if not isinstance(entries, list):
            raise ValueError('Invalid bar layout: ' + section)
        if remove:
            sections[section] = [entry for entry in entries if entry.get('id') != PLUGIN]
        else:
            present |= any(entry.get('id') == PLUGIN for entry in entries)
    if not remove and not present:
        right = sections['right']
        index = next((i for i, entry in enumerate(right) if entry.get('id') == 'omarchy.clock'), len(right))
        right.insert(index, {'id': PLUGIN})
    if remove:
        config['plugins'] = [entry for entry in config.get('plugins', []) if entry.get('id') != PLUGIN]
    return config


def install(config_home, runtime, remove=False, reload=True):
    shell_config = config_home / 'omarchy/shell.json'
    source_config = shell_config if shell_config.exists() else runtime / 'config/omarchy/shell.json'
    config = update_layout(json.loads(source_config.read_text()), remove)
    target = config_home / 'omarchy/plugins' / PLUGIN
    if target.is_symlink() or shell_config.is_symlink():
        raise ValueError('Refusing to replace symlinked plugin or shell configuration')
    source = Path(__file__).resolve().parent
    # Read/validate all payloads before changing an installed plugin or layout.
    payloads = {}
    if not remove:
        payloads = {name: (source / name).read_text() for name in ('manifest.json', 'Widget.qml', 'adapter.py')}
        assert json.loads(payloads['manifest.json'])['id'] == PLUGIN
        compile(payloads['adapter.py'], 'adapter.py', 'exec')
        if not (runtime / 'shell/Commons/ThemeColor.qml').exists():
            if not (runtime / 'shell/Commons/Color.qml').exists():
                raise ValueError('Could not find Omarchy theme API')
            payloads['Widget.qml'] = payloads['Widget.qml'].replace('import qs.Commons\n', 'import qs.Commons\nimport qs.Commons as Common\n').replace('ThemeColor.', 'Common.Color.')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup = config_home / 'calendar-reminder-overlay/adapter-backups' / stamp
    backup.mkdir(parents=True, mode=0o700)
    if shell_config.exists():
        shutil.copy2(shell_config, backup / 'shell.json')
    if target.exists():
        shutil.copytree(target, backup / PLUGIN)
    if remove:
        if target.exists():
            shutil.rmtree(target)
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.calendar-reminders-', dir=target.parent))
        try:
            for name, text in payloads.items():
                (stage / name).write_text(text)
            if target.exists():
                shutil.rmtree(target)
            stage.rename(target)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
    shell_config.parent.mkdir(parents=True, exist_ok=True)
    staging_config = shell_config.with_name('.shell-reminders-new.json')
    staging_config.write_text(json.dumps(config, indent=2) + '\n')
    staging_config.replace(shell_config)
    print(('Removed' if remove else 'Installed') + ' Omarchy calendar reminder widget. Backup: ' + str(backup))
    if reload:
        helper = runtime / 'bin/omarchy-shell'
        env = dict(os.environ, OMARCHY_PATH=str(runtime), PATH=str(runtime / 'bin') + ':' + os.environ.get('PATH', ''))
        try:
            result = subprocess.run([str(helper), 'shell', 'rescanPlugins'], env=env, capture_output=True, text=True, timeout=10)
            refreshed = result.returncode == 0
        except (OSError, subprocess.SubprocessError):
            refreshed = False
        if not refreshed:
            print('Files are ready; the widget will load at the next Omarchy shell start.')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--uninstall', action='store_true')
    parser.add_argument('--no-reload', action='store_true')
    parser.add_argument('--config-home', type=Path, default=Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')))
    parser.add_argument('--runtime', type=Path, default=Path(os.environ.get('OMARCHY_PATH', '/usr/share/omarchy')))
    args = parser.parse_args()
    install(args.config_home, args.runtime, args.uninstall, not args.no_reload)


if __name__ == '__main__':
    main()
