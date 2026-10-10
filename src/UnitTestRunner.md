# In-Game Unit Tests

Run `pvz-portable.exe -unittest` with the usual game resources available
(`-resdir` is supported). No clicks are required. Results appear at the top
left and in `unittest.log` beside the executable, overwritten for each run.
After the last case the final scene is frozen for five wall-clock seconds,
then the game shuts down. Exit status is 0 for a completed passing suite,
1 for a failed or interrupted suite.

All profile and registry I/O uses a newly created temporary sandbox, even
if `-savedir` is supplied. The sandbox is removed after game destruction.
Automatic mid-level saving is disabled; explicit serialization tests write
only inside that sandbox. Tests bypass menus, cutscenes, wave spawning,
level progression and board input, but run real game-object updates,
projectile collisions and effects at the normal 10 ms simulation tick.

Add cases to `RegisterLawnTests` in `UnitTestCases.cpp`. Each registry entry
has setup, post-update and optional projectile-created callbacks, plus a
tick timeout, and optional `ticksPerUpdate` (default 1). The two animated
Threepeater cases use 2 ticks per update for 2x playback: all 600 simulation
ticks and per-tick assertions remain, but each case takes about 3 rather than
6 seconds. Other tests and normal gameplay speed are unchanged.
Setup must create a fresh board and reset its case state.
Call `Check` for assertions and `Finish` when observation is complete.
Failures are accumulated and subsequent cases still run serially.
`Finish` only records the result. Case transitions and shutdown run in
`AfterFrame`, called by `LawnApp::UpdateFrames` after widget updates return;
callbacks must not replace the board or shut down from a board update.
Projectile callbacks occur immediately after allocation, before the firing
plant sets motion, so inspect motion in the post-update callback instead.
Use object IDs rather than retaining pointers across deletion queues.

The leftpeater burst case uses one-based row 3 / column 8 for a stationary normal
zombie and row 3 / column 9 for the leftpeater. Current and maximum health
are both 80; normal headless decay is allowed after shot three. It verifies
leftward peas at ticks 0, 16, 32, 48, damage and fourth-shot death, and keeps
observing through tick 140 to reject extra burst shots. It also checks the
shooting animation and launch-counter reset to 120 at tick 48, followed by
the normal decrement to 119 at tick 49, resolving the plant by ID.

The disabled burst case sets the runtime global `ENABLE_LEFTPEATER_PLANTING_BURST`
to false before planting. It checks that no bonus shot or animation is
scheduled, then fixes the random initial attack counter at 150 for a
repeatable normal-attack test. At tick 50 the target must still have 80
health and the counter must be 100. Normal shots must occur at ticks 150
and 175, leaving the target alive with 40 health at tick 200. The switch
defaults to true and is restored after this case.

The Plantern healing cases run with `ENABLE_PLANTERN_HEALING` enabled and
disabled (default true), restoring its prior value afterward. A damaged
Plantern starts with launch counter 100, surrounded by eight damaged
wallnuts, with a pumpkin in its own cell and a wallnut two columns away.
Every simulation tick through 200 checks health and the healing timer:
only the eight neighbors gain 45 HP at ticks 100 and 200, never earlier;
one neighbor starts just 10 HP below maximum to check the cap. Neither the
Plantern, its same-cell pumpkin nor the distant plant is healed. With the
switch disabled, all health and the unused timer remain unchanged.

The green-vase cases use real endless `ScaryPotterPopulate` calls at stages
0, 1, 8 and 9 with `ENABLE_PLANTERN_GREEN_VASE` enabled and disabled (default
true), restoring its prior value afterward. Each population is isolated
by clearing the previous grid items. Assertions check 35 uniquely placed
vases, exactly two green seed vases, the complete original seed inventory,
one valid sun vase, five bucketheads, one jack-in-the-box, and difficulty
counts of `1 + min(stage, 8)` Gargantuars and `8 - min(stage, 8)` normals.
When enabled, the single existing Plantern must be green. When disabled,
its color is deliberately unconstrained: random selection may still make
it green, so the test does not assert a chance outcome.

Synchronous setup-only cases may call `Finish` before returning, as the
vase cases do. Their post-update callback can be null because the runner
skips updates for finished cases; transitions still occur in `AfterFrame`.

## Empowered Peashooter

`ENABLE_PEASHOOTER_EMPOWERED_PEA` in `GameConstants.h` is an independent,
mutable inline bool, default true. Only plants whose actual `mSeedType` is
`SEED_PEASHOOTER` qualify (including an Imitater after it becomes that type).
Outside roguelike runs, each plant counts emitted projectiles modulo three,
not attack attempts or hits. In roguelike runs the Heavy Peas level changes
that divisor to 3, 2 or 1. Empowerment is captured at emission. Disabled shots still
advance cadence; changing the toggle does not alter projectiles in flight.
New/reinitialized plants start at zero, including recycled allocation slots.

An empowered pea draws at 1.5x scale with unchanged collision dimensions,
speed, and targeting. Direct damage is 30 instead of 20. Surviving targets
are displaced opposite their walking direction by 11.525217 pixels; both
Gargantuar and red-eye Giga are immune to knockback. Integer collision
X is synchronized immediately. Dead/dying targets are not displaced.

Torchwood retains the projectile's empowerment, scaling its fire animation
1.5x and direct fire damage from 40 to 60. Fire splash damage and its radius
stay unchanged, and splash-only targets receive no knockback. Snow peas
converted into ordinary peas do not acquire empowerment.

Fire animation scaling uses the nominal local pivot `(40, 40)`: the loaded
38x38 body begins at `(20.8, 19.9)`, near that center, and the original
80-pixel backward mirror correction fixes X=40. Reanimation overlay scaling
acts about the origin, so offsets are `-25 + 40 * (1 - scale)` on both axes;
backward shots additionally add `80 * scale` to X. The world pivot remains
`projectile position + (15, 15)`. At 1.5x, forward offsets are `(-45, -45)`
and backward offsets are `(75, -45)`; 1x offsets remain unchanged.
The fire pea alignment case checks both scales and directions immediately
and after attachment movement, plus the unchanged 30x40 fireball collision
rectangle. It logs loaded resource track metadata for pivot evidence.

### Movement Evidence

Normal `Zombie::PickRandomSpeed` chooses uniformly from 0.23 to 0.37, mean
0.30. However, `UpdateZombieWalking` uses
`GetTrackVelocity("_ground") * mScaleZombie`, not that velocity directly.
`UpdateAnimSpeed` sets animation rate to `mVelX * N / D * 47 / scale`, where
`N` is the walk frame count and `D` its total ground displacement.
`Reanimation::GetTrackVelocity` uses adjacent ground displacement times
`SECONDS_PER_UPDATE` (0.01) times the animation rate. Ordinary `REANIM_LOOP`
samples `N - 1` intervals in `GetFrameTime`. Thus the full-gait mean is:

```text
mean speed = (D / (N - 1)) * (0.30 * N / D * 47) = 0.30 * 47 * N/(N - 1)
N = 47 for both loaded normal walk animations
mean speed = 14.406522 pixels/second
knockback = 80% * 14.406522 = 11.525217 pixels
Gargantuar/Giga knockback = 0 pixels
```

The walking-evidence case samples 10,000 evenly spaced gait phases through
real `UpdateZombieWalking` calls at mean speed. With the current resources,
`anim_walk` measures 14.408191 px/s and `anim_walk2` 14.403781 px/s (float
position rounding and sampling account for the small difference). This is a
fixed normal-zombie baseline, not a target's chilled or instantaneous speed.

### Coverage And Saves

Enabled and disabled integration cases run five staggered normal Peashooters
for 1,050 simulation ticks, observing six real emitted projectiles per plant
and all resulting collisions. They check per-plant third/sixth-shot cadence,
visual-scale selection, unchanged collision sizes, aggregate damage, normal
knockback, both giants' knockback immunity, and real Torchwood conversion.

A setup-only case round-trips cadence and in-flight empowerment through
`LawnSaveGame`/`LawnLoadGame` in the sandbox, resumes the sixth shot, checks
runtime disabling and recycled projectile slots, and excludes Repeater,
Leftpeater, Threepeater, Gatling Pea and Snow Pea. It also hides the new save
fields as unknown TLVs, recomputes the CRC and reloads, verifying absent-field
defaults and unknown-field skipping without losing entities.

Portable saves keep their existing versions and positional tails. Optional
entity field 101 stores the plant counter or projectile flag; older portable
saves default them to zero/false. Older readers skip this optional field,
so loading and resaving there loses empowerment state. Legacy raw ABI loading
retains the original entity sizes and ID offsets, explicitly clearing new
members after copying old bytes (which may include overlapping tail padding).
No user saves or profiles are touched by tests. Historical raw-save fixtures
and pixel-level visual comparisons are not covered by this runner.

### Windows Verification

From the repository root, use the installed VS developer shell and existing
build directory:

```powershell
Test-Path -LiteralPath 'D:\games\PvZ-Portable\build-msvc'
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --build 'D:\games\PvZ-Portable\build-msvc' --parallel 4
if (!(Test-Path -LiteralPath 'D:\games\PvZ-Portable\build-msvc')) { throw 'Missing test directory' }
$p = Start-Process -FilePath 'D:\games\PvZ-Portable\build-msvc\pvz-portable.exe' -ArgumentList '-unittest' -WorkingDirectory 'D:\games\PvZ-Portable\build-msvc' -PassThru
if (!$p.WaitForExit(120000)) { Stop-Process -Id $p.Id; throw 'Unit tests timed out' }
$p.Refresh()
$p.ExitCode
```

Results are in `build-msvc/unittest.log`.

## Threepeater Homing

Homing peas and fire peas render in `RENDER_LAYER_TOP`, above every lawn
row's vases but below fog and UI. This is applied after movement so it
also covers the first homing frame. Ordinary peas and Cattail spikes
retain their original render layers.

`ENABLE_THREEPEATER_HOMING` in `GameConstants.h` is an independent, mutable
inline bool, default true. When disabled, empty-lane peas keep their original
straight fan-out path and original row-based render layer; emission and the
normal attack trigger are otherwise unchanged. The switch is read per tick,
so it affects in-flight peas immediately. The normal attack
trigger is unchanged: Threepeater still needs a target in one of its three
firing lanes to start a volley. Each emitted pea independently checks its
assigned lane before every movement tick, including during the initial
fan-out and after Torchwood conversion. Any living enemy eligible for its
ordinary damage flags in that lane keeps it on its original trajectory,
even if that enemy is behind the pea. A damageable boss counts in every
lane. Dying, mind-controlled, submerged, underground and flying enemies
do not keep an ordinary pea flying straight; existing damage eligibility
also excludes off-board and temporarily invulnerable enemies.

When the lane is empty, `Plant::FindCattailTarget` selects from the same
attack rectangle and damage flags (11) as Cattail. Both Cattail and the new
ability call this one selector: integer-truncated Euclidean distance,
flying priority bonus 10,000, first candidate on equal weights. Cattail
measures from its plant center as before; a Threepeater pea measures from
its current projectile center. A flying-only lane can therefore trigger
homing onto its own balloon. No eligible target anywhere means no state
change: the pea keeps its original flight and retries each tick until it
finds a target or leaves the board. If the lane becomes occupied first,
it continues ordinary flight.

Acquisition permanently switches the pea to existing `MOTION_HOMING`, with
Cattail's 2-pixel/tick speed, age-dependent steering and target-only
cross-row collision. Initial fan-out Y velocity is retained as the initial
steering direction. Ordinary peas retain their image, size and 20 damage;
they are not converted into Cattail spikes or empowered peas. The damage
eligibility switches to Cattail's flags so airborne targets can actually
be hit. An acquired target dying, disappearing or becoming ineligible
does not cause retargeting: as with Cattail, the projectile coasts along its
last velocity. Returning enemies in the original lane do not cancel homing.

Torchwood uses its existing lane/overlap conversion, before or after
acquisition, without losing motion or lock. Fire peas retain normal 40
damage and fire effects. Homing fire impact sets the splash lane to the
target's lane, avoiding a missed direct hit at lane boundaries. The center
pea retains its prior Torchwood collision look-ahead and portal eligibility;
side peas and homing shots retain their previous portal exclusion. A portal
changes the center pea's assigned lane to its destination, as it already
did for collision purposes. Other pea sources and Cattail movement are
unchanged.

All three launch directions now use existing `MOTION_THREEPEATER`; center
shots have zero Y velocity. This motion previously identified side shots
throughout their lifetime, not just during fan-out, so no new persistent
field, motion enum, TLV or save version is needed. Existing serialized motion,
lane, target, velocity and damage flags fully preserve the ability. In older
saves, already-flying side shots gain the ability; already-flying center
shots remain ordinary because their old `MOTION_STRAIGHT` cannot reliably
identify their source. Newly emitted center shots qualify normally.

Three new cases consolidate the Threepeater coverage:

- A real animated volley combines an initially empty upper lane, an occupied
  center lane and a lower lane emptied during flight. All three peas must
  collide with the center target for 60 total damage. The mixed-flight check
  runs at tick 60, after animated emission and before removing the lower target.
- A center pea crosses real Torchwood before its lane empties, then homes
  across rows for 40 fire damage.
- The scope/save case checks targetless flight followed by acquisition,
  sandbox save/load of both flight states, resumed acquisition and lost-lock
  behavior. It excludes Peashooter, Repeater, Gatling Pea, Snow Pea, both
  Split Pea directions and Leftpeater; checks ground distance, tie order and
  mind-control exclusion; and combines same-lane balloon priority, Torchwood
  overlap after acquisition and a real balloon-popping collision. Repositioning
  into Torchwood preserves the pea's height above its shadow. The hit must
  consume 20 flying health and 20 body health, enter the popping phase and
  leave the nearer ground enemy untouched. `IsFlying()` remains true during
  popping until the zombie animation advances, unlike these projectile-only updates.
  It finally disables `ENABLE_THREEPEATER_HOMING` and confirms an empty-lane
  pea stays `MOTION_THREEPEATER`, alive and row-layered over 120 updates,
  then restores the toggle.

The first two cases run 600 real board ticks each; the scope/save case drives
selected real object updates synchronously. No user saves or profiles are
modified. The suite has 14 registered cases: all 11 pre-existing cases plus
these three Threepeater cases.

Roof, pool, high-gravity and portal gameplay are not covered by new automated
scenarios; they retain existing movement/terrain rules. No pixel-level visual
comparison or historical save fixture was run.

Verified the consolidated suite with the existing `build-msvc` via the VS
developer shell and a bounded `-unittest` run: 14 passed, 0 failed, 5,181
checks, process exit code 0. Both per-frame render-order checks passed, as
did the corrected mixed-volley and balloon-collision checks. All 11 older
cases remain included. `git diff --check` also passed.

## Squash Regression Tests

Two setup-only cases exercise `ENABLE_SQUASH_ENHANCEMENT` enabled and disabled,
restoring its previous value. Real `DoSquashDamage` calls check both edges of
the original 45-pixel rectangle: 2 pixels outside hits only when enabled,
3 pixels outside misses, and interior overlap still deals 1800 damage.
This guards the floating-point 2.5-pixel padding per edge (50 total),
without changing `FindSquashTarget` or its 70-pixel non-eating trigger limit.

Real `UpdateSquash` calls at falling countdowns 5, 1 and 0 distinguish damage
from landing stun. Distant rows and the boss receive 50 only at landing;
allies, dead and dying zombies are excluded, longer stuns are preserved,
and newly stunned enemies produce unattached star particles. Fifty direct
`Zombie::Update` calls freeze position, body animation, age and status timers,
including the boss; update 51 resumes. Reapplication uses maximum duration,
and becoming mind-controlled clears an existing stun. These synchronous
tests do not cover wall-clock pause UI, pixel appearance or stun save/load.

## Wallnut Cards And Bowling

`ENABLE_WALLNUT_DOUBLE_VASE_CARDS` and `ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING`
are independent, default-on switches in `GameConstants.h`. The first makes
each normal wallnut vase release two separately usable cards, spaced 40 pixels
apart. Other vase contents and the number of vases remain unchanged.

The second lets a left double click launch an already planted normal wallnut,
including a transformed Imitater, using the original bowling animation,
contact damage, ricochets and chain-hit rewards. I, Zombie cardboard props and
Zen Garden plants are excluded. Board input requires the preceding single
click to have reached the same plant; planting, collecting a card, shoveling,
right/middle clicking, and clicking another plant do not qualify. The nut
vacates its cell and leaves any lily pad, flower pot or pumpkin behind.
Disabling the switch prevents new launches but does not stop existing rollers.

Direct bowling impacts deal 600 damage to both Gargantuar types globally,
including giant wallnuts in the original bowling minigames, regardless of
either switch. Other targets retain the original body/helmet/shield rules.
Explode-o-nut explosions are unchanged.

Five setup-only regression cases cover enabled/disabled vase counts, separate
card cancellation and planting, real Board click dispatch, excluded inputs
and plants, movement, freed occupancy, zombie targeting, offscreen cleanup,
projectile layering, global damage in both ordinary and bowling levels, and
straight-versus-ricochet door shielding. Synthetic mouse-down calls temporarily
lift the runner's live-input guard without pumping events. Pool and roof cases
use actual level initialization and check slopes, six-row bounds, support
detachment, and straight/bounced save-load continuation.

Rolling identity uses an appended `STATE_BOWLING_STRAIGHT` plus the existing
up/down states, all serialized by the existing plant tail. Existing state
numbers and plant layouts are unchanged. The raw Board byte span now ends
after `mGameID`, excluding removed telemetry; click history is never restored. Tests exercise
portable saves, not an archived legacy binary fixture or visual appearance.

## Roguelike Runs

New Endless Vasebreaker boards now start with all eight upgrades locked.
`Board::IsUpgradeEnabled` reads this board's unlock mask; the standalone
`ENABLE_*` globals continue to control other modes and isolated ability tests.
The fixed 600 bowling damage against Gargantuars remains global, not an upgrade.

Every cleared stage drops a moneybag, replacing the old every-ten-stage award
schedule. Only collecting it rolls the offer. Offers contain up to three distinct
eligible upgrades; unchosen upgrades remain eligible later. Opening Volley can
be selected six times, granting 4/8/16/32/64/128 peas. Each pea uses a
fixed 16-tick firing interval, extending the volley rather than firing simultaneous
peas. Heavy Peas can be selected three times: every third pea, every second
pea, then every pea is empowered. Empowered peas retain their 50% extra damage
and knockback against other enemies; both Gargantuar types are immune to
knockback. Other upgrades remain single picks. With two or one eligible upgrades,
all remaining choices are shown. Once every upgrade reaches its maximum, bag
collection continues to advance stages without an empty selection screen.

Run phases are `Playing -> Reward -> Choosing -> Advancing -> Playing`, with
`Ended` on loss. The existing puzzle cleanup/population path runs once after a
valid choice, retaining unlocks and refreshing seed cooldowns. Selection freezes
gameplay before the modal is even created. Repeated pickup, repeated completion,
and duplicate UI events cannot reroll, pay twice, or grant another upgrade.
New/restarted boards reset the run; losing invalidates the run checkpoint.

`RogueUpgradeDialog` reuses the game's tiled dialog shell, seed artwork and fonts,
with drawn parchment cards. Mouse release over the pressed card, number keys
1-3, or arrows followed by Enter/Space select an upgrade. Escape cannot skip a
reward. Localization keys use `ROGUE_*`, with English fallbacks for the supplied
bitmap fonts. Closing the application uses the existing save-on-shutdown path.

New SAVE4 chunk 21 (version 1, data field 1) stores schema 3, active flag, unlock
mask, phase, the ordered three offer IDs and Opening Volley/Heavy Peas levels.
Schema-1 and schema-2 run saves remain readable, preserving their exact offers
and treating previously unlocked upgrades with no stored level as level one.
Loading preserves the exact offer,
then recreates the dialog after Continue closes. Validation rejects invalid
masks, phases, duplicate/maxed offers, invalid levels, malformed or duplicate run chunks, and
phase/countdown mismatches. Pre-roguelike endless saves are deliberately rejected;
no migration is provided. Non-endless saves keep their existing behavior.

Eleven cases in `RogueTestCases.cpp` cover all 256 unlock masks, real scoped
effects, sixteen consecutive stage rewards (including pool exhaustion), all four
resumable phases, malformed saves, production input/update gates, actual modal
focus and selection, death/restart, all six volley counts and firing intervals,
mid-volley save continuation, all three Heavy Peas patterns, independent saved
shot counters, all 28 mixed stack-level combinations, and schema-1/2 compatibility.
They are appended so tests 09 and 10
retain their numbering and 2x playback. The last case leaves a read-only version
of the real three-card dialog visible during the five-second final hold.

## Removal of the achievement system

Achievement screens, the selector entry, award overlays, notifications,
resource bindings, player flags and all achievement triggers have been removed.
Fifteen write-only Board telemetry fields and their save entries are also gone.
Explosion damage, bowling rewards, collectible effects, player coin balances,
challenge records and endless stage progression remain active.

Player details now use version 13 without the 40-byte achievement block.
Portable saves use header revision 2 after the Board fields and resource IDs
were compacted. Saves from earlier builds are not supported by this change.
The separate run-chunk schema parser does not migrate those earlier files.

The added regression case exercises actual Cherry Bomb, Potato Mine and Corn
Cannon attacks, coin scoring and a compact player-details save/load roundtrip.
The full suite also covers 16 endless stages, all buff levels and live-run saves.
