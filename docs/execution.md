# Epic execution and integration review

## Execute

**Task:** Execute epic #1 using one Luna worker at a time. Related issues are grouped into draft PRs; the coordinator reviews and dissents on every draft, corrects alignment/complexity/useless tests, then merges accepted work only into `codex/attention-overlay-integration` for user testing.

**Aim:** Users notice selected commitments and deliberately open/join, snooze or dismiss them through the accepted large overlay and optional sound. Ordinary notification ownership and appearance remain independent.

**Selected approach:** Reuse the current C/GTK overlay and native EDS state; establish a small alert lifecycle, validated TOML and selected sources, a local CLI, reliable locally owned state and display adapters. Prefer a small Python TOML helper over vendoring a large parser. Source/UI boundaries must reduce coupling, not create a plugin framework.

**Pre-flight:** Aim, constraints, planned acceptance, authority and repository context are loaded from `attention-overlay-plan.md` and issues #1–#11. The repository starts clean; required GTK, EDS, JSON-GLib, SQLite and playback facilities are available. User authorizes draft creation, review/fixes and merges into the integration branch. Production services/settings and live calendars are not required for implementation verification.

**Groups:**
1. Shared core, TOML selection, durable lifecycle and CLI (#2/#3/#4/#6).
2. Selected EDS calendars, common display behavior and audio (#5/#7/#9).
3. GNOME feasibility/helper, native-KDE discovery, installation and diagnostic handoff (#8/#10/#11).

**Success criteria:** Working synthetic CLI/UI; selection enforced through native/fallback paths; replacement/cancel/action behavior; persisted synthetic snooze and restart/expiry policy; audio lifecycle; full-output multi-monitor display; reproducible build/install/migration path; explicit desktop/source capability evidence. One testable integration branch and reviewed grouped PR history.

**Preservation constraints:** Keep accepted large UI/buttons. No global notification interception, new account/sync engine or broad external source enrollment. No live-calendar test event or auto-install/start of the new service during this implementation run. Existing service remains available until the user tests the branch.

**Assumptions/trade-offs:** Existing native library APIs are usable; native EDS scheduler state is reused. Configuration can use a small Python helper. A public framework, a cloud push service and new distribution formats are deferred. GNOME/Akonadi/compositor sessions unavailable locally must be reported as unverified rather than represented as passed.

### Risk retirement checklist

| Risk | Check that can fail the tempting shortcut | Required disposition |
|---|---|---|
| Selection applies only to fallback scan | Native and fallback events from selected/unselected fixture sources | Retire with evidence before claiming correctness |
| Native/fallback duplicate authority | Change/cancel/add-native-alarm scenarios with occurrence identities | Retire with evidence |
| Local state disappears on restart | Synthetic snooze/dismiss/update/cancel/expiry across process restart | Retire with evidence |
| CLI only sends and cannot observe/withdraw | Submit/update/cancel and action-result scenarios, sender exit | Retire with evidence |
| Audio outlives one window | Each action/withdrawal and multi-output teardown stops one logical playback | Retire with evidence |
| Layer-shell presence counted as runtime proof | Existing-fullscreen and actual-button/output scenario per available desktop | Record runtime evidence or explicit unavailable-session boundary |
| GNOME keep-above treated as universal | GTK helper fullscreen/interaction experiment on actual GNOME | Unverified locally unless that session is available; no supported claim |
| Native KDE discovery implies adapter delivered | Capability report with API evidence and runnable/read-only fixture where possible | Distinguish report from production adapter |
| Rename leaves duplicate service | Isolated fresh/legacy install state checks, normal notification unit unaffected | Retire with evidence |
| Tests mirror code and hide wrong behavior | Review each test for an actual failure mode; remove scaffolding-only tests | Coordinator review per draft |

**Stop/pivot:** A proposed source needs new account/sync infrastructure, the accepted UI is replaced without evidence, any implementation takes over normal notifications, or a display failure is hidden by weakening acceptance. Adjust at the responsible draft instead of silently changing scope.

## Dissent

**Decision under review:** Extend the existing small implementation with a shared domain boundary and grouped drafts rather than rebuilding the application.

**Steel-man:** Reusing the approved GTK layout and EDS native alarms preserves what already works while a compact contract permits CLI and other displays.

**Contrary evidence:** The prototype combines all concerns in one file; synthetic fallback uses process-only state; installed unit names differ. A clean build alone would conceal these failures.

**Pre-mortem:** Functional failure: separate native/local timers duplicate events. Adoption failure: a generic interface makes setup harder than the original utility. Opportunity cost: a large framework consumes work needed for reliable local reminders.

**Hidden assumptions:** A small boundary can express native actions (check with real EDS APIs); TOML does not require rewriting UI (use a narrow parser helper); protocol compatibility implies no guarantee of focus/visibility (verify actual sessions); synthetic inputs can test most behavior without changing user calendars (separate headless and presentation checks).

**Reconstructed story:** Preserve the C UI, minimize new dependencies and require behavior evidence per draft. The weakest assumption is the integration between native scheduling and generic state; retire it explicitly. Cross-desktop availability is a test boundary, not a reason to claim support prematurely.

**Decision:** ADJUST toward small components and isolated synthetic verification; detailed choices remain subject to per-draft dissent. Confidence: medium.

## Draft review log

Pending first Luna draft.
