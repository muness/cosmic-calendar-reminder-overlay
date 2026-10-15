#include <gio/gio.h>
#include <glib-unix.h>
#include <sqlite3.h>
#include <glib/gstdio.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <json-glib/json-glib.h>
#include <libecal/libecal.h>
#include "reminder-eds.h"

#define BUS_NAME "org.muness.CalendarReminderOverlay.Core"
#define OBJECT_PATH "/org/muness/CalendarReminderOverlay/Core"
#define IFACE "org.muness.CalendarReminderOverlay.Core1"
#define RENDERER_APP_ID "com.muness.calendar-reminder-overlay"

static sqlite3 *db;
static GMainLoop *loop;
static gchar *config_path;
static gchar *helper_path;
static gchar *overlay_path;
static gchar *policy_mode;
static GHashTable *selected_calendars;
static gboolean eds_enabled;
static gboolean follow_calendar_selection = TRUE;
static gboolean presentation_enabled = TRUE;
static ESourceRegistry *eds_registry;
static EReminderWatcher *eds_watcher;
static GHashTable *native_reminders;
static GDBusConnection *bus_connection;
static GSubprocess *overlay_process;
static gchar *foreground_id;
static gboolean eds_starting;
static GCancellable *eds_start_cancellable;
static GHashTable *eds_scans;
static guint eds_rescan_source;
static guint eds_retry_source;
static gboolean global_native_default;
static GSettings *global_calendar_settings;
static GHashTable *presented_alerts;
static gboolean renderer_check_pending;
static gboolean renderer_retry_exhausted;
static guint overlay_retry_source;
static guint overlay_retry_count;
static gint64 overlay_started_at;
static gchar *audio_file;
static gchar *validated_policy_json;
static JsonNode *validated_policy_node;
static gboolean audio_enabled;
static gdouble audio_volume = 0.5;
static guint fallback_lead_minutes = 5;
static guint audio_repeat_seconds;
static GSubprocess *audio_process;
static guint audio_repeat_source;
static gboolean audio_suppressed;
static const gchar *playback_program = "/usr/bin/pw-play";
static gboolean has_active_alerts (void);
static void stop_audio (void);
static void start_audio (void);
static void start_eds (void);
static void stop_eds (void);
static void scan_selected_calendars (void);
static void scan_source (ESource *source);
static void ensure_renderer (void);
static void reset_renderer_retries (void);
static gboolean any_eligible_calendar_alerts (void);
static gboolean renderer_retry_cb (gpointer user_data);
static void registry_source_changed (ESourceRegistry *registry, ESource *source, gpointer user_data);
static const gchar introspection_xml[] =
  "<node><interface name='" IFACE "'>"
  "<method name='Submit'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='t' direction='out'/></method>"
  "<method name='Update'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='t' direction='out'/></method>"
  "<method name='Cancel'><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
  "<method name='Act'><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
  "<method name='Status'><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
  "<method name='Get'><arg type='s' direction='in'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='t' direction='out'/><arg type='t' direction='out'/></method>"
  "<method name='List'><arg type='s' direction='out'/></method>"
  "<method name='ReloadPolicy'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
  "<method name='StopAudio'></method>"
  "<method name='SetForeground'><arg type='s' direction='in'/></method>"
  "<method name='GetPresentation'><arg type='b' direction='out'/></method>"
  "<method name='SetPresentation'><arg type='b' direction='in'/><arg type='b' direction='out'/></method>"
  "<method name='GetPolicy'><arg type='s' direction='out'/></method>"
  "<method name='Diagnostics'><arg type='s' direction='out'/></method>"
  "<signal name='Changed'><arg type='s'/><arg type='t'/></signal>"
  "<signal name='PresentationChanged'><arg type='b'/></signal>"
  "</interface></node>";

static gboolean
load_policy (const gchar *path, const gchar *helper, GError **error)
{
	GSubprocess *process;
	gchar *stdout_text = NULL, *stderr_text = NULL;
	JsonParser *parser = NULL;
	JsonObject *object;
	JsonArray *ids;
	GHashTable *next_ids;
	gchar *next_mode = NULL;
	gboolean ok = FALSE;
	process = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
		error, "/usr/bin/python3", helper, "--config", path, "policy-json", NULL);
	if (!process) return FALSE;
	if (!g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, error) ||
	    !g_subprocess_get_successful (process)) {
		if (error && !*error) g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s", stderr_text ? stderr_text : "config helper failed");
		goto done;
	}
	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, stdout_text, -1, error)) goto done;
	object = json_node_get_object (json_parser_get_root (parser));
	JsonObject *selection = json_object_get_object_member (object, "selection");
	JsonObject *audio = json_object_get_object_member (object, "audio");
	JsonObject *calendar = json_object_get_object_member (object, "calendar");
	if (!selection || !audio || !calendar) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "config helper omitted selection, calendar, or audio policy"); goto done;
	}
	next_mode = g_strdup (json_object_get_string_member (selection, "mode"));
	ids = json_object_get_array_member (selection, "calendars");
	if (!next_mode || !ids || !(g_str_equal (next_mode, "include") || g_str_equal (next_mode, "exclude") || g_str_equal (next_mode, "all"))) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "config helper returned invalid source selection"); goto done;
	}
	next_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (guint i = 0; i < json_array_get_length (ids); i++)
		g_hash_table_add (next_ids, g_strdup (json_array_get_string_element (ids, i)));
	g_free (policy_mode); policy_mode = next_mode; next_mode = NULL;
	g_clear_pointer (&selected_calendars, g_hash_table_unref); selected_calendars = next_ids;
	g_free (audio_file); audio_file = g_strdup (json_object_get_string_member (audio, "file"));
	audio_enabled = json_object_get_boolean_member (audio, "enabled");
	audio_volume = json_object_get_double_member (audio, "volume");
	audio_repeat_seconds = (guint) json_object_get_int_member (audio, "repeat_seconds");
	eds_enabled = json_object_get_boolean_member (calendar, "eds_enabled");
	follow_calendar_selection = json_object_get_boolean_member (calendar, "follow_selection");
	JsonObject *reminders = json_object_get_object_member (object, "reminders");
	fallback_lead_minutes = (guint) json_object_get_int_member (reminders, "fallback_lead_minutes");
	g_free (validated_policy_json);
	validated_policy_json = g_strdup (stdout_text);
	g_clear_pointer (&validated_policy_node, json_node_unref);
	validated_policy_node = json_node_copy (json_parser_get_root (parser));
	gchar *next_path = g_strdup (path), *next_helper = g_strdup (helper);
	g_free (config_path); config_path = next_path;
	g_free (helper_path); helper_path = next_helper;
	ok = TRUE;
done:
	if (parser) g_object_unref (parser);
	g_free (next_mode); g_free (stdout_text); g_free (stderr_text); g_object_unref (process);
	return ok;
}

static gboolean
has_active_alerts (void)
{
	sqlite3_stmt *st = NULL;
	gboolean active = FALSE;
	if (foreground_id && *foreground_id) {
		if (sqlite3_prepare_v2 (db, "SELECT 1 FROM alerts WHERE id=? AND state='active'", -1, &st, NULL) == SQLITE_OK) {
			sqlite3_bind_text (st, 1, foreground_id, -1, SQLITE_TRANSIENT);
			active = sqlite3_step (st) == SQLITE_ROW;
		}
	}
	sqlite3_finalize (st);
	return active;
}

static gchar *
ensure_default_tone (void)
{
	const guint sample_rate = 22050, samples = 11025;
	gchar *directory = g_build_filename (g_get_user_cache_dir (), "calendar-reminder-overlay", NULL);
	gchar *path = g_build_filename (directory, "default-tone.wav", NULL);
	if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) { g_free (directory); return path; }
	g_mkdir_with_parents (directory, 0700);
	guint32 data_size = samples * 2, riff_size = 36 + data_size;
	GByteArray *wav = g_byte_array_sized_new (44 + data_size);
	const guint8 header[] = {
		'R','I','F','F', riff_size & 255, (riff_size >> 8) & 255, (riff_size >> 16) & 255, (riff_size >> 24) & 255,
		'W','A','V','E','f','m','t',' ', 16,0,0,0, 1,0, 1,0,
		sample_rate & 255, (sample_rate >> 8) & 255, (sample_rate >> 16) & 255, (sample_rate >> 24) & 255,
		(sample_rate * 2) & 255, ((sample_rate * 2) >> 8) & 255, ((sample_rate * 2) >> 16) & 255, ((sample_rate * 2) >> 24) & 255,
		2,0,16,0, 'd','a','t','a', data_size & 255, (data_size >> 8) & 255, (data_size >> 16) & 255, (data_size >> 24) & 255
	};
	g_byte_array_append (wav, header, sizeof header);
	for (guint i = 0; i < samples; i++) {
		gdouble envelope = MIN (1.0, MIN (i / 350.0, (samples - i) / 500.0));
		gint16 sample = (gint16) (sin (2.0 * G_PI * 880.0 * i / sample_rate) * 5000.0 * envelope);
		guint8 bytes[] = { sample & 255, ((guint16) sample >> 8) & 255 };
		g_byte_array_append (wav, bytes, sizeof bytes);
	}
	GError *error = NULL;
	if (!g_file_set_contents (path, (const gchar *) wav->data, wav->len, &error)) {
		g_warning ("Could not create default reminder tone: %s", error ? error->message : "write failed");
		g_clear_error (&error); g_clear_pointer (&path, g_free);
	}
	g_byte_array_unref (wav); g_free (directory);
	return path;
}

static void
stop_audio (void)
{
	audio_suppressed = TRUE;
	if (audio_repeat_source) { g_source_remove (audio_repeat_source); audio_repeat_source = 0; }
	if (audio_process) {
		g_subprocess_force_exit (audio_process);
		g_clear_object (&audio_process);
	}
}

static void
reset_audio_for_new_alert (void)
{
	audio_suppressed = FALSE;
	if (audio_repeat_source) { g_source_remove (audio_repeat_source); audio_repeat_source = 0; }
	if (audio_process) {
		g_subprocess_force_exit (audio_process);
		g_clear_object (&audio_process);
	}
}

static void
audio_finished (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GSubprocess *process = G_SUBPROCESS (source);
	GError *error = NULL;
	(void) user_data;
	if (!g_subprocess_wait_finish (process, result, &error)) {
		g_debug ("Audio playback ended: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
	}
	else if (audio_process == process && !g_subprocess_get_successful (process))
		g_warning ("Audible reminder playback failed; check the audio file and output device");
	if (audio_process == process) g_clear_object (&audio_process);
}

static gboolean
repeat_audio (gpointer data)
{
	(void) data;
	if (!has_active_alerts ()) { audio_repeat_source = 0; stop_audio (); return G_SOURCE_REMOVE; }
	start_audio ();
	return G_SOURCE_CONTINUE;
}

static void
start_audio (void)
{
	if (!foreground_id || !*foreground_id) return;
	gchar *volume;
	GError *error = NULL;
	gboolean enabled = audio_enabled;
	gchar *file = g_strdup (audio_file);
	gdouble effective_volume = audio_volume;
	guint repeat_seconds = audio_repeat_seconds;
	sqlite3_stmt *st = NULL;
	gchar *id = foreground_id && *foreground_id ? g_strdup (foreground_id) : NULL;
	const gchar *source = NULL, *calendar = NULL;
	if (id) {
		if (sqlite3_prepare_v2 (db, "SELECT source_id,calendar_id FROM alerts WHERE id=? AND state='active'", -1, &st, NULL) == SQLITE_OK) {
			sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
			if (sqlite3_step (st) == SQLITE_ROW) { source = g_strdup ((const gchar *) sqlite3_column_text (st, 0)); calendar = g_strdup ((const gchar *) sqlite3_column_text (st, 1)); }
		}
	}
	if (id && source && *source && calendar && *calendar && validated_policy_node) {
				gchar *key = g_strdup_printf ("%s/%s", source, calendar);
				JsonObject *root = json_node_get_object (validated_policy_node);
				JsonObject *overrides = json_object_has_member (root, "calendar_overrides") ? json_object_get_object_member (root, "calendar_overrides") : NULL;
				JsonObject *override = overrides && json_object_has_member (overrides, key) ? json_object_get_object_member (overrides, key) : NULL;
				if (override) {
					if (json_object_has_member (override, "audio_enabled")) enabled = json_object_get_boolean_member (override, "audio_enabled");
					if (json_object_has_member (override, "audio_file")) { g_free (file); file = g_strdup (json_object_get_string_member (override, "audio_file")); }
					if (json_object_has_member (override, "audio_volume")) effective_volume = json_object_get_double_member (override, "audio_volume");
					if (json_object_has_member (override, "audio_repeat_seconds")) repeat_seconds = (guint) json_object_get_int_member (override, "audio_repeat_seconds");
				}
				g_free (key);
	}
	sqlite3_finalize (st);
	g_free (id); g_free ((gchar *) source); g_free ((gchar *) calendar);
	if (!presentation_enabled || !enabled || audio_suppressed || !has_active_alerts ()) { g_free (file); return; }
	if (!file || !*file) { g_free (file); file = ensure_default_tone (); }
	if (!file) return;
	if (audio_process) { g_free (file); return; }
	if (!g_file_test (file, G_FILE_TEST_IS_REGULAR)) {
		g_warning ("Audio file is unavailable: %s", file ? file : "(none)");
		g_free (file);
		return;
	}
	volume = g_strdup_printf ("%.3f", effective_volume);
	audio_process = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error, playback_program, "--volume", volume, file, NULL);
	g_free (volume);
	g_free (file);
	if (!audio_process) {
		g_warning ("Could not start audible reminder with %s: %s", playback_program, error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	g_subprocess_wait_async (audio_process, NULL, audio_finished, NULL);
	if (repeat_seconds && !audio_repeat_source)
		audio_repeat_source = g_timeout_add_seconds (repeat_seconds, repeat_audio, NULL);
}

static gboolean
source_selected (const gchar *source, const gchar *calendar)
{
	gchar *key;
	gboolean included, result;
	if (!source || !*source || !calendar || !*calendar) return FALSE;
	key = g_strdup_printf ("%s/%s", source, calendar);
	included = selected_calendars && g_hash_table_contains (selected_calendars, key);
	result = g_str_equal (policy_mode, "all") || (g_str_equal (policy_mode, "include") && included) || (g_str_equal (policy_mode, "exclude") && !included);
	g_free (key);
	return result;
}

static gboolean
calendar_source_selected (ESource *source)
{
	const gchar *uid;
	ESourceSelectable *selectable;
	gboolean enabled;
	if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) return FALSE;
	/* Registry check includes disabled parent accounts, not only this calendar. */
	enabled = eds_registry ? e_source_registry_check_enabled (eds_registry, source) : e_source_get_enabled (source);
	if (!enabled) return FALSE;
	uid = e_source_get_uid (source);
	if (!source_selected ("eds-local", uid)) return FALSE;
	if (!follow_calendar_selection) return TRUE;
	selectable = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR));
	return e_source_selectable_get_selected (selectable);
}

static gboolean
calendar_uid_selected (const gchar *uid)
{
	ESource *source;
	gboolean selected;
	if (!eds_enabled || !eds_registry || !source_selected ("eds-local", uid)) return FALSE;
	source = e_source_registry_ref_source (eds_registry, uid);
	selected = calendar_source_selected (source);
	g_clear_object (&source);
	return selected;
}

static gboolean
alert_presentable (const gchar *id)
{
	sqlite3_stmt *statement = NULL;
	gboolean presentable = FALSE;
	if (sqlite3_prepare_v2 (db, "SELECT source_id,calendar_id,owner,event_end FROM alerts WHERE id=? AND state='active'", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (statement) == SQLITE_ROW) {
			const gchar *source = (const gchar *) sqlite3_column_text (statement, 0);
			const gchar *calendar = (const gchar *) sqlite3_column_text (statement, 1);
			const gchar *owner = (const gchar *) sqlite3_column_text (statement, 2);
			gint64 end = sqlite3_column_int64 (statement, 3);
			presentable = TRUE;
			if (g_strcmp0 (source, "eds-local") == 0 &&
			    (g_strcmp0 (owner, "native") == 0 || g_strcmp0 (owner, "fallback") == 0))
				presentable = calendar_uid_selected (calendar) && (end <= 0 || end > (gint64) time (NULL));
		}
	}
	sqlite3_finalize (statement);
	return presentable;
}

static gboolean
calendar_uid_may_retain_snapshot (const gchar *uid)
{
	if (!source_selected ("eds-local", uid)) return FALSE;
	/* A registry outage leaves cached data stale, but cannot make it
	 * presentable. Preserve it until the source's enabled/selected state is
	 * known again. */
	if (eds_enabled && follow_calendar_selection && !eds_registry) return TRUE;
	return calendar_uid_selected (uid);
}

static gboolean
exec_sql (const gchar *sql, GError **error)
{
  gchar *message = NULL;
  int rc = sqlite3_exec (db, sql, NULL, NULL, &message);
  if (rc == SQLITE_OK) return TRUE;
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", message ? message : sqlite3_errmsg (db));
  sqlite3_free (message);
  return FALSE;
}

static gchar *
row_json (sqlite3_stmt *st)
{
  JsonObject *object = json_object_new ();
  JsonNode *node = json_node_new (JSON_NODE_OBJECT);
  const gchar *strings[] = { "id", "title", "body", "destination", "state", "action", "payload" };
  const gchar *numbers[] = { "revision", "expires_at", "snooze_until", "action_revision" };
  for (guint i = 0; i < G_N_ELEMENTS (strings); i++)
    json_object_set_string_member (object, strings[i], (const gchar *) sqlite3_column_text (st, i));
  for (guint i = 0; i < G_N_ELEMENTS (numbers); i++)
    json_object_set_int_member (object, numbers[i], sqlite3_column_int64 (st, i + 7));
  json_object_set_string_member (object, "source_id", (const gchar *) sqlite3_column_text (st, 11));
  json_object_set_string_member (object, "calendar_id", (const gchar *) sqlite3_column_text (st, 12));
  json_object_set_int_member (object, "event_start", sqlite3_column_int64 (st, 13));
  json_object_set_int_member (object, "event_end", sqlite3_column_int64 (st, 14));
  json_object_set_string_member (object, "occurrence", (const gchar *) sqlite3_column_text (st, 15));
  json_object_set_string_member (object, "owner", (const gchar *) sqlite3_column_text (st, 16));
  json_node_take_object (node, object);
  gchar *result = json_to_string (node, FALSE);
  json_node_free (node);
  return result;
}

static gboolean
read_one (const gchar *id, gchar **json, gchar **state, gint64 *revision, gint64 *expires, gint64 *snooze)
{
  sqlite3_stmt *st = NULL;
  gboolean found = FALSE;
  const gchar *sql = "SELECT id,title,body,destination,state,action,payload,revision,expires_at,snooze_until,action_revision,source_id,calendar_id,event_start,event_end,occurrence,owner FROM alerts WHERE id=?";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK) {
    sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step (st) == SQLITE_ROW) {
      found = TRUE;
      if (json) *json = row_json (st);
      if (state) *state = g_strdup ((const gchar *) sqlite3_column_text (st, 4));
      if (revision) *revision = sqlite3_column_int64 (st, 7);
      if (expires) *expires = sqlite3_column_int64 (st, 8);
      if (snooze) *snooze = sqlite3_column_int64 (st, 9);
    }
  }
  sqlite3_finalize (st);
  return found;
}

static gchar *
read_source_id (const gchar *id)
{
	sqlite3_stmt *st = NULL;
	gchar *source = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT source_id FROM alerts WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (st) == SQLITE_ROW) source = g_strdup ((const gchar *) sqlite3_column_text (st, 0));
	}
	sqlite3_finalize (st);
	return source;
}

static gchar *
read_owner (const gchar *id)
{
	sqlite3_stmt *st = NULL;
	gchar *owner = NULL;
	if (sqlite3_prepare_v2 (db, "SELECT owner FROM alerts WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (st) == SQLITE_ROW) owner = g_strdup ((const gchar *) sqlite3_column_text (st, 0));
	}
	sqlite3_finalize (st);
	return owner;
}

static void
refresh_deadlines (void)
{
  sqlite3_stmt *st = NULL;
  gint64 now = (gint64) time (NULL);
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='active',revision=revision+1 WHERE state='scheduled' AND owner='fallback' AND trigger_at>0 AND trigger_at<=? AND event_end>?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_bind_int64 (st, 2, now); sqlite3_step (st); sqlite3_finalize (st);
  st = NULL;
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='expired',revision=revision+1 WHERE state IN ('active','scheduled','snoozed') AND owner!='native' AND expires_at>0 AND expires_at<=?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_step (st); sqlite3_finalize (st);
  st = NULL;
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='expired',revision=revision+1 WHERE state IN ('active','scheduled','snoozed') AND owner='native' AND event_end>0 AND event_end<=?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_step (st); sqlite3_finalize (st);
  st = NULL;
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='active',snooze_until=0,revision=revision+1 WHERE state='snoozed' AND owner!='native' AND snooze_until>0 AND snooze_until<=?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_step (st); sqlite3_finalize (st);
	if (!has_active_alerts ()) {
		stop_audio ();
		g_clear_pointer (&foreground_id, g_free);
	}
}

static gboolean
deadline_poll (gpointer data)
{
	(void) data;
	refresh_deadlines ();
	ensure_renderer ();
	return G_SOURCE_CONTINUE;
}

static gboolean
terminate_core (gpointer data)
{
	(void) data;
	stop_audio ();
	stop_eds ();
	if (overlay_process) g_subprocess_force_exit (overlay_process);
	if (loop) g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

static gboolean
valid_id (const gchar *id)
{
  return id && g_regex_match_simple ("^[A-Za-z0-9][A-Za-z0-9._-]*:[A-Za-z0-9][A-Za-z0-9._/-]*$", id, 0, 0);
}

static gboolean
save_alert (const gchar *id, const gchar *title, const gchar *body, const gchar *destination, guint64 expires, gboolean update, guint64 *revision, GError **error)
{
  gchar *old_state = NULL;
  sqlite3_stmt *st = NULL;
	const gchar *sql = "INSERT INTO alerts(id,title,body,destination,state,action,payload,revision,expires_at,snooze_until) VALUES(?,?,?,?,?,'','',1,?,0) ON CONFLICT(id) DO UPDATE SET title=excluded.title,body=excluded.body,destination=excluded.destination,revision=alerts.revision+1,expires_at=excluded.expires_at,state=CASE WHEN excluded.state='expired' THEN 'expired' WHEN alerts.state IN ('dismissed','cancelled','expired','snoozed') THEN alerts.state ELSE 'active' END,snooze_until=CASE WHEN alerts.state='snoozed' THEN alerts.snooze_until ELSE 0 END";
  refresh_deadlines ();
  if (expires > G_MAXINT64 || !valid_id (id) || !title || !*title || strlen (id) > 180 || strlen (title) > 512 || strlen (body) > 8192 || strlen (destination) > 2048) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "invalid alert id or field size (id must be namespaced, title non-empty)"); return FALSE;
  }
  read_one (id, NULL, &old_state, NULL, NULL, NULL);
	if (update && !old_state) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "cannot update unknown alert ID %s", id);
		g_free (old_state);
		return FALSE;
	}
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK) goto failed;
  sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, title, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, body ? body : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 4, destination ? destination : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 5, expires > 0 && expires <= (guint64) time (NULL) ? "expired" : "active", -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 6, expires);
  if (sqlite3_step (st) != SQLITE_DONE) goto failed;
  sqlite3_finalize (st); st = NULL;
  read_one (id, NULL, NULL, (gint64 *) revision, NULL, NULL);
  g_free (old_state);
  return TRUE;
failed:
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", sqlite3_errmsg (db));
  sqlite3_finalize (st); g_free (old_state);
  return FALSE;
}

static gchar *
native_alert_id (const gchar *source_uid, const gchar *event_uid, const gchar *recurrence_id)
{
	return reminder_eds_alert_id (source_uid, event_uid, recurrence_id);
}

static gboolean
native_occurrence_is_current (const gchar *source_uid, const gchar *id, gint64 start)
{
	sqlite3_stmt *statement = NULL;
	gboolean snapshot_ready = FALSE, current = FALSE;
	if (sqlite3_prepare_v2 (db, "SELECT last_success_unix FROM calendar_sync WHERE calendar_id=?", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (statement) == SQLITE_ROW) snapshot_ready = sqlite3_column_int64 (statement, 0) > 0;
	}
	sqlite3_finalize (statement); statement = NULL;
	if (!snapshot_ready) return TRUE;
	if (sqlite3_prepare_v2 (db, "SELECT native_alarm,event_start FROM eds_occurrences WHERE alert_id=? AND calendar_id=?", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, id, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 2, source_uid, -1, SQLITE_TRANSIENT);
		if (sqlite3_step (statement) == SQLITE_ROW)
			current = sqlite3_column_int (statement, 0) != 0 && sqlite3_column_int64 (statement, 1) == start;
	}
	sqlite3_finalize (statement);
	return current;
}

static gchar *
native_reminder_id (const EReminderData *reminder)
{
	ECalComponent *component = e_reminder_data_get_component (reminder);
	ECalComponentAlarmInstance *instance = e_reminder_data_get_instance (reminder);
	const gchar *source_uid = e_reminder_data_get_source_uid (reminder);
	const gchar *event_uid = component ? e_cal_component_get_uid (component) : NULL;
	const gchar *rid = instance ? e_cal_component_alarm_instance_get_rid (instance) : NULL;
	if (!source_uid || !event_uid || !instance) return NULL;
	return native_alert_id (source_uid, event_uid, rid);
}

static gchar *
native_meeting_url (ECalComponent *component)
{
	gchar *url = NULL, *ical = e_cal_component_get_as_string (component);
	GString *text = g_string_new (ical ? ical : "");
	const gchar *patterns[] = {
		"(?im)^X-GOOGLE-CONFERENCE:(https?://[^\\r\\n]+)",
		"(?im)^X-MICROSOFT-CDO-ONLINE-MEETINGURL:(https?://[^\\r\\n]+)",
		"(?i)(https?://(?:[a-z0-9-]+\\.)*(?:zoom\\.us|zoomgov\\.com|meet\\.google\\.com|teams\\.microsoft\\.com|teams\\.live\\.com|webex\\.com|whereby\\.com|bluejeans\\.com|gotomeeting\\.com|gotowebinar\\.com|join\\.skype\\.com|skype\\.com|ringcentral\\.com|chime\\.aws|discord\\.gg)[^\\s<>\\\"\\r\\n\\\\]+)",
		"(?im)^URL:(https?://[^\\r\\n]+)"
	};
	GSList *descriptions = e_cal_component_get_descriptions (component);
	for (GSList *item = descriptions; item; item = item->next) {
		const gchar *description = e_cal_component_text_get_value (item->data);
		if (description && *description) { g_string_append_c (text, '\n'); g_string_append (text, description); }
	}
	g_slist_free_full (descriptions, (GDestroyNotify) e_cal_component_text_free);
	for (guint i = 0; i < G_N_ELEMENTS (patterns) && !url; i++) {
		GRegex *regex = g_regex_new (patterns[i], G_REGEX_OPTIMIZE, 0, NULL);
		GMatchInfo *match = NULL;
		if (regex && g_regex_match (regex, text->str, 0, &match)) url = g_match_info_fetch (match, 1);
		if (match) g_match_info_free (match);
		if (regex) g_regex_unref (regex);
	}
	g_string_free (text, TRUE);
	g_free (ical);
	if (!url) url = e_cal_component_get_url (component);
	if (!url) return NULL;
	g_strstrip (url);
	while (*url && strchr (".,;:)]}", url[strlen (url) - 1])) url[strlen (url) - 1] = '\0';
	if (!g_uri_is_valid (url, G_URI_FLAGS_NONE, NULL)) { g_free (url); return NULL; }
	gchar *scheme = g_uri_parse_scheme (url);
	gboolean http = scheme && (g_ascii_strcasecmp (scheme, "http") == 0 || g_ascii_strcasecmp (scheme, "https") == 0);
	g_free (scheme);
	if (!http) g_clear_pointer (&url, g_free);
	return url;
}

static void
native_reminder_triggered (EReminderWatcher *watcher, const GSList *reminders, gboolean snoozed, gpointer user_data)
{
	const GSList *link;
	(void) user_data;
	for (link = reminders; link; link = link->next) {
		EReminderData *reminder = link->data;
		const gchar *source_uid = e_reminder_data_get_source_uid (reminder);
		ECalComponent *component = e_reminder_data_get_component (reminder);
		ECalComponentAlarmInstance *instance = e_reminder_data_get_instance (reminder);
		ESource *source;
		const ECalComponentText *summary_text;
		const gchar *event_uid, *summary;
		gchar *id, *body, *occurrence, *url;
		gint64 start, end;
		guint64 revision = 0;
		gboolean reactivated = FALSE;
		GError *error = NULL;
		gchar *state = NULL;
		if (!source_uid || !component || !instance) continue;
		reset_renderer_retries ();
		source = e_source_registry_ref_source (eds_registry, source_uid);
		if (!calendar_source_selected (source) || g_strcmp0 (e_source_get_uid (source), source_uid) != 0) {
			g_clear_object (&source);
			continue;
		}
		event_uid = e_cal_component_get_uid (component);
		if (!event_uid || !*event_uid) { g_object_unref (source); continue; }
		start = e_cal_component_alarm_instance_get_occur_start (instance);
		end = e_cal_component_alarm_instance_get_occur_end (instance);
		const gchar *rid = e_cal_component_alarm_instance_get_rid (instance);
		occurrence = g_strdup_printf ("%s/%s", event_uid, rid ? rid : "");
		id = native_alert_id (source_uid, event_uid, rid);
		if (!native_occurrence_is_current (source_uid, id, start)) {
			g_free (id); g_free (occurrence); g_object_unref (source);
			continue;
		}
		summary_text = e_cal_component_get_summary (component);
		summary = summary_text ? e_cal_component_text_get_value (summary_text) : NULL;
		body = e_reminder_watcher_describe_data (watcher, reminder, E_REMINDER_WATCHER_DESCRIBE_FLAG_NONE);
		if (!body) body = g_strdup ("");
		url = native_meeting_url (component);
		if (snoozed && read_one (id, NULL, &state, NULL, NULL, NULL) && g_str_equal (state, "snoozed")) {
			sqlite3_stmt *st = NULL;
			if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='active',revision=revision+1,snooze_until=0 WHERE id=? AND state='snoozed'", -1, &st, NULL) == SQLITE_OK) {
				sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
				if (sqlite3_step (st) == SQLITE_DONE && sqlite3_changes (db) == 1) {
					read_one (id, NULL, NULL, (gint64 *) &revision, NULL, NULL);
					reactivated = TRUE;
				}
			}
			sqlite3_finalize (st);
		} else if (!save_alert (id, summary && *summary ? summary : "Calendar reminder", body, url ? url : "", 0, FALSE, &revision, &error)) {
			g_warning ("Could not save native EDS reminder: %s", error ? error->message : "unknown error");
		}
		g_clear_error (&error);
		g_free (state); state = NULL;
		if (read_one (id, NULL, &state, NULL, NULL, NULL) && g_str_equal (state, "active")) {
			sqlite3_stmt *st = NULL;
			if (sqlite3_prepare_v2 (db, "UPDATE alerts SET source_id='eds-local',calendar_id=?,event_start=?,event_end=?,occurrence=?,owner='native' WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
				sqlite3_bind_text (st, 1, source_uid, -1, SQLITE_TRANSIENT);
				sqlite3_bind_int64 (st, 2, start);
				sqlite3_bind_int64 (st, 3, end);
				sqlite3_bind_text (st, 4, occurrence, -1, SQLITE_TRANSIENT);
				sqlite3_bind_text (st, 5, id, -1, SQLITE_TRANSIENT);
				sqlite3_step (st);
			}
			sqlite3_finalize (st);
			g_hash_table_replace (native_reminders, g_strdup (id), e_reminder_data_copy (reminder));
			(void) reactivated;
			if (bus_connection) g_dbus_connection_emit_signal (bus_connection, NULL, OBJECT_PATH, IFACE, "Changed", g_variant_new ("(st)", id, revision), NULL);
		}
		g_free (state);
		g_free (body);
		g_free (url);
		g_free (id);
		g_free (occurrence);
		g_object_unref (source);
	}
}

static void
restore_native_reminder_pointers (void)
{
	if (!eds_watcher || !eds_registry) return;
	GSList *past = e_reminder_watcher_dup_past (eds_watcher);
	GSList *snoozed = e_reminder_watcher_dup_snoozed (eds_watcher);
	GSList *link;
	if (past) native_reminder_triggered (eds_watcher, past, FALSE, NULL);
	for (link = snoozed; link; link = link->next) {
		EReminderData *reminder = link->data;
		gchar *id = native_reminder_id (reminder), *source_id = id ? read_source_id (id) : NULL;
		const gchar *uid = e_reminder_data_get_source_uid (reminder);
		if (id && g_strcmp0 (source_id, "eds-local") == 0 && calendar_uid_selected (uid))
			g_hash_table_replace (native_reminders, g_strdup (id), e_reminder_data_copy (reminder));
		g_free (source_id);
		g_free (id);
	}
	g_slist_free_full (past, e_reminder_data_free);
	g_slist_free_full (snoozed, e_reminder_data_free);
}

static void
native_reminders_remove_source (const gchar *source_uid)
{
	GHashTableIter iter; gpointer key, value;
	if (!native_reminders) return;
	g_hash_table_iter_init (&iter, native_reminders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *uid = e_reminder_data_get_source_uid (value);
		if (g_strcmp0 (uid, source_uid) == 0) g_hash_table_iter_remove (&iter);
	}
}

static void
reconcile_native_reminders (const gchar *source_uid)
{
	GHashTableIter iter; gpointer key, value;
	if (!native_reminders) return;
	g_hash_table_iter_init (&iter, native_reminders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		EReminderData *reminder = value;
		const gchar *owner_source = e_reminder_data_get_source_uid (reminder);
		if (g_strcmp0 (owner_source, source_uid) != 0) continue;
		const gchar *state = NULL, *owner = NULL;
		gint64 stored_start = 0;
		sqlite3_stmt *statement = NULL;
		gboolean keep = FALSE;
		if (sqlite3_prepare_v2 (db, "SELECT state,owner,event_start FROM alerts WHERE id=?", -1, &statement, NULL) == SQLITE_OK) {
			sqlite3_bind_text (statement, 1, key, -1, SQLITE_TRANSIENT);
			if (sqlite3_step (statement) == SQLITE_ROW) {
				state = (const gchar *) sqlite3_column_text (statement, 0);
				owner = (const gchar *) sqlite3_column_text (statement, 1);
				stored_start = sqlite3_column_int64 (statement, 2);
				ECalComponentAlarmInstance *instance = e_reminder_data_get_instance (reminder);
				gint64 pointer_start = instance ? e_cal_component_alarm_instance_get_occur_start (instance) : -1;
				keep = instance && g_strcmp0 (owner, "native") == 0 && (g_strcmp0 (state, "active") == 0 || g_strcmp0 (state, "snoozed") == 0) && stored_start == pointer_start;
			}
		}
		sqlite3_finalize (statement);
		if (!keep) g_hash_table_iter_remove (&iter);
	}
}

typedef struct {
	gchar *source_uid;
	ESource *source;
	ECalClient *client;
	GCancellable *cancellable;
	guint timeout_source;
	gboolean for_every_event;
	gboolean scan_again;
	gboolean invalidated;
} EdsScan;

static void
eds_scan_free (EdsScan *scan)
{
	if (!scan) return;
	if (scan->timeout_source) g_source_remove (scan->timeout_source);
	g_free (scan->source_uid);
	g_clear_object (&scan->source);
	g_clear_object (&scan->client);
	g_clear_object (&scan->cancellable);
	g_free (scan);
}

static gboolean
eds_scan_timeout (gpointer user_data)
{
	EdsScan *scan = user_data;
	scan->timeout_source = 0;
	g_cancellable_cancel (scan->cancellable);
	return G_SOURCE_REMOVE;
}

static void
eds_scan_finish (EdsScan *scan)
{
	ESource *source = scan->scan_again ? g_object_ref (scan->source) : NULL;
	if (eds_scans && g_hash_table_lookup (eds_scans, scan->source_uid) == scan)
		g_hash_table_remove (eds_scans, scan->source_uid);
	eds_scan_free (scan);
	if (source) { scan_source (source); g_object_unref (source); }
}

static void
calendar_policy (const gchar *source_uid, gboolean *fallback_enabled, guint *lead_minutes)
{
	JsonObject *root = validated_policy_node ? json_node_get_object (validated_policy_node) : NULL;
	JsonObject *overrides = root && json_object_has_member (root, "calendar_overrides") ? json_object_get_object_member (root, "calendar_overrides") : NULL;
	gchar *key = g_strdup_printf ("eds-local/%s", source_uid);
	JsonObject *override = overrides && json_object_has_member (overrides, key) ? json_object_get_object_member (overrides, key) : NULL;
	*fallback_enabled = !override || !json_object_has_member (override, "fallback_enabled") || json_object_get_boolean_member (override, "fallback_enabled");
	*lead_minutes = override && json_object_has_member (override, "fallback_lead_minutes") ?
		(guint) json_object_get_int_member (override, "fallback_lead_minutes") : fallback_lead_minutes;
	g_free (key);
}

static void
apply_cached_fallback_policy (void)
{
	sqlite3_stmt *rows = NULL, *update = NULL;
	GPtrArray *changes = g_ptr_array_new_with_free_func (g_free);
	gint64 now = (gint64) time (NULL);
	if (sqlite3_prepare_v2 (db, "SELECT id,calendar_id,event_start,event_end,state FROM alerts WHERE source_id='eds-local' AND owner='fallback' AND state IN ('active','scheduled','snoozed')", -1, &rows, NULL) == SQLITE_OK) {
		while (sqlite3_step (rows) == SQLITE_ROW) {
			const gchar *id = (const gchar *) sqlite3_column_text (rows, 0);
			const gchar *calendar = (const gchar *) sqlite3_column_text (rows, 1);
			const gchar *state = (const gchar *) sqlite3_column_text (rows, 4);
			gint64 start = sqlite3_column_int64 (rows, 2), end = sqlite3_column_int64 (rows, 3);
			gboolean enabled = FALSE;
			guint lead = fallback_lead_minutes;
			calendar_policy (calendar, &enabled, &lead);
			if (!calendar_uid_may_retain_snapshot (calendar) || !enabled || start <= 0 || end <= now) {
				g_ptr_array_add (changes, g_strdup_printf ("%s\twithdrawn\t0", id));
				continue;
			}
			if (g_str_equal (state, "snoozed")) continue;
			gint64 trigger = start - (gint64) lead * 60;
			const gchar *next = trigger <= now ? "active" : "scheduled";
			g_ptr_array_add (changes, g_strdup_printf ("%s\t%s\t%" G_GINT64_FORMAT, id, next, trigger));
		}
	}
	sqlite3_finalize (rows);
	if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state=?,trigger_at=?,revision=revision+CASE WHEN state!=? OR trigger_at!=? THEN 1 ELSE 0 END WHERE id=? AND owner='fallback' AND state IN ('active','scheduled','snoozed')", -1, &update, NULL) == SQLITE_OK) {
		for (guint i = 0; i < changes->len; i++) {
			gchar **parts = g_strsplit (g_ptr_array_index (changes, i), "\t", 3);
			gint64 trigger = g_ascii_strtoll (parts[2], NULL, 10);
			sqlite3_reset (update); sqlite3_clear_bindings (update);
			sqlite3_bind_text (update, 1, parts[1], -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64 (update, 2, trigger);
			sqlite3_bind_text (update, 3, parts[1], -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64 (update, 4, trigger);
			sqlite3_bind_text (update, 5, parts[0], -1, SQLITE_TRANSIENT);
			sqlite3_step (update);
			g_strfreev (parts);
		}
	}
	sqlite3_finalize (update);
	g_ptr_array_unref (changes);
}

static void
calendar_syncing (const gchar *source_uid)
{
	sqlite3_stmt *statement = NULL;
	if (sqlite3_prepare_v2 (db, "INSERT INTO calendar_sync(calendar_id,state,last_success_unix,error,event_count) VALUES(?,'syncing',0,'',0) ON CONFLICT(calendar_id) DO UPDATE SET state='syncing',error=''", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_step (statement);
	}
	sqlite3_finalize (statement);
}

static void
calendar_sync_state (const gchar *source_uid, const gchar *state, const gchar *message)
{
	sqlite3_stmt *statement = NULL;
	if (sqlite3_prepare_v2 (db, "INSERT INTO calendar_sync(calendar_id,state,last_success_unix,error,event_count) VALUES(?,?,0,?,0) ON CONFLICT(calendar_id) DO UPDATE SET state=excluded.state,error=excluded.error", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 2, state, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (statement, 3, message ? message : "", -1, SQLITE_TRANSIENT);
		sqlite3_step (statement);
	}
	sqlite3_finalize (statement);
}

static void
clear_occurrence_snapshot (const gchar *source_uid)
{
	sqlite3_stmt *statement = NULL;
	if (sqlite3_prepare_v2 (db, "DELETE FROM eds_occurrences WHERE calendar_id=?", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, source_uid, -1, SQLITE_TRANSIENT);
		sqlite3_step (statement);
	}
	sqlite3_finalize (statement);
}

static void
clear_unselected_occurrence_snapshots (void)
{
	sqlite3_stmt *statement = NULL;
	GPtrArray *calendars = g_ptr_array_new_with_free_func (g_free);
	if (sqlite3_prepare_v2 (db, "SELECT DISTINCT calendar_id FROM eds_occurrences", -1, &statement, NULL) == SQLITE_OK) {
		while (sqlite3_step (statement) == SQLITE_ROW) {
			const gchar *calendar = (const gchar *) sqlite3_column_text (statement, 0);
			if (!calendar_uid_may_retain_snapshot (calendar)) g_ptr_array_add (calendars, g_strdup (calendar));
		}
	}
	sqlite3_finalize (statement);
	for (guint i = 0; i < calendars->len; i++) clear_occurrence_snapshot (g_ptr_array_index (calendars, i));
	g_ptr_array_unref (calendars);
}

static gchar *
calendar_snapshot_to_ical (GSList *components)
{
	GString *text = g_string_new ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Calendar Reminder Overlay//EN\r\n");
	for (GSList *link = components; link; link = link->next) {
		gchar *component = e_cal_component_get_as_string (link->data);
		if (!component) { g_string_free (text, TRUE); return NULL; }
		g_string_append (text, component);
		if (!g_str_has_suffix (component, "\n")) g_string_append (text, "\r\n");
		g_free (component);
	}
	g_string_append (text, "END:VCALENDAR\r\n");
	return g_string_free (text, FALSE);
}

typedef struct {
	gchar *ical;
	ICalTimezone *zone;
	gint64 now;
	gboolean for_every_event;
} EdsParse;

typedef struct {
	EdsScan *scan;
	EdsParse *parse;
} EdsWork;

static void
eds_parse_free (gpointer data)
{
	EdsParse *parse = data;
	g_free (parse->ical);
	g_clear_object (&parse->zone);
	g_free (parse);
}

static void
eds_work_free (gpointer data)
{
	EdsWork *work = data;
	eds_parse_free (work->parse);
	g_free (work);
}

static void
eds_parse_worker (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	EdsWork *work = task_data;
	EdsScan *scan = work ? work->scan : NULL;
	EdsParse *parse = work ? work->parse : NULL;
	GError *error = NULL;
	GPtrArray *events;
	(void) cancellable; (void) source_object;
	if (!parse || !scan) { g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Missing calendar parse context"); return; }
	events = reminder_eds_expand_ics (scan->source_uid, parse->ical, parse->zone,
		parse->now - 7 * 86400, parse->now + 90 * 86400, parse->for_every_event, &error);
	if (!events) g_task_return_error (task, error ? error : g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, "Calendar expansion failed"));
	else g_task_return_pointer (task, events, (GDestroyNotify) g_ptr_array_unref);
}

static void
eds_parse_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	EdsScan *scan = user_data;
	GError *error = NULL;
	GPtrArray *events = g_task_propagate_pointer (G_TASK (result), &error);
	if (events && !scan->invalidated && eds_enabled && calendar_source_selected (scan->source)) {
		gboolean fallback_enabled;
		guint lead_minutes;
		calendar_policy (scan->source_uid, &fallback_enabled, &lead_minutes);
		if (!reminder_eds_apply_snapshot (db, scan->source_uid, events, TRUE, fallback_enabled,
			global_native_default, lead_minutes, (gint64) time (NULL), NULL, &error))
			g_warning ("Could not apply EDS snapshot for %s: %s", scan->source_uid, error ? error->message : "unknown error");
		else {
			reset_renderer_retries ();
			reconcile_native_reminders (scan->source_uid);
			restore_native_reminder_pointers ();
		}
	}
	if (error && !scan->invalidated && eds_enabled && calendar_source_selected (scan->source)) {
		g_warning ("Could not reconcile EDS calendar %s; retaining last successful snapshot: %s", scan->source_uid, error->message);
		reminder_eds_apply_snapshot (db, scan->source_uid, NULL, FALSE, FALSE, FALSE, 0, (gint64) time (NULL), error->message, NULL);
	}
	g_clear_error (&error);
	if (events) g_ptr_array_unref (events);
	eds_scan_finish (scan);
	(void) source;
}

static void
eds_fetch_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	EdsScan *scan = user_data;
	GSList *components = NULL;
	GError *error = NULL;
	gboolean parsing = FALSE;
	if (e_cal_client_get_object_list_as_comps_finish (E_CAL_CLIENT (source), result, &components, &error)) {
		if (scan->timeout_source) { g_source_remove (scan->timeout_source); scan->timeout_source = 0; }
		if (scan->invalidated) {
			g_slist_free_full (components, g_object_unref);
			eds_scan_finish (scan);
			return;
		}
		gchar *ical = calendar_snapshot_to_ical (components);
		if (!ical) {
			g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "EDS returned an event that could not be serialized");
		} else {
			ICalTimezone *zone = e_cal_client_get_default_timezone (scan->client);
			if (!zone) {
				GTimeZone *local = g_time_zone_new_local ();
				const gchar *identifier = g_time_zone_get_identifier (local);
				zone = i_cal_timezone_get_builtin_timezone_from_tzid (identifier);
				if (!zone) zone = i_cal_timezone_get_builtin_timezone (identifier);
				g_time_zone_unref (local);
			}
			if (!zone) g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No timezone is available for this calendar; preserving the prior snapshot");
			if (zone) {
				EdsParse *parse = g_new0 (EdsParse, 1);
				parse->ical = ical; parse->zone = g_object_ref (zone); parse->now = (gint64) time (NULL); parse->for_every_event = scan->for_every_event;
				EdsWork *work = g_new0 (EdsWork, 1); work->scan = scan; work->parse = parse;
				GTask *task = g_task_new (scan->client, scan->cancellable, eds_parse_done, scan);
				/* The worker reads scan->source_uid, so cancellation must not
				 * free the scan before parsing finishes. */
				g_task_set_return_on_cancel (task, FALSE);
				g_task_set_task_data (task, work, eds_work_free);
				g_task_run_in_thread (task, eds_parse_worker);
				g_object_unref (task);
				parsing = TRUE;
				ical = NULL;
			}
			g_free (ical);
		}
	} else if (error) {
		g_warning ("Could not fetch EDS calendar %s; retaining last successful snapshot: %s", scan->source_uid, error->message);
	}
	if (!error && !parsing)
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "EDS returned no usable calendar snapshot; preserving the previous snapshot");
	if (error) {
		if (!scan->invalidated && eds_enabled && calendar_source_selected (scan->source))
			reminder_eds_apply_snapshot (db, scan->source_uid, NULL, FALSE, FALSE, FALSE, 0, (gint64) time (NULL), error->message, NULL);
		eds_scan_finish (scan);
	}
	g_clear_error (&error);
	g_slist_free_full (components, g_object_unref);
}

static void
eds_connect_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	EdsScan *scan = user_data;
	GError *error = NULL;
	EClient *client = e_cal_client_connect_finish (result, &error);
	if (!client) {
		if (!scan->invalidated && eds_enabled && calendar_source_selected (scan->source))
			reminder_eds_apply_snapshot (db, scan->source_uid, NULL, FALSE, FALSE, FALSE, 0, (gint64) time (NULL), error ? error->message : "EDS connection failed", NULL);
		g_warning ("Could not connect to EDS calendar %s; retaining last successful snapshot: %s", scan->source_uid, error ? error->message : "unknown error");
		g_clear_error (&error);
		eds_scan_finish (scan);
		return;
	}
	scan->client = E_CAL_CLIENT (client);
	if (scan->invalidated) { eds_scan_finish (scan); return; }
	e_cal_client_get_object_list_as_comps (scan->client, "#t", scan->cancellable, eds_fetch_done, scan);
	(void) source;
}

static void
scan_source (ESource *source)
{
	const gchar *uid = e_source_get_uid (source);
	EdsScan *scan;
	if (!eds_enabled || !eds_registry || !uid || !calendar_source_selected (source)) return;
	if (eds_scans && g_hash_table_contains (eds_scans, uid)) { ((EdsScan *) g_hash_table_lookup (eds_scans, uid))->scan_again = TRUE; return; }
	scan = g_new0 (EdsScan, 1);
	scan->source_uid = g_strdup (uid);
	scan->source = g_object_ref (source);
	scan->cancellable = g_cancellable_new ();
	ESourceAlarms *alarms = e_source_has_extension (source, E_SOURCE_EXTENSION_ALARMS) ?
		 e_source_get_extension (source, E_SOURCE_EXTENSION_ALARMS) : NULL;
	scan->for_every_event = global_native_default || (alarms && e_source_alarms_get_for_every_event (alarms));
	if (!eds_scans) eds_scans = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_insert (eds_scans, g_strdup (uid), scan);
	calendar_syncing (uid);
	scan->timeout_source = g_timeout_add_seconds (30, eds_scan_timeout, scan);
	e_cal_client_connect (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 20, scan->cancellable, eds_connect_done, scan);
}

static void
scan_selected_calendars (void)
{
	GList *sources;
	if (!eds_enabled || !eds_registry) return;
	sources = e_source_registry_list_sources (eds_registry, E_SOURCE_EXTENSION_CALENDAR);
	for (GList *link = sources; link; link = link->next) {
		ESource *source = link->data;
		if (calendar_source_selected (source)) scan_source (source);
		else registry_source_changed (eds_registry, source, NULL);
	}
	g_list_free_full (sources, g_object_unref);
}

static void
registry_source_changed (ESourceRegistry *registry, ESource *source, gpointer user_data)
{
	(void) registry; (void) user_data;
	if (calendar_source_selected (source)) {
		scan_source (source);
		restore_native_reminder_pointers ();
	}
	else {
		const gchar *uid = e_source_get_uid (source);
		EdsScan *pending = eds_scans ? g_hash_table_lookup (eds_scans, uid) : NULL;
		if (pending) {
			pending->invalidated = TRUE;
			g_cancellable_cancel (pending->cancellable);
		}
		clear_occurrence_snapshot (uid);
		gboolean enabled = eds_registry ? e_source_registry_check_enabled (eds_registry, source) : e_source_get_enabled (source);
		calendar_sync_state (uid, !enabled ? "disabled" : "not-selected",
			!enabled ? "calendar source or parent account is disabled" :
			(follow_calendar_selection && e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
			 !e_source_selectable_get_selected (E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR))) ?
			 "calendar is not selected in Evolution" : "calendar is excluded by source-selection policy"));
		sqlite3_stmt *statement = NULL;
		if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE source_id='eds-local' AND calendar_id=? AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) == SQLITE_OK) {
			sqlite3_bind_text (statement, 1, uid, -1, SQLITE_TRANSIENT); sqlite3_step (statement);
		}
		sqlite3_finalize (statement);
		native_reminders_remove_source (uid);
		if (foreground_id) { gchar *calendar = NULL; sqlite3_stmt *query = NULL; if (sqlite3_prepare_v2 (db, "SELECT calendar_id FROM alerts WHERE id=?", -1, &query, NULL) == SQLITE_OK) { sqlite3_bind_text (query, 1, foreground_id, -1, SQLITE_TRANSIENT); if (sqlite3_step (query) == SQLITE_ROW) calendar = g_strdup ((const gchar *) sqlite3_column_text (query, 0)); } sqlite3_finalize (query); if (g_strcmp0 (calendar, uid) == 0) { stop_audio (); g_clear_pointer (&foreground_id, g_free); } g_free (calendar); }
	}
}

static void
registry_source_removed (ESourceRegistry *registry, ESource *source, gpointer user_data)
{
	ESource *disabled = source;
	(void) registry; (void) user_data;
	const gchar *uid = e_source_get_uid (disabled);
	clear_occurrence_snapshot (uid);
	calendar_sync_state (uid, "removed", "calendar source was removed");
	sqlite3_stmt *statement = NULL;
	if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE source_id='eds-local' AND calendar_id=? AND state IN ('active','scheduled','snoozed')", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_text (statement, 1, uid, -1, SQLITE_TRANSIENT); sqlite3_step (statement);
	}
	sqlite3_finalize (statement);
	native_reminders_remove_source (uid);
}

static void
registry_source_enabled (ESourceRegistry *registry, ESource *source, gpointer user_data)
{
	(void) registry; (void) user_data;
	if (calendar_source_selected (source)) scan_source (source);
}

static gboolean
periodic_eds_scan (gpointer user_data)
{
	(void) user_data;
	scan_selected_calendars ();
	return G_SOURCE_CONTINUE;
}

static gboolean
retry_eds_registry (gpointer user_data)
{
	(void) user_data;
	if (!eds_enabled) return G_SOURCE_CONTINUE;
	if (!eds_registry && !eds_starting) start_eds ();
	return G_SOURCE_CONTINUE;
}

static void
watcher_changed (EReminderWatcher *watcher, gpointer user_data)
{
	(void) watcher; (void) user_data;
	restore_native_reminder_pointers ();
}

static gboolean
global_default_reminders (void)
{
	GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
	GSettingsSchema *schema = source ? g_settings_schema_source_lookup (source, "org.gnome.evolution-data-server.calendar", TRUE) : NULL;
	if (!schema) return FALSE;
	global_calendar_settings = g_settings_new_full (schema, NULL, NULL);
	g_settings_schema_unref (schema);
	return g_settings_get_boolean (global_calendar_settings, "defall-reminder-enabled");
}

static void
global_default_changed (GSettings *settings, const gchar *key, gpointer user_data)
{
	gboolean next = g_settings_get_boolean (settings, key);
	(void) user_data;
	if (next == global_native_default) return;
	global_native_default = next;
	scan_selected_calendars ();
}

static void
eds_registry_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	ESourceRegistry *registry = e_source_registry_new_finish (result, &error);
	eds_starting = FALSE;
	g_clear_object (&eds_start_cancellable);
	if (!registry) {
		if (eds_enabled) {
			g_warning ("EDS registry unavailable; retaining cached calendar reminders: %s", error ? error->message : "unknown error");
			GHashTableIter iter; gpointer key;
			g_hash_table_iter_init (&iter, selected_calendars);
			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				const gchar *selection_key = key;
				if (g_str_has_prefix (selection_key, "eds-local/"))
					reminder_eds_apply_snapshot (db, selection_key + strlen ("eds-local/"), NULL, FALSE, FALSE, FALSE, 0, time (NULL), error ? error->message : "EDS registry unavailable", NULL);
			}
		}
		g_clear_error (&error);
		return;
	}
	if (!eds_enabled) { g_object_unref (registry); return; }
	eds_registry = registry;
	if (eds_retry_source) { g_source_remove (eds_retry_source); eds_retry_source = 0; }
	global_native_default = global_default_reminders ();
	if (global_calendar_settings)
		g_signal_connect (global_calendar_settings, "changed::defall-reminder-enabled", G_CALLBACK (global_default_changed), NULL);
	eds_watcher = e_reminder_watcher_new (eds_registry);
	g_signal_connect (eds_watcher, "triggered", G_CALLBACK (native_reminder_triggered), NULL);
	g_signal_connect (eds_watcher, "changed", G_CALLBACK (watcher_changed), NULL);
	restore_native_reminder_pointers ();
	g_signal_connect (eds_registry, "source-added", G_CALLBACK (registry_source_changed), NULL);
	g_signal_connect (eds_registry, "source-changed", G_CALLBACK (registry_source_changed), NULL);
	g_signal_connect (eds_registry, "source-removed", G_CALLBACK (registry_source_removed), NULL);
	g_signal_connect (eds_registry, "source-enabled", G_CALLBACK (registry_source_enabled), NULL);
	g_signal_connect (eds_registry, "source-disabled", G_CALLBACK (registry_source_changed), NULL);
	scan_selected_calendars ();
	if (!eds_rescan_source) eds_rescan_source = g_timeout_add_seconds (300, periodic_eds_scan, NULL);
	g_message ("Native EDS reminders enabled; selected calendar snapshots are fetched asynchronously");
	(void) source; (void) user_data;
}

static void
start_eds (void)
{
	if (!eds_enabled || eds_registry || eds_starting) return;
	if (!eds_retry_source) eds_retry_source = g_timeout_add_seconds (30, retry_eds_registry, NULL);
	eds_starting = TRUE;
	eds_start_cancellable = g_cancellable_new ();
	e_source_registry_new (eds_start_cancellable, eds_registry_ready, NULL);
}

static void
stop_eds (void)
{
	if (eds_start_cancellable) g_cancellable_cancel (eds_start_cancellable);
	if (eds_scans) {
		GHashTableIter iter; gpointer value;
		g_hash_table_iter_init (&iter, eds_scans);
		while (g_hash_table_iter_next (&iter, NULL, &value)) g_cancellable_cancel (((EdsScan *) value)->cancellable);
	}
	if (eds_rescan_source) { g_source_remove (eds_rescan_source); eds_rescan_source = 0; }
	if (eds_retry_source) { g_source_remove (eds_retry_source); eds_retry_source = 0; }
	g_clear_object (&eds_watcher);
	g_clear_object (&eds_registry);
	g_clear_object (&global_calendar_settings);
	if (native_reminders) g_hash_table_remove_all (native_reminders);
}

static gboolean
any_eligible_calendar_alerts (void)
{
	sqlite3_stmt *statement = NULL;
	gboolean found = FALSE;
	if (sqlite3_prepare_v2 (db, "SELECT calendar_id FROM alerts WHERE source_id='eds-local' AND owner IN ('native','fallback') AND state='active' AND (event_end<=0 OR event_end>?)", -1, &statement, NULL) == SQLITE_OK) {
		sqlite3_bind_int64 (statement, 1, (gint64) time (NULL));
		while (sqlite3_step (statement) == SQLITE_ROW) {
			const gchar *calendar = (const gchar *) sqlite3_column_text (statement, 0);
			if (eds_enabled && calendar_uid_selected (calendar)) { found = TRUE; break; }
		}
	}
	sqlite3_finalize (statement);
	return found;
}

static gboolean
renderer_should_run (void)
{
	gboolean armed_active = FALSE;
	if (!presentation_enabled) return FALSE;
	if (presented_alerts) {
		GHashTableIter iter;
		gpointer id;
		g_hash_table_iter_init (&iter, presented_alerts);
		while (g_hash_table_iter_next (&iter, &id, NULL)) {
			gchar *state = NULL;
			gboolean exists = read_one (id, NULL, &state, NULL, NULL, NULL);
			if (exists && g_str_equal (state, "active")) armed_active = TRUE;
			else if (!exists || (!g_str_equal (state, "snoozed") && !g_str_equal (state, "scheduled")))
				g_hash_table_iter_remove (&iter);
			g_free (state);
		}
	}
	return overlay_path && *overlay_path && (any_eligible_calendar_alerts () || armed_active);
}

static gchar *
effective_policy_json (void)
{
	JsonNode *copy = validated_policy_node ? json_node_copy (validated_policy_node) : json_node_new (JSON_NODE_OBJECT);
	JsonObject *root = json_node_get_object (copy);
	JsonObject *presentation = json_object_new ();
	JsonGenerator *generator = json_generator_new ();
	gchar *json;
	json_object_set_boolean_member (presentation, "enabled", presentation_enabled);
	json_object_set_object_member (root, "presentation", presentation);
	json_generator_set_root (generator, copy);
	json = json_generator_to_data (generator, NULL);
	g_object_unref (generator);
	json_node_unref (copy);
	return json;
}

static void
overlay_wait_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GSubprocess *process = G_SUBPROCESS (source);
	GError *error = NULL;
	gboolean successful = g_subprocess_wait_finish (process, result, &error);
	gboolean current = overlay_process == process;
	if (current) g_clear_object (&overlay_process);
	if (!successful) g_warning ("Alert renderer monitor failed: %s", error ? error->message : "unknown error");
	g_clear_error (&error);
	if (current && renderer_should_run ()) {
		if ((gint64) time (NULL) - overlay_started_at >= 30) overlay_retry_count = 0;
		if (overlay_retry_count < 5) {
			guint delay = 1u << MIN (overlay_retry_count, 4u);
			overlay_retry_count++;
			g_warning ("Shared alert renderer exited while an alert still needs presentation; retrying in %u seconds", delay);
			if (overlay_retry_source) g_source_remove (overlay_retry_source);
		overlay_retry_source = g_timeout_add_seconds (delay, renderer_retry_cb, NULL);
		} else {
			g_warning ("Shared alert renderer failed repeatedly; further retries wait for a new alert or source change");
			renderer_retry_exhausted = TRUE;
		}
	}
	(void) user_data;
}

static void
renderer_owner_checked (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	GVariant *response = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	 gboolean has_owner = FALSE;
	renderer_check_pending = FALSE;
	if (response) { g_variant_get (response, "(b)", &has_owner); g_variant_unref (response); }
	else {
		if (error) g_warning ("Could not check whether the shared alert UI is already open: %s", error->message);
		g_clear_error (&error);
		if (renderer_should_run () && !overlay_retry_source) {
			if (overlay_retry_count < 5) {
				guint delay = 1u << MIN (overlay_retry_count, 4u);
				overlay_retry_count++;
				overlay_retry_source = g_timeout_add_seconds (delay, renderer_retry_cb, NULL);
			} else {
				g_warning ("Could not reach the session bus to check the shared renderer; retrying after the next alert change");
				renderer_retry_exhausted = TRUE;
			}
		}
		return;
	}
	g_clear_error (&error);
	if (has_owner) reset_renderer_retries ();
	if (!renderer_should_run () || has_owner || overlay_process) return;
	GError *spawn_error = NULL;
	overlay_process = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &spawn_error, overlay_path, "--watch", NULL);
	if (!overlay_process) {
		g_warning ("Could not start the shared alert renderer: %s", spawn_error ? spawn_error->message : "unknown error");
		g_clear_error (&spawn_error);
		if (overlay_retry_count < 5) {
			guint delay = 1u << MIN (overlay_retry_count, 4u); overlay_retry_count++;
			overlay_retry_source = g_timeout_add_seconds (delay, renderer_retry_cb, NULL);
		} else {
			renderer_retry_exhausted = TRUE;
			g_warning ("Shared alert renderer could not start; further retries wait for a new alert or source change");
		}
		return;
	}
	overlay_started_at = (gint64) time (NULL);
	g_subprocess_wait_async (overlay_process, NULL, overlay_wait_done, NULL);
	(void) user_data;
}

static gboolean
renderer_retry_cb (gpointer user_data)
{
	(void) user_data;
	overlay_retry_source = 0;
	ensure_renderer ();
	return G_SOURCE_REMOVE;
}

static void
ensure_renderer (void)
{
	if (!renderer_should_run () || renderer_retry_exhausted || overlay_process || renderer_check_pending || !bus_connection) return;
	if (overlay_retry_source) { g_source_remove (overlay_retry_source); overlay_retry_source = 0; }
	renderer_check_pending = TRUE;
	g_dbus_connection_call (bus_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
		g_variant_new ("(s)", RENDERER_APP_ID), G_VARIANT_TYPE ("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, renderer_owner_checked, NULL);
}

static void
reset_renderer_retries (void)
{
	overlay_retry_count = 0;
	renderer_retry_exhausted = FALSE;
	if (overlay_retry_source) { g_source_remove (overlay_retry_source); overlay_retry_source = 0; }
}

static gchar *
diagnostics_json (void)
{
	JsonBuilder *builder = json_builder_new ();
	JsonGenerator *generator;
	gchar *result;
	sqlite3_stmt *st = NULL;
	gint64 last_scan = 0;
	if (sqlite3_prepare_v2 (db, "SELECT COALESCE(MAX(last_success_unix),0) FROM calendar_sync", -1, &st, NULL) == SQLITE_OK && sqlite3_step (st) == SQLITE_ROW)
		last_scan = sqlite3_column_int64 (st, 0);
	sqlite3_finalize (st); st = NULL;
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "eds_enabled"); json_builder_add_boolean_value (builder, eds_enabled);
	json_builder_set_member_name (builder, "follow_selection"); json_builder_add_boolean_value (builder, follow_calendar_selection);
	json_builder_set_member_name (builder, "presentation_enabled"); json_builder_add_boolean_value (builder, presentation_enabled);
	json_builder_set_member_name (builder, "registry_state");
	json_builder_add_string_value (builder, !eds_enabled ? "disabled" : (eds_registry ? "ready" : (eds_starting ? "starting" : "unavailable")));
	json_builder_set_member_name (builder, "active_scans"); json_builder_add_int_value (builder, eds_scans ? g_hash_table_size (eds_scans) : 0);
	json_builder_set_member_name (builder, "last_scan_unix"); json_builder_add_int_value (builder, last_scan);
	json_builder_set_member_name (builder, "calendars"); json_builder_begin_array (builder);
	if (sqlite3_prepare_v2 (db, "SELECT calendar_id,state,last_success_unix,error,event_count FROM calendar_sync ORDER BY calendar_id", -1, &st, NULL) == SQLITE_OK) {
		while (sqlite3_step (st) == SQLITE_ROW) {
			const gchar *calendar_id = (const gchar *) sqlite3_column_text (st, 0);
			json_builder_begin_object (builder);
			json_builder_set_member_name (builder, "calendar_id"); json_builder_add_string_value (builder, calendar_id);
			json_builder_set_member_name (builder, "selected"); json_builder_add_boolean_value (builder, eds_enabled && calendar_uid_selected (calendar_id));
			json_builder_set_member_name (builder, "state"); json_builder_add_string_value (builder, (const gchar *) sqlite3_column_text (st, 1));
			json_builder_set_member_name (builder, "last_success_unix"); json_builder_add_int_value (builder, sqlite3_column_int64 (st, 2));
			json_builder_set_member_name (builder, "error"); json_builder_add_string_value (builder, (const gchar *) sqlite3_column_text (st, 3));
			json_builder_set_member_name (builder, "events"); json_builder_add_int_value (builder, sqlite3_column_int (st, 4));
			json_builder_end_object (builder);
		}
	}
	sqlite3_finalize (st);
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	generator = json_generator_new ();
	JsonNode *root = json_builder_get_root (builder);
	json_generator_set_root (generator, root);
	result = json_generator_to_data (generator, NULL);
	json_node_unref (root);
	g_object_unref (generator); g_object_unref (builder);
	return result;
}

typedef struct {
	GDBusMethodInvocation *invocation;
	EReminderWatcher *watcher;
	EReminderData *reminder;
	gchar *id;
	gchar *state;
	gchar *action;
	gchar *payload;
	guint64 revision;
	gint64 snooze_until;
	GCancellable *cancellable;
	guint timeout_source;
	gboolean cancel;
} NativeDismiss;

static gboolean
native_dismiss_timeout (gpointer user_data)
{
	NativeDismiss *pending = user_data;
	pending->timeout_source = 0;
	g_cancellable_cancel (pending->cancellable);
	return G_SOURCE_REMOVE;
}

static void
native_dismiss_free (NativeDismiss *pending)
{
	g_clear_object (&pending->invocation);
	g_clear_object (&pending->watcher);
	if (pending->timeout_source) g_source_remove (pending->timeout_source);
	g_clear_object (&pending->cancellable);
	e_reminder_data_free (pending->reminder);
	g_free (pending->id); g_free (pending->state); g_free (pending->action); g_free (pending->payload); g_free (pending);
}

static void
native_dismiss_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	NativeDismiss *pending = user_data;
	GError *error = NULL;
	gboolean dismissed = e_reminder_watcher_dismiss_finish (E_REMINDER_WATCHER (source), result, &error);
	gboolean updated = FALSE;
	sqlite3_stmt *statement = NULL;
	if (dismissed && pending->cancel) {
		if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='cancelled',revision=revision+1,snooze_until=0 WHERE id=? AND state NOT IN ('dismissed','cancelled','expired','withdrawn')", -1, &statement, NULL) == SQLITE_OK) {
			sqlite3_bind_text (statement, 1, pending->id, -1, SQLITE_TRANSIENT);
			updated = sqlite3_step (statement) == SQLITE_DONE && sqlite3_changes (db) == 1;
		}
	} else if (dismissed) {
		if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state=?,action=?,payload=?,action_revision=revision+1,revision=revision+1,snooze_until=0 WHERE id=? AND revision=? AND state='active'", -1, &statement, NULL) == SQLITE_OK) {
			sqlite3_bind_text (statement, 1, pending->state, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text (statement, 2, pending->action, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text (statement, 3, pending->payload ? pending->payload : "", -1, SQLITE_TRANSIENT);
			sqlite3_bind_text (statement, 4, pending->id, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64 (statement, 5, pending->revision);
			updated = sqlite3_step (statement) == SQLITE_DONE && sqlite3_changes (db) == 1;
		}
	}
	sqlite3_finalize (statement);
	if (updated) {
		if (native_reminders) g_hash_table_remove (native_reminders, pending->id);
		if (g_strcmp0 (foreground_id, pending->id) == 0) { stop_audio (); g_clear_pointer (&foreground_id, g_free); }
		if (pending->cancel) g_dbus_method_invocation_return_value (pending->invocation, g_variant_new ("(b)", TRUE));
		else g_dbus_method_invocation_return_value (pending->invocation, g_variant_new ("(s)", "accepted"));
	} else {
		if (error) g_warning ("Native EDS dismiss failed for %s: %s", pending->id, error->message);
		if (pending->cancel) g_dbus_method_invocation_return_value (pending->invocation, g_variant_new ("(b)", FALSE));
		else g_dbus_method_invocation_return_value (pending->invocation, g_variant_new ("(s)", "failed"));
	}
	g_clear_error (&error);
	native_dismiss_free (pending);
}

static void
begin_native_dismiss (GDBusMethodInvocation *invocation, const gchar *id,
		      const EReminderData *reminder, gboolean cancel,
		      guint64 revision, const gchar *state,
		      const gchar *action, const gchar *payload)
{
	NativeDismiss *pending = g_new0 (NativeDismiss, 1);
	pending->invocation = g_object_ref (invocation);
	pending->watcher = g_object_ref (eds_watcher);
	pending->reminder = e_reminder_data_copy (reminder);
	pending->id = g_strdup (id);
	pending->cancel = cancel;
	pending->revision = revision;
	pending->state = g_strdup (state);
	pending->action = g_strdup (action);
	pending->payload = g_strdup (payload);
	pending->cancellable = g_cancellable_new ();
	pending->timeout_source = g_timeout_add_seconds (6, native_dismiss_timeout, pending);
	e_reminder_watcher_dismiss (pending->watcher, pending->reminder, pending->cancellable, native_dismiss_done, pending);
}

static void
method_call (GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *interface, const gchar *method, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
  const gchar *id;
  sqlite3_stmt *metadata = NULL;
  (void) sender; (void) path; (void) interface; (void) data;
  if (g_str_equal (method, "Submit") || g_str_equal (method, "Update")) {
    const gchar *source_id, *calendar_id, *title, *body, *destination; guint64 expires, revision = 0; GError *error = NULL;
    gchar *previous_owner = NULL;
    g_variant_get (parameters, "(&s&s&s&s&s&st)", &source_id, &calendar_id, &id, &title, &body, &destination, &expires);
	previous_owner = read_owner (id);
	if (previous_owner && !g_str_equal (previous_owner, "producer"))
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "provider-owned alerts cannot be replaced through the producer API");
	else if (((source_id && *source_id) || (calendar_id && *calendar_id)) && !source_selected (source_id, calendar_id))
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "source/calendar is not selected by the active config");
	else
		save_alert (id, title, body, destination, expires, g_str_equal (method, "Update"), &revision, &error);
	if (!error && sqlite3_prepare_v2 (db, "UPDATE alerts SET source_id=?,calendar_id=?,owner='producer' WHERE id=?", -1, &metadata, NULL) == SQLITE_OK) {
		sqlite3_bind_text (metadata, 1, source_id ? source_id : "", -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (metadata, 2, calendar_id ? calendar_id : "", -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (metadata, 3, id, -1, SQLITE_TRANSIENT);
		sqlite3_step (metadata);
	}
	sqlite3_finalize (metadata); metadata = NULL;
	g_free (previous_owner);
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else {
	  reset_renderer_retries ();
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(t)", revision));
      g_dbus_connection_emit_signal (connection, NULL, OBJECT_PATH, IFACE, "Changed", g_variant_new ("(st)", id, revision), NULL);
    }
    g_clear_error (&error);
  } else if (g_str_equal (method, "Cancel")) {
    sqlite3_stmt *st = NULL; gboolean found;
    g_variant_get (parameters, "(&s)", &id); refresh_deadlines ();
    found = read_one (id, NULL, NULL, NULL, NULL, NULL);
	EReminderData *native = native_reminders ? g_hash_table_lookup (native_reminders, id) : NULL;
	gchar *source_id = found ? read_source_id (id) : NULL;
	gchar *owner = found ? read_owner (id) : NULL;
	if (found && g_strcmp0 (owner, "native") == 0 && !native) found = FALSE;
	if (found && native) {
		begin_native_dismiss (invocation, id, native, TRUE, 0, NULL, NULL, NULL);
		g_free (source_id); g_free (owner);
		return;
	}
	if (found && sqlite3_prepare_v2 (db, "UPDATE alerts SET state='cancelled',revision=revision+1,snooze_until=0 WHERE id=? AND state NOT IN ('dismissed','cancelled','expired','withdrawn')", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT); sqlite3_step (st); found = sqlite3_changes (db) == 1; sqlite3_finalize (st);
    }
    if (found && native_reminders) g_hash_table_remove (native_reminders, id);
	    g_free (source_id); g_free (owner);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", found));
    if (found && g_strcmp0 (foreground_id, id) == 0) {
		stop_audio ();
		g_clear_pointer (&foreground_id, g_free);
	}
  } else if (g_str_equal (method, "Act")) {
    guint64 revision; const gchar *action, *payload, *state = NULL; gchar *old_state = NULL; gint64 current = 0, snooze_until = 0; sqlite3_stmt *st = NULL; const gchar *result = "failed"; EReminderData *native = NULL;
    g_variant_get (parameters, "(&st&s&s)", &id, &revision, &action, &payload); refresh_deadlines ();
    native = native_reminders ? g_hash_table_lookup (native_reminders, id) : NULL;
    gchar *source_id = read_source_id (id);
    gchar *owner = read_owner (id);
    gboolean native_owner = g_strcmp0 (owner, "native") == 0;
    if (read_one (id, NULL, &old_state, &current, NULL, NULL) && current == (gint64) revision && g_str_equal (old_state, "active")) {
      if (g_str_equal (action, "dismiss")) state = "dismissed";
      else if (g_str_equal (action, "snooze")) {
        gchar *end = NULL;
        gint64 minutes = g_ascii_strtoll (payload, &end, 10);
        if (payload && *payload && end && !*end && minutes >= 1 && minutes <= 10080) {
          state = "snoozed";
          snooze_until = (gint64) time (NULL) + minutes * 60;
        }
      } else if (g_str_equal (action, "snooze-until")) {
        gchar *end = NULL;
        errno = 0;
        gint64 timestamp = g_ascii_strtoll (payload, &end, 10);
		if (payload && *payload && end && !*end && errno != ERANGE && timestamp > (gint64) time (NULL) && timestamp <= (gint64) time (NULL) + 315360000) {
          state = "snoozed";
          snooze_until = timestamp;
        }
      }
      else if (g_str_equal (action, "open") && g_str_equal (payload, "launched")) state = "dismissed";
	  if (state && native_owner && !native)
		g_warning ("Native EDS action %s is unavailable until the provider reminder is restored", action);
	  else if (state && native && !g_str_equal (state, "snoozed")) {
		begin_native_dismiss (invocation, id, native, FALSE, revision, state, action, payload);
		sqlite3_finalize (st); g_free (old_state); g_free (source_id); g_free (owner);
		return;
	  } else if (state && native)
		e_reminder_watcher_snooze (eds_watcher, native, snooze_until);
	  if (state && (!native_owner || native) && sqlite3_prepare_v2 (db, "UPDATE alerts SET state=?,action=?,payload=?,action_revision=revision+1,revision=revision+1,snooze_until=? WHERE id=? AND revision=? AND state='active'", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text (st, 1, state, -1, SQLITE_STATIC); sqlite3_bind_text (st, 2, action, -1, SQLITE_TRANSIENT); sqlite3_bind_text (st, 3, payload ? payload : "", -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (st, 4, state && g_str_equal (state, "snoozed") ? snooze_until : 0);
        sqlite3_bind_text (st, 5, id, -1, SQLITE_TRANSIENT); sqlite3_bind_int64 (st, 6, revision);
        if (sqlite3_step (st) == SQLITE_DONE && sqlite3_changes (db) == 1) result = "accepted";
      }
    }
    sqlite3_finalize (st); g_free (old_state);
    g_free (source_id); g_free (owner);
    if (g_str_equal (result, "accepted")) {
		if (g_strcmp0 (foreground_id, id) == 0) {
			stop_audio ();
			g_clear_pointer (&foreground_id, g_free);
		}
		if (native && !g_str_equal (state, "snoozed")) g_hash_table_remove (native_reminders, id);
	}
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", result));
  } else if (g_str_equal (method, "Status")) {
    gchar *json = NULL; gchar *state = NULL; gint64 expires = 0, snooze = 0; refresh_deadlines ();
    g_variant_get (parameters, "(&s)", &id);
    if (!read_one (id, &json, &state, NULL, &expires, &snooze)) json = g_strdup ("null");
    g_free (state); g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", json)); g_free (json);
  } else if (g_str_equal (method, "Get")) {
    sqlite3_stmt *st = NULL; refresh_deadlines ();
    g_variant_get (parameters, "(&s)", &id);
    if (sqlite3_prepare_v2 (db, "SELECT title,body,destination,revision,state,event_start FROM alerts WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
      if (sqlite3_step (st) == SQLITE_ROW) {
        const gchar *state = (const gchar *) sqlite3_column_text (st, 4);
        if (g_str_equal (state, "active"))
		  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(sssstt)", sqlite3_column_text (st, 0), sqlite3_column_text (st, 1), sqlite3_column_text (st, 2), state, sqlite3_column_int64 (st, 3), sqlite3_column_int64 (st, 5)));
        else
          g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "alert is not active");
      } else g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "alert not found");
    }
    sqlite3_finalize (st);
  } else if (g_str_equal (method, "List")) {
    sqlite3_stmt *st = NULL; GString *list = g_string_new ("["); gboolean first = TRUE; refresh_deadlines ();
    if (sqlite3_prepare_v2 (db, "SELECT id,title,body,destination,state,action,payload,revision,expires_at,snooze_until,action_revision,source_id,calendar_id,event_start,event_end,occurrence,owner FROM alerts ORDER BY id", -1, &st, NULL) == SQLITE_OK) while (sqlite3_step (st) == SQLITE_ROW) {
	  const gchar *source = (const gchar *) sqlite3_column_text (st, 11);
	  const gchar *calendar = (const gchar *) sqlite3_column_text (st, 12);
	  const gchar *owner = (const gchar *) sqlite3_column_text (st, 16);
	  gint64 event_end = sqlite3_column_int64 (st, 14);
	  if (g_strcmp0 (source, "eds-local") == 0 && (g_strcmp0 (owner, "native") == 0 || g_strcmp0 (owner, "fallback") == 0) &&
	      (!calendar_uid_selected (calendar) || (event_end > 0 && event_end <= (gint64) time (NULL)))) continue;
      gchar *item = row_json (st); if (!first) g_string_append_c (list, ','); g_string_append (list, item); g_free (item); first = FALSE;
    }
    sqlite3_finalize (st); g_string_append_c (list, ']'); g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", list->str)); g_string_free (list, TRUE);
  } else if (g_str_equal (method, "ReloadPolicy")) {
	const gchar *path_in, *helper_in; GError *error = NULL; gboolean ok;
	g_variant_get (parameters, "(&s&s)", &path_in, &helper_in);
	ok = load_policy (path_in, helper_in, &error);
	if (!ok && error) g_warning ("Config reload failed; preserving last valid source selection: %s", error->message);
	if (ok) {
		/* A reload must stop old playback/repeat settings without undoing mute. */
		stop_audio ();
		sqlite3_stmt *rows = NULL, *update = NULL;
		GPtrArray *withdraw = g_ptr_array_new_with_free_func (g_free);
		if (sqlite3_prepare_v2 (db, "SELECT id,source_id,calendar_id FROM alerts WHERE state IN ('active','snoozed','scheduled') AND source_id!='' AND calendar_id!=''", -1, &rows, NULL) == SQLITE_OK) {
			while (sqlite3_step (rows) == SQLITE_ROW) {
				const gchar *source = (const gchar *) sqlite3_column_text (rows, 1);
				const gchar *calendar = (const gchar *) sqlite3_column_text (rows, 2);
				if (g_str_equal (source, "eds-local") ? !calendar_uid_may_retain_snapshot (calendar) : !source_selected (source, calendar))
					g_ptr_array_add (withdraw, g_strdup ((const gchar *) sqlite3_column_text (rows, 0)));
			}
		}
		sqlite3_finalize (rows);
		if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE id=?", -1, &update, NULL) == SQLITE_OK) {
			for (guint i = 0; i < withdraw->len; i++) { sqlite3_reset (update); sqlite3_bind_text (update, 1, g_ptr_array_index (withdraw, i), -1, SQLITE_TRANSIENT); sqlite3_step (update); }
		}
		sqlite3_finalize (update); g_ptr_array_unref (withdraw);
		update = NULL;
		clear_unselected_occurrence_snapshots ();
		apply_cached_fallback_policy ();
		if (native_reminders) {
			GHashTableIter iter; gpointer key, value;
			g_hash_table_iter_init (&iter, native_reminders);
			while (g_hash_table_iter_next (&iter, &key, &value)) {
				const gchar *uid = e_reminder_data_get_source_uid (value);
				if (!calendar_uid_selected (uid)) g_hash_table_iter_remove (&iter);
			}
		}
		if (foreground_id) { gchar *state = NULL; if (!read_one (foreground_id, NULL, &state, NULL, NULL, NULL) || !g_str_equal (state, "active")) { stop_audio (); g_clear_pointer (&foreground_id, g_free); } g_free (state); }
		if (eds_enabled) { if (!eds_registry) start_eds (); else scan_selected_calendars (); }
		else {
			stop_eds ();
			if (sqlite3_prepare_v2 (db, "UPDATE alerts SET state='withdrawn',revision=revision+1 WHERE source_id='eds-local' AND state IN ('active','scheduled','snoozed')", -1, &update, NULL) == SQLITE_OK)
				sqlite3_step (update);
			sqlite3_finalize (update);
			if (native_reminders) g_hash_table_remove_all (native_reminders);
			sqlite3_exec (db, "DELETE FROM eds_occurrences", NULL, NULL, NULL);
			stop_audio ();
			g_clear_pointer (&foreground_id, g_free);
		}
	}
	g_clear_error (&error);
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", ok));
	} else if (g_str_equal (method, "StopAudio")) {
		stop_audio ();
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_str_equal (method, "SetForeground")) {
		const gchar *next_id;
		g_variant_get (parameters, "(&s)", &next_id);
		if (!presentation_enabled || !next_id || !*next_id) {
			g_clear_pointer (&foreground_id, g_free);
			stop_audio ();
		} else {
			gchar *state = NULL;
			if (read_one (next_id, NULL, &state, NULL, NULL, NULL) && g_str_equal (state, "active") && alert_presentable (next_id) && g_strcmp0 (foreground_id, next_id) != 0) {
				reset_renderer_retries ();
				g_free (foreground_id);
				foreground_id = g_strdup (next_id);
				reset_audio_for_new_alert ();
				start_audio ();
			}
			if (state && g_str_equal (state, "active") && alert_presentable (next_id)) {
				if (!presented_alerts) presented_alerts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
				g_hash_table_add (presented_alerts, g_strdup (next_id));
				ensure_renderer ();
			}
			else if (state && g_str_equal (state, "active") && g_strcmp0 (foreground_id, next_id) == 0) {
				stop_audio ();
				g_clear_pointer (&foreground_id, g_free);
			}
			g_free (state);
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_str_equal (method, "GetPresentation")) {
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", presentation_enabled));
	} else if (g_str_equal (method, "SetPresentation")) {
		gboolean enabled, accepted = FALSE;
		sqlite3_stmt *statement = NULL;
		g_variant_get (parameters, "(b)", &enabled);
		if (sqlite3_prepare_v2 (db, "INSERT INTO app_settings(key,value) VALUES('presentation_enabled',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &statement, NULL) == SQLITE_OK) {
			sqlite3_bind_text (statement, 1, enabled ? "1" : "0", -1, SQLITE_STATIC);
			accepted = sqlite3_step (statement) == SQLITE_DONE;
		}
		sqlite3_finalize (statement);
		if (accepted && presentation_enabled != enabled) {
			presentation_enabled = enabled;
			refresh_deadlines ();
			if (!enabled) {
				stop_audio ();
				g_clear_pointer (&foreground_id, g_free);
			} else {
				reset_renderer_retries ();
				ensure_renderer ();
			}
			g_dbus_connection_emit_signal (connection, NULL, OBJECT_PATH, IFACE, "PresentationChanged", g_variant_new ("(b)", enabled), NULL);
		}
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", accepted));
	} else if (g_str_equal (method, "GetPolicy")) {
		gchar *effective = effective_policy_json ();
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", effective));
		g_free (effective);
	} else if (g_str_equal (method, "Diagnostics")) {
		gchar *json = diagnostics_json ();
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", json));
		g_free (json);
  }
}

static const GDBusInterfaceVTable vtable = { method_call, NULL, NULL, { 0 } };

int main (int argc, char **argv)
{
  GError *error = NULL; gchar *state_path = NULL; gchar *dir; const gchar *override;
  GDBusNodeInfo *node; GDBusConnection *connection; guint registration;
  config_path = g_build_filename (g_get_user_config_dir (), "calendar-reminder-overlay", "config.toml", NULL);
  policy_mode = g_strdup ("include");
  selected_calendars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  for (int i = 1; i < argc; i++) {
    if (g_str_equal (argv[i], "--headless")) continue;
    if (g_str_equal (argv[i], "--state") && i + 1 < argc) state_path = g_strdup (argv[++i]);
    else if (g_str_equal (argv[i], "--config") && i + 1 < argc) { g_free (config_path); config_path = g_strdup (argv[++i]); }
    else if (g_str_equal (argv[i], "--helper") && i + 1 < argc) helper_path = g_strdup (argv[++i]);
    else if (g_str_equal (argv[i], "--overlay") && i + 1 < argc) overlay_path = g_strdup (argv[++i]);
    else { g_printerr ("usage: reminder-core --headless [--state PATH] [--config PATH --helper reminderctl.py]\n"); return 2; }
  }
  if (!helper_path) { g_printerr ("--helper reminderctl.py is required for validated source policy\n"); return 2; }
  if (g_getenv ("REMINDER_PLAYBACK_PROGRAM")) playback_program = g_getenv ("REMINDER_PLAYBACK_PROGRAM");
  if (!load_policy (config_path, helper_path, &error)) { g_printerr ("invalid config; refusing to start core: %s\n", error ? error->message : "unknown error"); return 1; }
  if (!state_path) {
    override = g_getenv ("REMINDER_STATE_HOME");
    dir = override ? g_strdup (override) : g_build_filename (g_get_user_state_dir (), "calendar-reminder-overlay", NULL);
    g_mkdir_with_parents (dir, 0700); state_path = g_build_filename (dir, "alerts.sqlite3", NULL); g_free (dir);
  } else {
    dir = g_path_get_dirname (state_path); g_mkdir_with_parents (dir, 0700); g_free (dir);
  }
  if (sqlite3_open (state_path, &db) != SQLITE_OK) { g_printerr ("open state db: %s\n", sqlite3_errmsg (db)); return 1; }
  sqlite3_busy_timeout (db, 5000); sqlite3_exec (db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;", NULL, NULL, NULL);
  if (!exec_sql ("CREATE TABLE IF NOT EXISTS app_settings(key TEXT PRIMARY KEY,value TEXT NOT NULL); INSERT OR IGNORE INTO app_settings(key,value) VALUES('presentation_enabled','1');", &error)) { g_printerr ("settings schema: %s\n", error->message); return 1; }
  sqlite3_stmt *presentation_statement = NULL;
  if (sqlite3_prepare_v2 (db, "SELECT value FROM app_settings WHERE key='presentation_enabled'", -1, &presentation_statement, NULL) == SQLITE_OK && sqlite3_step (presentation_statement) == SQLITE_ROW)
    presentation_enabled = g_strcmp0 ((const gchar *) sqlite3_column_text (presentation_statement, 0), "0") != 0;
  sqlite3_finalize (presentation_statement);
  if (!exec_sql ("CREATE TABLE IF NOT EXISTS alerts(id TEXT PRIMARY KEY,title TEXT NOT NULL,body TEXT NOT NULL,destination TEXT NOT NULL,state TEXT NOT NULL,action TEXT NOT NULL,payload TEXT NOT NULL,revision INTEGER NOT NULL,expires_at INTEGER NOT NULL,snooze_until INTEGER NOT NULL,action_revision INTEGER NOT NULL DEFAULT 0,source_id TEXT NOT NULL DEFAULT '',calendar_id TEXT NOT NULL DEFAULT '',event_start INTEGER NOT NULL DEFAULT 0,event_end INTEGER NOT NULL DEFAULT 0,occurrence TEXT NOT NULL DEFAULT '',owner TEXT NOT NULL DEFAULT 'producer',trigger_at INTEGER NOT NULL DEFAULT 0);", &error)) { g_printerr ("state schema: %s\n", error->message); return 1; }
	if (!exec_sql ("CREATE TABLE IF NOT EXISTS calendar_sync(calendar_id TEXT PRIMARY KEY,state TEXT NOT NULL,last_success_unix INTEGER NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '',event_count INTEGER NOT NULL DEFAULT 0); CREATE TABLE IF NOT EXISTS eds_occurrences(alert_id TEXT PRIMARY KEY,calendar_id TEXT NOT NULL,event_uid TEXT NOT NULL,recurrence_id TEXT NOT NULL,event_start INTEGER NOT NULL,event_end INTEGER NOT NULL,native_alarm INTEGER NOT NULL);", &error)) { g_printerr ("calendar diagnostics schema: %s\n", error->message); return 1; }
  sqlite3_stmt *columns = NULL;
  gboolean has_action_revision = FALSE, has_source_id = FALSE, has_calendar_id = FALSE, has_event_start = FALSE, has_event_end = FALSE, has_occurrence = FALSE, has_owner = FALSE, has_trigger_at = FALSE;
  if (sqlite3_prepare_v2 (db, "PRAGMA table_info(alerts)", -1, &columns, NULL) != SQLITE_OK) return 1;
  while (sqlite3_step (columns) == SQLITE_ROW) {
    const gchar *name = (const gchar *) sqlite3_column_text (columns, 1);
    if (g_strcmp0 (name, "action_revision") == 0) has_action_revision = TRUE;
    else if (g_strcmp0 (name, "source_id") == 0) has_source_id = TRUE;
    else if (g_strcmp0 (name, "calendar_id") == 0) has_calendar_id = TRUE;
    else if (g_strcmp0 (name, "event_start") == 0) has_event_start = TRUE;
    else if (g_strcmp0 (name, "event_end") == 0) has_event_end = TRUE;
    else if (g_strcmp0 (name, "occurrence") == 0) has_occurrence = TRUE;
    else if (g_strcmp0 (name, "owner") == 0) has_owner = TRUE;
    else if (g_strcmp0 (name, "trigger_at") == 0) has_trigger_at = TRUE;
  }
  sqlite3_finalize (columns);
  if (!has_action_revision && !exec_sql ("ALTER TABLE alerts ADD COLUMN action_revision INTEGER NOT NULL DEFAULT 0", &error)) {
    g_printerr ("state migration: %s\n", error->message); return 1;
  }
  if (!has_source_id && !exec_sql ("ALTER TABLE alerts ADD COLUMN source_id TEXT NOT NULL DEFAULT ''", &error)) return 1;
  if (!has_calendar_id && !exec_sql ("ALTER TABLE alerts ADD COLUMN calendar_id TEXT NOT NULL DEFAULT ''", &error)) return 1;
  if (!has_event_start && !exec_sql ("ALTER TABLE alerts ADD COLUMN event_start INTEGER NOT NULL DEFAULT 0", &error)) return 1;
  if (!has_event_end && !exec_sql ("ALTER TABLE alerts ADD COLUMN event_end INTEGER NOT NULL DEFAULT 0", &error)) return 1;
  if (!has_occurrence && !exec_sql ("ALTER TABLE alerts ADD COLUMN occurrence TEXT NOT NULL DEFAULT ''", &error)) return 1;
  if (!has_owner && !exec_sql ("ALTER TABLE alerts ADD COLUMN owner TEXT NOT NULL DEFAULT 'producer'; UPDATE alerts SET owner='native' WHERE source_id='eds-local';", &error)) return 1;
  if (!has_trigger_at && !exec_sql ("ALTER TABLE alerts ADD COLUMN trigger_at INTEGER NOT NULL DEFAULT 0", &error)) return 1;
  node = g_dbus_node_info_new_for_xml (introspection_xml, &error);
  if (!node) { g_printerr ("D-Bus schema: %s\n", error->message); return 1; }
  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!connection) { g_printerr ("session bus: %s\n", error->message); return 1; }
  registration = g_dbus_connection_register_object (connection, OBJECT_PATH, node->interfaces[0], &vtable, NULL, NULL, &error);
  if (!registration) { g_printerr ("register API: %s\n", error->message); return 1; }
  GVariant *name_result = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new ("(su)", BUS_NAME, 4u), G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
  if (!name_result) { g_printerr ("own API name: %s\n", error->message); return 1; }
  guint name_status;
  g_variant_get (name_result, "(u)", &name_status);
  g_variant_unref (name_result);
  if (name_status != 1) { g_printerr ("another reminder core already owns the session API\n"); return 1; }
  bus_connection = connection;
  native_reminders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_reminder_data_free);
  if (eds_enabled) start_eds ();
  g_timeout_add_seconds (1, deadline_poll, NULL);
  g_print ("headless local alert core ready; state=%s\n", state_path);
  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGTERM, terminate_core, NULL);
  g_unix_signal_add (SIGINT, terminate_core, NULL);
  g_main_loop_run (loop);
  g_clear_pointer (&presented_alerts, g_hash_table_unref); stop_audio (); stop_eds (); if (overlay_process) g_subprocess_force_exit (overlay_process); g_clear_object (&overlay_process); g_clear_pointer (&eds_scans, g_hash_table_unref); g_clear_pointer (&native_reminders, g_hash_table_unref); g_dbus_connection_unregister_object (connection, registration); bus_connection = NULL; g_object_unref (connection); g_dbus_node_info_unref (node); sqlite3_close (db); g_free (state_path); g_free (config_path); g_free (helper_path); g_free (overlay_path); g_free (policy_mode); g_free (audio_file); g_hash_table_unref (selected_calendars); return 0;
}
