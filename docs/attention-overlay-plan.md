# Reusable attention overlay: epic planning

Planning date: 2026-10-15. This document records the user's requested epic and top-level issue scope. It does not report implementation as complete.

## Aim

**Aim:** Users notice selected imminent commitments and requests for action, then deliberately join/open, snooze or dismiss them while retaining control over unrelated interruptions.

**Why it matters:** Small or hidden reminders have led to missed commitments; confusing actions and a previous global notification takeover made attempted fixes frustrating.

**Current State:** The user approved the large COSMIC UI and its explicit controls. The C prototype is coupled to EDS; portability, TOML selection, generic inputs and audio are not delivered features.

**Desired State:** Selected reminders are noticeable and actionable in supported desktop sessions, optional audio stops when the user acts, and source/calendar choices predict which alerts interrupt.

### Mechanism

**Change:** Preserve the accepted large UI while establishing source eligibility, alert/action state, display support and optional audible notice.

**Hypothesis:** A deliberate, persistent interruption with visible actions reduces missed commitments when admission is selective and actions are reliable.

**Assumptions:** The accepted visual interaction remains appropriate with realistic content; native providers can supply much of the scheduling; a small reusable boundary can accommodate sources without erasing their semantics; cross-desktop presentation must be demonstrated.

**Misunderstanding Signal:** The work delivers a general notification replacement, a new calendar sync product, or many integrations while the user still cannot reliably notice and act on a selected reminder.

### Feedback

**Signal:** Representative synthetic and isolated calendar scenarios show the right reminder at the useful moment, each button produces its documented result, and no unrelated desktop alert changes route or appearance. Subsequent user use confirms the alerts help rather than become routinely dismissed noise; no baseline percentage reduction is claimed.

**Timeframe:** During each capability's acceptance scenarios, again after installation, then in user use before broad support claims.

### Guardrails

- Retain huge overlays and visible Open, Dismiss and Snooze controls; preserve the accepted UI.
- Ordinary notifications keep their existing daemon and appearance.
- Source/calendar eligibility is explicit; malformed configuration must not expand it.
- Sound is optional, configurable and stopped by the alert/action lifecycle.
- Reuse native account/scheduler behavior; no new sync service, mail client or push server.
- Demonstrate display support in real target sessions before claiming it. UI work uses synthetic alerts; a production calendar event is not required to test a display.

## Problem Space

Canonical terrain, constraints and fact-check evidence: [attention-overlay-research.md](attention-overlay-research.md#problem-space). That audit remains intact. This plan adds the user's explicit audible-alert requirement and request to create the epic and issues.

The binding uncertainty is presentation across target desktops. Provider-native state, explicit eligibility, reliable actions, service migration and diagnostics are separate capabilities; an overlay screenshot cannot establish them.

## Problem Weave

**Current frame:** Users need their chosen commitments and requests to become noticeable and resolvable without broadening unrelated interruptions. The accepted COSMIC UI is an observed starting point; cross-desktop/source support is a target, and sound/TOML/CLI are proposed capabilities. New account sync, mail integration, a general notification takeover and implementing this roadmap during this planning turn are outside scope.

**Layers used:** Interaction/outcome protects agency and action meanings. Source interoperability protects selection and scheduler ownership. Display/operations protects observed portability, recovery and installed-state integrity. Each lens was asked not to assume that another lens would close its gaps.

**Independence:** Three parallel agents received the same contract and only their own lens, with no sibling outputs. The coordinator normalized their candidate trees and made the bounded planning selection below. Provenance IDs U, S and D refer to those independent passes.

**Status:** Selected top-level capability work with explicit discovery gates; detailed public APIs and conditional fallback implementations are not preselected.

**Inputs:** Aim above; existing Problem Space and source audit; user approval of large overlay/buttons; requirements for selection, TOML, optional audio and reuse; current source/installer inspection.

### Shared Core

All three frames require deliberate eligibility, meaningful actions and evidence that the user can notice the intended alert. A stronger interruption makes wrong selection, stale state and failed actions more consequential. Scheduling, interaction and compositor behavior must be assessed separately.

### Distinct Problem Statements

1. **Interaction:** Users need to understand and resolve selected commitments, but appearance approval does not establish action completion, concurrent-alert behavior or optional audio lifecycle.
2. **Sources:** Users need chosen calendars and opt-in producers to feed coherent reminders, but discovery, identity, selection and scheduler authority differ across providers.
3. **Display/operations:** Users need presentation above existing fullscreen applications and continuity across session disruptions, but protocol availability and source correctness do not establish live desktop behavior or safe migration of installed services.

### Normalization and Provenance

Independent tactics began as candidate or deferred. These excerpts preserve the original framing rather than implying cross-agent consensus on an implementation.

| Provenance | Original enabling move | Normalized steps | Decision |
|---|---|---|---|
| U1; S1, S4 | “Define explicit enrollment and selection semantics”; “Establish user-readable source/calendar discovery and selection behavior” | R1.1 | Merge policy/discovery into one bounded configuration issue; preserve native/fallback enforcement as a dependency. |
| U3, U4; S3 | “Define distinct join/open, snooze, and dismiss meanings”; “Establish a source-neutral alert/action contract” | R1.2, R2.1 | Keep user completion and source semantics distinct because their tests differ; one contract issue and runtime evidence jointly cover them. |
| U6 | “Define audio opt-in, start, repetition/duration, and stop semantics” | R1.3 | Retain as its own capability, not a display styling task. |
| S2, S5; D4 | “Distinguish native-alarm sources, occurrence feeds and immediate producers”; “Preserve trustworthy alert lifecycle through session disruptions” | R2.2, R3.3 | Link, do not merge: provider-native correctness and local durable state require different evidence. |
| S6 | “Conduct a bounded capability comparison of Akonadi/native alarms, local occurrence feeds and producer hooks” | R2.4; R2.5 | Select discovery; defer full Akonadi implementation until evidence supports it. |
| S7 | “Explore a minimal explicit producer interaction” (initially deferred) | R2.3 | Select only a bounded local CLI/lifecycle capability, justified by user-requested reuse and synthetic input; keep new producer ecosystems deferred. |
| D1, D2, D3; U2 | “Run a bounded presentation discovery matrix”; “Preserve UI reuse where it meets the behavior contract” | R3.1, R3.2, R3.2a, R3.2b | Separate common layer-shell validation from the GNOME experiment. A GNOME renderer remains conditional. |
| D5, D6 | “Make installation, migration and removal preserve the user's desktop”; “distinguish eligibility, trigger, presentation and action failures” | R3.4 | Combine release-facing operations and diagnostic evidence; runtime instrumentation remains a dependency. |
| D7 | “Explore additional packaging formats or distro integration” | R3.5 | Defer until the existing user-service path is reproducible. |

### Interwoven S&T Steps

Final dispositions below come from the explicit Solution Space selection later in this document. Necessity describes the needed capability; it does not prove a particular library or transport is indispensable. Every step also has an invalidation signal in the following table.

| ID | Parent | Depth | Layer | Strategy (what/why) | Tactic (how) | Disposition | N | PA | Owner | Review trigger | Evidence | Links |
|---|---|---:|---|---|---|---|---|---|---|---|---|---|
| R0 | — | 0 | Shared | Notice and resolve chosen commitments with controlled interruption | Deliver a bounded reusable attention capability and evidence of the behavior | selected | Root aim | Accepted COSMIC UI is a viable starting point | Project maintainer; Muness owns product policy | Acceptance or invalidation evidence | User requirement and observed UI acceptance | G0 |
| R1 | R0 | 1 | Interaction | Preserve user control and useful notice | Establish eligibility, action meanings and optional sound | selected | Visibility without control is insufficient | Current UI can express these behaviors | Interaction maintainer | Noise or ambiguous-action feedback | U0–U6 | G1 |
| R1.1 | R1 | 2 | Interaction + sources | Make interruption scope predictable | Validated TOML, readable discovery and calendar/source selection | selected | Wrong eligibility defeats the aim | Registries expose useful identities and names | Configuration maintainer | Default/identity changes | U1; S1/S4 | G1; depends R2.1 |
| R1.2 | R1 | 2 | Interaction | Make each button's consequence understandable | Preserve accepted UI; define and demonstrate success/failure and competing-alert behavior | selected | A click alone is not action completion | Shared outcome vocabulary is plausible | Interaction + core maintainers | Source semantics or action failures change | U2–U5 | G1; complements R2.1/R3.3 |
| R1.3 | R1 | 2 | Interaction | Add audible notice while retaining control | Optional playback, repetition and stop behavior integrated with alert state | selected | Required capability when the user chooses audio | Existing playback facilities can supplement visuals | Audio maintainer | Playback leak or competing alerts | U6; explicit user audio request | G1; depends R1.1/R2.1/R3.3 |
| R2 | R0 | 1 | Sources | Reuse useful producers without rebuilding sync | Establish a semantic source boundary and incremental input paths | selected | Calendar/provider differences otherwise spread through UI | Native scheduler capabilities and process helpers are available | Source maintainer | A provider needs incompatible action semantics | S1–S7 | G2 |
| R2.1 | R2 | 2 | Sources | Preserve identity and scheduling/action ownership | Define a common internal alert lifecycle and provider capabilities | selected | Prevent stale/duplicate or misdirected actions | Normalize semantics without claiming identical backend APIs | Core/API maintainer | Before exposing CLI/API | S2/S3; U3/U4 | G2; enables R1.1/R2.2/R2.3/R3.3 |
| R2.2 | R2 | 2 | Sources | Preserve working selected EDS calendars | Isolate the EDS adapter; reconcile native and fallback changes | selected | Existing sources must survive refactoring | EDS already provides native persisted alarm state | EDS maintainer | Native/fallback overlap or source reconnect | S2/S5; inspected source | G2; depends R2.1/R1.1 |
| R2.3 | R2 | 2 | Sources | Let a selected local producer request and resolve attention | Bounded local CLI/JSON input and observable action results | selected | Reuse and synthetic input need a calendar-independent boundary | Scripts and existing tools can use a small client | CLI maintainer | Before stabilizing external interface | S7; user reuse request; audit | G2; depends R2.1/R1.1/R3.3 |
| R2.4 | R2 | 2 | Sources + evidence | Determine a credible native KDE calendar path | Bounded Akonadi capability comparison and read-only evidence | selected | KDE display support does not answer calendar access | A provider-specific helper may preserve configured accounts | Interoperability investigator | Capability matrix result | S6; unverified interoperability | G2; depends R2.1 |
| R2.5 | R2 | 2 | Sources | Provide production native Akonadi integration if justified | Implement the path selected after R2.4 | deferred | Conditional on a demonstrated need and feasible ownership | Unknown until discovery | Calendar maintainer | R2.4 decision | Assumed | Conditional extension to G2 |
| R3 | R0 | 1 | Display + operations | Make attention available and honestly supportable | Establish presentation, continuity and installed-state evidence | selected | A correct model cannot help if it never appears | Existing prototype and components support bounded validation | Runtime/display maintainers | Compatibility or recovery failure | D1–D6 | G3 |
| R3.1 | R3 | 2 | Display | Preserve huge actionable presentation on layer-shell desktops | Shared GTK display boundary; actual COSMIC/Hyprland/KDE/output validation | selected | Compositor behavior decides visibility | Protocol/source support justifies experiments | Display maintainer | Desktop/library/topology changes | D1/D2/D3; U2 | G3; depends R2.1 |
| R3.2 | R3 | 2 | Display + evidence | Establish a supported GNOME attention path | Test shared UI first, then select a different renderer only if needed | selected | GNOME lacks the current protocol path | Inspected Sane Break pattern is a useful hypothesis | GNOME maintainer | Experiment go/no-go | D1/D2; hypothesis | G3/GN |
| R3.2a | R3.2 | 3 | Display + evidence | Determine whether one UI suffices | Bounded GNOME window-helper experiment above existing fullscreen apps | selected | Resolves the shared-UI assumption | Upstream demonstrates window manipulation with Qt | GNOME investigator | First fullscreen/focus result | D2; source precedent, GTK untested | GN; depends R2.1 |
| R3.2b | R3.2 | 3 | Display | Preserve GNOME behavior if the helper cannot meet it | Evaluate/select a Shell actor renderer after experiment failure | deferred | Conditional fallback, not current evidence | Shell actors provide a distinct implementation route | GNOME maintainer | R3.2a fails | Candidate precedent | GN conditional alternative |
| R3.3 | R3 | 2 | Runtime | Keep decisions coherent across time and concurrency | Persistent locally owned state; queue/update/cancel and recovery policy | selected | In-memory timers do not establish restart continuity | Native state can stay with providers | Runtime maintainer | Lost/stale/duplicate alert | D4; U5; S2/S5 | G3; depends R2.1 |
| R3.4 | R3 | 2 | Operations | Make installed behavior repeatable and diagnosable | User-service migration, one daemon, synthetic demo and compatibility report | selected | Source correctness is not installed-state integrity | Existing installer is small enough to reconcile | Packaging maintainer | Before installation/support claims ship | D5/D6; inspected installer | G3; depends capability acceptance |
| R3.5 | R3 | 2 | Operations | Broaden distribution reach when necessary | Additional package formats/platform distribution | deferred | Only necessary if current audience cannot install | Demand is not yet established | Packaging maintainer | Validated core or blocked testers | D7 | Conditional extension to G3 |

### Sufficiency Groups

| Group | Parent strategy | Child steps | Mode | Collective coverage claim | Gap/deferred branch |
|---|---|---|---|---|---|
| G0 | R0 | R1, R2, R3 | all required | Deliberate interaction, usable inputs and dependable availability jointly address the aim | User impact after release still needs observation |
| G1 | R1 | R1.1, R1.2, R1.3 | all required capabilities; sound enabled by user choice | Predictable eligibility, meaningful resolution and controlled optional audio cover initial interaction scope | Presenting/quiet behavior needs documented source-specific policy during config work |
| G2 | R2 | R2.1, R2.2, R2.3, R2.4 | all required for initial boundary and native-KDE decision | Shared semantics, working EDS, bounded generic input and an honest KDE integration decision permit incremental reuse | R2.5 production Akonadi adapter deferred; the discovery issue cannot advertise support |
| G3 | R3 | R3.1, R3.2, R3.3, R3.4 | all required | Observed presentation, continuity and controlled installed state support the claimed environments | GNOME passes only if its selected path actually works; a failed experiment alone does not complete R3.2 |
| GN | R3.2 | R3.2a, R3.2b | conditional alternatives | Test helper first. If it passes, it may supply GNOME support; if it fails, select a new implementation before claiming coverage | Actor renderer is deferred, not secretly included as a selected architecture |

### Overlap and Complement

- **Overlap:** U1 and S1/S4 share predictable eligibility; R1.1 merges them with provenance.
- **Complement:** R1.2 defines what user success means; R2.1 defines the source/action exchange; R3.3 makes it survive concurrency and restart. They remain distinct because passing one does not imply the others.
- **Dependency:** Calendar selection applies to EDS native and fallback paths. Config acceptance depends on real provider evidence, not only parsing a TOML example.
- **Dependency:** Audio uses logical alert state; output mirroring must not create multiple sound loops.
- **Dependency:** R3.4 consumes compatibility and lifecycle evidence rather than inferring installation correctness from a successful compile.
- **Gap:** Native KDE calendar semantics and the GTK GNOME helper remain evidence gaps. R2.4 and R3.2a are bounded checks; neither is answered by protocol names.

### Opposition and Uncertainty

| Tension | Type | Decision or check | Owner |
|---|---|---|---|
| Attention versus unwanted interruption | Product trade-off | Explicit selection; no default escalation of ordinary notifications; record presenting/quiet policy | Muness/product policy; configuration maintainer implements |
| Consistent buttons versus native provider truth | Semantic trade-off | Capability-aware actions and explicit scheduling ownership | Core + source maintainer |
| One UI versus reliable GNOME visibility | Evidence dispute | Existing-fullscreen/action test of the GTK helper; renderer alternative only if it fails | GNOME maintainer |
| Recovery versus replaying obsolete alerts | Policy trade-off | State-dependent expiry/catch-up acceptance scenarios | Runtime + product policy owner |
| Generic reuse versus becoming a notification platform | Scope | Bounded local CLI and example bridges; new servers/plugins remain deferred | Project maintainer |
| KDE presentation versus native Akonadi access | Scope distinction | Separate R3.1 display evidence from R2.4 calendar capability discovery | Display + source maintainers |

### Recommended Working Frame

Users need a selected, actionable interruption they can trust across their desktop session. Preserve control over admission, clear action consequences and honest support evidence while reusing existing source scheduling. This framing allows a shared UI and adapters, existing producers, or a future complete replacement if evidence supports it.

### Alternate Frames Worth Keeping

- A complete existing alarm application could meet the aim with less maintenance. No inspected replacement has established all required behavior; reassess if one passes the same scenarios.
- A calendar-only utility would be smaller. Keep generic input bounded so it does not consume the work needed for reliable calendar reminders.
- A standard notification receiver/proxy would broaden application compatibility but reintroduce the rejected daemon-wide blast radius; it is outside the accepted scope.

### Evidence and Invalidation

| Step | Need to learn / invalidation signal |
|---|---|
| R0, R1 | Users routinely dismiss irrelevant alerts or still miss chosen commitments: revisit eligibility and the working frame |
| R1.1 | Reconnect/default/reload unexpectedly expands selection: scope handling is wrong |
| R1.2, R2.1 | An apparently completed action silently fails or affects a different alert: contract is inadequate |
| R1.3 | Sound plays while disabled or continues after the defined stop event: audio lifecycle fails |
| R2, R2.2 | Source integration requires a new account/sync engine or duplicates native scheduling: revisit adapter ownership |
| R2.3 | Producer cannot withdraw a stale alert or distinguish input acceptance from a button action: CLI boundary is insufficient |
| R2.4, R2.5 | No honest native action path exists without disproportionate reimplementation: defer native provider support and state the limitation |
| R3, R3.1 | Protocol-compatible session cannot show clickable UI above existing fullscreen apps: advertised compatibility is false |
| R3.2, R3.2a, R3.2b | Helper cannot retain visibility/focus/cleanup: trigger alternative selection, not weaker acceptance criteria |
| R3.3 | Restart loses a snooze or replays a dismissed/canceled alert: continuity is not achieved |
| R3.4 | Upgrade leaves competing old/new units or changes normal notification ownership: installation boundary is wrong |
| R3.5 | Intended testers cannot install the existing path: revisit distribution deferral |

### Ready for Next Phase?

Yes for bounded planning selection and the top-level issues requested by the user. GNOME implementation choice and a production Akonadi adapter remain gated; issue creation does not decide those architectures.

### Next Route

Begin with the alert/action contract and independent display experiments. Once the contract exists, configuration and runtime work unlock source integration, CLI and audio. Consolidate evidence through installation/migration acceptance.

### Wiring Follow-up

**Offer:** Durable outcome-family records remain candidates in this repository; no external `$record` registry is configured and none was created.

**Plan gate:** The user explicitly requested GitHub epic/issues. The Solution Space selection below chooses only bounded tactics and discovery checks for issue creation. `/oh-plan` is not installed; the coordinator performs the requested planning write directly while preserving lineage, dependencies and acceptance signals.

| ID | Type | Artifact | S&T step | Parent step | Disposition | Owner | Record route | Planning route |
|---|---|---|---|---|---|---|---|---|
| W0 | outcome | Users notice and deliberately resolve selected commitments | R0 | — | candidate record | Muness/project maintainer | Candidate canonical parent outcome | Epic |
| W1 | objective | Preserve useful notice and user control | R1 | R0 | candidate record | Interaction maintainer | Candidate objective under W0 | Config, actions and audio issues |
| W2 | capability | Coherent source inputs and returned actions | R2 | R0 | candidate record | Source maintainer | Candidate capability under W0 | Contract, EDS, CLI and KDE discovery |
| W3 | capability | Observed presentation and session continuity | R3 | R0 | candidate record | Display/runtime maintainers | Candidate capability under W0 | Display, recovery and install issues |
| W4 | signal | Synthetic/isolated scenarios demonstrate eligibility, notice and action results | R0/R3 | R0 | candidate record | Compatibility maintainer | Candidate signal | Acceptance evidence across issues |
| W5 | guardrail | Ordinary notification delivery remains independent | R0 | — | candidate record | Project maintainer | Candidate guardrail | Epic and install/CLI acceptance |
| W6 | metis | Provider-native state, displayed UI and installed service integrity require different evidence | R2/R3 | R0 | candidate record | Project maintainer | Candidate metis | Research and this synthesis |

## Solution Space

### Solution Space Analysis

**Problem:** Make chosen commitments noticeable and resolvable across desktop sessions without capturing unrelated notifications.

**Key Constraint:** The accepted huge overlay and explicit actions are required; a general notification takeover is rejected.

**Working Story:** Preserve the existing UI, reuse native source scheduling and introduce only the interfaces and local state needed for selected sources and honest portability.

**Success Signal:** Representative alerts are selected, displayed and acted on reliably; sound and copies stop correctly; ordinary notifications and accounts remain intact.

**Decision Criteria:** Meets accepted user behavior; reuses proven state/sync; scopes uncertainty into checks; minimizes duplicate UI and dependency burden; produces evidence of installed behavior.

**Critical Assumptions:** EDS remains the working first source; shared alert semantics are possible without erasing native capabilities; GNOME needs an experiment; the user prefers intentional input scope over maximizing notification compatibility.

### Candidates Considered

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A | Local optimum | Keep the current COSMIC/EDS prototype with small additions | Fast local progress; leaves input coupling, continuity and other desktop claims unresolved |
| B | Different frame | Adopt an existing complete alarm/break application | Potentially less maintenance; inspected evidence does not establish the required calendar/actions/overlay combination |
| C | Reframe | Shared accepted UI, semantic source boundary, bounded local CLI and display experiments | Selected for planning; requires a small owned lifecycle and honest capability gates |
| D | Redesign | New account/sync platform plus a replacement notification server | Expands scope and repeats the rejected global notification blast radius |

### Interpretive Variety Check

A assumes the personal COSMIC utility is enough. B tests whether owning the tool is unnecessary. C separates attention from existing producers. D would change the product into a larger platform and violates accepted scope. If C needs broad sync or notification-proxy work, revisit B or the calendar-only frame rather than silently expanding D.

### Risk Retirement Plan

These are planned checks, not claims that risk has already been retired.

| Risk / alternate frame | Planned disposition | Tempting patch this must fail | Evidence or rationale | Stop/pivot if |
|---|---|---|---|---|
| Shared UI fails on GNOME | Retired by evidence — pending R3.2a | A keep-above/fullscreen request counted as success | Existing-fullscreen app, actual button input, workspace and two-output checks | Helper cannot meet behavior; select a renderer alternative |
| Calendar selection is cosmetic | Retired by evidence — pending R1.1/R2.2 | Filter only the scan, leaving native callbacks unfiltered | Same selected/unselected fixtures exercise both native and fallback paths | Eligibility differs by path |
| Two scheduling authorities duplicate one event | Retired by evidence — pending R2.2/R3.3 | Leave fallback timers running after native alarm/source changes | Change/cancel/native-alarm transition and restart scenarios | Duplicate or stale recurrence appears |
| CLI grows into a platform | Accepted with rationale | N/A | Bounded local interface and disabled example transports satisfy current reuse; no plugin/server work selected | A producer requires new cloud/account/sync infrastructure |
| Audio or output copies outlive an action | Retired by evidence — pending R1.3/R3.3 | Close only the visible window | Exercise all stop paths across simultaneous alerts and display copies | Sound/window remains for a resolved logical alert |
| Installation hides duplicate daemons | Retired by evidence — pending R3.4 | Rename files without checking installed units | Fresh and legacy-unit upgrade checks plus normal-notification continuity | Old and new services compete or notification ownership changes |
| Existing complete replacement becomes viable | Triggered | N/A | Keep the same acceptance scenarios for an alternative | A maintained candidate demonstrates all required behavior with less owned work |

### Recommendation

**Selected:** Option C for bounded capability planning. This selection does not freeze a public schema, audio library, IPC transport or GNOME renderer implementation.

**Accepted trade-offs:** EDS is the first real source; production native Akonadi support follows discovery. Generic input stays local and explicit. Prefer one UI, but require behavior evidence. Retain the current installation approach before expanding packaging formats.

### S&T Selection

| Steps | Disposition | Parent | Sufficiency group | Why | Owner | Review trigger |
|---|---|---|---|---|---|---|
| R0 | selected | — | G0 | Existing user aim | Project maintainer | User outcome evidence |
| R1, R2, R3 | selected | R0 | G0 | Complementary required outcomes | Respective maintainers | Any core branch invalidated |
| R1.1, R1.2, R1.3 | selected | R1 | G1 | Selection, action completion and optional audio requested | Configuration/interaction/audio maintainers | Scope or interaction failure |
| R2.1, R2.2, R2.3, R2.4 | selected | R2 | G2 | Shared contract, first source, bounded input and native-KDE decision | Core/source maintainers | Capability and source semantics evidence |
| R2.5 | deferred | R2 | Conditional G2 extension | Native implementation needs R2.4 evidence | Calendar maintainer | Discovery go/no-go |
| R3.1, R3.2, R3.3, R3.4 | selected | R3 | G3 | Presentation, GNOME gate, continuity and installation | Display/runtime/packaging maintainers | Acceptance evidence or failure |
| R3.2a | selected | R3.2 | GN | Bounded experiment chooses next display decision | GNOME investigator | Experiment result |
| R3.2b | deferred | R3.2 | GN | Renderer alternative only after the helper decision | GNOME maintainer | Helper failure |
| R3.5 | deferred | R3 | Conditional G3 extension | Broad packaging not needed for current planning | Packaging maintainer | Existing install path blocks testers |

**Selected-step sufficiency:** Ten top-level issues cover the selected leaf tactics, with action semantics covered jointly by the contract and runtime issues. Native Akonadi implementation and the GNOME renderer alternative are explicit gaps/gates rather than hidden commitments. The epic cannot claim GNOME support from a failed experiment or production Akonadi support from a discovery report.

### Execution Handoff

- Preserve the Aim and guardrails, provider ownership and accepted interaction.
- Verify with synthetic UI/CLI inputs and isolated calendar fixtures before requiring real calendar events.
- Prioritize the contract and independent display experiments, then configuration/runtime and source/CLI/audio work, then consolidated installation evidence.
- Reassess at the named review triggers; do not weaken fullscreen/action criteria to make a display experiment pass.
- Human session verification is needed where a target desktop or output setup is unavailable. Record that gap; do not mark it validated.

## GitHub Plan

The epic and issue links are recorded below after creation. Each issue contains its parent objective, canonical S&T step and sufficiency group, concrete acceptance signals, dependencies, owner role and review trigger. The machine-readable [issue index](planning/issue-map.json) preserves those links.

<!-- github-plan-links -->

**Epic:** [#1 — Epic: Reusable fullscreen reminders with selected sources, actions and sound](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/1)

| Issue | Selected step | Parent / group | Dependencies |
|---|---|---|---|
| [#2 — Define a shared alert lifecycle and actionable UI contract](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/2) | R2.1 | R2 / G2 | None |
| [#3 — Add validated TOML configuration and explicit source/calendar selection](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/3) | R1.1 | R1 / G1 | #2 |
| [#4 — Make alert state reliable across concurrency, restart and suspend/resume](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/4) | R3.3 | R3 / G3 | #2 |
| [#5 — Isolate the EDS source adapter and reconcile native and fallback reminders](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/5) | R2.2 | R2 / G2 | #2, #3 |
| [#6 — Expose an opt-in local CLI/JSON interface for external alert producers](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/6) | R2.3 | R2 / G2 | #2, #3, #4 |
| [#7 — Support the shared large overlay on COSMIC, Hyprland and KDE Wayland](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/7) | R3.1 | R3 / G3 | #2 |
| [#8 — Establish GNOME Wayland display support with a shared-UI feasibility gate](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/8) | R3.2 | R3 / G3 | #2 |
| [#9 — Add configurable audible alerts tied to the visible reminder lifecycle](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/9) | R1.3 | R1 / G1 | #2, #3, #4 |
| [#10 — Discovery: establish the native KDE/Akonadi calendar adapter boundary](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/10) | R2.4 | R2 / G2 | #2 |
| [#11 — Ship repeatable user-service installation, migration and diagnostics](https://github.com/muness/cosmic-calendar-reminder-overlay/issues/11) | R3.4 | R3 / G3 | #3, #5, #6, #7, #4, #9 |
