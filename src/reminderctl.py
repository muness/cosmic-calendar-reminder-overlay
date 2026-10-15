#!/usr/bin/python3
"""Local alert CLI and strict stdlib TOML configuration loader."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import time
import tomllib
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

BUS = "org.muness.CalendarReminderOverlay.Core"
PATH = "/org/muness/CalendarReminderOverlay/Core"
IFACE = "org.muness.CalendarReminderOverlay.Core1"
CONFIG = pathlib.Path(os.environ.get("XDG_CONFIG_HOME", pathlib.Path.home() / ".config")) / "calendar-reminder-overlay" / "config.toml"


def _keys(section, allowed, name):
    if not isinstance(section, dict):
        raise ValueError(f"[{name}] must be a TOML table")
    unknown = sorted(set(section) - set(allowed))
    if unknown:
        raise ValueError(f"unknown key(s) in [{name}]: {', '.join(unknown)}")


def load_config(path):
    try:
        config = tomllib.loads(pathlib.Path(path).read_text())
    except FileNotFoundError:
        return {"selection": {"mode": "include", "calendars": []}, "reminders": {"fallback_lead_minutes": 5}, "snooze": {"minutes": [1, 5]}, "display": {"backend": "native", "outputs": "all"}, "appearance": {"theme": "default"}, "audio": {"enabled": False, "file": "", "repeat_seconds": 0, "volume": 0.5}, "calendar": {"eds_enabled": False}, "calendar_overrides": {}}
    except (tomllib.TOMLDecodeError, OSError) as exc:
        raise ValueError(f"cannot load config {path}: {exc}") from exc
    _keys(config, {"version", "selection", "reminders", "snooze", "display", "appearance", "audio", "calendar", "calendar_overrides"}, "root")
    if type(config.get("version")) is not int or config["version"] != 1:
        raise ValueError("config requires version = 1")
    selection = config.get("selection", {})
    _keys(selection, {"mode", "calendars"}, "selection")
    mode, calendars = selection.get("mode", "include"), selection.get("calendars", [])
    if mode not in ("include", "exclude", "all") or not isinstance(calendars, list) or any(not isinstance(v, str) or not v for v in calendars):
        raise ValueError("[selection] mode must be include/exclude/all and calendars must be non-empty string IDs")
    if mode == "all" and calendars:
        raise ValueError("[selection] calendars must be empty when mode = 'all'")
    reminders = config.get("reminders", {})
    _keys(reminders, {"fallback_lead_minutes"}, "reminders")
    lead = reminders.get("fallback_lead_minutes", 5)
    if type(lead) is not int or not 1 <= lead <= 120:
        raise ValueError("[reminders].fallback_lead_minutes must be an integer from 1 to 120")
    snooze = config.get("snooze", {})
    _keys(snooze, {"minutes"}, "snooze")
    minutes = snooze.get("minutes", [1, 5])
    if not isinstance(minutes, list) or not minutes or any(type(v) is not int or not 1 <= v <= 10080 for v in minutes):
        raise ValueError("[snooze].minutes must be a non-empty list of integers from 1 to 10080")
    display = config.get("display", {})
    _keys(display, {"backend", "outputs"}, "display")
    outputs = display.get("outputs", "all")
    if display.get("backend", "native") not in ("native", "fullscreen") or not isinstance(outputs, str) or not outputs or (outputs not in ("all", "first") and not outputs.startswith("connector:")):
        raise ValueError("[display] backend must be native/fullscreen and outputs must be all, first, or connector:NAME")
    appearance = config.get("appearance", {})
    _keys(appearance, {"theme"}, "appearance")
    if appearance.get("theme", "default") not in ("default", "dark", "light"):
        raise ValueError("[appearance].theme must be default, dark, or light")
    audio = config.get("audio", {})
    _keys(audio, {"enabled", "file", "repeat_seconds", "volume"}, "audio")
    if not isinstance(audio.get("enabled", False), bool) or not isinstance(audio.get("file", ""), str):
        raise ValueError("[audio].enabled must be boolean and [audio].file must be a path")
    repeat = audio.get("repeat_seconds", 0)
    if type(repeat) is not int or not 0 <= repeat <= 3600:
        raise ValueError("[audio].repeat_seconds must be an integer from 0 to 3600")
    volume = audio.get("volume", 0.5)
    if type(volume) not in (int, float) or not 0 <= volume <= 1:
        raise ValueError("[audio].volume must be a number from 0 to 1")
    calendar = config.get("calendar", {})
    _keys(calendar, {"eds_enabled"}, "calendar")
    if not isinstance(calendar.get("eds_enabled", False), bool):
        raise ValueError("[calendar].eds_enabled must be boolean")
    overrides = config.get("calendar_overrides", {})
    if not isinstance(overrides, dict):
        raise ValueError("[calendar_overrides] must be a table keyed by source_id/calendar_id")
    normalized_overrides = {}
    for key, override in overrides.items():
        if not isinstance(key, str) or key.count("/") != 1 or not all(key.split("/")):
            raise ValueError("calendar override keys must be source_id/calendar_id")
        if not isinstance(override, dict):
            raise ValueError(f"calendar override {key!r} must be a table")
        allowed_override = {"fallback_enabled", "fallback_lead_minutes", "audio_enabled", "audio_file", "audio_volume", "audio_repeat_seconds", "snooze_minutes"}
        _keys(override, allowed_override, f"calendar_overrides.{key}")
        item = {}
        if "fallback_enabled" in override:
            if not isinstance(override["fallback_enabled"], bool): raise ValueError(f"calendar override {key!r} fallback_enabled must be boolean")
            item["fallback_enabled"] = override["fallback_enabled"]
        if "fallback_lead_minutes" in override:
            lead = override["fallback_lead_minutes"]
            if type(lead) is not int or not 1 <= lead <= 120: raise ValueError(f"calendar override {key!r} fallback_lead_minutes must be an integer from 1 to 120")
            item["fallback_lead_minutes"] = lead
        if "audio_enabled" in override:
            if not isinstance(override["audio_enabled"], bool): raise ValueError(f"calendar override {key!r} audio_enabled must be boolean")
            item["audio_enabled"] = override["audio_enabled"]
        if "audio_file" in override:
            if not isinstance(override["audio_file"], str): raise ValueError(f"calendar override {key!r} audio_file must be a path")
            item["audio_file"] = override["audio_file"]
        if "audio_volume" in override:
            value = override["audio_volume"]
            if type(value) not in (int, float) or not 0 <= value <= 1: raise ValueError(f"calendar override {key!r} audio_volume must be from 0 to 1")
            item["audio_volume"] = value
        if "audio_repeat_seconds" in override:
            value = override["audio_repeat_seconds"]
            if type(value) is not int or not 0 <= value <= 3600: raise ValueError(f"calendar override {key!r} audio_repeat_seconds must be 0 to 3600")
            item["audio_repeat_seconds"] = value
        if "snooze_minutes" in override:
            value = override["snooze_minutes"]
            if not isinstance(value, list) or not value or any(type(v) is not int or not 1 <= v <= 10080 for v in value):
                raise ValueError(f"calendar override {key!r} snooze_minutes must be a non-empty list from 1 to 10080")
            item["snooze_minutes"] = value
        normalized_overrides[key] = item
    return {
        **config,
        "selection": {"mode": mode, "calendars": calendars},
        "calendar": {"eds_enabled": calendar.get("eds_enabled", False)},
        "audio": {"enabled": audio.get("enabled", False), "file": audio.get("file", ""), "repeat_seconds": repeat, "volume": volume},
        "reminders": {"fallback_lead_minutes": lead},
        "snooze": {"minutes": minutes},
        "display": {"backend": display.get("backend", "native"), "outputs": display.get("outputs", "all")},
        "appearance": {"theme": appearance.get("theme", "default")},
        "calendar_overrides": normalized_overrides,
    }


def selected(config, source_id, calendar_id):
    mode, ids = config["selection"]["mode"], set(config["selection"]["calendars"])
    key = f"{source_id}/{calendar_id}"
    if mode == "all":
        return True
    if mode == "include":
        return key in ids
    return key not in ids


def call(method, signature=None, args=()):
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    parameters = GLib.Variant(signature, args) if signature else None
    try:
        result = connection.call_sync(BUS, PATH, IFACE, method, parameters, None, Gio.DBusCallFlags.NONE, 20000, None)
    except GLib.Error as exc:
        raise SystemExit(exc.message) from exc
    return result.unpack() if result else ()


def ensure_service(core, config):
    try:
        connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        result = connection.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", GLib.Variant("(s)", (BUS,)), GLib.VariantType.new("(b)"), Gio.DBusCallFlags.NONE, 3000, None)
        running = result.unpack()[0]
    except GLib.Error:
        running = False
    repo = pathlib.Path(__file__).resolve().parent.parent
    executable = core or os.environ.get("REMINDER_CORE") or str(repo / "build" / "reminder-core")
    helper = str(pathlib.Path(__file__).resolve())
    overlay = str(repo / "build" / "calendar-reminder-overlay")
    if not running and not os.path.isfile(executable):
        raise SystemExit(f"alert service not running and core executable not found: {executable}")
    if not running:
        subprocess.Popen([executable, "--headless", "--config", config, "--helper", helper, "--overlay", overlay], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        for _ in range(50):
            time.sleep(.1)
            try:
                connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
                result = connection.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", GLib.Variant("(s)", (BUS,)), GLib.VariantType.new("(b)"), Gio.DBusCallFlags.NONE, 3000, None)
                if result.unpack()[0]:
                    running = True
                    break
            except GLib.Error:
                pass
    if not running:
        raise SystemExit("could not start local alert service")


def main():
    parser = argparse.ArgumentParser(prog="reminderctl")
    parser.add_argument("--config", default=str(CONFIG), help="TOML config path (defaults to XDG config)")
    parser.add_argument("--core", help=argparse.SUPPRESS)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("notify", "update"):
        p = sub.add_parser(name)
        p.add_argument("id", nargs="?", help="stable namespaced ID, for example timer:tea")
        p.add_argument("title", nargs="?")
        p.add_argument("--body", default="")
        p.add_argument("--url", default="")
        p.add_argument("--expires-at", type=int, default=0, help="Unix timestamp; 0 means no expiry")
        p.add_argument("--source-id", default="")
        p.add_argument("--calendar-id", default="")
        p.add_argument("--json", action="store_true", help="read alert fields as one JSON object from stdin")
        p.add_argument("--show", action="store_true", help="open the alert in the existing large GTK overlay")
    p = sub.add_parser("cancel"); p.add_argument("id")
    p = sub.add_parser("status"); p.add_argument("id")
    sub.add_parser("diagnostics", help="show calendar source freshness and errors from the running core")
    p = sub.add_parser("list")
    p = sub.add_parser("act"); p.add_argument("id"); p.add_argument("revision", type=int); p.add_argument("action", choices=["open", "dismiss", "snooze", "snooze-until"]); p.add_argument("--minutes", type=int, default=5); p.add_argument("--until", type=int, help="Unix timestamp for snooze-until")
    p = sub.add_parser("wait"); p.add_argument("id"); p.add_argument("--after-revision", type=int, default=0); p.add_argument("--timeout", type=int, default=0)
    p = sub.add_parser("check-selection"); p.add_argument("source_id"); p.add_argument("calendar_id")
    p = sub.add_parser("reload", help="explicitly reload the daemon policy from --config")
    p = sub.add_parser("config-check")
    p = sub.add_parser("calendars", help="list local EDS calendars without changing account settings")
    p.add_argument("--json", action="store_true")
    p.add_argument("--snippet", metavar="CALENDAR_ID", help="print a one-calendar TOML selection snippet for this calendar ID or full selection key")
    sub.add_parser("policy-json", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        config = load_config(args.config)
    except ValueError as exc:
        raise SystemExit(str(exc))
    if args.command == "config-check":
        print(json.dumps({"valid": True, "selection": config["selection"], "calendar": config["calendar"], "audio": config["audio"], "capabilities": {"display": "C GTK adapter", "audio": "pw-play", "calendar_listing": "EDS read-only"}}, indent=2)); return
    if args.command == "check-selection":
        print(json.dumps({"source_id": args.source_id, "calendar_id": args.calendar_id, "selected": selected(config, args.source_id, args.calendar_id)})); return
    if args.command == "policy-json":
        print(json.dumps({"selection": config["selection"], "audio": config["audio"], "calendar": config["calendar"], "reminders": config["reminders"], "snooze": config["snooze"], "display": config["display"], "appearance": config["appearance"], "calendar_overrides": config["calendar_overrides"]})); return
    if args.command == "calendars":
        try:
            gi.require_version("EDataServer", "1.2")
            from gi.repository import EDataServer
            registry = EDataServer.SourceRegistry.new_sync(None)
            sources = registry.list_sources(EDataServer.SOURCE_EXTENSION_CALENDAR)
            records = []
            for source in sources:
                uid = source.get_uid()
                parent_uid = source.get_parent()
                parent = registry.ref_source(parent_uid) if parent_uid else None
                extension = source.get_extension(EDataServer.SOURCE_EXTENSION_CALENDAR)
                selection_key = f"eds-local/{uid}"
                snippet = f'[calendar]\neds_enabled = true\n\n[selection]\nmode = "include"\ncalendars = [{json.dumps(selection_key)}]\n'
                records.append({"source_id": "eds-local", "calendar_id": uid, "selection_key": selection_key, "name": source.get_display_name(), "account": parent.get_display_name() if parent else "", "provider": extension.get_backend_name(), "selected": selected(config, "eds-local", uid), "toml_snippet": snippet})
            records.sort(key=lambda item: (item["account"].casefold(), item["name"].casefold(), item["calendar_id"]))
            if args.snippet:
                match = next((item for item in records if args.snippet in (item["calendar_id"], item["selection_key"])), None)
                if not match:
                    raise SystemExit(f"calendar {args.snippet!r} was not found; use one of the selection keys shown by `reminderctl calendars`")
                print(match["toml_snippet"], end="")
                return
            if args.json:
                print(json.dumps({"calendars": records, "error": None}, indent=2))
            else:
                if not records:
                    print("No EDS calendars found. Add or connect a calendar in your desktop calendar app, then run `reminderctl calendars` again.")
                for item in records: print(f"{'*' if item['selected'] else ' '} {item['name']} — {item['account'] or item['provider']} [{item['selection_key']}]")
                if records:
                    print("\nTo enable one calendar, copy its full selection key above into this config snippet (or run `reminderctl calendars --snippet <selection-key>`):")
                    print('[calendar]\neds_enabled = true\n\n[selection]\nmode = "include"\ncalendars = ["eds-local/<calendar-id>"]')
        except Exception as exc:
            message = f"EDS calendar discovery failed: {exc}"
            if args.json: print(json.dumps({"calendars": [], "error": message}, indent=2))
            else: raise SystemExit(message)
        return
    ensure_service(args.core, args.config)
    if args.command == "reload":
        ok = call("ReloadPolicy", "(ss)", (args.config, str(pathlib.Path(__file__).resolve())))[0]
        print(json.dumps({"reloaded": ok}))
        raise SystemExit(0 if ok else 1)
    if args.command in ("notify", "update"):
        data = {"id": args.id, "title": args.title, "body": args.body, "destination": args.url, "expires_at": args.expires_at, "source_id": args.source_id, "calendar_id": args.calendar_id}
        if args.json:
            try:
                incoming = json.load(sys.stdin)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"invalid JSON input: {exc}")
            allowed = set(data)
            if not isinstance(incoming, dict) or set(incoming) - allowed:
                raise SystemExit(f"JSON input must be an object with fields: {', '.join(sorted(allowed))}")
            data.update(incoming)
        if not isinstance(data["id"], str) or not isinstance(data["title"], str) or not data["id"] or not data["title"]:
            raise SystemExit("notify/update requires non-empty id and title (positional or JSON input)")
        if bool(data["source_id"]) != bool(data["calendar_id"]):
            raise SystemExit("source_id and calendar_id must be provided together")
        for key in ("id", "title", "body", "destination", "source_id", "calendar_id"):
            if not isinstance(data[key], str):
                raise SystemExit(f"{key} must be a string")
        if not isinstance(data["expires_at"], int) or isinstance(data["expires_at"], bool) or data["expires_at"] < 0:
            raise SystemExit("expires_at must be a non-negative Unix timestamp")
        method = "Submit" if args.command == "notify" else "Update"
        rev = call(method, "(sssssst)", (data["source_id"], data["calendar_id"], data["id"], data["title"], data["body"], data["destination"], data["expires_at"]))[0]
        print(json.dumps({"id": data["id"], "revision": rev, "accepted": True}))
        if args.show:
            binary = str(pathlib.Path(__file__).resolve().parent.parent / "build" / "calendar-reminder-overlay")
            if not os.path.isfile(binary): raise SystemExit("GTK overlay is not built; run make first")
            subprocess.Popen([binary, "--watch"], start_new_session=True)
        return
    if args.command == "cancel":
        print(json.dumps({"id": args.id, "cancelled": call("Cancel", "(s)", (args.id,))[0]})); return
    if args.command == "status": print(call("Status", "(s)", (args.id,))[0]); return
    if args.command == "diagnostics": print(call("Diagnostics")[0]); return
    if args.command == "list": print(call("List")[0]); return
    if args.command == "act":
        payload = str(args.minutes) if args.action == "snooze" else ""
        if args.action == "snooze-until":
            if args.until is None:
                raise SystemExit("snooze-until requires --until UNIX_TIMESTAMP")
            payload = str(args.until)
        if args.action == "open":
            try:
                title, body, destination, state, current_revision, event_start = call("Get", "(s)", (args.id,))
                if current_revision != args.revision or not destination:
                    raise SystemExit("Open requires the current alert revision and a destination URL")
                call("StopAudio")
                if not Gio.AppInfo.launch_default_for_uri(destination, None):
                    raise SystemExit("could not open alert destination: no URI handler accepted the link")
                payload = "launched"
            except GLib.Error as exc:
                raise SystemExit(f"could not open alert destination: {exc.message}")
        print(json.dumps({"result": call("Act", "(stss)", (args.id, args.revision, args.action, payload))[0]})); return
    if args.command == "wait":
        started = time.monotonic()
        while True:
            raw = call("Status", "(s)", (args.id,))[0]
            item = json.loads(raw)
            if item and item.get("action") and item.get("action_revision", 0) > args.after_revision:
                print(json.dumps(item)); return
            if args.timeout and time.monotonic() - started >= args.timeout:
                print(json.dumps({"timeout": True, "id": args.id})); raise SystemExit(3)
            time.sleep(.4)


if __name__ == "__main__":
    main()
