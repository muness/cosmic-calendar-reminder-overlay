#pragma once

#include <libecal/libecal.h>
#include <sqlite3.h>

typedef struct {
	gchar *source_uid;
	gchar *event_uid;
	gchar *recurrence_id; /* Empty for a non-recurring event; otherwise original instance key. */
	gchar *occurrence;
	gchar *title;
	gchar *body;
	gchar *destination;
	gint64 start;
	gint64 end;
	gboolean native_alarm;
	gboolean all_day;
	gboolean declined;
} ReminderEdsEvent;

void reminder_eds_event_free (gpointer data);
GPtrArray *reminder_eds_expand_ics (const gchar *source_uid,
	const gchar *ical_text, const gchar *identity,
	ICalTimezone *default_timezone,
	gint64 range_start,
	gint64 range_end,
	gboolean for_every_event,
	GError **error);
gchar *reminder_eds_alert_id (const gchar *source_uid,
	const gchar *event_uid,
	const gchar *recurrence_id);
gboolean reminder_eds_apply_snapshot (sqlite3 *db,
	const gchar *source_uid,
	GPtrArray *events,
	gboolean fetch_succeeded,
	gboolean fallback_enabled,
	gboolean all_day_enabled,
	gboolean global_native_default,
	guint fallback_lead_minutes,
	gint64 now,
	const gchar *error_message,
	GError **error);
