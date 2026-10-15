# Reminder controls in the panel

The optional native COSMIC applet controls reminder presentation. Calendar browsing stays in the existing COSMIC calendar applet.

## Install and use

Build/install with `./install.sh --with-applet --activate`, then add **Reminder Controls** in COSMIC Settings → Desktop → Panel → Applets. Cargo and the libcosmic build dependencies are required; the applet has its own pinned Cargo.lock. The default overlay-only install does not require Rust.

The popup has a confirmed **Reminders on/off** switch and **Open settings**. After opening the TOML file in your default desktop plain-text editor, save it and use **Apply settings** in the popup. Invalid settings keep the previous policy. Missing editor associations, service failures, and rejected actions appear in the popup. The panel icon distinguishes On, Off, and unavailable states; the popup also labels each state in text.

Off persists in the core database. It suppresses overlays and audio, including current presentation, while calendar ingestion continues. It does not dismiss events or provider alarms. Re-enabling presents eligible reminders; past events are not resurrected merely because the switch changed. The same control is available through `reminderctl enable`, `disable`, and `presentation-status`.

The applet is a session D-Bus client. It observes state and core owner changes rather than polling providers, uses bounded requests without implicit service activation, and retains confirmed state until a write succeeds. No agenda, month grid, provider setup, timed pause, or applet-owned alert database is included.

The native integration follows the [official applet template](https://github.com/pop-os/cosmic-applet-template) and [panel popup pattern](https://pop-os.github.io/libcosmic-book/panel-applets.html).

## Follow existing calendar selection

The [COSMIC calendar applet's EDS backend](https://github.com/Hasmolam/cosmic-ext-applet-calendar/blob/main/src/event/eds.rs) discovers enabled sources and excludes those with `Selected=false`. It already uses the same EDS account infrastructure as Evolution. An earlier proposal incorrectly described a separate account/sync model; no extra account setup is needed for its EDS backend.

Enabled and selected are different: a connected calendar may be unchecked in Evolution. The reminder core follows both by default.

The default `calendar.follow_selection = true` follows Evolution's checked calendars using [ESourceSelectable.get_selected](https://gnome.pages.gitlab.gnome.org/evolution-data-server/libedataserver/method.SourceSelectable.get_selected.html), intersected with the TOML include/exclude/all selection. Set `calendar.follow_selection = false` to ignore the checked state; disabled sources remain excluded. Registry source changes update eligibility. The same filter applies to native alarms, fallback scans, pending fetch results, and diagnostics; deselection withdraws local presentation without a provider dismissal.
