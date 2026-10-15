# Epic execution and integration review

## Execute

**Task:** Execute epic #1 using Luna workers. Related issues are grouped into draft PRs; the coordinator reviews and dissents on every draft, corrects alignment/complexity/useless tests, then merges accepted work only into `codex/attention-overlay-integration` for user testing.

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

### Execution update — independent packaging and design review

Group 2 was split into native delivery/audio (#13) and reconciliation/display (next draft) to keep review coherent. When the remaining group-2 estimate reached 60–90 minutes, a second Luna worker started independent packaging/docs in a separate worktree. Source/config/Makefile/core tests remain exclusively owned by the first worker; install/service/docs/install tests by the second. Reviews and merges stay sequential. This replaces the initial one-worker scheduling choice with bounded, non-overlapping work rather than parallelizing individual issues.

The user added one Impeccable critique pass, one polish pass, execution of every suggestion, followed by one onboarding pass and execution of every suggestion. Preserve the accepted huge overlay and visible buttons. Critique ran against frozen integration `46cc2e6`, using native source review plus a COSMIC capture and user observations; a web-markup detector is inapplicable. The initial result is 21/40, with C1–C5: full-output centered presentation, safe keyboard arrival, inline accessible failure/retry, action grouping/clear labels, and constrained-output/long-content/disabled-state handling. These are assigned to the UI worker. The snapshot is under `.impeccable/critique/` in the primary checkout.

The capture demonstrated a real failure despite available Wayland layer-shell: the surface occupied only an upper-left portion of the output, with gray outer margins. The user also reported typing resolved the mock. Do not treat capability detection as passing placement or keyboard evidence. Temporary mock processes were stopped; further presentation is reserved for one bounded confirmation after fixes. No live calendar event or actual audio was used.

**Polish — one pass:** Source assessment found P1–P6: unattached centered container; unretained CSS provider/unused backend selection; asynchronous callback lifetime and mirrored busy/error states; duplicate Open handoffs before the pending guard; deliberate keyboard arrival/focus; and complete semantic/disabled/time/hotplug states. All were assigned and implementation was reported complete before onboarding. A native GTK CSS parser probe against the actual policy application passed after replacing unsupported web `max-width` properties with GTK layout APIs. The critique storage helper returned exit 2 because changed source automatically closed the old snapshot backlog; the finite C1–C5 list remains tracked here rather than being silently discarded. No second critique or polish pass is planned.

**Onboarding — one pass:** First value is the real overlay/buttons before calendar enrollment, without a mandatory wizard. Execute all O1–O4: explicit repeatable fresh-ID preview, complete copy-ready calendar keys/TOML guidance, a short preview → selection → validation → activation → diagnostics setup path, and actionable disabled/empty/stale/display failure states. Packaging owns the preview/docs/diagnostics work; the source worker owns calendar-list output. No preview or audio runs automatically during installation. Implementation and final checks remain pending.

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

### Draft #15 — live display, policy, audio and design execution

**Review:** ADJUST, then merge as display/policy integration. The GTK-owned legacy scheduler is removed. Runtime ownership/source/calendar metadata, per-calendar audio/snooze settings, output selection/copies/hotplug, policy reload withdrawal, visible retry states and the complete critique/polish implementation are included. EDS snapshots/fallback/recurrence reconciliation and asynchronous native restoration remain explicitly required in the next source draft.

**Coordinator corrections:** Playback still started on headless admission and `SetForeground` undid mute whenever `audio_suppressed` was true. Require an explicit foreground ID, preserve suppression for repeated foreground publication, clear foreground on resolution, and avoid muting it when a different hidden alert is canceled. Stop old playback settings on policy reload without rearming the current alert. Preserve native owner during group-2a database migration, and reject producer replacement of provider-owned rows. Guard failure to generate the default tone and release diagnostic JSON nodes. Tests now exercise actual foreground protocol rather than relying on invisible admission to make sound.

**Dissent:** A shared renderer and core reduce duplicate scheduling, but more configuration is not proof it is applied safely. Contrary evidence: unformatted JSON compared with compact JSON recreated windows every poll; center content was unattached; a second timeout duplicated GIO's action deadline; and the muted foreground restarted sound. The worker corrected the first three after direct coordinator reproduction/review; coordinator fixed the last and admission/ownership gaps. Functional pre-mortem: visible controls point at dead windows or an invisible alert makes noise. Adoption pre-mortem: typing resolves an arriving reminder or repeated demo never appears. Opportunity-cost pre-mortem: a growing UI duplicates source logic; removing the old scanner and reusing GIO deadlines narrows that risk. Snapshot freshness remains unimplemented rather than fabricated as a successful scan.

**Verification:** `make check` passes core lifecycle/admission/migration, real CLI URI handoff with an isolated handler, unchanged-policy formatting, and per-calendar silence/volume/default-WAV checks using stub playback. The core lifecycle suite also passes with AddressSanitizer and UndefinedBehaviorSanitizer (leak detection disabled; no leak-safety claim). A native CSS policy probe passes without a window. Final visual placement/typing confirmation remains pending; only the earlier failed placement/typing observation is established. No live calendar or actual sound was used.

**Decision:** Continue into the source reconciliation draft. Keep epic/source/desktop acceptance open until its checks and the user test boundary are accounted for.

## 2026-10-15 — README/install onboarding and whole-path polish

One onboarding pass and one polish pass, preserving the locally accepted huge overlay and its visible buttons. Read mode for README and Operate mode for installer/CLI. Existing session context and accepted live overlay supplied incumbent evidence; no new visual mock loop.

Implemented a preview-first README, concrete selection TOML, account prerequisites, activation success criteria, script example, reload/stop/update/uninstall paths, and a troubleshooting table. Installer now checks tools, development libraries, and Python bindings before changing installed files, and reports the next first-run action. Diagnostics summarizes alerts by default; --verbose explicitly exposes event contents/journal warnings. Source status includes registry activity and readable local sync times. Compatibility now records the actual installed COSMIC acceptance without extending it to other desktops.

Polish found explicit CLI presentation intent was coupled to audio's foreground ID and lost on snooze/idle. Keep a session-local set of actually presented alert IDs until terminal state, so supervised renderer recovery can present them when snooze expires. It does not arm headless submissions or start an overlay without an explicit renderer path.

Verification: installer smoke passed (temporary homes, fake systemd, no production changes); shellcheck and shell/Python syntax checks passed. A private-bus check with a stub renderer verified a shown CLI alert snoozes, survives renderer exit and foreground clear, and starts a replacement when due. Initial check fixture used a source excluded by its own config; corrected the fixture selection, then passed. No live calendar event was created, no extra visual pass was run. Existing overlay appearance was retained.

Applet investigation: documented a proposed native COSMIC popup, pause semantics, agenda/paused/offline states, and core API gaps in docs/applet-control.md. Official libcosmic panel/template and existing calendar applet reviewed. No applet or global pause capability is claimed implemented; avoid a second provider sync stack.
