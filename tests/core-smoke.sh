#!/usr/bin/env bash
set -euo pipefail

core="$1"
cli="$2"
tmp="$(mktemp -d)"
state="$tmp/alerts.sqlite3"
config="$tmp/config.toml"
daemon=""
cleanup() {
  if [[ -n "$daemon" ]]; then kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true; fi
  rm -rf "$tmp"
}
trap cleanup EXIT

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
  "$core" --headless --state "$state" --config "$config" --helper "$cli" >"$tmp/core.log" 2>&1 &
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
assert_state() {
  local got
  got="$(status "$1" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["state"])')"
  [[ "$got" == "$2" ]] || { echo "$1 expected $2; got $got" >&2; exit 1; }
}

start_core
/usr/bin/python3 - "$cli" "$config" <<'PY'
import json, subprocess, sys
cli, config = sys.argv[1:]
accepted = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'notify', '--json'], input=json.dumps({'id':'json:accepted','title':'JSON input','source_id':'fixture','calendar_id':'selected'}), text=True, capture_output=True)
assert accepted.returncode == 0, accepted.stderr
rejected = subprocess.run(['/usr/bin/python3', cli, '--config', config, 'notify', '--json'], input=json.dumps({'id':'json:rejected','title':'Unselected','source_id':'fixture','calendar_id':'other'}), text=True, capture_output=True)
assert rejected.returncode != 0, 'service admitted an unselected calendar'
PY
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
stop_core
echo "core lifecycle and regression checks passed"
