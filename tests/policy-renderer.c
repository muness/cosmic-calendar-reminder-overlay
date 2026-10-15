#define main calendar_reminder_renderer_main
#include "../src/calendar-reminder-overlay.c"
#undef main

int
main (void)
{
	const gchar *first = "{\"display\": {\"outputs\": \"all\", \"backend\": \"native\"}, \"appearance\": {\"theme\": \"default\"}, \"snooze\": {\"minutes\": [1, 5]}}";
	const gchar *same = "{\n  \"display\":{\"outputs\":\"all\",\"backend\":\"native\"},\n  \"appearance\":{\"theme\":\"default\"},\n  \"snooze\":{\"minutes\":[1,5]}\n}";
	const gchar *changed = "{\"display\": {\"outputs\": \"all\", \"backend\": \"native\"}, \"appearance\": {\"theme\": \"light\"}, \"snooze\": {\"minutes\": [1, 5]}}";
	apply_policy_json (first);
	if (!policy_changed) return 1;
	policy_changed = FALSE;
	apply_policy_json (same);
	if (policy_changed) return 2;
	apply_policy_json (changed);
	if (policy_changed) return 3;
	policy_changed = TRUE;
	apply_policy_json (changed);
	if (!policy_changed) return 4;
	apply_policy_json ("{\"display\":{\"outputs\":\"all\",\"backend\":\"native\"},\"appearance\":{\"theme\":\"default\"},\"snooze\":{\"minutes\":[1,5]},\"presentation\":{\"enabled\":false}}");
	if (renderer_presentation_enabled) return 8;
	apply_policy_json ("{\"display\":{\"outputs\":\"all\",\"backend\":\"native\"},\"appearance\":{\"theme\":\"default\"},\"snooze\":{\"minutes\":[1,5]},\"presentation\":{\"enabled\":true}}");
	if (!renderer_presentation_enabled) return 9;
	ReminderUi ui = {0};
	if (!on_arrival_key (NULL, GDK_KEY_Return, 0, 0, &ui)) return 5;
	if (!on_arrival_key (NULL, GDK_KEY_space, 0, 0, &ui)) return 6;
	on_arrival_key (NULL, GDK_KEY_Tab, 0, 0, &ui);
	if (on_arrival_key (NULL, GDK_KEY_Return, 0, 0, &ui)) return 7;
	g_clear_pointer (&current_policy, json_node_unref);
	g_clear_pointer (&active_snooze_minutes, g_array_unref);
	g_free (outputs_mode);
	g_free (display_backend);
	return 0;
}
