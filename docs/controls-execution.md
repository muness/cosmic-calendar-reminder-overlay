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

## Review and dissent

Root reviewed the actual core, helper, renderer, applet, and installer diffs against the two-control scope. Adjustments made before integration: effective source enablement includes disabled parent accounts; registry outages fail closed for presentation while retaining cached data; deselection invalidates async fetches; re-enable expires ended native occurrences; generic CLI presentation recovery also checks calendar eligibility. Snapshot retention no longer depends on whether the checkbox-following override is enabled. Removed a small allocation leak from the selection fixture.

Dissent: an applet-local switch would reset or lie after a core restart; stopping systemd would also stop synchronization; a second agenda would duplicate the existing calendar applet. Those shortcuts were rejected. The core persists one presentation flag; the applet uses confirmed replies and ownership/change signals, not provider polling. Default text editing uses Gio association/file APIs rather than interpolating a command. Errors remain actionable, and Apply settings is explicit after saving.

### Risk retirement

| Risk | Result | Evidence that rejects the tempting shortcut |
|---|---|---|
| Applet-only toggle/reset at login | Retired | Actual native Rust Get/Set client on private D-Bus; disabled state remains false after terminating/restarting the actual core. |
| Off stops synchronization or destroys alarms | Retired for mechanism | SetPresentation does not stop EDS or perform provider actions; isolated check submits and retains an active alert while disabled. Live provider signal continuity remains a user/session boundary. |
| Failed persistence yields optimistic On | Retired | SQLite rejection trigger makes SetPresentation return false and GetPresentation remain false. UI only changes confirmed state. |
| Re-enable resurfaces ended events | Retired | Isolated native-owned ended occurrence becomes expired before renderer eligibility. |
| Only Enabled checked / override bypasses disabled source | Retired | In-memory ESourceSelectable fixture covers selected false, override, include intersection and disabled source. Actual registry uses check_enabled for ancestor accounts. |
| Registry outage renders or deletes stale snapshots | Retired | UID presentation fails closed; fixture verifies cached snapshot retention even with follow_selection false. |
| Deselect race writes a late snapshot | Retired by code review | Registry invalidates/cancels matching scan; connect, fetch and parse completion gate on invalidation and effective selection. No provider mutation. |
| Shell interpolation / wrong editor / spaces | Retired | Actual Gio launch through a temporary plain-text association captures the exact XDG config path with spaces; TOML unchanged. |
| Status read starts a stopped core | Retired | Native Rust client's unavailable check leaves the private bus without a core owner; NoAutoStart and bounded timeout in source. |
| Applet packaging mutates ordinary notifications | Retired | Existing installer smoke passes; optional applet staging and actual desktop launcher with spaces verified in temporary home; uninstall removes only applet/settings payload and preserves config. |
| Native popup input and appearance | Human verification | Release build succeeds, native COSMIC patterns used; tooling cannot operate this session's native controls. User must click the installed popup. |

Focused build, calendar selection, renderer policy, EDS fixtures, installer smoke, shellcheck, Python syntax, Rust format, native D-Bus client, editor, and optional staging checks passed. The full core smoke reached its pre-existing audio shutdown tail and reported a surviving PID; a separate actual-core blocking-playback shutdown check passed and cleanup code was unchanged. Do not report a clean full-suite run. No calendar event was created and no notification daemon was reconfigured.
