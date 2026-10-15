#include <time.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>
#include <libecal/libecal.h>

typedef struct {
	EReminderData *reminder;
	gchar *meeting_url;
	GtkWindow *window;
	GtkLabel *countdown;
	guint countdown_source;
	gchar *dedupe_key;
	gint64 event_start;
	gboolean finished;
	gboolean fallback;
} ReminderUi;

static EReminderWatcher *watcher;
static ESourceRegistry *registry;
static GtkApplication *application;
static GHashTable *active_reminders;
static GHashTable *fallback_clients;
static GHashTable *fallback_scheduled;
static GHashTable *fallback_dismissed;
static guint fallback_poll_source;
static guint fallback_lead_minutes = 5;
static gboolean layer_shell_supported;

typedef struct {
	EReminderData *reminder;
	gint64 event_start;
	gchar *key;
	guint source_id;
} FallbackAlarm;

static void fallback_alarm_free (gpointer data);
static void show_reminder_full (EReminderData *reminder, gboolean fallback);

static gboolean
fallback_timeout_cb (gpointer data)
{
	FallbackAlarm *alarm = data;
	alarm->source_id = 0;
	if (!g_hash_table_contains (active_reminders, alarm->key))
		show_reminder_full (alarm->reminder, TRUE);
	g_hash_table_remove (fallback_scheduled, alarm->key);
	return G_SOURCE_REMOVE;
}

static void
fallback_alarm_free (gpointer data)
{
	FallbackAlarm *alarm = data;
	if (!alarm) return;
	if (alarm->source_id) g_source_remove (alarm->source_id);
	e_reminder_data_free (alarm->reminder);
	g_free (alarm->key);
	g_free (alarm);
}

static void
reminder_ui_free (gpointer data)
{
	ReminderUi *ui = data;
	if (!ui)
		return;
	e_reminder_data_free (ui->reminder);
	g_free (ui->meeting_url);
	g_free (ui->dedupe_key);
	g_free (ui);
}

static void
finish_reminder (ReminderUi *ui, gboolean dismiss)
{
	GError *error = NULL;
	if (ui->finished)
		return;
	ui->finished = TRUE;
	if (ui->countdown_source) {
		g_source_remove (ui->countdown_source);
		ui->countdown_source = 0;
	}
	if (active_reminders && ui->dedupe_key)
		g_hash_table_remove (active_reminders, ui->dedupe_key);
	if (dismiss && !ui->fallback && !e_reminder_watcher_dismiss_sync (watcher, ui->reminder, NULL, &error)) {
		g_warning ("Could not dismiss calendar reminder: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
	}
	if (ui->fallback && dismiss)
		g_hash_table_add (fallback_dismissed, g_strdup (ui->dedupe_key));
	gtk_window_destroy (ui->window);
}

static gboolean
on_close_request (GtkWindow *window, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) window;
	finish_reminder (ui, TRUE);
	return FALSE;
}

static void
on_dismiss_clicked (GtkButton *button, gpointer user_data)
{
	(void) button;
	g_message ("Calendar reminder button: Dismiss");
	finish_reminder (user_data, TRUE);
}

static void
on_snooze_one_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) button;
	g_message ("Calendar reminder button: Snooze 1 minute");
	if (ui->fallback) {
		FallbackAlarm *alarm = g_new0 (FallbackAlarm, 1);
		alarm->reminder = e_reminder_data_copy (ui->reminder);
		alarm->key = g_strdup (ui->dedupe_key);
		alarm->event_start = ui->event_start;
		alarm->source_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, 60, fallback_timeout_cb, alarm, fallback_alarm_free);
		g_hash_table_replace (fallback_scheduled, g_strdup (ui->dedupe_key), alarm);
	} else e_reminder_watcher_snooze (watcher, ui->reminder, time (NULL) + 60);
	finish_reminder (ui, FALSE);
}

static void
on_snooze_five_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) button;
	g_message ("Calendar reminder button: Snooze 5 minutes");
	if (ui->fallback) {
		FallbackAlarm *alarm = g_new0 (FallbackAlarm, 1);
		alarm->reminder = e_reminder_data_copy (ui->reminder);
		alarm->key = g_strdup (ui->dedupe_key);
		alarm->event_start = ui->event_start;
		alarm->source_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, 5 * 60, fallback_timeout_cb, alarm, fallback_alarm_free);
		g_hash_table_replace (fallback_scheduled, g_strdup (ui->dedupe_key), alarm);
	} else e_reminder_watcher_snooze (watcher, ui->reminder, time (NULL) + 5 * 60);
	finish_reminder (ui, FALSE);
}

static void
on_snooze_until_start_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) button;
	if (ui->event_start <= time (NULL))
		return;
	g_message ("Calendar reminder button: Snooze until event starts");
	if (ui->fallback) {
		FallbackAlarm *alarm = g_new0 (FallbackAlarm, 1);
		alarm->reminder = e_reminder_data_copy (ui->reminder);
		alarm->key = g_strdup (ui->dedupe_key);
		alarm->event_start = ui->event_start;
		alarm->source_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, (guint) (ui->event_start - time (NULL)), fallback_timeout_cb, alarm, fallback_alarm_free);
		g_hash_table_replace (fallback_scheduled, g_strdup (ui->dedupe_key), alarm);
	} else e_reminder_watcher_snooze (watcher, ui->reminder, ui->event_start);
	finish_reminder (ui, FALSE);
}

static gboolean
update_countdown (gpointer user_data)
{
	ReminderUi *ui = user_data;
	gint64 remaining = ui->event_start - time (NULL);
	gchar *text;
	if (ui->finished)
		return G_SOURCE_REMOVE;
	if (remaining > 0) {
		text = g_strdup_printf ("Starts in %02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
			remaining / 60, remaining % 60);
	} else {
		text = g_strdup ("Event is starting now");
	}
	gtk_label_set_text (ui->countdown, text);
	g_free (text);
	return G_SOURCE_CONTINUE;
}

static gint64
get_event_start (ECalComponent *component)
{
	ECalComponentDateTime *dt = e_cal_component_get_dtstart (component);
	ICalTime *value;
	ICalTimezone *zone = NULL;
	const gchar *tzid;
	time_t result;
	if (!dt)
		return 0;
	value = e_cal_component_datetime_get_value (dt);
	tzid = e_cal_component_datetime_get_tzid (dt);
	if (tzid && *tzid)
		zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
	if (value)
		result = zone ? i_cal_time_as_timet_with_zone (value, zone) : i_cal_time_as_timet (value);
	else
		result = 0;
	e_cal_component_datetime_free (dt);
	return result;
}

static void
on_open_meeting_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	GError *error = NULL;
	(void) button;
	if (!ui->meeting_url || !g_uri_is_valid (ui->meeting_url, G_URI_FLAGS_NONE, NULL))
		return;
	g_message ("Calendar reminder button: Open meeting");
	if (!g_app_info_launch_default_for_uri (ui->meeting_url, NULL, &error)) {
		g_warning ("Could not open meeting link: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	finish_reminder (ui, TRUE);
}

static gchar *
find_meeting_url (ECalComponent *component)
{
	gchar *url = NULL;
	gchar *calendar_url = e_cal_component_get_url (component);
	gchar *ical = NULL;
	GString *searchable;
	GRegex *regex;
	GMatchInfo *match = NULL;
	const gchar *patterns[] = {
		"(?im)^X-GOOGLE-CONFERENCE:(https?://[^\\r\\n]+)",
		"(?im)^X-MICROSOFT-CDO-ONLINE-MEETINGURL:(https?://[^\\r\\n]+)",
		"(?i)(https?://(?:[a-z0-9-]+\\.)*(?:zoom\\.us|zoomgov\\.com|meet\\.google\\.com|teams\\.microsoft\\.com|teams\\.live\\.com|webex\\.com|whereby\\.com|bluejeans\\.com|gotomeeting\\.com|gotowebinar\\.com|join\\.skype\\.com|skype\\.com|ringcentral\\.com|chime\\.aws|discord\\.gg)[^\\s<>\\\"\\r\\n\\\\]+)",
		"(?im)^URL:(https?://[^\\r\\n]+)"
	};
	guint i;

	ical = e_cal_component_get_as_string (component);
	if (!ical) {
		url = calendar_url;
		goto validate_url;
	}
	searchable = g_string_new (ical);
	{
		GSList *descriptions = e_cal_component_get_descriptions (component);
		GSList *link;
		for (link = descriptions; link; link = link->next) {
			const gchar *description = e_cal_component_text_get_value (link->data);
			if (description && *description) {
				g_string_append_c (searchable, '\n');
				g_string_append (searchable, description);
			}
		}
		g_slist_free_full (descriptions, (GDestroyNotify) e_cal_component_text_free);
	}
	/* Match link href values as well as plain text links in HTML descriptions. */
	for (i = 0; i < G_N_ELEMENTS (patterns); i++) {
		regex = g_regex_new (patterns[i], G_REGEX_OPTIMIZE, 0, NULL);
		if (regex && g_regex_match (regex, searchable->str, 0, &match)) {
			url = g_match_info_fetch (match, 1);
			g_match_info_free (match);
			g_regex_unref (regex);
			break;
		}
		if (match) {
			g_match_info_free (match);
			match = NULL;
		}
		if (regex)
			g_regex_unref (regex);
	}
	g_string_free (searchable, TRUE);
	g_free (ical);
	if (url)
		g_free (calendar_url);
	else
		url = calendar_url;
	if (url)
		goto validate_url;
	return url;

validate_url:
	if (!url)
		return NULL;
	g_strstrip (url);
	while (*url && strchr (".,;:)]}", url[strlen (url) - 1]))
		url[strlen (url) - 1] = '\0';
	if (!g_uri_is_valid (url, G_URI_FLAGS_NONE, NULL)) {
		g_clear_pointer (&url, g_free);
		return NULL;
	}
	{
		gchar *scheme = g_uri_parse_scheme (url);
		gboolean http = scheme && (g_ascii_strcasecmp (scheme, "http") == 0 ||
			g_ascii_strcasecmp (scheme, "https") == 0);
		g_free (scheme);
		if (!http)
			g_clear_pointer (&url, g_free);
	}
	return url;
}

static void
show_reminder_full (EReminderData *reminder, gboolean fallback)
{
	ECalComponent *component = e_reminder_data_get_component (reminder);
	const ECalComponentText *summary_text;
	const gchar *summary;
	gchar *body;
	gchar *dedupe_key;
	gint64 event_start;
	GtkWidget *outer, *content, *title, *details, *status, *primary_buttons, *snooze_buttons, *button;
	ReminderUi *ui;

	if (!component)
		return;
	summary_text = e_cal_component_get_summary (component);
	summary = summary_text ? e_cal_component_text_get_value (summary_text) : NULL;
	event_start = get_event_start (component);
	dedupe_key = g_strdup_printf ("%s@%" G_GINT64_FORMAT,
		e_cal_component_get_uid (component) ? e_cal_component_get_uid (component) : (summary ? summary : "event"),
		event_start);
	if (g_hash_table_contains (active_reminders, dedupe_key)) {
		g_message ("Duplicate calendar reminder skipped: %s", summary ? summary : "Calendar reminder");
		g_free (dedupe_key);
		return;
	}
	g_hash_table_add (active_reminders, g_strdup (dedupe_key));
	body = fallback ? g_strdup ("Your calendar event starts soon.") : e_reminder_watcher_describe_data (watcher, reminder, E_REMINDER_WATCHER_DESCRIBE_FLAG_NONE);
	ui = g_new0 (ReminderUi, 1);
	ui->reminder = e_reminder_data_copy (reminder);
	ui->fallback = fallback;
	ui->meeting_url = find_meeting_url (component);
	ui->event_start = event_start;
	ui->dedupe_key = dedupe_key;
	ui->window = GTK_WINDOW (gtk_application_window_new (application));
	if (layer_shell_supported) {
		/* A compositor overlay surface stays above normal and fullscreen app windows. */
		gtk_layer_init_for_window (ui->window);
		gtk_layer_set_namespace (ui->window, "cosmic-calendar-reminder-overlay");
		gtk_layer_set_layer (ui->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
		gtk_layer_set_exclusive_zone (ui->window, 0);
		gtk_layer_set_keyboard_mode (ui->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	}
	g_object_set_data_full (G_OBJECT (ui->window), "reminder-ui", ui, reminder_ui_free);
	gtk_window_set_title (ui->window, summary && *summary ? summary : "Calendar reminder");
	gtk_window_set_decorated (ui->window, FALSE);
	gtk_window_set_resizable (ui->window, FALSE);
	gtk_window_set_default_size (ui->window, 1240, 700);
	g_signal_connect (ui->window, "close-request", G_CALLBACK (on_close_request), ui);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 28);
	gtk_widget_add_css_class (outer, "reminder-overlay");
	gtk_widget_set_margin_top (outer, 64);
	gtk_widget_set_margin_bottom (outer, 52);
	gtk_widget_set_margin_start (outer, 80);
	gtk_widget_set_margin_end (outer, 80);

	content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 20);
	title = gtk_label_new (summary && *summary ? summary : "Calendar reminder");
	gtk_widget_add_css_class (title, "reminder-title");
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (title), TRUE);
	gtk_box_append (GTK_BOX (content), title);

	details = gtk_label_new (body && *body ? body : "An event reminder is due.");
	gtk_widget_add_css_class (details, "reminder-details");
	gtk_label_set_xalign (GTK_LABEL (details), 0.0f);
	gtk_label_set_yalign (GTK_LABEL (details), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (details), TRUE);
	gtk_box_append (GTK_BOX (content), details);

	status = gtk_label_new (NULL);
	ui->countdown = GTK_LABEL (status);
	gtk_widget_add_css_class (status, "reminder-status");
	gtk_label_set_xalign (GTK_LABEL (status), 0.0f);
	gtk_box_append (GTK_BOX (content), status);
	gtk_widget_set_vexpand (content, TRUE);
	gtk_box_append (GTK_BOX (outer), content);

	primary_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 22);
	gtk_box_set_homogeneous (GTK_BOX (primary_buttons), TRUE);
	button = gtk_button_new_with_label ("Open meeting");
	gtk_widget_add_css_class (button, "reminder-action");
	gtk_widget_set_sensitive (button, ui->meeting_url && g_uri_is_valid (ui->meeting_url, G_URI_FLAGS_NONE, NULL));
	g_signal_connect (button, "clicked", G_CALLBACK (on_open_meeting_clicked), ui);
	gtk_box_append (GTK_BOX (primary_buttons), button);
	button = gtk_button_new_with_label ("Dismiss");
	gtk_widget_add_css_class (button, "reminder-action");
	g_signal_connect (button, "clicked", G_CALLBACK (on_dismiss_clicked), ui);
	gtk_box_append (GTK_BOX (primary_buttons), button);
	gtk_box_append (GTK_BOX (outer), primary_buttons);

	snooze_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 22);
	gtk_box_set_homogeneous (GTK_BOX (snooze_buttons), TRUE);
	button = gtk_button_new_with_label ("Snooze 1 min");
	gtk_widget_add_css_class (button, "reminder-action");
	g_signal_connect (button, "clicked", G_CALLBACK (on_snooze_one_clicked), ui);
	gtk_box_append (GTK_BOX (snooze_buttons), button);
	button = gtk_button_new_with_label ("Snooze 5 min");
	gtk_widget_add_css_class (button, "reminder-action");
	g_signal_connect (button, "clicked", G_CALLBACK (on_snooze_five_clicked), ui);
	gtk_box_append (GTK_BOX (snooze_buttons), button);
	button = gtk_button_new_with_label ("Until start");
	gtk_widget_add_css_class (button, "reminder-action");
	gtk_widget_set_sensitive (button, ui->event_start > time (NULL));
	g_signal_connect (button, "clicked", G_CALLBACK (on_snooze_until_start_clicked), ui);
	gtk_box_append (GTK_BOX (snooze_buttons), button);
	gtk_box_append (GTK_BOX (outer), snooze_buttons);

	gtk_window_set_child (ui->window, outer);
	update_countdown (ui);
	ui->countdown_source = g_timeout_add_seconds (1, update_countdown, ui);
	gtk_window_present (ui->window);
	if (!layer_shell_supported)
		gtk_window_fullscreen (ui->window);
	g_message ("Calendar reminder shown with buttons; meeting URL: %s", ui->meeting_url ? "yes" : "no");
	g_free (body);
}

static void
show_reminder (EReminderData *reminder)
{
	show_reminder_full (reminder, FALSE);
}

static gchar *
fallback_key_for (const gchar *source_uid, ECalComponent *component, gint64 event_start)
{
	const gchar *uid = e_cal_component_get_uid (component);
	(void) source_uid;
	return g_strdup_printf ("%s@%" G_GINT64_FORMAT, uid ? uid : "event", event_start);
}

static void
schedule_fallback (const gchar *source_uid, ECalComponent *component)
{
	GSList *alarm_uids;
	gint64 event_start = get_event_start (component);
	gint64 fire_at;
	gchar *key;
	FallbackAlarm *alarm;
	ECalComponentAlarmInstance *instance;
	EReminderData *reminder;
	if (!event_start || event_start <= time (NULL)) return;
	alarm_uids = e_cal_component_get_alarm_uids (component);
	if (alarm_uids) {
		g_slist_free_full (alarm_uids, g_free);
		return;
	}
	key = fallback_key_for (source_uid, component, event_start);
	if (g_hash_table_contains (fallback_scheduled, key) ||
	    g_hash_table_contains (fallback_dismissed, key) ||
	    g_hash_table_contains (active_reminders, key)) {
		g_free (key);
		return;
	}
	fire_at = event_start - fallback_lead_minutes * 60;
	if (fire_at < time (NULL)) fire_at = time (NULL) + 1;
	instance = e_cal_component_alarm_instance_new ("COSMIC-FALLBACK", (time_t) fire_at,
		(time_t) event_start, (time_t) event_start);
	reminder = e_reminder_data_new (source_uid, component, instance);
	e_cal_component_alarm_instance_free (instance);
	if (!reminder) { g_free (key); return; }
	alarm = g_new0 (FallbackAlarm, 1);
	alarm->reminder = reminder;
	alarm->event_start = event_start;
	alarm->key = key;
	alarm->source_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
		(guint) MAX ((gint64) 1, fire_at - time (NULL)), fallback_timeout_cb, alarm, fallback_alarm_free);
	g_hash_table_insert (fallback_scheduled, g_strdup (key), alarm);
}

static gboolean
scan_calendar_fallbacks (gpointer user_data)
{
	GList *sources, *link;
	gchar *start_iso, *end_iso, *query;
	time_t now = time (NULL);
	(void) user_data;
	g_message ("Scanning enabled calendars for fullscreen fallback reminders");
	start_iso = g_strdup_printf ("%04d%02d%02dT%02d%02d%02dZ",
		1900 + gmtime (&now)->tm_year, 1 + gmtime (&now)->tm_mon, gmtime (&now)->tm_mday,
		gmtime (&now)->tm_hour, gmtime (&now)->tm_min, gmtime (&now)->tm_sec);
	{
		time_t later = now + 30 * 24 * 60 * 60;
		struct tm *tm = gmtime (&later);
		end_iso = g_strdup_printf ("%04d%02d%02dT%02d%02d%02dZ",
			1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	}
	query = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))", start_iso, end_iso);
	g_free (start_iso);
	g_free (end_iso);
	sources = e_source_registry_list_enabled (registry, E_SOURCE_EXTENSION_CALENDAR);
	g_message ("Found %u enabled calendar sources", g_list_length (sources));
	for (link = sources; link; link = link->next) {
		ESource *source = link->data;
		const gchar *source_uid = e_source_get_uid (source);
		ECalClient *client = g_hash_table_lookup (fallback_clients, source_uid);
		GError *error = NULL;
		GSList *components = NULL, *item;
		if (!client) {
			client = E_CAL_CLIENT (e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, NULL, &error));
			if (!client) {
				g_debug ("Fallback reminder could not connect to %s: %s", source_uid,
					error ? error->message : "unknown error");
				g_clear_error (&error);
				continue;
			}
			g_hash_table_insert (fallback_clients, g_strdup (source_uid), client);
		}
		if (!e_cal_client_get_object_list_as_comps_sync (client, query, &components, NULL, &error)) {
			g_debug ("Fallback reminder scan failed for %s: %s", source_uid,
				error ? error->message : "unknown error");
			g_clear_error (&error);
			continue;
		}
		for (item = components; item; item = item->next)
			schedule_fallback (source_uid, E_CAL_COMPONENT (item->data));
		g_slist_free_full (components, g_object_unref);
	}
	g_list_free_full (sources, g_object_unref);
	g_free (query);
	g_message ("Calendar fallback scan complete; %u alarms scheduled", g_hash_table_size (fallback_scheduled));
	if (fallback_poll_source)
		g_source_remove (fallback_poll_source);
	fallback_poll_source = g_timeout_add_seconds (60, scan_calendar_fallbacks, NULL);
	return G_SOURCE_CONTINUE;
}

static void
load_fallback_config (void)
{
	GKeyFile *keyfile = g_key_file_new ();
	gchar *path = g_build_filename (g_get_user_config_dir (), "evolution-calendar-reminder-overlay.conf", NULL);
	GError *error = NULL;
	if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &error)) {
		gint value = g_key_file_get_integer (keyfile, "reminders", "fallback-lead-minutes", NULL);
		if (value >= 1 && value <= 120) fallback_lead_minutes = (guint) value;
	} else if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_warning ("Could not read %s: %s", path, error->message);
	}
	g_clear_error (&error);
	g_key_file_unref (keyfile);
	g_free (path);
}

static void
on_triggered (EReminderWatcher *source, const GSList *reminders, gboolean snoozed, gpointer user_data)
{
	const GSList *link;
	(void) source;
	(void) snoozed;
	(void) user_data;
	for (link = reminders; link; link = link->next)
		show_reminder (link->data);
}

static void
on_activate (GtkApplication *app, gpointer user_data)
{
	GError *error = NULL;
	(void) user_data;
	g_application_hold (G_APPLICATION (app));
	layer_shell_supported = gtk_layer_is_supported ();
	registry = e_source_registry_new_sync (NULL, &error);
	if (!registry) {
		g_printerr ("source registry: %s\n", error ? error->message : "unknown error");
		g_clear_error (&error);
		g_application_quit (G_APPLICATION (app));
		return;
	}
	watcher = e_reminder_watcher_new (registry);
	active_reminders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	fallback_clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	fallback_scheduled = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, fallback_alarm_free);
	fallback_dismissed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	load_fallback_config ();
	g_signal_connect (watcher, "triggered", G_CALLBACK (on_triggered), NULL);
	fallback_poll_source = g_timeout_add_seconds (5, scan_calendar_fallbacks, NULL);
	g_message ("Evolution calendar reminder overlay ready (%s; fallback alert: %u minutes before events without alarms)",
		layer_shell_supported ? "COSMIC overlay layer" : "fullscreen fallback", fallback_lead_minutes);
}

int
main (int argc, char **argv)
{
	GtkCssProvider *css;
	gint status;
	gtk_init ();
	application = gtk_application_new ("com.muness.EvolutionReminderOverlay", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect (application, "activate", G_CALLBACK (on_activate), NULL);
	css = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css,
		".reminder-overlay { background: #151821; color: #f4f5f7; }"
		".reminder-title { font-size: 38pt; font-weight: 700; }"
		".reminder-details { font-size: 23pt; }"
		".reminder-status { font-size: 20pt; color: #a9c7ff; min-height: 42px; }"
		".reminder-action { font-size: 23pt; font-weight: 650; padding: 22px 30px; min-width: 230px; min-height: 78px; }",
		-1);
	gtk_style_context_add_provider_for_display (gdk_display_get_default (),
		GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (css);
	status = g_application_run (G_APPLICATION (application), argc, argv);
	g_clear_object (&watcher);
	g_clear_object (&registry);
	if (fallback_poll_source) g_source_remove (fallback_poll_source);
	g_clear_pointer (&fallback_clients, g_hash_table_unref);
	g_clear_pointer (&fallback_scheduled, g_hash_table_unref);
	g_clear_pointer (&fallback_dismissed, g_hash_table_unref);
	g_clear_object (&application);
	return status;
}
