/* Exercise the real widget tree without presenting an overlay on the desktop. */
#include <gtk/gtk.h>
#define gtk_window_present(window) ((void) (window))
#define gtk_window_fullscreen(window) ((void) (window))
#define gtk_window_fullscreen_on_monitor(window, monitor) ((void) (window))
#define main calendar_reminder_renderer_main
#include "../src/calendar-reminder-overlay.c"
#undef main

static void
check_layout (int width, int height, gboolean large_content)
{
	gchar *long_text = g_strnfill (2048, 'W');
	show_overlay (large_content ? long_text : "Calendar reminder",
		long_text, NULL, "layout-test", time (NULL) + 3600,
		"", "", FALSE, 1, NULL, FALSE);
	GtkWindow *window = gtk_application_get_windows (application)->data;
	GtkWidget *outer = gtk_window_get_child (window);
	GtkWidget *bounds = gtk_widget_get_first_child (outer);
	int minimum, natural;
	gtk_widget_measure (outer, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, NULL, NULL);
	g_assert_cmpint (minimum, <=, width);
	g_assert_cmpint (natural, <=, 1104);
	gtk_widget_measure (outer, GTK_ORIENTATION_VERTICAL, width, &minimum, &natural, NULL, NULL);
	g_assert_cmpint (minimum, <=, height);
	g_assert_cmpint (natural, <=, 964);
	gtk_widget_allocate (outer, width, height, -1, NULL);
	g_assert_cmpint (gtk_widget_get_width (bounds), <=, MIN (1040, width - 64));
	g_assert_cmpint (gtk_widget_get_height (bounds), <=, MIN (900, height - 64));
	g_assert_cmpint (gtk_widget_get_width (bounds), >, 0);
	g_assert_cmpint (gtk_widget_get_height (bounds), >, 0);
	GtkWidget *center = gtk_viewport_get_child (GTK_VIEWPORT (gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (bounds))));
	GtkWidget *content_scroll = gtk_widget_get_first_child (center);
	GtkWidget *scroll_content = gtk_viewport_get_child (GTK_VIEWPORT (gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (content_scroll))));
	GtkWidget *content = gtk_widget_get_first_child (scroll_content);
	GtkWidget *title = gtk_widget_get_first_child (content);
	GtkWidget *details_scroll = gtk_widget_get_next_sibling (title);
	GtkWidget *details = gtk_viewport_get_child (GTK_VIEWPORT (gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (details_scroll))));
	/* A viewport alone could hide the original bug. Long tokens themselves
	 * must wrap, rather than forcing a huge label inside that viewport. */
	gtk_widget_measure (details, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, NULL, NULL, NULL);
	g_assert_cmpint (minimum, <, 100);
	gtk_widget_measure (title, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, NULL, NULL, NULL);
	g_assert_cmpint (minimum, <, 100);
	GtkWidget *primary_buttons = gtk_widget_get_last_child (center);
	graphene_rect_t actions;
	g_assert_true (gtk_widget_compute_bounds (primary_buttons, outer, &actions));
	g_assert_cmpfloat (actions.origin.y, >=, 0);
	g_assert_cmpfloat (actions.origin.y + actions.size.height, <=, height);
	GtkAdjustment *vertical = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (content_scroll));
	if (large_content) {
		/* Long titles/many snoozes must scroll instead of enlarging the surface. */
		g_assert_cmpfloat (gtk_adjustment_get_upper (vertical), >, gtk_adjustment_get_page_size (vertical));
		gtk_adjustment_set_value (vertical, gtk_adjustment_get_upper (vertical));
		g_assert_cmpfloat (gtk_adjustment_get_value (vertical), >, 0);
	}
	g_print ("layout %dx%d, %s: viewport %dx%d\n", width, height,
		large_content ? "long title + extra snoozes" : "long unbroken body",
		gtk_widget_get_width (bounds), gtk_widget_get_height (bounds));
	ReminderUi *ui = g_object_get_data (G_OBJECT (window), "reminder-ui");
	g_source_remove (ui->countdown_source);
	gtk_window_destroy (window);
	g_free (long_text);
}

int
main (void)
{
	gtk_init ();
	application = gtk_application_new ("com.muness.reminder-layout-test", G_APPLICATION_NON_UNIQUE);
	g_assert_true (g_application_register (G_APPLICATION (application), NULL, NULL));
	application_css = gtk_css_provider_new ();
	gtk_style_context_add_provider_for_display (gdk_display_get_default (),
		GTK_STYLE_PROVIDER (application_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	apply_policy_json ("{\"display\":{\"outputs\":\"all\",\"backend\":\"native\"},\"appearance\":{\"theme\":\"dark\"},\"snooze\":{\"minutes\":[1,5]}}");
	check_layout (2016, 1260, FALSE);
	check_layout (1280, 720, FALSE);
	check_layout (640, 480, FALSE);
	for (guint minute = 10; minute <= 100; minute += 5)
		g_array_append_val (active_snooze_minutes, minute);
	check_layout (2016, 1260, TRUE);
	check_layout (640, 480, TRUE);
	g_clear_pointer (&current_policy, json_node_unref);
	g_clear_pointer (&active_snooze_minutes, g_array_unref);
	g_clear_object (&application_css);
	g_clear_object (&application);
	g_free (outputs_mode);
	g_free (display_backend);
	return 0;
}
