#define main reminder_core_main
#include "../src/reminder-core.c"
#undef main

int
main (void)
{
	GError *error = NULL;
	ESource *source = e_source_new_with_uid ("selection-fixture", NULL, &error);
	if (!source) { g_printerr ("Could not create in-memory ESource: %s\n", error ? error->message : "unknown"); return 1; }
	ESourceSelectable *selectable = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR));
	policy_mode = g_strdup ("all");
	selected_calendars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	eds_enabled = TRUE;
	follow_calendar_selection = TRUE;
	if (!calendar_source_selected (source)) { g_printerr ("selected EDS source was unexpectedly excluded\n"); return 2; }
	e_source_selectable_set_selected (selectable, FALSE);
	if (calendar_source_selected (source)) { g_printerr ("Evolution-deselected source remained eligible by default\n"); return 3; }
	follow_calendar_selection = FALSE;
	if (!calendar_source_selected (source)) { g_printerr ("TOML escape hatch did not bypass Evolution selection\n"); return 4; }
	g_free (policy_mode);
	policy_mode = g_strdup ("include");
	if (calendar_source_selected (source)) { g_printerr ("TOML include list was not intersected with enabled source\n"); return 5; }
	g_hash_table_add (selected_calendars, g_strdup ("eds-local/selection-fixture"));
	if (!calendar_source_selected (source)) { g_printerr ("included enabled source was unexpectedly excluded\n"); return 6; }
	e_source_set_enabled (source, FALSE);
	if (calendar_source_selected (source)) { g_printerr ("disabled EDS source remained eligible\n"); return 7; }
	if (calendar_uid_selected ("selection-fixture")) { g_printerr ("registry outage allowed presentation\n"); return 8; }
	if (!calendar_uid_may_retain_snapshot ("selection-fixture")) { g_printerr ("registry outage discarded cached snapshot with selection override\n"); return 9; }
	g_hash_table_unref (selected_calendars);
	g_free (policy_mode);
	g_object_unref (source);
	return 0;
}
