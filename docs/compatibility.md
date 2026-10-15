# Desktop compatibility and evidence

This table describes what has been verified for this implementation branch. A prior COSMIC personal setup is user acceptance evidence for that setup; it is not a test of this packaging and core integration branch.

| Environment | Evidence for this branch | Claim |
|---|---|---|
| COSMIC Wayland | The older prototype was accepted in a COSMIC session. This integration branch has not been installed and exercised there. | Target; current branch unverified |
| Hyprland / Omarchy | No session test was available for this work. Layer Shell support depends on compositor behavior. | Unverified |
| KDE Plasma Wayland | No session test was available for this work. | Unverified |
| GNOME Wayland | No GNOME session or binary was available. A Mutter helper has not been implemented or tested. | Unsupported in this branch |

The GTK renderer requests the GTK Layer Shell overlay layer where available and has a fullscreen fallback. That fallback is not evidence that a window will remain above another fullscreen application. Do not infer support for fullscreen stacking, multi-monitor placement, focus behavior, or lock-screen presentation from a successful build.

## GNOME boundary

GNOME Shell does not expose GTK Layer Shell's compositor overlay behavior. Mutter APIs such as `Meta.Window.get_gtk_application_id()` and `Meta.Window.make_above()` are possible building blocks for a small Shell extension, but this repository has no tested extension. The window's GTK application ID is `com.muness.calendar-reminder-overlay`; an extension would need to wait for that exact ID because a newly created window may initially report no ID. Any future helper must avoid stealing focus, moving the user to another workspace, or bypassing the lock screen, and must clean up its signals and timers when disabled. A GNOME issue remains open until tested in a real GNOME session.

API references: [get_gtk_application_id](https://mutter.gnome.org/meta/method.Window.get_gtk_application_id.html), [make_above](https://mutter.gnome.org/meta/method.Window.make_above.html), and [GNOME extension creation](https://gjs.guide/extensions/development/creating.html).
