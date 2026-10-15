#include <time.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>
#include <json-glib/json-glib.h>

typedef struct {
	gchar *meeting_url;
	GtkWindow *window;
	GtkLabel *countdown;
	GtkLabel *status;
	GtkWidget *until_button;
	guint countdown_source;
	gchar *dedupe_key;
	gchar *source_id;
	gchar *calendar_id;
	gint64 event_start;
	gboolean finished;
	gboolean pending_action;
	gboolean keyboard_actions_armed;
	guint64 generation;
	gboolean local;
	guint64 local_revision;
} ReminderUi;

static GtkApplication *application;
static GHashTable *active_reminders;
static gboolean layer_shell_supported;
static gchar *synthetic_alert_id;
static gboolean watch_mode;
static guint watch_poll_source;
static ReminderUi *watch_current_ui;
static GDBusConnection *core_connection;
static gboolean watch_refreshing;
static gboolean watch_action_pending;
static gboolean policy_changed;
static GPtrArray *watch_views;
static GtkCssProvider *application_css;
static gchar *outputs_mode;
static GArray *active_snooze_minutes;
static JsonNode *current_policy;
static gchar *display_backend;
static guint64 ui_generation;
static GListModel *monitor_model;
static GdkMonitor *preferred_monitor (GdkDisplay *display);
static gboolean refresh_watch (gpointer user_data);
static void finish_reminder (ReminderUi *ui, gboolean dismiss);

static void
reminder_ui_free (gpointer data)
{
	ReminderUi *ui = data;
	if (!ui)
		return;
	g_free (ui->meeting_url);
	g_free (ui->dedupe_key);
	g_free (ui->source_id);
	g_free (ui->calendar_id);
	g_free (ui);
}

static void
local_action_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GtkWindow *window = user_data;
	ReminderUi *ui = g_object_get_data (G_OBJECT (window), "reminder-ui");
	GError *error = NULL;
	GVariant *response = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	if (!ui || ui->finished || ui->generation != ui_generation) {
		if (response) g_variant_unref (response);
		g_clear_error (&error);
		g_object_unref (window);
		return;
	}
	ui->pending_action = FALSE;
	watch_action_pending = FALSE;
	for (guint i = 0; watch_views && i < watch_views->len; i++) {
		GtkWindow *copy_window = g_ptr_array_index (watch_views, i);
		ReminderUi *copy = g_object_get_data (G_OBJECT (copy_window), "reminder-ui");
		if (copy) copy->pending_action = FALSE;
	}
	if (response) {
		const gchar *answer;
		g_variant_get (response, "(&s)", &answer);
		if (g_str_equal (answer, "accepted")) {
			g_variant_unref (response);
			finish_reminder (ui, FALSE);
			g_object_unref (window);
			return;
		}
		g_variant_unref (response);
	}
	gtk_label_set_text (ui->status, "The reminder service did not accept that action. Try again or choose another button.");
	for (guint i = 0; watch_views && i < watch_views->len; i++) {
		GtkWindow *copy_window = g_ptr_array_index (watch_views, i);
		ReminderUi *copy = g_object_get_data (G_OBJECT (copy_window), "reminder-ui");
		if (copy) gtk_label_set_text (copy->status, "The reminder service did not accept that action. Try again or choose another button.");
	}
	g_clear_error (&error);
	g_object_unref (window);
}

static void
local_action (ReminderUi *ui, const gchar *action, const gchar *payload)
{
	if (watch_action_pending || ui->pending_action || ui->finished || !core_connection) return;
	ui->pending_action = TRUE;
	watch_action_pending = TRUE;
	gtk_label_set_text (ui->status, "Working…");
	for (guint i = 0; watch_views && i < watch_views->len; i++) {
		GtkWindow *window = g_ptr_array_index (watch_views, i);
		ReminderUi *copy = g_object_get_data (G_OBJECT (window), "reminder-ui");
		if (copy) { copy->pending_action = TRUE; gtk_label_set_text (copy->status, "Working…"); }
	}
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core",
		"/org/muness/CalendarReminderOverlay/Core", "org.muness.CalendarReminderOverlay.Core1", "Act",
		g_variant_new ("(stss)", ui->dedupe_key, ui->local_revision, action, payload ? payload : ""),
		G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, 8000, NULL, local_action_done, g_object_ref (ui->window));
}

static void
clear_watch_views (void)
{
	watch_current_ui = NULL;
	ui_generation++;
	watch_action_pending = FALSE;
	if (!watch_views) return;
	for (guint i = 0; i < watch_views->len; i++) {
		GtkWindow *window = g_ptr_array_index (watch_views, i);
		ReminderUi *ui = g_object_get_data (G_OBJECT (window), "reminder-ui");
		if (ui) {
			ui->finished = TRUE;
			if (ui->countdown_source) { g_source_remove (ui->countdown_source); ui->countdown_source = 0; }
		}
		gtk_window_destroy (window);
	}
	g_ptr_array_set_size (watch_views, 0);
}

static void
local_stop_audio (void)
{
	if (!core_connection) return;
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core", "/org/muness/CalendarReminderOverlay/Core",
		"org.muness.CalendarReminderOverlay.Core1", "StopAudio", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL, NULL);
}

static void
apply_policy_json (const gchar *json)
{
	GError *error = NULL;
	JsonParser *parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, json, -1, &error)) {
		g_debug ("Renderer policy is invalid: %s", error ? error->message : "invalid JSON");
		g_clear_error (&error); g_object_unref (parser); return;
	}
	JsonNode *incoming = json_parser_get_root (parser);
	if (current_policy && json_node_equal (current_policy, incoming)) {
		g_object_unref (parser);
		return;
	}
	gboolean layout_changed = current_policy == NULL;
	const gchar *layout_keys[] = { "display", "snooze", "calendar_overrides" };
	for (guint i = 0; current_policy && i < G_N_ELEMENTS (layout_keys); i++) {
		JsonNode *before = json_object_get_member (json_node_get_object (current_policy), layout_keys[i]);
		JsonNode *after = json_object_get_member (json_node_get_object (incoming), layout_keys[i]);
		if (before != after && (!before || !after || !json_node_equal (before, after))) layout_changed = TRUE;
	}
	policy_changed |= layout_changed;
	g_clear_pointer (&current_policy, json_node_unref);
	current_policy = json_node_copy (json_parser_get_root (parser));
	JsonObject *root = json_node_get_object (current_policy);
	JsonObject *display = json_object_get_object_member (root, "display");
	JsonObject *appearance = json_object_get_object_member (root, "appearance");
	JsonArray *minutes = json_object_get_array_member (json_object_get_object_member (root, "snooze"), "minutes");
	g_free (outputs_mode); outputs_mode = g_strdup (display ? json_object_get_string_member (display, "outputs") : "all");
	g_free (display_backend); display_backend = g_strdup (display ? json_object_get_string_member (display, "backend") : "native");
	g_clear_pointer (&active_snooze_minutes, g_array_unref);
	active_snooze_minutes = g_array_new (FALSE, FALSE, sizeof (guint));
	if (minutes) for (guint i = 0; i < json_array_get_length (minutes); i++) {
		guint value = (guint) json_array_get_int_element (minutes, i);
		g_array_append_val (active_snooze_minutes, value);
	}
	if (!active_snooze_minutes->len) { guint defaults[] = { 1, 5 }; g_array_append_vals (active_snooze_minutes, defaults, G_N_ELEMENTS (defaults)); }
	if (application_css) {
		const gchar *theme = appearance ? json_object_get_string_member (appearance, "theme") : "default";
		const gchar *bg = g_strcmp0 (theme, "light") == 0 ? "#f4f5f7" : "#151821";
		const gchar *fg = g_strcmp0 (theme, "light") == 0 ? "#16181d" : "#f4f5f7";
		const gchar *accent = g_strcmp0 (theme, "light") == 0 ? "#244f9b" : "#a9c7ff";
		const gchar *action_bg = g_strcmp0 (theme, "light") == 0 ? "#e3e8f0" : "#303745";
		const gchar *primary_bg = g_strcmp0 (theme, "light") == 0 ? "#d3e2fb" : "#294e86";
		const gchar *snooze_bg = g_strcmp0 (theme, "light") == 0 ? "#eef1f5" : "#232a36";
		const gchar *border = g_strcmp0 (theme, "light") == 0 ? "#718097" : "#606b7d";
		const gchar *disabled_fg = g_strcmp0 (theme, "light") == 0 ? "#626a76" : "#a9b0ba";
		gchar *css = g_strdup_printf (".reminder-overlay { background: %s; color: %s; }.reminder-title { font-size: 38pt; font-weight: 700; }.reminder-details { font-size: 23pt; }.reminder-status { font-size: 20pt; color: %s; min-height: 42px; }.reminder-hint { font-size: 15pt; opacity: 0.9; }.reminder-group-heading { font-size: 17pt; font-weight: 700; margin-top: 8px; }.reminder-action { font-size: 23pt; font-weight: 650; padding: 22px 30px; min-width: 170px; min-height: 78px; color: %s; background: %s; border: 2px solid %s; border-radius: 8px; }.reminder-primary { background: %s; border-color: %s; }.reminder-secondary { background: %s; }.reminder-snooze { background: %s; }.reminder-action:focus { outline: 4px solid %s; outline-offset: 3px; }.reminder-action:hover { border-color: %s; }.reminder-action:disabled { color: %s; background: %s; border-color: %s; }", bg, fg, accent, fg, action_bg, border, primary_bg, accent, action_bg, snooze_bg, accent, accent, disabled_fg, action_bg, border);
		gtk_css_provider_load_from_string (application_css, css);
		g_free (css);
	}
	g_object_unref (parser);
}

static GdkMonitor *
preferred_monitor (GdkDisplay *display)
{
	GListModel *monitors = gdk_display_get_monitors (display);
	GdkMonitor *primary = g_list_model_get_n_items (monitors) ? g_list_model_get_item (monitors, 0) : NULL;
	if (outputs_mode && g_str_has_prefix (outputs_mode, "connector:")) {
		for (guint i = 0; i < g_list_model_get_n_items (monitors); i++) {
			GdkMonitor *candidate = g_list_model_get_item (monitors, i);
			gboolean match = g_strcmp0 (gdk_monitor_get_connector (candidate), outputs_mode + strlen ("connector:")) == 0;
			if (match) { g_clear_object (&primary); return candidate; }
			g_object_unref (candidate);
		}
		g_clear_object (&primary);
		return NULL;
	}
	return primary;
}

static void
finish_reminder (ReminderUi *ui, gboolean dismiss)
{
	gboolean local = ui->local;
	(void) dismiss;
	if (ui->finished)
		return;
	ui->finished = TRUE;
	if (ui->countdown_source) {
		g_source_remove (ui->countdown_source);
		ui->countdown_source = 0;
	}
	if (active_reminders && ui->dedupe_key)
		g_hash_table_remove (active_reminders, ui->dedupe_key);
	if (watch_mode) clear_watch_views ();
	else gtk_window_destroy (ui->window);
	if (local && !watch_mode) g_application_quit (G_APPLICATION (application));
}

static gboolean
on_close_request (GtkWindow *window, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) window;
	gtk_label_set_text (ui->status, "Use Dismiss or a Snooze button to resolve this reminder.");
	return TRUE;
}

/* Arrival must not turn typing in the previous app into a button action. */
static gboolean
on_arrival_key (GtkEventControllerKey *controller, guint keyval, guint keycode,
                GdkModifierType state, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) controller; (void) keycode; (void) state;
	if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)
		ui->keyboard_actions_armed = TRUE;
	if (!ui->keyboard_actions_armed &&
	    (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_space))
		return TRUE;
	return FALSE;
}

static void
on_surface_click (GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	GtkWidget *outer = user_data;
	(void) gesture; (void) n_press; (void) x; (void) y;
	gtk_widget_grab_focus (outer);
}

static void
on_dismiss_clicked (GtkButton *button, gpointer user_data)
{
	(void) button;
	g_message ("Calendar reminder button: Dismiss");
	local_action (user_data, "dismiss", "");
}

static void
on_snooze_choice_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	gint minutes = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "snooze-minutes"));
	gchar *payload = g_strdup_printf ("%d", minutes);
	g_message ("Calendar reminder button: Snooze %d minutes", minutes);
	local_action (ui, "snooze", payload);
	g_free (payload);
}

static void
on_snooze_until_start_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	(void) button;
	if (ui->event_start <= time (NULL))
		return;
	g_message ("Calendar reminder button: Snooze until event starts");
	gchar *timestamp = g_strdup_printf ("%" G_GINT64_FORMAT, ui->event_start);
	local_action (ui, "snooze-until", timestamp);
	g_free (timestamp);
}

static gboolean
update_countdown (gpointer user_data)
{
	ReminderUi *ui = user_data;
	gint64 remaining = ui->event_start - time (NULL);
	gchar *text;
	if (ui->finished)
		return G_SOURCE_REMOVE;
	if (ui->event_start <= 0) {
		text = g_strdup ("No event start time is available");
	} else if (remaining > 0) {
		text = g_strdup_printf ("Starts in %02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
			remaining / 60, remaining % 60);
	} else {
		text = ui->local ? g_strdup ("Event has already started") : g_strdup ("Event has already started");
	}
	gtk_label_set_text (ui->countdown, text);
	if (ui->until_button) gtk_widget_set_sensitive (ui->until_button, ui->event_start > time (NULL));
	g_free (text);
	return G_SOURCE_CONTINUE;
}

static void
monitor_list_changed (GListModel *model, guint position, guint removed, guint added, gpointer user_data)
{
	(void) model; (void) position; (void) removed; (void) added; (void) user_data;
	if (watch_current_ui) {
		policy_changed = TRUE;
		refresh_watch (NULL);
	}
}

static void
on_open_meeting_clicked (GtkButton *button, gpointer user_data)
{
	ReminderUi *ui = user_data;
	GError *error = NULL;
	(void) button;
	if (watch_action_pending || ui->pending_action || ui->finished || !ui->meeting_url || !g_uri_is_valid (ui->meeting_url, G_URI_FLAGS_NONE, NULL))
		return;
	ui->pending_action = TRUE;
	watch_action_pending = TRUE;
	for (guint i = 0; watch_views && i < watch_views->len; i++) {
		GtkWindow *window = g_ptr_array_index (watch_views, i);
		ReminderUi *copy = g_object_get_data (G_OBJECT (window), "reminder-ui");
		if (copy) copy->pending_action = TRUE;
	}
	gtk_label_set_text (ui->status, "Opening meeting link…");
	g_message ("Calendar reminder button: Open meeting");
	if (ui->local) local_stop_audio ();
	if (!g_app_info_launch_default_for_uri (ui->meeting_url, NULL, &error)) {
		g_warning ("Could not open meeting link: %s", error ? error->message : "unknown error");
		gtk_label_set_text (ui->status, "Could not open the meeting link. Retry Open or choose another action.");
		ui->pending_action = FALSE;
		watch_action_pending = FALSE;
		for (guint i = 0; watch_views && i < watch_views->len; i++) {
			GtkWindow *window = g_ptr_array_index (watch_views, i);
			ReminderUi *copy = g_object_get_data (G_OBJECT (window), "reminder-ui");
			if (copy) copy->pending_action = FALSE;
		}
		g_clear_error (&error);
		return;
	}
	ui->pending_action = FALSE;
	watch_action_pending = FALSE;
	local_action (ui, "open", "launched");
}

static void
show_overlay (const gchar *summary, const gchar *body, gchar *meeting_url,
	      const gchar *dedupe_key, gint64 event_start, const gchar *source_id,
	      const gchar *calendar_id, gboolean local, guint64 revision,
	      GdkMonitor *monitor, gboolean copy)
{
	GtkWidget *outer, *center, *content, *title, *details, *details_scroll, *status, *message, *primary_buttons, *snooze_buttons, *button;
	ReminderUi *ui;
	ui = g_new0 (ReminderUi, 1);
	ui->generation = ui_generation;
	ui->local = local;
	ui->local_revision = revision;
	ui->meeting_url = meeting_url;
	ui->event_start = event_start;
	ui->dedupe_key = g_strdup (dedupe_key);
	ui->source_id = g_strdup (source_id ? source_id : "");
	ui->calendar_id = g_strdup (calendar_id ? calendar_id : "");
	ui->window = GTK_WINDOW (gtk_application_window_new (application));
	if (watch_mode) {
		if (!watch_views) watch_views = g_ptr_array_new_with_free_func (g_object_unref);
		g_ptr_array_add (watch_views, g_object_ref (ui->window));
		if (!copy) watch_current_ui = ui;
	}
	if (layer_shell_supported) {
		/* A compositor overlay surface stays above normal and fullscreen app windows. */
		gtk_layer_init_for_window (ui->window);
		gtk_layer_set_namespace (ui->window, "cosmic-calendar-reminder-overlay");
		gtk_layer_set_layer (ui->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
		gtk_layer_set_anchor (ui->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
		gtk_layer_set_exclusive_zone (ui->window, -1);
		gtk_layer_set_keyboard_mode (ui->window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
		if (monitor) gtk_layer_set_monitor (ui->window, monitor);
	}
	g_object_set_data_full (G_OBJECT (ui->window), "reminder-ui", ui, reminder_ui_free);
	gtk_window_set_title (ui->window, summary && *summary ? summary : "Calendar reminder");
	gtk_window_set_decorated (ui->window, FALSE);
	gtk_window_set_resizable (ui->window, TRUE);
	g_signal_connect (ui->window, "close-request", G_CALLBACK (on_close_request), ui);
	GtkEventController *arrival_keys = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (arrival_keys, GTK_PHASE_CAPTURE);
	g_signal_connect (arrival_keys, "key-pressed", G_CALLBACK (on_arrival_key), ui);
	gtk_widget_add_controller (GTK_WIDGET (ui->window), arrival_keys);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 28);
	gtk_widget_add_css_class (outer, "reminder-overlay");
	gtk_widget_set_hexpand (outer, TRUE);
	gtk_widget_set_vexpand (outer, TRUE);
	gtk_widget_set_halign (outer, GTK_ALIGN_FILL);
	gtk_widget_set_valign (outer, GTK_ALIGN_FILL);
	gtk_widget_set_focusable (outer, TRUE);
	GtkGesture *surface_click = gtk_gesture_click_new ();
	g_signal_connect (surface_click, "pressed", G_CALLBACK (on_surface_click), outer);
	gtk_widget_add_controller (outer, GTK_EVENT_CONTROLLER (surface_click));
	center = gtk_box_new (GTK_ORIENTATION_VERTICAL, 28);
	gtk_widget_set_hexpand (center, TRUE);
	gtk_widget_set_vexpand (center, TRUE);
	gtk_widget_set_halign (center, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (center, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top (center, 32);
	gtk_widget_set_margin_bottom (center, 32);
	gtk_widget_set_margin_start (center, 32);
	gtk_widget_set_margin_end (center, 32);

	content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 20);
	gtk_widget_add_css_class (content, "reminder-content");
	gtk_widget_set_size_request (content, 0, -1);
	gtk_widget_set_size_request (center, 0, -1);
	gtk_widget_set_hexpand (content, TRUE);
	gtk_widget_set_halign (content, GTK_ALIGN_FILL);
	gtk_widget_set_valign (content, GTK_ALIGN_CENTER);
	title = gtk_label_new (summary && *summary ? summary : "Calendar reminder");
	gtk_widget_add_css_class (title, "reminder-title");
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (title), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (title), 64);
	gtk_box_append (GTK_BOX (content), title);

	details = gtk_label_new (body && *body ? body : "An event reminder is due.");
	gtk_widget_add_css_class (details, "reminder-details");
	gtk_label_set_xalign (GTK_LABEL (details), 0.0f);
	gtk_label_set_yalign (GTK_LABEL (details), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (details), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (details), 100);
	details_scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (details_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (details_scroll), 320);
	gtk_scrolled_window_set_max_content_width (GTK_SCROLLED_WINDOW (details_scroll), 1400);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (details_scroll), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (details_scroll), details);
	gtk_box_append (GTK_BOX (content), details_scroll);

	status = gtk_label_new (NULL);
	ui->countdown = GTK_LABEL (status);
	gtk_widget_add_css_class (status, "reminder-status");
	gtk_label_set_xalign (GTK_LABEL (status), 0.0f);
	gtk_box_append (GTK_BOX (content), status);
	message = gtk_label_new (ui->meeting_url ? "" : "No meeting link is available for this reminder.");
	ui->status = GTK_LABEL (message);
	gtk_widget_add_css_class (message, "reminder-status");
	gtk_label_set_xalign (GTK_LABEL (message), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (message), TRUE);
	gtk_box_append (GTK_BOX (content), message);
	GtkWidget *keyboard_hint = gtk_label_new ("Click the background to focus the reminder, then press Tab to move between buttons.");
	gtk_widget_add_css_class (keyboard_hint, "reminder-hint");
	gtk_label_set_xalign (GTK_LABEL (keyboard_hint), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (keyboard_hint), TRUE);
	gtk_box_append (GTK_BOX (content), keyboard_hint);
	gtk_box_append (GTK_BOX (center), content);

	primary_buttons = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (primary_buttons), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (primary_buttons), TRUE);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (primary_buttons), 2);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (primary_buttons), 18);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (primary_buttons), 18);
	GtkWidget *primary_heading = gtk_label_new ("Meeting actions");
	gtk_widget_add_css_class (primary_heading, "reminder-group-heading");
	gtk_label_set_xalign (GTK_LABEL (primary_heading), 0.0f);
	gtk_box_append (GTK_BOX (center), primary_heading);
	button = gtk_button_new_with_label ("Open meeting");
	gtk_widget_add_css_class (button, "reminder-action");
	gtk_widget_add_css_class (button, "reminder-primary");
	gtk_widget_set_sensitive (button, ui->meeting_url && g_uri_is_valid (ui->meeting_url, G_URI_FLAGS_NONE, NULL));
	g_signal_connect (button, "clicked", G_CALLBACK (on_open_meeting_clicked), ui);
	gtk_flow_box_insert (GTK_FLOW_BOX (primary_buttons), button, -1);
	button = gtk_button_new_with_label ("Dismiss");
	gtk_widget_add_css_class (button, "reminder-action");
	gtk_widget_add_css_class (button, "reminder-secondary");
	g_signal_connect (button, "clicked", G_CALLBACK (on_dismiss_clicked), ui);
	gtk_flow_box_insert (GTK_FLOW_BOX (primary_buttons), button, -1);
	gtk_box_append (GTK_BOX (center), primary_buttons);

	snooze_buttons = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (snooze_buttons), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (snooze_buttons), TRUE);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (snooze_buttons), 3);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (snooze_buttons), 18);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (snooze_buttons), 18);
	GtkWidget *snooze_heading = gtk_label_new ("Snooze choices");
	gtk_widget_add_css_class (snooze_heading, "reminder-group-heading");
	gtk_label_set_xalign (GTK_LABEL (snooze_heading), 0.0f);
	gtk_box_append (GTK_BOX (center), snooze_heading);
	for (guint i = 0; active_snooze_minutes && i < active_snooze_minutes->len; i++) {
		guint minutes = g_array_index (active_snooze_minutes, guint, i);
		gchar *label = g_strdup_printf ("Snooze %u min", minutes);
		button = gtk_button_new_with_label (label);
		g_free (label);
		g_object_set_data (G_OBJECT (button), "snooze-minutes", GINT_TO_POINTER ((gint) minutes));
		gtk_widget_add_css_class (button, "reminder-action");
		gtk_widget_add_css_class (button, "reminder-snooze");
		g_signal_connect (button, "clicked", G_CALLBACK (on_snooze_choice_clicked), ui);
		gtk_flow_box_insert (GTK_FLOW_BOX (snooze_buttons), button, -1);
	}
	button = gtk_button_new_with_label ("Until event starts");
	gtk_widget_add_css_class (button, "reminder-action");
	gtk_widget_add_css_class (button, "reminder-snooze");
	ui->until_button = button;
	gtk_widget_set_sensitive (button, ui->event_start > time (NULL));
	if (ui->event_start <= 0)
		gtk_label_set_text (ui->status, ui->meeting_url ? "Until event starts is unavailable because no start time is available." : "No meeting link or event start time is available.");
	else if (ui->event_start <= time (NULL))
		gtk_label_set_text (ui->status, ui->meeting_url ? "Until event starts is unavailable because the event has already started." : "No meeting link is available; the event has already started.");
	g_signal_connect (button, "clicked", G_CALLBACK (on_snooze_until_start_clicked), ui);
	gtk_flow_box_insert (GTK_FLOW_BOX (snooze_buttons), button, -1);
	gtk_box_append (GTK_BOX (center), snooze_buttons);

	gtk_window_set_child (ui->window, outer);
	gtk_box_append (GTK_BOX (outer), center);
	update_countdown (ui);
	ui->countdown_source = g_timeout_add_seconds (1, update_countdown, ui);
	gtk_window_present (ui->window);
	gtk_widget_grab_focus (outer);
	if (local && watch_mode && !copy) watch_current_ui = ui;
	if (!layer_shell_supported) {
		if (monitor) gtk_window_fullscreen_on_monitor (ui->window, monitor);
		else gtk_window_fullscreen (ui->window);
	}
	g_message ("Calendar reminder shown with buttons; meeting URL: %s", ui->meeting_url ? "yes" : "no");
	if (watch_mode && !copy) {
		GdkDisplay *display = gdk_display_get_default ();
		GListModel *monitors = gdk_display_get_monitors (display);
		GdkMonitor *primary = preferred_monitor (display);
		for (guint i = 0; i < g_list_model_get_n_items (monitors); i++) {
			GdkMonitor *target = g_list_model_get_item (monitors, i);
			const gchar *connector = gdk_monitor_get_connector (target);
			gboolean include = g_strcmp0 (outputs_mode ? outputs_mode : "all", "all") == 0 ||
			(g_strcmp0 (outputs_mode ? outputs_mode : "all", "first") == 0 && target == primary) ||
				(g_str_has_prefix (outputs_mode ? outputs_mode : "", "connector:") && g_strcmp0 (connector, outputs_mode + strlen ("connector:")) == 0);
			if (include && target != primary)
				show_overlay (summary, body, g_strdup (ui->meeting_url), dedupe_key, event_start, source_id, calendar_id, local, revision, target, TRUE);
			g_object_unref (target);
		}
		g_clear_object (&primary);
	}
}

static void
watch_set_foreground (const gchar *id)
{
	if (!core_connection) return;
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core",
		"/org/muness/CalendarReminderOverlay/Core", "org.muness.CalendarReminderOverlay.Core1", "SetForeground",
		g_variant_new ("(s)", id ? id : ""), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
render_list_snapshot (const gchar *json)
{
	GError *error = NULL;
	JsonParser *parser = json_parser_new ();
	JsonArray *items;
	JsonObject *selected = NULL;
	if (!json_parser_load_from_data (parser, json, -1, &error)) {
		g_debug ("Alert watch received invalid core state: %s", error ? error->message : "invalid JSON");
		g_clear_error (&error);
		g_object_unref (parser);
		return;
	}
	if (watch_action_pending) { g_object_unref (parser); return; }
	layer_shell_supported = g_strcmp0 (display_backend, "native") == 0 && gtk_layer_is_supported ();
	items = json_node_get_array (json_parser_get_root (parser));
	for (guint i = 0; i < json_array_get_length (items); i++) {
		JsonObject *item = json_array_get_object_element (items, i);
		if (g_strcmp0 (json_object_get_string_member (item, "state"), "active") != 0) continue;
		if (synthetic_alert_id) {
			if (g_strcmp0 (synthetic_alert_id, json_object_get_string_member (item, "id")) == 0) { selected = item; break; }
			continue;
		}
		if (watch_current_ui && g_strcmp0 (watch_current_ui->dedupe_key, json_object_get_string_member (item, "id")) == 0) { selected = item; break; }
		if (!selected) selected = item;
	}
	if (policy_changed) {
		policy_changed = FALSE;
		clear_watch_views ();
	}
	if (!selected) {
		watch_set_foreground ("");
		if (watch_current_ui) { g_hash_table_remove (active_reminders, watch_current_ui->dedupe_key); clear_watch_views (); }
		g_object_unref (parser);
		return;
	}
	const gchar *id = json_object_get_string_member (selected, "id");
	const gchar *source_id = json_object_get_string_member (selected, "source_id");
	const gchar *calendar_id = json_object_get_string_member (selected, "calendar_id");
	guint64 revision = (guint64) json_object_get_int_member (selected, "revision");
	if (watch_current_ui && (g_strcmp0 (watch_current_ui->dedupe_key, id) != 0 || watch_current_ui->local_revision != revision)) {
		g_hash_table_remove (active_reminders, watch_current_ui->dedupe_key);
		clear_watch_views ();
	}
	if (!watch_current_ui) {
		const gchar *title = json_object_get_string_member (selected, "title");
		const gchar *body = json_object_get_string_member (selected, "body");
		const gchar *url = json_object_get_string_member (selected, "destination");
		gint64 event_start = json_object_get_int_member (selected, "event_start");
		JsonObject *overrides = json_object_get_object_member (json_node_get_object (current_policy), "calendar_overrides");
		JsonArray *defaults = json_object_get_array_member (json_object_get_object_member (json_node_get_object (current_policy), "snooze"), "minutes");
		g_clear_pointer (&active_snooze_minutes, g_array_unref);
		active_snooze_minutes = g_array_new (FALSE, FALSE, sizeof (guint));
		for (guint i = 0; defaults && i < json_array_get_length (defaults); i++) { guint value = (guint) json_array_get_int_element (defaults, i); g_array_append_val (active_snooze_minutes, value); }
		gchar *key = g_strdup_printf ("%s/%s", source_id, calendar_id);
		JsonObject *override = overrides && json_object_has_member (overrides, key) ? json_object_get_object_member (overrides, key) : NULL;
		JsonArray *custom = override ? json_object_get_array_member (override, "snooze_minutes") : NULL;
		if (custom) { g_clear_pointer (&active_snooze_minutes, g_array_unref); active_snooze_minutes = g_array_new (FALSE, FALSE, sizeof (guint)); for (guint i = 0; i < json_array_get_length (custom); i++) { guint value = (guint) json_array_get_int_element (custom, i); g_array_append_val (active_snooze_minutes, value); } }
		g_hash_table_add (active_reminders, g_strdup (id));
		GdkMonitor *monitor = preferred_monitor (gdk_display_get_default ());
		if (monitor)
			show_overlay (title, body, *url ? g_strdup (url) : NULL, id, event_start, source_id, calendar_id, TRUE, revision, monitor, FALSE);
		else {
			watch_set_foreground ("");
			g_warning ("Configured output %s is unavailable; reminder remains active in core", outputs_mode ? outputs_mode : "all");
		}
		g_clear_object (&monitor);
		g_free (key);
	}
	if (watch_current_ui)
		watch_set_foreground (id);
	g_object_unref (parser);
}

static void
watch_list_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	GVariant *response = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	watch_refreshing = FALSE;
	if (!response) {
		g_debug ("Alert watch refresh failed: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	const gchar *json;
	g_variant_get (response, "(&s)", &json);
	render_list_snapshot (json);
	g_variant_unref (response);
	(void) user_data;
}

static void
watch_policy_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	GVariant *response = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	if (!response) {
		watch_refreshing = FALSE;
		g_debug ("Alert policy refresh failed: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	const gchar *json;
	g_variant_get (response, "(&s)", &json);
	apply_policy_json (json);
	g_variant_unref (response);
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core",
		"/org/muness/CalendarReminderOverlay/Core", "org.muness.CalendarReminderOverlay.Core1", "List",
		NULL, G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, watch_list_done, NULL);
	(void) user_data;
}

static void
watch_bus_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	core_connection = g_bus_get_finish (result, &error);
	if (!core_connection) {
		watch_refreshing = FALSE;
		g_debug ("Alert watch cannot reach core: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core",
		"/org/muness/CalendarReminderOverlay/Core", "org.muness.CalendarReminderOverlay.Core1", "GetPolicy",
		NULL, G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, watch_policy_done, NULL);
	(void) source; (void) user_data;
}

static gboolean
refresh_watch (gpointer user_data)
{
	(void) user_data;
	if (watch_refreshing) return G_SOURCE_CONTINUE;
	watch_refreshing = TRUE;
	if (!core_connection) {
		g_bus_get (G_BUS_TYPE_SESSION, NULL, watch_bus_ready, NULL);
		return G_SOURCE_CONTINUE;
	}
	g_dbus_connection_call (core_connection, "org.muness.CalendarReminderOverlay.Core",
		"/org/muness/CalendarReminderOverlay/Core", "org.muness.CalendarReminderOverlay.Core1", "GetPolicy",
		NULL, G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, watch_policy_done, NULL);
	return G_SOURCE_CONTINUE;
}

static void
on_activate (GtkApplication *app, gpointer user_data)
{
	(void) user_data;
	if (active_reminders) return;
	g_application_hold (G_APPLICATION (app));
	layer_shell_supported = gtk_layer_is_supported ();
	active_reminders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	monitor_model = gdk_display_get_monitors (gdk_display_get_default ());
	g_signal_connect (monitor_model, "items-changed", G_CALLBACK (monitor_list_changed), NULL);
	if (watch_mode) {
		if (!watch_poll_source) watch_poll_source = g_timeout_add_seconds (1, refresh_watch, NULL);
		refresh_watch (NULL);
		return;
	}
}

static void
print_capabilities (void)
{
	GdkDisplay *display = gdk_display_get_default ();
	GListModel *monitors = gdk_display_get_monitors (display);
	g_print ("{\"version\":1,\"renderer\":\"gtk4\",\"layer_shell_supported\":%s,\"backend\":\"%s\",\"monitors\":[",
		gtk_layer_is_supported () ? "true" : "false", G_OBJECT_TYPE_NAME (display));
	for (guint i = 0; i < g_list_model_get_n_items (monitors); i++) {
		GdkMonitor *monitor = g_list_model_get_item (monitors, i);
		GdkRectangle geometry;
		gdk_monitor_get_geometry (monitor, &geometry);
		if (i) g_print (",");
		g_print ("{\"connector\":\"%s\",\"scale_factor\":%d,\"width\":%d,\"height\":%d}",
			gdk_monitor_get_connector (monitor) ? gdk_monitor_get_connector (monitor) : "",
			gdk_monitor_get_scale_factor (monitor), geometry.width, geometry.height);
		g_object_unref (monitor);
	}
	g_print ("] ,\"output_modes\":[\"all\",\"first\",\"connector:<name>\"],\"theme_modes\":[\"default\",\"dark\",\"light\"]}\n");
}

int
main (int argc, char **argv)
{
	GtkCssProvider *css;
	gint status;
	int output_arg = 1;
	gboolean capabilities = FALSE;
	for (int i = 1; i < argc; i++) {
		if (g_str_equal (argv[i], "--synthetic") && i + 1 < argc) {
			synthetic_alert_id = g_strdup (argv[++i]);
		} else if (g_str_equal (argv[i], "--watch")) {
			watch_mode = TRUE;
		} else if (g_str_equal (argv[i], "--capabilities")) {
			capabilities = TRUE;
		} else argv[output_arg++] = argv[i];
	}
	argc = output_arg;
	argv[argc] = NULL;
	if (!synthetic_alert_id && !watch_mode && !capabilities) {
		g_printerr ("Select --watch for the shared core renderer or --synthetic ID for one test alert.\n");
		return 2;
	}
	if (synthetic_alert_id) watch_mode = TRUE;
	gtk_init ();
	if (capabilities) {
		GtkCssProvider *probe = gtk_css_provider_new ();
		gtk_css_provider_load_from_string (probe,
			".reminder-overlay { background: #151821; color: #f4f5f7; }"
			".reminder-title { font-size: 38pt; font-weight: 700; }"
			".reminder-details { font-size: 23pt; }"
			".reminder-status { font-size: 20pt; color: #a9c7ff; min-height: 42px; }"
			".reminder-hint { font-size: 15pt; opacity: 0.9; }"
			".reminder-group-heading { font-size: 17pt; font-weight: 700; margin-top: 8px; }"
			".reminder-action { font-size: 23pt; font-weight: 650; padding: 22px 30px; min-width: 170px; min-height: 78px; color: #f4f5f7; background: #303745; border: 2px solid #606b7d; border-radius: 8px; }"
			".reminder-primary { background: #294e86; border-color: #8db6ff; }.reminder-secondary { background: #303745; }.reminder-snooze { background: #232a36; }"
			".reminder-action:focus { outline: 4px solid #a9c7ff; outline-offset: 3px; }.reminder-action:hover { border-color: #a9c7ff; }.reminder-action:disabled { color: #a9b0ba; background: #303745; border-color: #606b7d; }");
		g_object_unref (probe);
		print_capabilities ();
		return 0;
	}
	application = gtk_application_new ("com.muness.calendar-reminder-overlay",
		G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect (application, "activate", G_CALLBACK (on_activate), NULL);
	css = gtk_css_provider_new ();
	application_css = g_object_ref (css);
	gtk_css_provider_load_from_string (css,
		".reminder-overlay { background: #151821; color: #f4f5f7; }"
		".reminder-title { font-size: 38pt; font-weight: 700; }"
		".reminder-details { font-size: 23pt; }"
		".reminder-status { font-size: 20pt; color: #a9c7ff; min-height: 42px; }"
		".reminder-group-heading { font-size: 17pt; font-weight: 700; margin-top: 8px; }"
		".reminder-hint { font-size: 15pt; opacity: 0.9; }"
		".reminder-action { font-size: 23pt; font-weight: 650; padding: 22px 30px; min-width: 170px; min-height: 78px; }"
		".reminder-action:focus { outline: 4px solid #a9c7ff; outline-offset: 3px; }");
	gtk_style_context_add_provider_for_display (gdk_display_get_default (),
		GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (css);
	status = g_application_run (G_APPLICATION (application), argc, argv);
	if (watch_poll_source) g_source_remove (watch_poll_source);
	if (monitor_model) g_signal_handlers_disconnect_by_func (monitor_model, G_CALLBACK (monitor_list_changed), NULL);
	g_clear_object (&core_connection);
	g_clear_object (&application_css);
	g_clear_object (&application);
	g_free (synthetic_alert_id);
	return status;
}
