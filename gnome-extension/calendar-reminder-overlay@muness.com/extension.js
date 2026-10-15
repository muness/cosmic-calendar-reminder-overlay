import GLib from 'gi://GLib';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const TARGET_APP_ID = 'com.muness.calendar-reminder-overlay';
const OBSERVE_INTERVAL_MS = 100;
const OBSERVE_TIMEOUT_US = 5_000_000;

export default class CalendarReminderOverlayExtension extends Extension {
  enable() {
    this._enabled = true;
    this._pending = new Map();
    this._raised = new Map();
    this._windowCreatedId = global.display.connect('window-created', (_display, window) => {
      this._observeWindow(window);
    });

    for (const actor of global.get_window_actors())
      this._observeWindow(actor.meta_window);
  }

  disable() {
    this._enabled = false;

    if (this._windowCreatedId !== null && this._windowCreatedId !== undefined) {
      global.display.disconnect(this._windowCreatedId);
      this._windowCreatedId = null;
    }

    for (const [window, state] of [...this._pending])
      this._clearPending(window, state, true);
    this._pending.clear();

    for (const [window, state] of this._raised) {
      if (state.unmanagedId) {
        try {
          window.disconnect(state.unmanagedId);
        } catch (_error) {
          // A window may already have been unmanaged.
        }
      }
      if (state.wasAbove || typeof window.unmake_above !== 'function')
        continue;
      try {
        window.unmake_above();
      } catch (_error) {
        // A window may have closed while the extension was being disabled.
      }
    }
    this._raised.clear();
  }

  _observeWindow(window) {
    if (!this._enabled || !window || this._pending.has(window))
      return;

    const state = {
      deadline: GLib.get_monotonic_time() + OBSERVE_TIMEOUT_US,
      actor: null,
      firstFrameId: 0,
      sourceId: 0,
    };
    state.sourceId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, OBSERVE_INTERVAL_MS, () => {
      return this._pollWindow(window, state);
    });
    this._pending.set(window, state);
  }

  _pollWindow(window, state) {
    if (!this._enabled || GLib.get_monotonic_time() >= state.deadline) {
      this._clearPending(window, state, false);
      return GLib.SOURCE_REMOVE;
    }

    let appId;
    try {
      if (typeof window.get_gtk_application_id !== 'function') {
        this._clearPending(window, state, false);
        return GLib.SOURCE_REMOVE;
      }
      appId = window.get_gtk_application_id();
    } catch (_error) {
      this._clearPending(window, state, false);
      return GLib.SOURCE_REMOVE;
    }

    // Mutter documents this value as nullable. Keep observing only while it is
    // null; once another app ID appears, this window is permanently out of scope.
    if (appId === null || appId === undefined)
      return GLib.SOURCE_CONTINUE;
    if (appId !== TARGET_APP_ID) {
      this._clearPending(window, state, false);
      return GLib.SOURCE_REMOVE;
    }

    if (!state.actor) {
      try {
        state.actor = window.get_compositor_private();
      } catch (_error) {
        state.actor = null;
      }
      if (!state.actor)
        return GLib.SOURCE_CONTINUE;

      // Attach before the first rendered frame. If this extension saw an
      // already-mapped window, the first-frame signal may have fired earlier.
      if (typeof state.actor.connect === 'function') {
        state.firstFrameId = state.actor.connect('first-frame', () => {
          this._clearPending(window, state, true);
          console.debug(`${this.uuid}: matched ${appId} at first frame`);
          this._requestAbove(window);
        });
      }
    }

    if (typeof state.actor.is_mapped === 'function' && state.actor.is_mapped()) {
      this._clearPending(window, state, false);
      console.debug(`${this.uuid}: matched ${appId} on an already-mapped window`);
      this._requestAbove(window);
      return GLib.SOURCE_REMOVE;
    }

    return GLib.SOURCE_CONTINUE;
  }

  _clearPending(window, state, removeTimer) {
    if (removeTimer && state.sourceId)
      GLib.Source.remove(state.sourceId);
    state.sourceId = 0;

    if (state.actor && state.firstFrameId) {
      try {
        state.actor.disconnect(state.firstFrameId);
      } catch (_error) {
        // The compositor actor may already have been destroyed.
      }
      state.firstFrameId = 0;
    }

    if (this._pending.get(window) === state)
      this._pending.delete(window);
  }

  _requestAbove(window) {
    if (Main.sessionMode.isLocked)
      return;
    if (typeof window.is_above !== 'function' ||
        typeof window.make_above !== 'function' ||
        typeof window.unmake_above !== 'function')
      return;

    try {
      const state = { wasAbove: window.is_above(), unmanagedId: 0 };
      state.unmanagedId = window.connect('unmanaged', () => {
        this._raised.delete(window);
      });
      this._raised.set(window, state);
      window.make_above();
    } catch (_error) {
      // The compositor may reject the request; this helper makes no guarantee.
    }
  }
}
