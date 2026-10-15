# Execute: reminder controls

## Aim and scope

A native COSMIC panel popup controls persistent reminder presentation and opens settings in the default desktop text editor. Calendar browsing stays in the existing calendar applet. EDS selection follows Evolution's checked calendars by default, with TOML overrides.

Preserve the accepted huge overlay, explicit action buttons, calendar synchronization while presentation is disabled, provider alarm state, unrelated desktop notifications, and existing configuration/state. No agenda, month grid, provider sign-in, or timed pause. A native applet is chosen because panel integration and native popup controls already exist in libcosmic.

## Execution checklist and adversarial checks

- [ ] Confirmed core state and persisted toggle; reject an applet-only boolean that resets at login.
- [ ] Disabled means no overlay/audio, including already-open views and renderer restart; reject merely hiding the panel icon.
- [ ] Sync and pending alarms retained; reject stopping the service or dismissing provider alarms.
- [ ] Evolution selected bit intersects source enablement/TOML selection on native/fallback and async completion; reject checking only source Enabled.
- [ ] Default editor launch uses argument/file APIs, supports spaces/XDG config paths, and reports missing associations; reject a shell-interpolated EDITOR command.
- [ ] Confirmed UI state, bounded requests, explicit failure/reconnect, no provider polling; reject optimistic state or service autostart from status reads.
- [ ] Add/remove only this applet's payload; preserve panel composition and unrelated notification daemons.
- [ ] Build the native applet and retire model-checkable core/editor risks in isolated checks; report native input interaction requiring the user separately.

Stop/pivot if libcosmic cannot build against the available COSMIC session or if disabling requires provider mutation. A service-stopped state is an error/recovery state, not a fake Off switch. No publication/visibility change is part of this task.
