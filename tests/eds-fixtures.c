#include "../src/reminder-eds.h"

static gchar *
row_field (sqlite3 *db, const gchar *id, const gchar *field)
{
	gchar *query = g_strdup_printf ("SELECT %s FROM alerts WHERE id=?", field);
	sqlite3_stmt *statement = NULL;
	gchar *value = NULL;
	if (sqlite3_prepare_v2 (db, query, -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (statement) == SQLITE_ROW) value = g_strdup ((const gchar *) sqlite3_column_text (statement, 0));
	}
	sqlite3_finalize (statement); g_free (query);
	return value;
}

static const gchar ics[] =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
"BEGIN:VEVENT\r\nUID:recur\r\nSUMMARY:Daily standup\r\nDTSTART:20261015T130000Z\r\nDTEND:20261015T133000Z\r\nRRULE:FREQ=DAILY;COUNT=4\r\nEXDATE:20261016T130000Z\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:recur\r\nRECURRENCE-ID:20261017T130000Z\r\nSUMMARY:Moved standup\r\nDTSTART:20261017T160000Z\r\nDTEND:20261017T163000Z\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:cancel\r\nSUMMARY:Cancelled\r\nDTSTART:20261015T140000Z\r\nDTEND:20261015T143000Z\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:alarm\r\nSUMMARY:Native alarm\r\nDTSTART:20261015T150000Z\r\nDTEND:20261015T153000Z\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT5M\r\nDESCRIPTION:Reminder\r\nEND:VALARM\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:all-day\r\nSUMMARY:No School\r\nDTSTART;VALUE=DATE:20261016\r\nDTEND;VALUE=DATE:20261017\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT5M\r\nDESCRIPTION:Reminder\r\nEND:VALARM\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:inherit-alarm\r\nSUMMARY:Inherited alarm\r\nDTSTART:20261015T170000Z\r\nDTEND:20261015T173000Z\r\nRRULE:FREQ=DAILY;COUNT=2\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT10M\r\nDESCRIPTION:Reminder\r\nEND:VALARM\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:inherit-alarm\r\nRECURRENCE-ID:20261016T170000Z\r\nSUMMARY:Moved inherited alarm\r\nDTSTART:20261016T190000Z\r\nDTEND:20261016T193000Z\r\nEND:VEVENT\r\n"
"END:VCALENDAR\r\n";

int
main (void)
{
	GError *error = NULL;
	ICalTimezone *utc = i_cal_timezone_get_utc_timezone ();
    const gchar *responses = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
      "BEGIN:VEVENT\r\nUID:declined\r\nDTSTART:20261015T130000Z\r\nDTEND:20261015T140000Z\r\nATTENDEE;PARTSTAT=DECLINED:mailto:ME@example.com\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:accepted\r\nDTSTART:20261015T150000Z\r\nDTEND:20261015T160000Z\r\nATTENDEE;PARTSTAT=ACCEPTED:mailto:me@example.com\r\nATTENDEE;PARTSTAT=DECLINED:mailto:other@example.com\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    GPtrArray *response_events = reminder_eds_expand_ics ("c", responses, "me@example.com", utc, 1792022400, 1792368000, FALSE, &error);
    if (!response_events || response_events->len != 2 || !((ReminderEdsEvent *) g_ptr_array_index (response_events, 0))->declined || ((ReminderEdsEvent *) g_ptr_array_index (response_events, 1))->declined) return 30;
    sqlite3 *response_db = NULL;
    sqlite3_open (":memory:", &response_db);
    sqlite3_exec (response_db, "CREATE TABLE alerts(id TEXT PRIMARY KEY,title TEXT,body TEXT,destination TEXT,state TEXT,action TEXT,payload TEXT,revision INTEGER,expires_at INTEGER,snooze_until INTEGER,action_revision INTEGER,source_id TEXT,calendar_id TEXT,event_start INTEGER,event_end INTEGER,occurrence TEXT,owner TEXT,trigger_at INTEGER); CREATE TABLE calendar_sync(calendar_id TEXT PRIMARY KEY,state TEXT,last_success_unix INTEGER,error TEXT,event_count INTEGER); CREATE TABLE eds_occurrences(alert_id TEXT PRIMARY KEY,calendar_id TEXT,event_uid TEXT,recurrence_id TEXT,event_start INTEGER,event_end INTEGER,native_alarm INTEGER,all_day INTEGER,title TEXT,declined INTEGER);", NULL, NULL, NULL);
    if (!reminder_eds_apply_snapshot (response_db, "c", response_events, TRUE, TRUE, FALSE, FALSE, 5, 1792000000, NULL, &error)) return 31;
    gchar *declined_id = reminder_eds_alert_id ("c", "declined", "");
    gchar *accepted_id = reminder_eds_alert_id ("c", "accepted", "");
    gchar *declined_state = row_field (response_db, declined_id, "state"), *accepted_state = row_field (response_db, accepted_id, "state");
    if (declined_state || g_strcmp0 (accepted_state, "scheduled")) return 32;
    g_free (declined_id); g_free (accepted_id); g_free (declined_state); g_free (accepted_state); sqlite3_close (response_db);
    g_ptr_array_unref (response_events);
	GPtrArray *events = reminder_eds_expand_ics ("calendar-uid", ics, NULL, utc,
		1792022400, 1792368000, FALSE, &error);
	if (!events) { g_printerr ("fixture expansion failed: %s\n", error ? error->message : "unknown"); return 1; }
	if (events->len != 7) {
		g_printerr ("expected 7 reconciled fixture events, got %u\n", events->len);
		for (guint i = 0; i < events->len; i++) { ReminderEdsEvent *event = g_ptr_array_index (events, i); g_printerr ("%s %s %" G_GINT64_FORMAT "\n", event->event_uid, event->recurrence_id, event->start); }
		return 2;
	}
	gboolean saw_moved = FALSE, saw_exdate = FALSE, saw_native = FALSE, saw_cancel = FALSE, saw_inherited_native = FALSE, saw_all_day = FALSE;
	for (guint i = 0; i < events->len; i++) {
		ReminderEdsEvent *event = g_ptr_array_index (events, i);
		if (g_str_equal (event->event_uid, "recur") && g_str_equal (event->recurrence_id, "20261017T130000Z")) {
			saw_moved = event->start == 1792252800 && g_str_equal (event->title, "Moved standup");
			gchar *id = reminder_eds_alert_id ("calendar-uid", event->event_uid, event->recurrence_id);
			gchar *same_id = reminder_eds_alert_id ("calendar-uid", event->event_uid, "20261017T130000Z");
			saw_moved = saw_moved && g_str_equal (id, same_id);
			g_free (id); g_free (same_id);
		}
		if (g_str_equal (event->event_uid, "recur") && g_str_equal (event->recurrence_id, "20261015T130000Z")) saw_exdate = TRUE;
		if (g_str_equal (event->event_uid, "recur") && g_str_equal (event->recurrence_id, "20261016T130000Z")) saw_exdate = FALSE;
		if (g_str_equal (event->event_uid, "alarm")) saw_native = event->native_alarm;
		if (g_str_equal (event->event_uid, "all-day")) saw_all_day = event->all_day && event->native_alarm;
		if (g_str_equal (event->event_uid, "inherit-alarm") && g_str_equal (event->recurrence_id, "20261016T170000Z"))
			saw_inherited_native = event->native_alarm && event->start == 1792177200;
		if (g_str_equal (event->event_uid, "cancel")) saw_cancel = TRUE;
	}
	if (!saw_moved || !saw_exdate || !saw_native || !saw_inherited_native || !saw_all_day || saw_cancel) {
		g_printerr ("fixture did not preserve recurrence/EXDATE/moved/native/cancel semantics\n");
		g_ptr_array_unref (events); return 3;
	}
	GPtrArray *global_default = reminder_eds_expand_ics ("calendar-uid", ics, NULL, utc, 1792022400, 1792368000, TRUE, &error);
	for (guint i = 0; global_default && i < global_default->len; i++) {
		ReminderEdsEvent *event = g_ptr_array_index (global_default, i);
		if (!event->native_alarm) { g_printerr ("global default reminder was not treated as native\n"); return 3; }
	}
	if (!global_default) return 3;
	g_ptr_array_unref (global_default);
	/* Run the same snapshot application used by the EDS worker against a private DB. */
	sqlite3 *db = NULL;
	gchar *alarm_id = reminder_eds_alert_id ("calendar-uid", "alarm", "");
	sqlite3_open (":memory:", &db);
	char *sql_error = NULL;
	if (sqlite3_exec (db,
		"CREATE TABLE alerts(id TEXT PRIMARY KEY,title TEXT,body TEXT,destination TEXT,state TEXT,action TEXT,payload TEXT,revision INTEGER,expires_at INTEGER,snooze_until INTEGER,action_revision INTEGER,source_id TEXT,calendar_id TEXT,event_start INTEGER,event_end INTEGER,occurrence TEXT,owner TEXT,trigger_at INTEGER);"
		"CREATE TABLE calendar_sync(calendar_id TEXT PRIMARY KEY,state TEXT,last_success_unix INTEGER,error TEXT,event_count INTEGER);"
		"CREATE TABLE eds_occurrences(alert_id TEXT PRIMARY KEY,calendar_id TEXT,event_uid TEXT,recurrence_id TEXT,event_start INTEGER,event_end INTEGER,native_alarm INTEGER,all_day INTEGER,title TEXT,declined INTEGER);"
		"INSERT INTO alerts VALUES('eds:orphan','Old fallback','','','active','','',1,0,0,0,'eds-local','calendar-uid',0,0,'orphan','fallback',0);",
		NULL, NULL, &sql_error) != SQLITE_OK) { g_printerr ("fixture DB schema failed: %s\n", sql_error); return 5; }
	sqlite3_stmt *seed = NULL;
	if (sqlite3_prepare_v2 (db, "INSERT INTO alerts VALUES(?, 'Native reminder', '', '', 'active', '', '', 1, 0, 0, 0, 'eds-local', 'calendar-uid', 1792076400, 1792078200, 'alarm/', 'native', 0)", -1, &seed, NULL) != SQLITE_OK) return 5;
	sqlite3_bind_text (seed, 1, alarm_id, -1, SQLITE_TRANSIENT);
	if (sqlite3_step (seed) != SQLITE_DONE) return 5;
	sqlite3_finalize (seed);
	GError *db_error = NULL;
	if (!reminder_eds_apply_snapshot (db, "calendar-uid", events, TRUE, TRUE, FALSE, FALSE, 120, 1792060000, NULL, &db_error)) {
		g_printerr ("snapshot application failed: %s\n", db_error ? db_error->message : "unknown"); return 6;
	}
	gchar *orphan = row_field (db, "eds:orphan", "state");
	gchar *native_kept = row_field (db, alarm_id, "state");
	gchar *alarm_owner = row_field (db, alarm_id, "owner");
	gchar *moved_id = reminder_eds_alert_id ("calendar-uid", "recur", "20261017T130000Z");
	gchar *moved_state = row_field (db, moved_id, "state");
	if (g_strcmp0 (orphan, "withdrawn") || g_strcmp0 (native_kept, "active") || g_strcmp0 (alarm_owner, "native") || g_strcmp0 (moved_state, "scheduled")) {
		g_printerr ("successful snapshot did not reconcile owner/state expectations\n"); return 7;
	}
	gchar *all_day_id = reminder_eds_alert_id ("calendar-uid", "all-day", "");
	sqlite3_stmt *sync = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT native_alarm,all_day FROM eds_occurrences WHERE alert_id=?", -1, &sync, NULL) != SQLITE_OK) return 7;
	sqlite3_bind_text (sync, 1, all_day_id, -1, SQLITE_TRANSIENT);
	if (sqlite3_step (sync) != SQLITE_ROW || sqlite3_column_int (sync, 0) != 0 || sqlite3_column_int (sync, 1) != 1) {
		g_printerr ("all-day event remained eligible for its native alarm by default\n"); return 7;
	}
	sqlite3_finalize (sync); sync = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM alerts WHERE id=?", -1, &sync, NULL) != SQLITE_OK) return 7;
	sqlite3_bind_text (sync, 1, all_day_id, -1, SQLITE_TRANSIENT);
	if (sqlite3_step (sync) != SQLITE_ROW || sqlite3_column_int (sync, 0) != 0) {
		g_printerr ("all-day event created a fallback alert while disabled\n"); return 7;
	}
	sqlite3_finalize (sync); sync = NULL; g_free (all_day_id);
	if (sqlite3_prepare_v2 (db, "SELECT state,last_success_unix,error,event_count FROM calendar_sync WHERE calendar_id='calendar-uid'", -1, &sync, NULL) != SQLITE_OK || sqlite3_step (sync) != SQLITE_ROW ||
	    g_strcmp0 ((const gchar *) sqlite3_column_text (sync, 0), "fresh") || sqlite3_column_int64 (sync, 1) != 1792060000 || sqlite3_column_int (sync, 3) != (gint) events->len) {
		g_printerr ("successful snapshot diagnostics do not report the actual fresh snapshot\n"); return 7;
	}
	sqlite3_finalize (sync); sync = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT native_alarm,event_start FROM eds_occurrences WHERE alert_id=?", -1, &sync, NULL) != SQLITE_OK) return 7;
	sqlite3_bind_text (sync, 1, alarm_id, -1, SQLITE_TRANSIENT);
	if (sqlite3_step (sync) != SQLITE_ROW || sqlite3_column_int (sync, 0) != 1 || sqlite3_column_int64 (sync, 1) != 1792076400) {
		g_printerr ("successful snapshot did not persist native occurrence identity\n"); return 7;
	}
	sqlite3_finalize (sync); sync = NULL;
	g_free (orphan); g_free (native_kept); g_free (alarm_owner); g_free (moved_state);
	if (!reminder_eds_apply_snapshot (db, "calendar-uid", NULL, FALSE, TRUE, FALSE, FALSE, 120, 1792063600, "offline fixture failure", &db_error)) return 8;
	if (sqlite3_prepare_v2 (db, "SELECT state,last_success_unix,error FROM calendar_sync WHERE calendar_id='calendar-uid'", -1, &sync, NULL) != SQLITE_OK || sqlite3_step (sync) != SQLITE_ROW ||
	    g_strcmp0 ((const gchar *) sqlite3_column_text (sync, 0), "stale") || sqlite3_column_int64 (sync, 1) != 1792060000 ||
	    g_strcmp0 ((const gchar *) sqlite3_column_text (sync, 2), "offline fixture failure")) {
		g_printerr ("failed fetch diagnostics did not preserve last-success freshness and expose the error\n"); return 8;
	}
	sqlite3_finalize (sync); sync = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM eds_occurrences WHERE calendar_id='calendar-uid'", -1, &sync, NULL) != SQLITE_OK || sqlite3_step (sync) != SQLITE_ROW || sqlite3_column_int (sync, 0) != (gint) events->len) {
		g_printerr ("failed fetch did not preserve the last-good occurrence snapshot\n"); return 8;
	}
	sqlite3_finalize (sync); sync = NULL;
	gchar *stale = row_field (db, moved_id, "state");
	if (g_strcmp0 (stale, "scheduled")) { g_printerr ("failed fetch changed the last-good alert snapshot\n"); return 9; }
	g_free (stale); g_clear_error (&db_error);
	GPtrArray *native_snapshot = g_ptr_array_new_with_free_func (reminder_eds_event_free);
	ReminderEdsEvent *native_moved = g_new0 (ReminderEdsEvent, 1);
	native_moved->source_uid = g_strdup ("calendar-uid"); native_moved->event_uid = g_strdup ("recur");
	native_moved->recurrence_id = g_strdup ("20261017T130000Z"); native_moved->occurrence = g_strdup ("recur/20261017T130000Z");
	native_moved->title = g_strdup ("Moved standup"); native_moved->start = 1792252800; native_moved->end = 1792254600; native_moved->native_alarm = TRUE;
	g_ptr_array_add (native_snapshot, native_moved);
	if (!reminder_eds_apply_snapshot (db, "calendar-uid", native_snapshot, TRUE, TRUE, FALSE, FALSE, 120, 1792063600, NULL, &db_error)) return 10;
	gchar *native_replaces_fallback = row_field (db, moved_id, "state");
	if (g_strcmp0 (native_replaces_fallback, "withdrawn")) { g_printerr ("new native alarm did not withdraw the matching fallback occurrence\n"); return 11; }
	g_free (native_replaces_fallback); g_ptr_array_unref (native_snapshot);
	GPtrArray *empty = g_ptr_array_new_with_free_func (reminder_eds_event_free);
	if (!reminder_eds_apply_snapshot (db, "calendar-uid", empty, TRUE, TRUE, FALSE, FALSE, 120, 1792067200, NULL, &db_error)) return 12;
	gchar *after_empty = row_field (db, moved_id, "state");
	gchar *native_after_empty = row_field (db, alarm_id, "state");
	if (g_strcmp0 (after_empty, "withdrawn") || g_strcmp0 (native_after_empty, "withdrawn")) return 13;
	if (sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM eds_occurrences WHERE calendar_id='calendar-uid'", -1, &sync, NULL) != SQLITE_OK || sqlite3_step (sync) != SQLITE_ROW || sqlite3_column_int (sync, 0) != 0) return 13;
	sqlite3_finalize (sync); sync = NULL;
	g_free (after_empty); g_free (native_after_empty); g_ptr_array_unref (empty); g_ptr_array_unref (events);
	sqlite3_close (db); g_free (alarm_id); g_free (moved_id);
	const gchar bad_tz[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:bad\r\nDTSTART;TZID=Unknown/CalendarZone:20261015T090000\r\nDTEND;TZID=Unknown/CalendarZone:20261015T100000\r\nRRULE:FREQ=DAILY;COUNT=2\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
	events = reminder_eds_expand_ics ("calendar-uid", bad_tz, NULL, utc, 1792022400, 1792368000, FALSE, &error);
	if (events || !error) { g_printerr ("unknown event timezone was not rejected\n"); return 4; }
	g_clear_error (&error);
	const gchar *iana_zones[] = { "America/New_York", "America/Toronto" };
	for (guint i = 0; i < G_N_ELEMENTS (iana_zones); i++) {
		gchar *iana_ics = g_strdup_printf (
			"BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:iana-%u\r\n"
			"DTSTART;TZID=%s:20261015T090000\r\nDTEND;TZID=%s:20261015T100000\r\n"
			"END:VEVENT\r\nEND:VCALENDAR\r\n", i, iana_zones[i], iana_zones[i]);
		events = reminder_eds_expand_ics ("calendar-uid", iana_ics, NULL, utc, 1792022400, 1792368000, FALSE, &error);
		g_free (iana_ics);
		if (!events || events->len != 1 || ((ReminderEdsEvent *) g_ptr_array_index (events, 0))->start != 1792069200) {
			g_printerr ("IANA event timezone %s was not resolved to the expected local time: %s\n",
				iana_zones[i], error ? error->message : "unexpected occurrence result");
			g_clear_pointer (&events, g_ptr_array_unref); g_clear_error (&error); return 14;
		}
		g_ptr_array_unref (events);
	}
	return 0;
}
