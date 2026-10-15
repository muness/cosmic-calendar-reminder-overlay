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
        return {"selection": {"mode": "include", "calendars": []}, "reminders": {"fallback_lead_minutes": 5}, "snooze": {"minutes": [1, 5]}, "display": {"backend": "native", "outputs": "all"}, "appearance": {"theme": "default"}, "audio": {"enabled": False, "file": "", "repeat_seconds": 0}}
    except (tomllib.TOMLDecodeError, OSError) as exc:
        raise ValueError(f"cannot load config {path}: {exc}") from exc
    _keys(config, {"version", "selection", "reminders", "snooze", "display", "appearance", "audio"}, "root")
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
    if display.get("backend", "native") not in ("native", "fullscreen") or display.get("outputs", "all") not in ("all", "primary"):
        raise ValueError("[display] backend must be native/fullscreen and outputs must be all/primary")
    appearance = config.get("appearance", {})
    _keys(appearance, {"theme"}, "appearance")
    if appearance.get("theme", "default") not in ("default", "dark", "light"):
        raise ValueError("[appearance].theme must be default, dark, or light")
    audio = config.get("audio", {})
    _keys(audio, {"enabled", "file", "repeat_seconds"}, "audio")
    if not isinstance(audio.get("enabled", False), bool) or not isinstance(audio.get("file", ""), str):
        raise ValueError("[audio].enabled must be boolean and [audio].file must be a path")
    repeat = audio.get("repeat_seconds", 0)
    if type(repeat) is not int or not 0 <= repeat <= 3600:
        raise ValueError("[audio].repeat_seconds must be an integer from 0 to 3600")
    return {**config, "selection": {"mode": mode, "calendars": calendars}}


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
    p = sub.add_parser("list")
    p = sub.add_parser("act"); p.add_argument("id"); p.add_argument("revision", type=int); p.add_argument("action", choices=["open", "dismiss", "snooze"]); p.add_argument("--minutes", type=int, default=5)
    p = sub.add_parser("wait"); p.add_argument("id"); p.add_argument("--after-revision", type=int, default=0); p.add_argument("--timeout", type=int, default=0)
    p = sub.add_parser("check-selection"); p.add_argument("source_id"); p.add_argument("calendar_id")
    p = sub.add_parser("reload", help="explicitly reload the daemon policy from --config")
    p = sub.add_parser("config-check")
    sub.add_parser("policy-json", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        config = load_config(args.config)
    except ValueError as exc:
        raise SystemExit(str(exc))
    if args.command == "config-check":
        print(json.dumps({"valid": True, "selection": config["selection"], "capabilities": {"display": "C GTK adapter", "audio": "not implemented", "calendar_listing": "EDS adapter pending"}}, indent=2)); return
    if args.command == "check-selection":
        print(json.dumps({"source_id": args.source_id, "calendar_id": args.calendar_id, "selected": selected(config, args.source_id, args.calendar_id)})); return
    if args.command == "policy-json":
        print(json.dumps(config["selection"])); return
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
            subprocess.Popen([binary, "--synthetic", data["id"]], start_new_session=True)
        return
    if args.command == "cancel":
        print(json.dumps({"id": args.id, "cancelled": call("Cancel", "(s)", (args.id,))[0]})); return
    if args.command == "status": print(call("Status", "(s)", (args.id,))[0]); return
    if args.command == "list": print(call("List")[0]); return
    if args.command == "act":
        payload = str(args.minutes) if args.action == "snooze" else ""
        if args.action == "open":
            try:
                title, body, destination, state, current_revision = call("Get", "(s)", (args.id,))
                if current_revision != args.revision or not destination:
                    raise SystemExit("Open requires the current alert revision and a destination URL")
                Gio.AppInfo.launch_default_for_uri(destination, None)
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
