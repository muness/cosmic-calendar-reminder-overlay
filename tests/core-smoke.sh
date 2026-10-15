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
[[ "$(/usr/bin/python3 "$cli" --config "$config" check-selection fixture selected | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["selected"])')" == True ]]
[[ "$(/usr/bin/python3 "$cli" --config "$config" check-selection fixture other | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["selected"])')" == False ]]
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
stop_core
echo "core smoke checks passed"
