CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
BUILD := build

.PHONY: all clean check check-layout
all: $(BUILD)/calendar-reminder-overlay $(BUILD)/reminder-core

$(BUILD):
	mkdir -p $@

$(BUILD)/calendar-reminder-overlay: src/calendar-reminder-overlay.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 libecal-2.0)

$(BUILD)/reminder-core: src/reminder-core.c src/reminder-eds.c src/reminder-eds.h | $(BUILD)
	$(CC) $(CFLAGS) src/reminder-core.c src/reminder-eds.c -o $@ $$(pkg-config --cflags --libs gio-2.0 sqlite3 json-glib-1.0 libecal-2.0) -lm

$(BUILD)/policy-renderer-check: tests/policy-renderer.c src/calendar-reminder-overlay.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 libecal-2.0)

$(BUILD)/overlay-layout-check: tests/overlay-layout.c src/calendar-reminder-overlay.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 libecal-2.0)

# Requires a GTK display; builds and allocates widgets without showing a window.
check-layout: $(BUILD)/overlay-layout-check
	$(BUILD)/overlay-layout-check

$(BUILD)/eds-fixtures: tests/eds-fixtures.c src/reminder-eds.c src/reminder-eds.h | $(BUILD)
	$(CC) $(CFLAGS) tests/eds-fixtures.c src/reminder-eds.c -o $@ $$(pkg-config --cflags --libs gio-2.0 sqlite3 json-glib-1.0 libecal-2.0)

$(BUILD)/calendar-selection-check: tests/calendar-selection.c src/reminder-core.c src/reminder-eds.c src/reminder-eds.h | $(BUILD)
	$(CC) $(CFLAGS) tests/calendar-selection.c src/reminder-eds.c -o $@ $$(pkg-config --cflags --libs gio-2.0 sqlite3 json-glib-1.0 libecal-2.0) -lm

check: all $(BUILD)/policy-renderer-check $(BUILD)/eds-fixtures $(BUILD)/calendar-selection-check
	$(BUILD)/policy-renderer-check
	$(BUILD)/eds-fixtures
	$(BUILD)/calendar-selection-check
	dbus-run-session -- tests/core-smoke.sh "$(CURDIR)/$(BUILD)/reminder-core" "$(CURDIR)/src/reminderctl.py"
	dbus-run-session -- /usr/bin/python3 tests/cli-open.py "$(CURDIR)"
	dbus-run-session -- /usr/bin/python3 tests/audio-policy.py "$(CURDIR)"

clean:
	rm -rf $(BUILD)
