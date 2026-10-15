# Desktop compatibility and evidence

This table describes what has been verified for this implementation branch. The packaged alpha was installed and exercised locally on COSMIC Wayland on 2026-10-15. This is a local acceptance result, not a claim for every compositor or account.

| Environment | Evidence for this branch | Claim |
|---|---|---|
| COSMIC Wayland | Packaged install, calendar ingestion, huge overlay, and an explicit CLI reminder exercised locally; user accepted the overlay. | Locally exercised alpha |
| Hyprland / Omarchy | No session test was available for this work. Layer Shell support depends on compositor behavior. | Unverified |
| KDE Plasma Wayland | No session test was available for this work. | Unverified |
| GNOME Wayland | A small optional Mutter helper prototype is included, but no GNOME session or binary was available to test it. | Experimental; unverified and unsupported |

The GTK renderer requests the GTK Layer Shell overlay layer where available and has a fullscreen fallback. That fallback is not evidence that a window will remain above another fullscreen application. Do not infer support for fullscreen stacking, multi-monitor placement, focus behavior, or lock-screen presentation from a successful build.

## GNOME boundary

GNOME Shell does not expose GTK Layer Shell's compositor overlay behavior. The optional [experimental helper](../gnome-extension/README.md) checks the exact GTK app ID `com.muness.calendar-reminder-overlay` because a new window may initially report no ID, then requests `Meta.Window.make_above()`. It keeps the GTK window's fullscreen and workspace choices, does not activate or focus the window, and restores the prior above state when disabled. It is not installed or enabled by the package installer. A GNOME issue remains open until the behavior is tested in a real GNOME session; Mutter may reject the stacking request.

API references: [get_gtk_application_id](https://mutter.gnome.org/meta/method.Window.get_gtk_application_id.html), [get_compositor_private](https://mutter.gnome.org/meta/method.Window.get_compositor_private.html), [Meta.WindowActor first-frame](https://mutter.gnome.org/meta/signal.WindowActor.first-frame.html), [is_above](https://mutter.gnome.org/meta/method.Window.is_above.html), [make_above](https://mutter.gnome.org/meta/method.Window.make_above.html), [unmake_above](https://mutter.gnome.org/meta/method.Window.unmake_above.html), and [GNOME extension creation](https://gjs.guide/extensions/development/creating.html).

## Known COSMIC runtime behavior

On the tested host, destroying/remapping a GTK Layer Shell surface can disconnect the renderer's Wayland client. The core retains alert state and supervises renderer restarts with bounded retry. Theme changes update CSS without replacing the surface. This workaround does not establish behavior on another compositor. Multi-monitor, lock/unlock, suspend/resume, and keyboard-only interaction still need broader session coverage.
