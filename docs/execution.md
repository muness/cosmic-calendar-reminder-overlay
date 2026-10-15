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

Drafts are reviewed sequentially before integration.

### Draft #12 — core/configuration/CLI/state

**Review:** ADJUST, then continue as a bounded foundation. Preserve the C/GTK layout; native EDS integration, configuration application to that path, live update/cancel presentation and full calendar discovery remain explicit dependencies of group 2. No epic issue is declared complete from this foundation alone.

**Findings corrected by coordinator:**
- Startup `load_policy` freed aliased path/helper arguments before copying; AddressSanitizer reproduced a heap-use-after-free. Copy replacements before releasing owned strings.
- Read-only CLI calls reloaded a caller's config and could change the daemon's selection. Added explicit `reload`; ordinary operations preserve active daemon policy.
- `wait` confused a content revision with a fresh action. Persist `action_revision` separately, including a narrow preview-schema migration.
- Rejected local UI actions still closed their window. Gate teardown on accepted results; exit the single-alert frontend after successful resolution instead of leaving invisible per-alert processes.
- Reject invalid TOML table shapes and booleans passed as numeric settings. Reject a second core owner rather than silently queuing another daemon.
- Replace custom JSON escaping with JSON-GLib already used by the core. Ignore build/Python cache output. Remove selection-function-only assertions now covered by actual admission tests.

**Dissent:** The strongest argument for the draft is that its small persisted core makes synthetic behavior independent of calendars. Contrary evidence was the green smoke suite alongside reproducible memory/policy/result bugs. Functional pre-mortem: an action appears completed but its window/state disagrees. Adoption pre-mortem: another client silently changes selected calendars. Opportunity-cost pre-mortem: duplicate lifecycle code grows around EDS rather than reusing native state. The corrected boundary is plausible, but only a foundation; group 2 must integrate provider ownership and live rendering rather than layering a second fake scheduler onto native alarms.

**Decision:** ADJUST to the fixes above, then merge into the integration branch. No public interface stability is promised yet. Confidence is medium; cross-desktop/rendering evidence is still pending.

**Verification:** `make check` passes lifecycle and new policy/action-revision regressions; isolated review probes changed from reproducing both policy/result bugs to passing. A sanitized core startup no longer reports the demonstrated use-after-free. Live UI button/render behavior is not yet claimed.

### Draft #13 — native EDS delivery, live watch and playback

**Review:** ADJUST, then merge as the first part of group 2. Native scheduling is opt-in; the core receives selected EDS alarms and delegates their actions. The shared GTK watch uses persisted state, retains meeting links and exact event-start snooze. A second sequential draft must finish fallback reconciliation, policy application and output copies before packaging.

**Corrections from coordinator review:** Audio polling initially replayed one-shot playback and defeated stop; admission now starts playback and content updates preserve suppression. Expired submissions stay silent. Native snoozes are excluded from local deadline authority; unresolved native actions fail rather than silently changing only SQLite. Configuration defaults are normalized. Playback shutdown checks now wait for a fresh child PID rather than passing against a stale PID. Legacy renderer startup requires an explicit mode. The frozen draft still returned a malformed `Get` tuple: a real D-Bus call timed out with expected `(sssstt)`, got `(ssstt)`. Corrected core and frontend signatures and added a real API call regression. Report unsuccessful playback subprocesses as failures rather than treating process exit as successful audio.

**Dissent:** Reusing EDS ownership avoids a second native scheduler, but a green headless suite did not establish a working renderer protocol. Functional pre-mortem: the UI cannot fetch an active event. Adoption pre-mortem: failed links/actions vanish without resolution. Opportunity-cost pre-mortem: retaining the old scanner alongside a new core grows two implementations. The first failure was reproduced and fixed; the renderer remains visually unverified, and the old scanner must be removed in the next group-2 draft. The weakest assumption is restoration and reconciliation of native state after asynchronous EDS startup; the next draft must exercise that boundary rather than count pointer lookup failure as complete recovery.

**Verification:** `make check` passes after the tuple fix, including native-owned persisted snooze exclusion, unavailable-native-action failure, exact timestamp snooze, playback repetition/suppression and shutdown with stub processes. A sanitized isolated startup with minimal config reports no demonstrated AddressSanitizer/UndefinedBehaviorSanitizer error. No live calendar, actual sound or compositor display was exercised. Actual EDS native trigger/action delivery and each target compositor remain human/session verification boundaries.

**Decision:** Continue with group 2b; no epic issue is closed from this intermediate draft. Current selection reload does not yet reconcile existing native records or enable/disable the watcher; that is required follow-up before the test branch is handed off.
