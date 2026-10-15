# Experimental GNOME helper

This original GNOME Shell extension is an optional experiment for the GTK window with application ID `com.muness.calendar-reminder-overlay`. It waits briefly for that exact ID because Mutter may report null at creation, then waits for the actor's first frame (or an already-mapped window) before requesting `Meta.Window.make_above()`.

It does not create a Shell-rendered overlay, force fullscreen, activate or focus the window, move it between workspaces, or bypass the lock screen. It makes no stacking request while GNOME reports a locked session. The GTK application retains its own fullscreen and workspace behavior; stacking over other fullscreen applications is unverified. The helper releases closed windows and restores the prior above state when the extension is disabled. Mutter may reject the stacking request.

The helper is **not installed or enabled by `install.sh`**. Its metadata lists GNOME Shell 51 because that is the API version inspected for `get_gtk_application_id()` and `make_above()`; the extension has not been run in a GNOME session and this is not a compatibility claim. Test manually in a disposable GNOME session before enabling it for daily use. Disable/remove it with GNOME Extensions if the window behavior is wrong.

This code is a small project-authored helper; it does not copy another extension's implementation.

## Manual test matrix (not run here)

Use a disposable GNOME Shell 51 session. The extension can be copied to `~/.local/share/gnome-shell/extensions/calendar-reminder-overlay@muness.com/`, then enabled explicitly with `gnome-extensions enable calendar-reminder-overlay@muness.com`. It is not part of the normal installer.

1. With the extension enabled, show one synthetic reminder and confirm the exact app ID is the only window whose stacking changes.
2. Check two displays at different scale factors, then hotplug a display. Record which display receives the GTK-owned window and whether the helper changes only stacking.
3. Check a normal desktop window and an already-fullscreen application. Observe visibility and stacking without focus theft or a workspace switch; fullscreen stacking remains unverified.
4. Verify pointer and keyboard access to Open, Dismiss, and Snooze. Link activation must not take focus until the user chooses Open; record any focus handoff.
5. Repeat on another workspace and with the screen locked. The helper must make no stacking request while locked and must not bypass the lock screen.
6. Disable the extension while the overlay is visible. Confirm pending observation stops and the above state is restored only if the window was not already above before the helper touched it.
7. Close the overlay before disabling and repeat. Confirm closed-window handlers, timers, and signals are cleaned up and no stale callback fires afterward.

Record the GNOME Shell/Mutter version and evidence for each display, scale, hotplug, input, focus, lock, and cleanup result. Any failure keeps GNOME unsupported; do not enable the helper automatically. No item in this matrix has been tested here.
