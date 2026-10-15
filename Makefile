CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
BUILD := build

.PHONY: all clean check
all: $(BUILD)/calendar-reminder-overlay $(BUILD)/reminder-core

$(BUILD):
	mkdir -p $@

$(BUILD)/calendar-reminder-overlay: src/calendar-reminder-overlay.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 libecal-2.0)

$(BUILD)/reminder-core: src/reminder-core.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gio-2.0 sqlite3 json-glib-1.0)

check: all
	dbus-run-session -- tests/core-smoke.sh "$(CURDIR)/$(BUILD)/reminder-core" "$(CURDIR)/src/reminderctl.py"

clean:
	rm -rf $(BUILD)
