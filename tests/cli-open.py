import json, os, pathlib, subprocess, sys, tempfile, time
root = pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='reminder-open-review-') as directory:
    temp = pathlib.Path(directory)
    env = os.environ.copy()
    env['GIO_USE_VFS'] = 'local'
    for key, suffix in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:
        env[key] = str(temp / suffix)
        pathlib.Path(env[key]).mkdir()
    applications = pathlib.Path(env['XDG_DATA_HOME']) / 'applications'
    applications.mkdir()
    handler = temp / 'open-handler'
    handler.write_text('#!/bin/sh\nprintf "%s" "$1" > "$REMINDER_OPEN_LOG"\n')
    handler.chmod(0o700)
    (applications / 'reminder-review.desktop').write_text(f'[Desktop Entry]\nType=Application\nName=Isolated reminder handler\nExec={handler} %u\nMimeType=x-scheme-handler/reminder-review;\nNoDisplay=true\n')
    (pathlib.Path(env['XDG_CONFIG_HOME']) / 'mimeapps.list').write_text('[Default Applications]\nx-scheme-handler/reminder-review=reminder-review.desktop\n')
    env['REMINDER_OPEN_LOG'] = str(temp / 'opened')
    config = temp / 'config.toml'
    config.write_text('version = 1\n')
    cli = ['/usr/bin/python3', str(root/'src/reminderctl.py'), '--config', str(config)]
    log = open(temp/'core.log','w+')
    core = subprocess.Popen([str(root/'build/reminder-core'),'--headless','--state',str(temp/'state.db'),'--config',str(config),'--helper',str(root/'src/reminderctl.py')], env=env, stdout=log, stderr=log)
    try:
        # Use bus owner query, not a CLI call that can autostart another core.
        for _ in range(60):
            result = subprocess.run(['gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.muness.CalendarReminderOverlay.Core'],env=env,capture_output=True,text=True)
            if 'true' in result.stdout: break
            time.sleep(.05)
        def call(*args):
            return subprocess.run(cli+list(args),env=env,capture_output=True,text=True,timeout=5)
        assert call('notify','review:open','Open','--url','reminder-review://meeting').returncode == 0
        stale = call('act','review:open','0','open')
        assert stale.returncode != 0 and not (temp/'opened').exists(), stale
        opened = call('act','review:open','1','open')
        assert opened.returncode == 0, opened.stderr
        assert json.loads(opened.stdout)['result'] == 'accepted', opened.stdout
        for _ in range(40):
            if (temp/'opened').exists(): break
            time.sleep(.05)
        assert (temp/'opened').read_text() == 'reminder-review://meeting'
        state = json.loads(call('status','review:open').stdout)
        assert state['state'] == 'dismissed' and state['action'] == 'open', state
        assert call('notify','review:fail','Failure','--url','unregistered-review-scheme://meeting').returncode == 0
        failed = call('act','review:fail','1','open')
        assert failed.returncode != 0, failed
        assert json.loads(call('status','review:fail').stdout)['state'] == 'active'
        print('Actual CLI Open: isolated handler launch, stale revision rejection and failed-launch state preservation passed')
    finally:
        core.terminate()
        core.wait(timeout=5)
        log.seek(0)
        output = log.read()
        if 'CRITICAL' in output or 'incorrect' in output: print(output)
        log.close()
