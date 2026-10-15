---
target: calendar reminder overlay
total_score: 21
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 3
target_identity: "file:/Volumes/src/muness/cosmic-calendar-reminder-overlay/src/calendar-reminder-overlay.c"
target_fingerprint: "sha256:3be6386df9defaff2adbdb00b06cfc34bd8a64925c8638dc1742cda31e1f24b8"
target_path: /Volumes/src/muness/cosmic-calendar-reminder-overlay/src/calendar-reminder-overlay.c
timestamp: 2026-10-15T15-03-47Z
slug: src-calendar-reminder-overlay-c
closed: true
---
Method: design review only (detector not applicable, native GTK desktop)

# Overlay critique — one pass

Target: src/calendar-reminder-overlay.c, frozen integration 46cc2e6. Source review plus COSMIC screenshot /tmp/reminder-design-review/Screenshot_2026-10-15_11-01-48.png and direct user feedback. Current working source is evolving; this report names the assessed revision.

## Design specificity

Large event text and explicit meeting/dismiss/snooze buttons fit the deliberate interruption task. Preserve the approved huge overlay and visible actions. Correct placement, keyboard safety and recovery instead of replacing the visual world.

## Design health

| Heuristic | Score | Main issue |
|---|---:|---|
| System status | 2/4 | Failures only in logs |
| Familiar language | 3/4 | Until start ambiguous |
| User control | 2/4 | Typing can resolve an alert |
| Consistency | 3/4 | Equal emphasis on all actions |
| Error prevention | 1/4 | Arrival captures activation keys |
| Recognition | 3/4 | Labeled actions, weak grouping |
| Efficiency | 2/4 | No explained deliberate keyboard path |
| Visual clarity | 2/4 | Offset surface, gray margins |
| Recovery | 1/4 | No visible retry guidance |
| Help | 2/4 | Disabled choices unexplained |
| Total | 21/40 | Significant improvements needed |

## Strengths

- Explicit text buttons replace hidden notification gestures.
- Large title/details/countdown provide event context.
- Invalid meeting and start-time choices are disabled.

## Priority issues — execute all

- C1 P1: Fill each selected output, paint the whole window backdrop, remove fixed size constraints and center readable content. Verify layer-shell escapes tiling; give fallback windows exact stable identity and documented exclusion behavior. Actual screenshot shows partial upper-left surface with light margins.
- C2 P1: No action focused/defaulted on arrival. Require deliberate Tab/button navigation before Enter/Space activation; held/repeated keys cannot resolve a newly arrived alert. Close requests must not silently dismiss; guide users to explicit controls. User reported typing closed it.
- C3 P1: Inline accessible launch/action errors, keep reminder active, provide retry and preserve alternate actions.
- C4 P2: Keep every button visible. Distinguish Open/Dismiss from a visibly labeled Snooze group. Rename Until start to Until event starts.
- C5 P2: Wrap/scroll long content, adapt controls to small/scaled outputs, visible focus, and explain missing meeting links and unavailable start-time snooze.

## Cognitive load and personas

Five equal-weight actions compete. Group snooze choices without hiding them. Alex needs quick predictable actions; Sam needs safe focus/keyboard and announced errors; Jordan needs literal labels and visible recovery. Passing source checks cannot establish actual screen-reader behavior.

## Minor observations

Keep the event identifiable after its start time. Disappearance after successful resolution is sufficient confirmation; do not add a second notification. Do not promise undo for native actions that EDS cannot restore safely.

## Questions to consider

Can each selected output display the same centered logical alert? Can any typing already in progress resolve a new alert? Does every failed action explain recovery?

Questions skipped: user explicitly requested execution of all findings.
