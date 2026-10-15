#!/usr/bin/python3
import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gtk4LayerShell", "1.0")
from gi.repository import Gtk, GLib, Gdk, Gtk4LayerShell

Gtk.init()

css = Gtk.CssProvider()
css.load_from_data(b"""
window { background: #151821; color: #f4f5f7; }
.title { font-size: 38pt; font-weight: 700; }
.details { font-size: 23pt; }
.status { font-size: 20pt; color: #a9c7ff; min-height: 42px; }
button { font-size: 23pt; font-weight: 650; padding: 22px 30px; min-width: 230px; min-height: 78px; }
""")
Gtk.StyleContext.add_provider_for_display(
    Gdk.Display.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
)

window = Gtk.Window(title="Calendar reminder mock")
window.set_default_size(1240, 680)
window.set_resizable(False)
Gtk4LayerShell.init_for_window(window)
Gtk4LayerShell.set_namespace(window, "cosmic-calendar-reminder-demo")
Gtk4LayerShell.set_layer(window, Gtk4LayerShell.Layer.OVERLAY)
for edge in (Gtk4LayerShell.Edge.TOP, Gtk4LayerShell.Edge.BOTTOM, Gtk4LayerShell.Edge.LEFT, Gtk4LayerShell.Edge.RIGHT):
    Gtk4LayerShell.set_anchor(window, edge, True)
Gtk4LayerShell.set_exclusive_zone(window, 0)
Gtk4LayerShell.set_keyboard_mode(window, Gtk4LayerShell.KeyboardMode.EXCLUSIVE)
window.set_decorated(False)
loop = GLib.MainLoop()

outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=26)
outer.add_css_class("reminder-overlay")
outer.set_margin_top(70)
outer.set_margin_bottom(54)
outer.set_margin_start(80)
outer.set_margin_end(80)

heading = Gtk.Label(label="Design review with Alex")
heading.add_css_class("title")
heading.set_xalign(0)
outer.append(heading)

body = Gtk.Label(label="Today · 2:30 PM\nQuarterly planning · Room 4B\nGoogle Meet: meet.google.com/abc-defg-hij")
body.add_css_class("details")
body.set_xalign(0)
outer.append(body)

status = Gtk.Label(label="This is a UI mock. No calendar or meeting link is connected.")
status.add_css_class("status")
status.set_xalign(0)
outer.append(status)

spacer = Gtk.Box()
spacer.set_vexpand(True)
outer.append(spacer)

buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=22)
buttons.set_homogeneous(True)
outer.append(buttons)

join = Gtk.Button(label="Open meeting")
dismiss = Gtk.Button(label="Dismiss")
for button in (join, dismiss):
    buttons.append(button)

snooze_buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=22)
snooze_buttons.set_homogeneous(True)
outer.append(snooze_buttons)
snooze = Gtk.Button(label="Snooze 1 min")
snooze5 = Gtk.Button(label="Snooze 5 min")
until = Gtk.Button(label="Until start")
for button in (snooze, snooze5, until):
    snooze_buttons.append(button)

def join_clicked(_button):
    print("MOCK HANDLER: Open meeting", flush=True)
    status.set_label("Example only: the Open meeting button handler fired. No browser was opened.")


join.connect("clicked", join_clicked)


def snooze_clicked(_button):
    print("MOCK HANDLER: Snooze 5 min (mocked as 5 seconds)", flush=True)
    snooze.set_sensitive(False)
    status.set_label("Snoozed. Mock reminder returns in 5 seconds.")
    GLib.timeout_add_seconds(5, reminder_returns)


def reminder_returns():
    print("MOCK HANDLER: Snoozed reminder returned", flush=True)
    status.set_label("Reminder returned after the mock snooze.")
    snooze.set_sensitive(True)
    return GLib.SOURCE_REMOVE

snooze.connect("clicked", snooze_clicked)
snooze5.connect("clicked", snooze_clicked)
until.connect("clicked", snooze_clicked)


def dismiss_clicked(_button):
    print("MOCK HANDLER: Dismiss", flush=True)
    window.close()


dismiss.connect("clicked", dismiss_clicked)

window.set_child(outer)
window.connect("close-request", lambda *_: (loop.quit(), False)[1])
print("Fullscreen mock overlay ready with visible, clickable reminder controls.", flush=True)
window.present()
loop.run()
