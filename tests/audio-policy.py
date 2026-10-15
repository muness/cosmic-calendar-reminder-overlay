import json, os, pathlib, subprocess, sys, tempfile, time, wave
import gi
from gi.repository import Gio, GLib
root=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory() as directory:
    p=pathlib.Path(directory); config=p/'config.toml'; log=p/'plays'
    config.write_text('version=1\n[selection]\nmode="all"\n[audio]\nenabled=true\nvolume=0.5\n[calendar_overrides."fixture/quiet"]\naudio_enabled=false\n[calendar_overrides."fixture/loud"]\naudio_volume=0.2\n')
    stub=p/'player'; stub.write_text('#!/usr/bin/python3\nimport json,os,sys\nwith open(os.environ["REVIEW_PLAYS"],"a") as out: out.write(json.dumps(sys.argv[1:])+"\\n")\n'); stub.chmod(0o700)
    env={**os.environ,'REMINDER_PLAYBACK_PROGRAM':str(stub),'REVIEW_PLAYS':str(log),'XDG_CACHE_HOME':str(p/'cache')}
    daemon=subprocess.Popen([str(root/'build/reminder-core'),'--headless','--state',str(p/'state.db'),'--config',str(config),'--helper',str(root/'src/reminderctl.py')],env=env,stdout=subprocess.DEVNULL)
    bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
    def call(method,signature=None,args=()):
        return bus.call_sync('org.muness.CalendarReminderOverlay.Core','/org/muness/CalendarReminderOverlay/Core','org.muness.CalendarReminderOverlay.Core1',method,GLib.Variant(signature,args) if signature else None,None,Gio.DBusCallFlags.NO_AUTO_START,1000,None)
    try:
        for _ in range(50):
            try: call('List'); break
            except GLib.Error: time.sleep(.05)
        def submit(identity,calendar): call('Submit','(sssssst)',('fixture',calendar,identity,'Audio policy','','',0))
        submit('review:quiet','quiet'); call('SetForeground','(s)',('review:quiet',)); time.sleep(.2); assert not log.exists(), 'quiet calendar sounded'
        submit('review:loud','loud'); call('SetForeground','(s)',('review:loud',))
        for _ in range(50):
            if log.exists(): break
            time.sleep(.05)
        args=json.loads(log.read_text().splitlines()[0]); assert args[:2]==['--volume','0.200'],args
        with wave.open(args[2]) as tone: assert tone.getnframes()>0 and tone.getnchannels()==1
        call('StopAudio'); call('SetForeground','(s)',('review:loud',)); time.sleep(.2); assert len(log.read_text().splitlines())==1, 'foreground refresh undid mute'
        print('Per-calendar silence/volume, generated playable WAV, and foreground mute preservation passed')
    finally:
        daemon.terminate(); daemon.wait(timeout=5)
