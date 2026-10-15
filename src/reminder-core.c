#include <gio/gio.h>
#include <sqlite3.h>
#include <glib/gstdio.h>
#include <time.h>
#include <stdlib.h>
#include <json-glib/json-glib.h>

#define BUS_NAME "org.muness.CalendarReminderOverlay.Core"
#define OBJECT_PATH "/org/muness/CalendarReminderOverlay/Core"
#define IFACE "org.muness.CalendarReminderOverlay.Core1"

static sqlite3 *db;
static GMainLoop *loop;
static gchar *config_path;
static gchar *helper_path;
static gchar *overlay_path;
static gchar *policy_mode;
static GHashTable *selected_calendars;
static const gchar introspection_xml[] =
  "<node><interface name='" IFACE "'>"
  "<method name='Submit'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='t' direction='out'/></method>"
  "<method name='Update'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='t' direction='out'/></method>"
  "<method name='Cancel'><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
  "<method name='Act'><arg type='s' direction='in'/><arg type='t' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
  "<method name='Status'><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
  "<method name='Get'><arg type='s' direction='in'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='t' direction='out'/></method>"
  "<method name='List'><arg type='s' direction='out'/></method>"
  "<method name='ReloadPolicy'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
  "<signal name='Changed'><arg type='s'/><arg type='t'/></signal>"
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
	next_mode = g_strdup (json_object_get_string_member (object, "mode"));
	ids = json_object_get_array_member (object, "calendars");
	if (!next_mode || !ids || !(g_str_equal (next_mode, "include") || g_str_equal (next_mode, "exclude") || g_str_equal (next_mode, "all"))) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "config helper returned invalid source selection"); goto done;
	}
	next_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (guint i = 0; i < json_array_get_length (ids); i++)
		g_hash_table_add (next_ids, g_strdup (json_array_get_string_element (ids, i)));
	g_free (policy_mode); policy_mode = next_mode; next_mode = NULL;
	g_clear_pointer (&selected_calendars, g_hash_table_unref); selected_calendars = next_ids;
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
  const gchar *sql = "SELECT id,title,body,destination,state,action,payload,revision,expires_at,snooze_until,action_revision FROM alerts WHERE id=?";
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

static void
refresh_deadlines (void)
{
  sqlite3_stmt *st = NULL;
  gint64 now = (gint64) time (NULL);
  GPtrArray *due_ids = g_ptr_array_new_with_free_func (g_free);
  if (sqlite3_prepare_v2 (db, "SELECT id FROM alerts WHERE state='snoozed' AND snooze_until>0 AND snooze_until<=? AND (expires_at=0 OR expires_at>?)", -1, &st, NULL) == SQLITE_OK) {
    sqlite3_bind_int64 (st, 1, now);
    sqlite3_bind_int64 (st, 2, now);
    while (sqlite3_step (st) == SQLITE_ROW) g_ptr_array_add (due_ids, g_strdup ((const gchar *) sqlite3_column_text (st, 0)));
  }
  sqlite3_finalize (st); st = NULL;
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='expired',revision=revision+1 WHERE state IN ('active','snoozed') AND expires_at>0 AND expires_at<=?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_step (st); sqlite3_finalize (st);
  st = NULL;
  sqlite3_prepare_v2 (db, "UPDATE alerts SET state='active',snooze_until=0,revision=revision+1 WHERE state='snoozed' AND snooze_until>0 AND snooze_until<=?", -1, &st, NULL);
  sqlite3_bind_int64 (st, 1, now); sqlite3_step (st); sqlite3_finalize (st);
  for (guint i = 0; i < due_ids->len; i++) {
    const gchar *id = g_ptr_array_index (due_ids, i);
    if (overlay_path && *overlay_path) {
      gchar *argv[] = { overlay_path, "--synthetic", (gchar *) id, NULL };
      GError *error = NULL;
      if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &error)) {
        g_warning ("Could not reopen snoozed alert %s: %s", id, error->message);
        g_clear_error (&error);
      }
    }
  }
  g_ptr_array_unref (due_ids);
}

static gboolean
deadline_poll (gpointer data)
{
	(void) data;
	refresh_deadlines ();
	return G_SOURCE_CONTINUE;
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
	const gchar *sql = "INSERT INTO alerts(id,title,body,destination,state,action,payload,revision,expires_at,snooze_until) VALUES(?,?,?,?,?,'','',1,?,0) ON CONFLICT(id) DO UPDATE SET title=excluded.title,body=excluded.body,destination=excluded.destination,revision=alerts.revision+1,expires_at=excluded.expires_at,state=CASE WHEN alerts.state IN ('dismissed','cancelled','expired','snoozed') THEN alerts.state ELSE 'active' END,snooze_until=CASE WHEN alerts.state='snoozed' THEN alerts.snooze_until ELSE 0 END";
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
  sqlite3_bind_text (st, 5, "active", -1, SQLITE_STATIC);
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

static void
method_call (GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *interface, const gchar *method, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
  const gchar *id;
  (void) sender; (void) path; (void) interface; (void) data;
  if (g_str_equal (method, "Submit") || g_str_equal (method, "Update")) {
    const gchar *source_id, *calendar_id, *title, *body, *destination; guint64 expires, revision = 0; GError *error = NULL;
    g_variant_get (parameters, "(&s&s&s&s&s&st)", &source_id, &calendar_id, &id, &title, &body, &destination, &expires);
	if (((source_id && *source_id) || (calendar_id && *calendar_id)) && !source_selected (source_id, calendar_id))
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "source/calendar is not selected by the active config");
	else
		save_alert (id, title, body, destination, expires, g_str_equal (method, "Update"), &revision, &error);
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(t)", revision));
      g_dbus_connection_emit_signal (connection, NULL, OBJECT_PATH, IFACE, "Changed", g_variant_new ("(st)", id, revision), NULL);
    }
    g_clear_error (&error);
  } else if (g_str_equal (method, "Cancel")) {
    sqlite3_stmt *st = NULL; gboolean found;
    g_variant_get (parameters, "(&s)", &id); refresh_deadlines ();
    found = read_one (id, NULL, NULL, NULL, NULL, NULL);
	if (found && sqlite3_prepare_v2 (db, "UPDATE alerts SET state='cancelled',revision=revision+1,snooze_until=0 WHERE id=? AND state NOT IN ('dismissed','cancelled','expired')", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT); sqlite3_step (st); found = sqlite3_changes (db) == 1; sqlite3_finalize (st);
    }
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", found));
  } else if (g_str_equal (method, "Act")) {
    guint64 revision; const gchar *action, *payload, *state = NULL; gchar *old_state = NULL; gint64 current = 0; sqlite3_stmt *st = NULL; const gchar *result = "failed";
    g_variant_get (parameters, "(&st&s&s)", &id, &revision, &action, &payload); refresh_deadlines ();
    if (read_one (id, NULL, &old_state, &current, NULL, NULL) && current == (gint64) revision && g_str_equal (old_state, "active")) {
      if (g_str_equal (action, "dismiss")) state = "dismissed";
      else if (g_str_equal (action, "snooze")) {
        gchar *end = NULL;
        gint64 minutes = g_ascii_strtoll (payload, &end, 10);
        if (payload && *payload && end && !*end && minutes >= 1 && minutes <= 10080)
          state = "snoozed";
      }
      else if (g_str_equal (action, "open") && g_str_equal (payload, "launched")) state = "dismissed";
      if (state && sqlite3_prepare_v2 (db, "UPDATE alerts SET state=?,action=?,payload=?,action_revision=revision+1,revision=revision+1,snooze_until=? WHERE id=? AND revision=? AND state='active'", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text (st, 1, state, -1, SQLITE_STATIC); sqlite3_bind_text (st, 2, action, -1, SQLITE_TRANSIENT); sqlite3_bind_text (st, 3, payload ? payload : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64 (st, 4, g_str_equal (action, "snooze") ? (gint64) time (NULL) + CLAMP (atoi (payload), 1, 10080) * 60 : 0);
        sqlite3_bind_text (st, 5, id, -1, SQLITE_TRANSIENT); sqlite3_bind_int64 (st, 6, revision);
        if (sqlite3_step (st) == SQLITE_DONE && sqlite3_changes (db) == 1) result = "accepted";
      }
    }
    sqlite3_finalize (st); g_free (old_state);
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", result));
  } else if (g_str_equal (method, "Status")) {
    gchar *json = NULL; gchar *state = NULL; gint64 expires = 0, snooze = 0; refresh_deadlines ();
    g_variant_get (parameters, "(&s)", &id);
    if (!read_one (id, &json, &state, NULL, &expires, &snooze)) json = g_strdup ("null");
    g_free (state); g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", json)); g_free (json);
  } else if (g_str_equal (method, "Get")) {
    sqlite3_stmt *st = NULL; refresh_deadlines ();
    g_variant_get (parameters, "(&s)", &id);
    if (sqlite3_prepare_v2 (db, "SELECT title,body,destination,revision,state FROM alerts WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_text (st, 1, id, -1, SQLITE_TRANSIENT);
      if (sqlite3_step (st) == SQLITE_ROW) {
        const gchar *state = (const gchar *) sqlite3_column_text (st, 4);
        if (g_str_equal (state, "active"))
          g_dbus_method_invocation_return_value (invocation, g_variant_new ("(sssst)", sqlite3_column_text (st, 0), sqlite3_column_text (st, 1), sqlite3_column_text (st, 2), state, sqlite3_column_int64 (st, 3)));
        else
          g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "alert is not active");
      } else g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "alert not found");
    }
    sqlite3_finalize (st);
  } else if (g_str_equal (method, "List")) {
    sqlite3_stmt *st = NULL; GString *list = g_string_new ("["); gboolean first = TRUE; refresh_deadlines ();
    if (sqlite3_prepare_v2 (db, "SELECT id,title,body,destination,state,action,payload,revision,expires_at,snooze_until,action_revision FROM alerts ORDER BY id", -1, &st, NULL) == SQLITE_OK) while (sqlite3_step (st) == SQLITE_ROW) {
      gchar *item = row_json (st); if (!first) g_string_append_c (list, ','); g_string_append (list, item); g_free (item); first = FALSE;
    }
    sqlite3_finalize (st); g_string_append_c (list, ']'); g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", list->str)); g_string_free (list, TRUE);
  } else if (g_str_equal (method, "ReloadPolicy")) {
	const gchar *path_in, *helper_in; GError *error = NULL; gboolean ok;
	g_variant_get (parameters, "(&s&s)", &path_in, &helper_in);
	ok = load_policy (path_in, helper_in, &error);
	if (!ok && error) g_warning ("Config reload failed; preserving last valid source selection: %s", error->message);
	g_clear_error (&error);
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", ok));
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
  if (!exec_sql ("CREATE TABLE IF NOT EXISTS alerts(id TEXT PRIMARY KEY,title TEXT NOT NULL,body TEXT NOT NULL,destination TEXT NOT NULL,state TEXT NOT NULL,action TEXT NOT NULL,payload TEXT NOT NULL,revision INTEGER NOT NULL,expires_at INTEGER NOT NULL,snooze_until INTEGER NOT NULL,action_revision INTEGER NOT NULL DEFAULT 0);", &error)) { g_printerr ("state schema: %s\n", error->message); return 1; }
  sqlite3_stmt *columns = NULL;
  gboolean has_action_revision = FALSE;
  if (sqlite3_prepare_v2 (db, "PRAGMA table_info(alerts)", -1, &columns, NULL) != SQLITE_OK) return 1;
  while (sqlite3_step (columns) == SQLITE_ROW)
    if (g_strcmp0 ((const gchar *) sqlite3_column_text (columns, 1), "action_revision") == 0) has_action_revision = TRUE;
  sqlite3_finalize (columns);
  if (!has_action_revision && !exec_sql ("ALTER TABLE alerts ADD COLUMN action_revision INTEGER NOT NULL DEFAULT 0", &error)) {
    g_printerr ("state migration: %s\n", error->message); return 1;
  }
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
  g_timeout_add_seconds (1, deadline_poll, NULL);
  g_print ("headless local alert core ready; state=%s\n", state_path);
  loop = g_main_loop_new (NULL, FALSE); g_main_loop_run (loop);
  g_dbus_connection_unregister_object (connection, registration); g_object_unref (connection); g_dbus_node_info_unref (node); sqlite3_close (db); g_free (state_path); g_free (config_path); g_free (helper_path); g_free (overlay_path); g_free (policy_mode); g_hash_table_unref (selected_calendars); return 0;
}
