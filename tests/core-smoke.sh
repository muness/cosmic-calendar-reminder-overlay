#!/usr/bin/env bash
set -euo pipefail

core="$1"
cli="$2"
tmp="$(mktemp -d)"
state="$tmp/alerts.sqlite3"
config="$tmp/config.toml"
daemon=""
renderer_log="$tmp/renderer.log"
renderer_stub="$tmp/renderer-stub"
cleanup() {
  if [[ -n "$daemon" ]]; then kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true; fi
  rm -rf "$tmp"
}
trap cleanup EXIT

cat >"$renderer_stub" <<EOF_RENDERER
#!/bin/sh
printf '%s\n' "\$*" >>"$renderer_log"
exec sleep 60
EOF_RENDERER
chmod +x "$renderer_stub"

cat >"$config" <<'EOF'
version = 1
[selection]
mode = "include"
calendars = ["fixture/selected"]
[reminders]
fallback_lead_minutes = 5
[snooze]
minutes = [1, 5]
[display]
backend = "native"
outputs = "all"
[appearance]
theme = "default"
[audio]
enabled = false
file = ""
repeat_seconds = 0
EOF

/usr/bin/python3 "$cli" --config "$config" config-check >/dev/null
/usr/bin/python3 - "$cli" "$tmp" <<'PYDEFAULTS'
import json, pathlib, subprocess, sys
cli, directory = sys.argv[1:]
config = pathlib.Path(directory) / 'minimal.toml'
override = pathlib.Path(directory) / 'override.toml'
config.write_text('version = 1\n')
policy = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', str(config), 'policy-json'], text=True))
assert policy['audio'] == {'enabled': False, 'file': '', 'repeat_seconds': 0, 'volume': 0.5}, policy
assert policy['calendar'] == {'eds_enabled': False, 'follow_selection': True}, policy
override.write_text('version=1\n[calendar]\nfollow_selection=false\n')
policy = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', str(override), 'policy-json'], text=True))
assert policy['calendar'] == {'eds_enabled': False, 'follow_selection': False}, policy
override.write_text('version=1\n[calendar]\nfollow_selection="false"\n')
bad = subprocess.run(['/usr/bin/python3', cli, '--config', str(override), 'config-check'], capture_output=True, text=True)
assert bad.returncode != 0, 'non-boolean follow_selection was accepted'
override.write_text('version=1\n[calendar_overrides."eds-local/cal"]\nfallback_enabled=true\nfallback_lead_minutes=17\n')
policy = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', str(override), 'policy-json'], text=True))
assert policy['calendar_overrides']['eds-local/cal'] == {'fallback_enabled': True, 'fallback_lead_minutes': 17}, policy
override.write_text('version=1\n[calendar_overrides."eds-local/cal"]\nfallback_lead_minutes=121\n')
bad = subprocess.run(['/usr/bin/python3', cli, '--config', str(override), 'config-check'], capture_output=True, text=True)
assert bad.returncode != 0, 'invalid per-calendar fallback lead was accepted'
PYDEFAULTS
cat >"$tmp/bad.toml" <<'EOF'
version = 1
[selection]
mode = "include"
calendars = []
typo = true
EOF
if /usr/bin/python3 "$cli" --config "$tmp/bad.toml" config-check >/dev/null 2>&1; then
  echo "unknown config key was accepted" >&2; exit 1
fi

start_core() {
  "$core" --headless --state "$state" --config "$config" --helper "$cli" --overlay "$renderer_stub" >"$tmp/core.log" 2>&1 &
  daemon=$!
  for _ in {1..40}; do
    if gdbus call --session --dest org.muness.CalendarReminderOverlay.Core \
      --object-path /org/muness/CalendarReminderOverlay/Core \
      --method org.muness.CalendarReminderOverlay.Core1.List >/dev/null 2>&1; then return; fi
    sleep .05
  done
  cat "$tmp/core.log" >&2
  echo "alert core did not start" >&2; exit 1
}
stop_core() { kill "$daemon"; wait "$daemon" 2>/dev/null || true; daemon=""; }
status() { /usr/bin/python3 "$cli" --config "$config" status "$1"; }
foreground() {
  gdbus call --session --dest org.muness.CalendarReminderOverlay.Core \
    --object-path /org/muness/CalendarReminderOverlay/Core \
    --method org.muness.CalendarReminderOverlay.Core1.SetForeground "$1" >/dev/null
}
assert_state() {
  local got
  got="$(status "$1" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["state"])')"
  [[ "$got" == "$2" ]] || { echo "$1 expected $2; got $got" >&2; exit 1; }
}

start_core
/usr/bin/python3 - "$cli" "$config" <<'PY'
import json, pathlib, subprocess, sys
cli, config = sys.argv[1:]
accepted = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'notify', '--json'], input=json.dumps({'id':'json:accepted','title':'JSON input','source_id':'fixture','calendar_id':'selected'}), text=True, capture_output=True)
assert accepted.returncode == 0, accepted.stderr
rejected = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'notify', '--json'], input=json.dumps({'id':'json:rejected','title':'Unselected','source_id':'fixture','calendar_id':'other'}), text=True, capture_output=True)
assert rejected.returncode != 0, 'service admitted an unselected calendar'
import gi
from gi.repository import Gio, GLib
connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
preview = connection.call_sync('org.muness.CalendarReminderOverlay.Core', '/org/muness/CalendarReminderOverlay/Core', 'org.muness.CalendarReminderOverlay.Core1', 'Get', GLib.Variant('(s)', ('json:accepted',)), GLib.VariantType.new('(sssstt)'), Gio.DBusCallFlags.NONE, 2000, None).unpack()
assert preview == ('JSON input', '', '', 'active', 1, 0), preview
diagnostics = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'diagnostics'], text=True))
assert diagnostics == {'eds_enabled': False, 'follow_selection': True, 'presentation_enabled': True, 'registry_state': 'disabled', 'active_scans': 0, 'last_scan_unix': 0, 'calendars': []}, diagnostics
items = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'list'], text=True))
owned = next(item for item in items if item['id'] == 'json:accepted')
assert (owned['source_id'], owned['calendar_id'], owned['owner']) == ('fixture', 'selected', 'producer'), owned
disabled = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'disable', '--json'], text=True, capture_output=True)
assert disabled.returncode == 0 and json.loads(disabled.stdout) == {'accepted': True, 'enabled': False}, disabled.stderr
assert json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'presentation-status', '--json'], text=True)) == {'enabled': False}
import gi
from gi.repository import Gio, GLib
connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
policy = connection.call_sync('org.muness.CalendarReminderOverlay.Core','/org/muness/CalendarReminderOverlay/Core','org.muness.CalendarReminderOverlay.Core1','GetPolicy',None,GLib.VariantType.new('(s)'),Gio.DBusCallFlags.NONE,2000,None).unpack()[0]
assert json.loads(policy)['presentation'] == {'enabled': False}, policy
active_while_disabled = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'status', 'json:accepted'], text=True))
assert active_while_disabled['state'] == 'active', active_while_disabled
subprocess.run(['gdbus','call','--session','--dest','org.muness.CalendarReminderOverlay.Core','--object-path','/org/muness/CalendarReminderOverlay/Core','--method','org.muness.CalendarReminderOverlay.Core1.SetForeground','json:accepted'], check=True, stdout=subprocess.DEVNULL)
PY
[[ ! -e "$renderer_log" ]] || { echo "disabled presentation started the renderer" >&2; exit 1; }
stop_core
start_core
/usr/bin/python3 - "$cli" "$config" <<'PRESENTATION'
import json, subprocess, sys
cli, config = sys.argv[1:]
status = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'presentation-status', '--json'], text=True))
assert status == {'enabled': False}, status
active = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'status', 'json:accepted'], text=True))
assert active['state'] == 'active', active
enabled = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'enable', '--json'], text=True, capture_output=True)
assert enabled.returncode == 0 and json.loads(enabled.stdout) == {'accepted': True, 'enabled': True}, enabled.stderr
PRESENTATION
/usr/bin/python3 - "$cli" "$config" <<'PYSEL'
import json, pathlib, subprocess, sys
cli, config = sys.argv[1:]
unselected = pathlib.Path(config).with_name('unselected.toml')
unselected.write_text('version=1\n[selection]\nmode="include"\ncalendars=[]\n')
withdraw = subprocess.run(['/usr/bin/python3', cli, '--config', str(unselected), 'reload'], text=True, capture_output=True)
assert withdraw.returncode == 0 and json.loads(withdraw.stdout)['reloaded'], withdraw.stderr
items = json.loads(subprocess.check_output(['/usr/bin/python3', cli, '--config', config, 'list'], text=True))
assert next(item for item in items if item['id'] == 'json:accepted')['state'] == 'withdrawn', items
restore = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'reload'], text=True, capture_output=True)
assert restore.returncode == 0 and json.loads(restore.stdout)['reloaded'], restore.stderr
PYSEL
/usr/bin/python3 "$cli" --config "$config" notify renderer:armed Renderer >/dev/null
sleep 1.2
[[ ! -e "$renderer_log" ]] || { echo "headless core started a renderer before presentation was armed" >&2; exit 1; }
foreground renderer:armed
for _ in {1..20}; do [[ -s "$renderer_log" ]] && break; sleep .05; done
[[ -s "$renderer_log" ]] || { echo "armed alert did not start the shared renderer" >&2; exit 1; }
/usr/bin/python3 "$cli" --config "$config" notify timer:tea Tea --body "boil water" >/dev/null
[[ "$(/usr/bin/python3 "$cli" --config "$config" act timer:tea 1 snooze --minutes 10080 | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == accepted ]]
assert_state timer:tea snoozed
stop_core
start_core
assert_state timer:tea snoozed
/usr/bin/python3 "$cli" --config "$config" update timer:tea "Updated tea" >/dev/null
assert_state timer:tea snoozed

# Advance the persisted deadline to exercise restart recovery without waiting a minute.
/usr/bin/python3 - "$state" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
db.execute("UPDATE alerts SET snooze_until=1 WHERE id='timer:tea'")
db.commit()
PY
current_revision="$(status timer:tea | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["revision"])')"
assert_state timer:tea active
[[ "$(/usr/bin/python3 "$cli" --config "$config" act timer:tea 1 dismiss | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == failed ]]
current_revision="$(status timer:tea | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["revision"])')"
[[ "$(/usr/bin/python3 "$cli" --config "$config" act timer:tea "$current_revision" dismiss | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == accepted ]]
/usr/bin/python3 "$cli" --config "$config" update timer:tea "Changed after dismissal" >/dev/null
assert_state timer:tea dismissed

until_start=$(( $(date +%s) + 600 ))
/usr/bin/python3 "$cli" --config "$config" notify timer:start "Starts later" >/dev/null
[[ "$(/usr/bin/python3 "$cli" --config "$config" act timer:start 1 snooze-until --until "$until_start" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == accepted ]]
stored_until="$(status timer:start | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["snooze_until"])')"
[[ "$stored_until" == "$until_start" ]] || { echo "until-start timestamp was rounded: $stored_until != $until_start" >&2; exit 1; }

/usr/bin/python3 "$cli" --config "$config" notify cancel:fixture Cancel >/dev/null
/usr/bin/python3 "$cli" --config "$config" cancel cancel:fixture >/dev/null
assert_state cancel:fixture cancelled
/usr/bin/python3 "$cli" --config "$config" notify expiry:fixture Expired --expires-at 1 >/dev/null
assert_state expiry:fixture expired
/usr/bin/python3 "$cli" --config "$config" notify stale:fixture Stale >/dev/null
/usr/bin/python3 "$cli" --config "$config" update stale:fixture New >/dev/null
[[ "$(/usr/bin/python3 "$cli" --config "$config" act stale:fixture 1 dismiss | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == failed ]]
/usr/bin/python3 "$cli" --config "$config" notify open:unverified Open --url https://example.org >/dev/null
[[ "$(gdbus call --session --dest org.muness.CalendarReminderOverlay.Core --object-path /org/muness/CalendarReminderOverlay/Core --method org.muness.CalendarReminderOverlay.Core1.Act open:unverified 1 open '')" == "('failed',)" ]]
# Content changes must not manufacture another user action result, and a
# read-only client must not silently replace the service's selected calendars.
/usr/bin/python3 - "$cli" "$config" "$tmp" <<'PYTEST'
import json, pathlib, subprocess, sys
cli, config, directory = sys.argv[1:]
def run(*args, config_path=config):
    return subprocess.run(['/usr/bin/python3', cli, '--config', config_path, *args], text=True, capture_output=True)
assert run('notify','review:result','Result').returncode == 0
assert run('act','review:result','1','dismiss').returncode == 0
assert run('update','review:result','Changed title').returncode == 0
assert run('wait','review:result','--after-revision','2','--timeout','1').returncode == 3
other = pathlib.Path(directory)/'different.toml'
other.write_text('version=1\n[selection]\nmode="all"\n')
assert run('status','review:result',config_path=str(other)).returncode == 0
rejected = run('notify','review:excluded','Excluded','--source-id','fixture','--calendar-id','other')
assert rejected.returncode != 0, 'read-only status call broadened daemon selection'
for invalid in ('version=true\n', 'version=1\n[reminders]\nfallback_lead_minutes=true\n', 'version=1\naudio=[]\n'):
    other.write_text(invalid)
    result = run('config-check',config_path=str(other))
    assert result.returncode != 0 and 'Traceback' not in result.stderr, result.stderr
PYTEST
/usr/bin/python3 "$cli" --config "$config" notify eds:orphan "Native reminder unavailable" >/dev/null
/usr/bin/python3 - "$state" <<'PYOWNER'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
db.execute("UPDATE alerts SET source_id='eds-local',calendar_id='fixture',owner='native',state='snoozed',snooze_until=1 WHERE id='eds:orphan'")
db.commit()
PYOWNER
assert_state eds:orphan snoozed
if /usr/bin/python3 "$cli" --config "$config" update eds:orphan "Must not replace provider state" >/dev/null 2>&1; then
  echo "producer update replaced a native-owned alert" >&2; exit 1
fi
orphan_revision="$(status eds:orphan | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["revision"])')"
[[ "$(/usr/bin/python3 "$cli" --config "$config" act eds:orphan "$orphan_revision" dismiss | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == failed ]]
[[ "$(/usr/bin/python3 "$cli" --config "$config" cancel eds:orphan | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["cancelled"])')" == False ]]
stop_core
# Upgrade a group-2a database: native rows must not become locally scheduled.
/usr/bin/python3 - "$state" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
db.execute('ALTER TABLE alerts DROP COLUMN owner')
db.commit()
PY
start_core
assert_state eds:orphan snoozed
[[ "$(status eds:orphan | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["owner"])')" == native ]]
stop_core
cat >"$tmp/pw-play-stub" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$REMINDER_AUDIO_LOG"
if [ "${REMINDER_AUDIO_MODE:-short}" = "block" ]; then
  printf '%s\n' "$$" >"$REMINDER_AUDIO_PID"
  exec sleep 60
fi
EOF
chmod +x "$tmp/pw-play-stub"
touch "$tmp/tone.wav"
cat >"$tmp/audio.toml" <<EOF
version = 1
[selection]
mode = "include"
calendars = []
[audio]
enabled = true
file = "$tmp/tone.wav"
repeat_seconds = 1
volume = 0.25
EOF
export REMINDER_PLAYBACK_PROGRAM="$tmp/pw-play-stub"
export REMINDER_AUDIO_LOG="$tmp/audio.log"
export REMINDER_AUDIO_PID="$tmp/audio.pid"
export REMINDER_AUDIO_MODE=short
: >"$tmp/audio.log"
"$core" --headless --state "$tmp/audio.sqlite3" --config "$tmp/audio.toml" --helper "$cli" >"$tmp/audio-core.log" 2>&1 &
daemon=$!
for _ in {1..40}; do
  if gdbus call --session --dest org.muness.CalendarReminderOverlay.Core --object-path /org/muness/CalendarReminderOverlay/Core --method org.muness.CalendarReminderOverlay.Core1.List >/dev/null 2>&1; then break; fi
  sleep .05
done
/usr/bin/python3 "$cli" --config "$tmp/audio.toml" notify audio:repeat Sound >/dev/null
sleep .2
[[ ! -s "$tmp/audio.log" ]] || { echo "invisible alert played audio before foreground admission" >&2; exit 1; }
foreground audio:repeat
for _ in {1..40}; do [[ "$(wc -l <"$tmp/audio.log" 2>/dev/null || echo 0)" -ge 2 ]] && break; sleep .1; done
[[ "$(wc -l <"$tmp/audio.log")" -ge 2 ]] || { echo "configured repeat did not launch a second playback" >&2; cat "$tmp/audio-core.log" >&2; exit 1; }
grep -q -- '--volume 0.250' "$tmp/audio.log" || { echo "configured volume was not passed to playback" >&2; exit 1; }
/usr/bin/python3 "$cli" --config "$tmp/audio.toml" notify audio:hidden Hidden >/dev/null
/usr/bin/python3 "$cli" --config "$tmp/audio.toml" cancel audio:hidden >/dev/null
previous_plays="$(wc -l <"$tmp/audio.log")"
for _ in {1..40}; do [[ "$(wc -l <"$tmp/audio.log")" -gt "$previous_plays" ]] && break; sleep .1; done
[[ "$(wc -l <"$tmp/audio.log")" -gt "$previous_plays" ]] || { echo "cancelling a hidden alert muted the foreground alert" >&2; exit 1; }
stop_core
cat >"$tmp/audio-once.toml" <<EOF
version = 1
[selection]
mode = "include"
calendars = []
[audio]
enabled = true
file = "$tmp/tone.wav"
repeat_seconds = 0
volume = 0.25
EOF
export REMINDER_AUDIO_MODE=short
: >"$tmp/audio.log"
"$core" --headless --state "$tmp/audio-once.sqlite3" --config "$tmp/audio-once.toml" --helper "$cli" >"$tmp/audio-once-core.log" 2>&1 &
daemon=$!
for _ in {1..40}; do
  if gdbus call --session --dest org.muness.CalendarReminderOverlay.Core --object-path /org/muness/CalendarReminderOverlay/Core --method org.muness.CalendarReminderOverlay.Core1.List >/dev/null 2>&1; then break; fi
  sleep .05
done
/usr/bin/python3 "$cli" --config "$tmp/audio-once.toml" notify audio:once Once >/dev/null
foreground audio:once
sleep 2.2
[[ "$(wc -l <"$tmp/audio.log")" -eq 1 ]] || { echo "one-shot audio replayed during active alert" >&2; exit 1; }
/usr/bin/python3 "$cli" --config "$tmp/audio-once.toml" update audio:once "Updated title" >/dev/null
/usr/bin/python3 "$cli" --config "$tmp/audio-once.toml" notify audio:expired Expired --expires-at 1 >/dev/null
[[ "$(wc -l <"$tmp/audio.log")" -eq 1 ]] || { echo "expired or updated alert restarted one-shot playback" >&2; exit 1; }
[[ "$(/usr/bin/python3 "$cli" --config "$tmp/audio-once.toml" status audio:expired | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["state"])')" == expired ]]
gdbus call --session --dest org.muness.CalendarReminderOverlay.Core --object-path /org/muness/CalendarReminderOverlay/Core --method org.muness.CalendarReminderOverlay.Core1.StopAudio >/dev/null
foreground audio:once
foreground audio:once
sleep 2.2
[[ "$(wc -l <"$tmp/audio.log")" -eq 1 ]] || { echo "StopAudio restarted while alert remained active" >&2; exit 1; }
stop_core
export REMINDER_AUDIO_MODE=block
"$core" --headless --state "$tmp/audio-stop.sqlite3" --config "$tmp/audio.toml" --helper "$cli" >"$tmp/audio-stop-core.log" 2>&1 &
daemon=$!
for _ in {1..40}; do
  if gdbus call --session --dest org.muness.CalendarReminderOverlay.Core --object-path /org/muness/CalendarReminderOverlay/Core --method org.muness.CalendarReminderOverlay.Core1.List >/dev/null 2>&1; then break; fi
  sleep .05
done
/usr/bin/python3 "$cli" --config "$tmp/audio.toml" notify audio:stop Sound >/dev/null
foreground audio:stop
for _ in {1..40}; do [[ -s "$tmp/audio.pid" ]] && break; sleep .05; done
audio_pid="$(cat "$tmp/audio.pid")"
[[ -n "$audio_pid" ]] || { echo "blocking playback stub did not start" >&2; exit 1; }
[[ "$(/usr/bin/python3 "$cli" --config "$tmp/audio.toml" act audio:stop 1 snooze --minutes 5 | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')" == accepted ]]
for _ in {1..40}; do kill -0 "$audio_pid" 2>/dev/null || break; sleep .05; done
if kill -0 "$audio_pid" 2>/dev/null; then echo "snooze did not stop active playback" >&2; exit 1; fi
: >"$tmp/audio.pid"
/usr/bin/python3 "$cli" --config "$tmp/audio.toml" notify audio:shutdown Shutdown >/dev/null
foreground audio:shutdown
for _ in {1..40}; do [[ -s "$tmp/audio.pid" ]] && break; sleep .05; done
shutdown_audio_pid="$(cat "$tmp/audio.pid")"
[[ -n "$shutdown_audio_pid" ]] || { echo "shutdown playback stub did not start" >&2; exit 1; }
stop_core
for _ in {1..40}; do kill -0 "$shutdown_audio_pid" 2>/dev/null || break; sleep .05; done
if kill -0 "$shutdown_audio_pid" 2>/dev/null; then echo "core shutdown left playback running" >&2; exit 1; fi
unset REMINDER_PLAYBACK_PROGRAM REMINDER_AUDIO_LOG REMINDER_AUDIO_PID REMINDER_AUDIO_MODE
echo "core lifecycle and regression checks passed"
