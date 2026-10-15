# Desktop compatibility and evidence

This table describes what has been verified for this implementation branch. A prior COSMIC personal setup is user acceptance evidence for that setup; it is not a test of this packaging and core integration branch.

| Environment | Evidence for this branch | Claim |
|---|---|---|
| COSMIC Wayland | The older prototype was accepted in a COSMIC session. This integration branch has not been installed and exercised there. | Target; current branch unverified |
| Hyprland / Omarchy | No session test was available for this work. Layer Shell support depends on compositor behavior. | Unverified |
| KDE Plasma Wayland | No session test was available for this work. | Unverified |
| GNOME Wayland | A small optional Mutter helper prototype is included, but no GNOME session or binary was available to test it. | Experimental; unverified and unsupported |

The GTK renderer requests the GTK Layer Shell overlay layer where available and has a fullscreen fallback. That fallback is not evidence that a window will remain above another fullscreen application. Do not infer support for fullscreen stacking, multi-monitor placement, focus behavior, or lock-screen presentation from a successful build.

## GNOME boundary

GNOME Shell does not expose GTK Layer Shell's compositor overlay behavior. The optional [experimental helper](../gnome-extension/README.md) checks the exact GTK app ID `com.muness.calendar-reminder-overlay` because a new window may initially report no ID, then requests `Meta.Window.make_above()`. It keeps the GTK window's fullscreen and workspace choices, does not activate or focus the window, and restores the prior above state when disabled. It is not installed or enabled by the package installer. A GNOME issue remains open until the behavior is tested in a real GNOME session; Mutter may reject the stacking request.

API references: [get_gtk_application_id](https://mutter.gnome.org/meta/method.Window.get_gtk_application_id.html), [get_compositor_private](https://mutter.gnome.org/meta/method.Window.get_compositor_private.html), [Meta.WindowActor first-frame](https://mutter.gnome.org/meta/signal.WindowActor.first-frame.html), [is_above](https://mutter.gnome.org/meta/method.Window.is_above.html), [make_above](https://mutter.gnome.org/meta/method.Window.make_above.html), [unmake_above](https://mutter.gnome.org/meta/method.Window.unmake_above.html), and [GNOME extension creation](https://gjs.guide/extensions/development/creating.html).
