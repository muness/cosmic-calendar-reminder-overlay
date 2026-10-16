#include "reminder-eds.h"

typedef struct {
	const gchar *source_uid;
	GPtrArray *events;
	ICalTimezone *default_timezone;
	ICalComponent *calendar;
	GPtrArray *owned_zones;
	gboolean for_every_event;
	gboolean recurring;
} Expansion;

static ICalTimezone *
resolve_timezone (const gchar *tzid, gpointer user_data, GCancellable *cancellable, GError **error)
{
	Expansion *expansion = user_data;
	ICalTimezone *zone = expansion && expansion->calendar ? i_cal_component_get_timezone (expansion->calendar, tzid) : NULL;
	(void) cancellable;
	if (zone) g_ptr_array_add (expansion->owned_zones, zone);
	else {
		zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
		/* Some installed libical timezone registries index Olson IDs by
		 * location rather than the TZID lookup table. */
		if (!zone) zone = i_cal_timezone_get_builtin_timezone (tzid);
	}
	if (!zone)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unknown event timezone '%s'; preserving the previous calendar snapshot", tzid);
	return zone;
}

static gint64
time_to_unix (Expansion *expansion, ICalTime *time, GError **error)
{
	const gchar *tzid = i_cal_time_get_tzid (time);
	ICalTimezone *zone = tzid && *tzid ? resolve_timezone (tzid, expansion, NULL, error) : expansion->default_timezone;
	if (!zone) return -1;
	return i_cal_time_as_timet_with_zone (time, zone);
}

void
reminder_eds_event_free (gpointer data)
{
	ReminderEdsEvent *event = data;
	if (!event) return;
	g_free (event->source_uid);
	g_free (event->event_uid);
	g_free (event->recurrence_id);
	g_free (event->occurrence);
	g_free (event->title);
	g_free (event->body);
	g_free (event->destination);
	g_free (event);
}

static gchar *
recurrence_key (ICalTime *time)
{
	return time ? i_cal_time_as_ical_string (time) : g_strdup ("");
}

gchar *
reminder_eds_alert_id (const gchar *source_uid, const gchar *event_uid, const gchar *rid)
{
	gchar *identity, *digest, *result;
	identity = g_strdup_printf ("%s\n%s\n%s", source_uid ? source_uid : "", event_uid ? event_uid : "", rid ? rid : "");
	digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, identity, -1);
	result = g_strdup_printf ("eds:%s", digest);
	g_free (identity);
	g_free (digest);
	return result;
}

static gboolean
component_has_alarm (ICalComponent *component)
{
	ICalComponent *alarm = i_cal_component_get_first_component (component, I_CAL_VALARM_COMPONENT);
	if (!alarm) return FALSE;
	g_object_unref (alarm);
	return TRUE;
}

static ReminderEdsEvent *
event_from_times (Expansion *expansion, ICalComponent *component, ICalTime *start, ICalTime *end, const gchar *rid, GError **error)
{
	const gchar *uid = i_cal_component_get_uid (component);
	ReminderEdsEvent *event;
	ICalProperty *url_property;
	if (!uid || !*uid || !start || i_cal_time_is_null_time (start)) return NULL;
	event = g_new0 (ReminderEdsEvent, 1);
	event->source_uid = g_strdup (expansion->source_uid);
	event->event_uid = g_strdup (uid);
	event->recurrence_id = g_strdup (rid ? rid : "");
	event->occurrence = g_strdup_printf ("%s/%s", uid, event->recurrence_id);
	event->title = g_strdup (i_cal_component_get_summary (component));
	event->body = g_strdup (i_cal_component_get_description (component));
	url_property = i_cal_component_get_first_property (component, I_CAL_URL_PROPERTY);
	event->destination = g_strdup (url_property ? i_cal_property_get_url (url_property) : NULL);
	g_clear_object (&url_property);
	event->start = time_to_unix (expansion, start, error);
	event->end = end && !i_cal_time_is_null_time (end) ? time_to_unix (expansion, end, error) : event->start;
	if (event->start < 0 || event->end < 0) { reminder_eds_event_free (event); return NULL; }
	event->native_alarm = expansion->for_every_event || component_has_alarm (component);
	event->all_day = i_cal_time_is_date (start);
	return event;
}

static gboolean
add_instance (ICalComponent *component, ICalTime *start, ICalTime *end,
	      gpointer user_data, GCancellable *cancellable, GError **error)
{
	Expansion *expansion = user_data;
	gchar *rid = NULL;
	ReminderEdsEvent *event;
	(void) cancellable;
	(void) error;
	if (expansion->recurring) {
		rid = recurrence_key (start);
	}
	event = event_from_times (expansion, component, start, end, rid, error);
	if (event) g_ptr_array_add (expansion->events, event);
	g_free (rid);
	return !(error && *error);
}

static gboolean
has_recurrence (ICalComponent *component)
{
	const ICalPropertyKind kinds[] = { I_CAL_RRULE_PROPERTY, I_CAL_RDATE_PROPERTY };
	for (guint i = 0; i < G_N_ELEMENTS (kinds); i++) {
		ICalProperty *property = i_cal_component_get_first_property (component, kinds[i]);
		if (property) { g_object_unref (property); return TRUE; }
	}
	return FALSE;
}

static void
remove_occurrence (GPtrArray *events, const gchar *uid, const gchar *rid)
{
	for (guint i = events->len; i > 0; i--) {
		ReminderEdsEvent *event = g_ptr_array_index (events, i - 1);
		if (g_str_equal (event->event_uid, uid) && g_str_equal (event->recurrence_id, rid))
			g_ptr_array_remove_index (events, i - 1);
	}
}

GPtrArray *
reminder_eds_expand_ics (const gchar *source_uid, const gchar *ical_text,
			 ICalTimezone *default_timezone, gint64 range_start,
			 gint64 range_end, gboolean for_every_event, GError **error)
{
	ICalComponent *root = NULL, *component;
	ICalTime *interval_start = NULL, *interval_end = NULL;
	GPtrArray *events = g_ptr_array_new_with_free_func (reminder_eds_event_free);
	GPtrArray *detached = g_ptr_array_new_with_free_func (g_object_unref);
	GPtrArray *owned_zones = g_ptr_array_new_with_free_func (g_object_unref);
	GHashTable *master_alarms = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (!source_uid || !*source_uid || !ical_text || !default_timezone) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "source, calendar data and a timezone are required");
		goto fail;
	}
	root = i_cal_component_new_from_string (ical_text);
	if (!root) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Calendar returned invalid iCalendar data");
		goto fail;
	}
	interval_start = i_cal_time_new_from_timet_with_zone (range_start, FALSE, default_timezone);
	interval_end = i_cal_time_new_from_timet_with_zone (range_end, FALSE, default_timezone);
	for (component = i_cal_component_get_first_component (root, I_CAL_VEVENT_COMPONENT);
	     component; ) {
		ICalComponent *next = i_cal_component_get_next_component (root, I_CAL_VEVENT_COMPONENT);
		ICalTime *recurrence = i_cal_component_get_recurrenceid (component);
		const gchar *uid = i_cal_component_get_uid (component);
		if ((!recurrence || i_cal_time_is_null_time (recurrence)) && uid && *uid &&
		    i_cal_component_get_status (component) != I_CAL_STATUS_CANCELLED && component_has_alarm (component))
			g_hash_table_add (master_alarms, g_strdup (uid));
		g_clear_object (&recurrence);
		g_clear_object (&component);
		component = next;
	}
	for (component = i_cal_component_get_first_component (root, I_CAL_VEVENT_COMPONENT);
	     component; ) {
		ICalComponent *next = i_cal_component_get_next_component (root, I_CAL_VEVENT_COMPONENT);
		ICalTime *recurrence = i_cal_component_get_recurrenceid (component);
		if (recurrence && !i_cal_time_is_null_time (recurrence)) {
			g_ptr_array_add (detached, g_object_ref (component));
			g_object_unref (recurrence);
		} else if (i_cal_component_get_status (component) == I_CAL_STATUS_CANCELLED) {
			g_clear_object (&recurrence);
		} else {
			Expansion expansion = { source_uid, events, default_timezone, root, owned_zones, for_every_event, has_recurrence (component) };
			if (expansion.recurring) {
				if (!e_cal_recur_generate_instances_sync (component, interval_start, interval_end, add_instance, &expansion,
						resolve_timezone, &expansion, default_timezone, NULL, error)) {
					if (error && !*error) g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Recurrence expansion failed; preserving the previous calendar snapshot");
					g_clear_object (&component); g_clear_object (&next); goto fail;
				}
			} else {
				ICalTime *start = i_cal_component_get_dtstart (component);
				ICalTime *end = i_cal_component_get_dtend (component);
				if (!start || i_cal_time_is_null_time (start)) {
					g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Event %s has no usable DTSTART; preserving the previous calendar snapshot", i_cal_component_get_uid (component));
					g_clear_object (&start); g_clear_object (&end); g_clear_object (&component); g_clear_object (&next); goto fail;
				}
				gint64 begin = time_to_unix (&expansion, start, error);
				gint64 finish = end && !i_cal_time_is_null_time (end) ? time_to_unix (&expansion, end, error) : begin;
				if (begin < 0 || finish < 0) { g_clear_object (&start); g_clear_object (&end); g_clear_object (&component); g_clear_object (&next); goto fail; }
				if (begin <= range_end && finish >= range_start) {
					ReminderEdsEvent *event = event_from_times (&expansion, component, start, end, "", error);
					if (!event && error && *error) { g_clear_object (&start); g_clear_object (&end); g_clear_object (&component); g_clear_object (&next); goto fail; }
					if (event) g_ptr_array_add (events, event);
				}
				g_clear_object (&start); g_clear_object (&end);
			}
			g_clear_object (&recurrence);
		}
		g_clear_object (&component);
		component = next;
	}
	for (guint i = 0; i < detached->len; i++) {
		component = g_ptr_array_index (detached, i);
		ICalTime *recurrence = i_cal_component_get_recurrenceid (component);
		ICalTime *start = i_cal_component_get_dtstart (component);
		ICalTime *end = i_cal_component_get_dtend (component);
		gchar *rid = recurrence_key (recurrence);
		const gchar *uid = i_cal_component_get_uid (component);
		remove_occurrence (events, uid ? uid : "", rid);
		if (i_cal_component_get_status (component) != I_CAL_STATUS_CANCELLED && start && !i_cal_time_is_null_time (start)) {
		Expansion expansion = { source_uid, events, default_timezone, root, owned_zones, for_every_event, FALSE };
		gint64 begin = time_to_unix (&expansion, start, error);
		gint64 finish = end && !i_cal_time_is_null_time (end) ? time_to_unix (&expansion, end, error) : begin;
		if (begin < 0 || finish < 0) { g_free (rid); g_clear_object (&recurrence); g_clear_object (&start); g_clear_object (&end); goto fail; }
			if (begin <= range_end && finish >= range_start) {
				ReminderEdsEvent *event = event_from_times (&expansion, component, start, end, rid, error);
				if (!event && error && *error) { g_free (rid); g_clear_object (&recurrence); g_clear_object (&start); g_clear_object (&end); goto fail; }
				if (event) g_ptr_array_add (events, event);
			}
		} else if (!start && i_cal_component_get_status (component) != I_CAL_STATUS_CANCELLED) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Moved recurrence exception %s has no DTSTART; preserving the previous calendar snapshot", uid ? uid : "");
			g_free (rid); g_clear_object (&recurrence); g_clear_object (&start); g_clear_object (&end); goto fail;
		}
		g_free (rid);
		g_clear_object (&recurrence); g_clear_object (&start); g_clear_object (&end);
	}
	/* Detached occurrences inherit the master VALARM unless the provider has
	 * already made a native reminder through the calendar-wide alarm policy. */
	for (guint i = 0; i < events->len; i++) {
		ReminderEdsEvent *event = g_ptr_array_index (events, i);
		if (g_hash_table_contains (master_alarms, event->event_uid)) event->native_alarm = TRUE;
	}
	g_hash_table_unref (master_alarms);
	g_ptr_array_unref (detached);
	g_ptr_array_unref (owned_zones);
	g_clear_object (&interval_start); g_clear_object (&interval_end); g_clear_object (&root);
	return events;
fail:
	g_hash_table_unref (master_alarms);
	g_ptr_array_unref (detached);
	g_ptr_array_unref (events);
	g_ptr_array_unref (owned_zones);
	g_clear_object (&interval_start); g_clear_object (&interval_end); g_clear_object (&root);
	return NULL;
}

static gboolean
step_done (sqlite3 *db, sqlite3_stmt *statement, GError **error)
{
	if (sqlite3_step (statement) == SQLITE_DONE) return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not reconcile calendar snapshot: %s", sqlite3_errmsg (db));
	return FALSE;
}

gboolean
reminder_eds_apply_snapshot (sqlite3 *database, const gchar *source_uid,
			     GPtrArray *events, gboolean fetch_succeeded,
				 gboolean fallback_enabled, gboolean all_day_enabled, gboolean global_native_default,
			     guint fallback_lead_minutes, gint64 now,
			     const gchar *error_message, GError **error)
{
	sqlite3_stmt *statement = NULL;
	GHashTable *present = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	gboolean ok = FALSE;
	if (!database || !source_uid || !*source_uid) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "database and calendar identity are required");
		goto done;
	}
	if (!fetch_succeeded || !events) {
		if (sqlite3_prepare_v2 (database, "INSERT INTO calendar_sync(calendar_id,state,last_success_unix,error,event_count) VALUES(?,'stale',0,?,0) ON CONFLICT(calendar_id) DO UPDATE SET state='stale',error=excluded.error", -1, &statement, NULL) != SQLITE_OK) goto sql_error;
		sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 2, error_message && *error_message ? error_message : "calendar fetch failed; last successful snapshot retained", -1, SQLITE_TRANSIENT);
		ok = step_done (database, statement, error);
		goto done;
	}
	if (sqlite3_exec (database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) goto sql_error;
	if (sqlite3_prepare_v2 (database, "DELETE FROM eds_occurrences WHERE calendar_id=?", -1, &statement, NULL) != SQLITE_OK) goto sql_error;
	sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
	if (!step_done (database, statement, error)) goto rollback;
	sqlite3_finalize (statement); statement = NULL;
	for (guint i = 0; i < events->len; i++) {
		ReminderEdsEvent *event = g_ptr_array_index (events, i);
		if (!event || g_strcmp0 (event->source_uid, source_uid) != 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Calendar snapshot contains a mismatched source identity");
			goto rollback;
		}
		gchar *id = reminder_eds_alert_id (source_uid, event->event_uid, event->recurrence_id);
		g_hash_table_add (present, g_strdup (id));
		gboolean native = event->native_alarm || global_native_default;
		if (event->all_day && !all_day_enabled) native = FALSE;
		if (sqlite3_prepare_v2 (database, "INSERT INTO eds_occurrences(alert_id,calendar_id,event_uid,recurrence_id,event_start,event_end,native_alarm,all_day) VALUES(?,?,?,?,?,?,?,?)", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
		sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 2, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 3, event->event_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 4, event->recurrence_id, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (statement, 5, event->start);
		sqlite3_bind_int64 (statement, 6, event->end);
		sqlite3_bind_int (statement, 7, native);
		sqlite3_bind_int (statement, 8, event->all_day);
		if (!step_done (database, statement, error)) { g_free (id); goto rollback; }
		sqlite3_finalize (statement); statement = NULL;
		if (event->end <= now) {
			if (sqlite3_prepare_v2 (database, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE id=? AND owner IN ('native','fallback') AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
			sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
			gboolean step_ok = step_done (database, statement, error);
			sqlite3_finalize (statement); statement = NULL;
			if (!step_ok) { g_free (id); goto rollback; }
			g_free (id);
			continue;
		}
		if (event->all_day && !all_day_enabled) {
			if (sqlite3_prepare_v2 (database, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE id=? AND owner IN ('native','fallback') AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
			sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
			gboolean step_ok = step_done (database, statement, error);
			sqlite3_finalize (statement); statement = NULL;
			g_free (id);
			if (!step_ok) goto rollback;
			continue;
		}
		if (native) {
			if (sqlite3_prepare_v2 (database, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE id=? AND owner='fallback' AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
			sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
			gboolean step_ok = step_done (database, statement, error);
			sqlite3_finalize (statement); statement = NULL;
			if (!step_ok) { g_free (id); goto rollback; }
			if (sqlite3_prepare_v2 (database, "UPDATE alerts SET state=CASE WHEN event_start!=? THEN 'withdrawn' ELSE state END,title=?,event_start=?,event_end=?,occurrence=?,revision=revision+CASE WHEN event_start!=? OR event_end!=? OR title!=? THEN 1 ELSE 0 END WHERE id=? AND owner='native' AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
			sqlite3_bind_int64 (statement, 1, event->start);
			sqlite3_bind_text (statement, 2, event->title && *event->title ? event->title : "Calendar reminder", -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64 (statement, 3, event->start);
			sqlite3_bind_int64 (statement, 4, event->end);
			sqlite3_bind_text (statement, 5, event->occurrence, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64 (statement, 6, event->start);
			sqlite3_bind_int64 (statement, 7, event->end);
			sqlite3_bind_text (statement, 8, event->title && *event->title ? event->title : "Calendar reminder", -1, SQLITE_TRANSIENT);
			sqlite3_bind_text (statement, 9, id, -1, SQLITE_TRANSIENT);
			step_ok = step_done (database, statement, error);
			sqlite3_finalize (statement); statement = NULL;
			g_free (id);
			if (!step_ok) goto rollback;
			continue;
		}
		if (!fallback_enabled) {
			/* A native alarm was removed, and this calendar has no local
			 * fallback policy: withdraw its former provider-owned record. */
			if (sqlite3_prepare_v2 (database, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE id=? AND owner IN ('native','fallback') AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
			sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
			gboolean step_ok = step_done (database, statement, error);
			sqlite3_finalize (statement); statement = NULL;
			g_free (id);
			if (!step_ok) goto rollback;
			continue;
		}
		gint64 trigger_at = event->start - (gint64) fallback_lead_minutes * 60;
		const gchar *next_state = trigger_at <= now ? "active" : "scheduled";
		if (sqlite3_prepare_v2 (database,
			"INSERT INTO alerts(id,title,body,destination,state,action,payload,revision,expires_at,snooze_until,action_revision,source_id,calendar_id,event_start,event_end,occurrence,owner,trigger_at) VALUES(?,?,?,?,?,'','',1,?,0,0,'eds-local',?,?,?,?,'fallback',?) "
			"ON CONFLICT(id) DO UPDATE SET title=excluded.title,body=excluded.body,destination=excluded.destination,expires_at=excluded.expires_at,source_id='eds-local',calendar_id=excluded.calendar_id,event_start=excluded.event_start,event_end=excluded.event_end,occurrence=excluded.occurrence,owner='fallback',trigger_at=excluded.trigger_at,state=CASE WHEN alerts.state IN ('dismissed','cancelled','snoozed') THEN alerts.state WHEN excluded.state='withdrawn' THEN 'withdrawn' WHEN alerts.state='expired' AND excluded.expires_at>excluded.event_start THEN excluded.state ELSE excluded.state END,revision=alerts.revision+CASE WHEN alerts.title!=excluded.title OR alerts.body!=excluded.body OR alerts.destination!=excluded.destination OR alerts.event_start!=excluded.event_start OR alerts.event_end!=excluded.event_end OR alerts.trigger_at!=excluded.trigger_at THEN 1 ELSE 0 END",
			-1, &statement, NULL) != SQLITE_OK) { g_free (id); goto sql_error; }
		const gchar *title = event->title && *event->title ? event->title : "Calendar reminder";
		sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 2, title, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 3, event->body ? event->body : "", -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 4, event->destination ? event->destination : "", -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 5, next_state, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (statement, 6, event->end);
		sqlite3_bind_text (statement, 7, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (statement, 8, event->start);
		sqlite3_bind_int64 (statement, 9, event->end);
		sqlite3_bind_text (statement, 10, event->occurrence, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (statement, 11, trigger_at);
		gboolean step_ok = step_done (database, statement, error);
		sqlite3_finalize (statement); statement = NULL;
		g_free (id);
		if (!step_ok) goto rollback;
	}
	if (sqlite3_prepare_v2 (database,
		"UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE source_id='eds-local' AND calendar_id=? AND owner IN ('native','fallback') AND state IN ('active','scheduled','snoozed') AND id NOT IN (SELECT value FROM json_each(?))",
		-1, &statement, NULL) != SQLITE_OK) goto sql_error;
	sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
	JsonBuilder *builder = json_builder_new ();
	json_builder_begin_array (builder);
	GHashTableIter iter; gpointer key;
	g_hash_table_iter_init (&iter, present);
	while (g_hash_table_iter_next (&iter, &key, NULL)) json_builder_add_string_value (builder, key);
	json_builder_end_array (builder);
	JsonGenerator *generator = json_generator_new ();
	JsonNode *ids_root = json_builder_get_root (builder);
	json_generator_set_root (generator, ids_root);
	json_node_unref (ids_root);
	gchar *ids_json = json_generator_to_data (generator, NULL);
	sqlite3_bind_text (statement, 2, ids_json, -1, SQLITE_TRANSIENT);
	gboolean withdraw_ok = step_done (database, statement, error);
	g_free (ids_json); g_object_unref (generator); g_object_unref (builder);
	sqlite3_finalize (statement); statement = NULL;
	if (!withdraw_ok) goto rollback;
	if (sqlite3_prepare_v2 (database, "INSERT INTO calendar_sync(calendar_id,state,last_success_unix,error,event_count) VALUES(?,'fresh',?,'',?) ON CONFLICT(calendar_id) DO UPDATE SET state='fresh',last_success_unix=excluded.last_success_unix,error='',event_count=excluded.event_count", -1, &statement, NULL) != SQLITE_OK) goto sql_error;
	sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64 (statement, 2, now);
	sqlite3_bind_int (statement, 3, events->len);
	if (!step_done (database, statement, error)) goto rollback;
	sqlite3_finalize (statement); statement = NULL;
	if (sqlite3_exec (database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) goto sql_error;
	ok = TRUE;
	goto done;
rollback:
	if (statement) sqlite3_finalize (statement);
	sqlite3_exec (database, "ROLLBACK", NULL, NULL, NULL);
	goto done;
sql_error:
	if (statement) sqlite3_finalize (statement);
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not reconcile calendar snapshot: %s", sqlite3_errmsg (database));
	if (fetch_succeeded) sqlite3_exec (database, "ROLLBACK", NULL, NULL, NULL);
done:
	g_hash_table_unref (present);
	return ok;
}
