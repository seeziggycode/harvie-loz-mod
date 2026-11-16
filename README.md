# Zelda's Errands

A Ship of Harkinian mod built for a five-year-old: Princess Zelda doing gentle
errands around Kakariko Village, with no combat, no fail states and almost no
reading.

## Design

The full design lives in a separate document. The short version:

Kakariko is getting ready for a festival tonight and Zelda is helping. Six
errands, all built in the shape of the vanilla Cucco round-up, because that
quest already works for the player this is for: the goal is a *place*, progress
is countable by looking, nothing can be lost, and one button does everything.

## Why the Cucco quest is the template

`En_Niw_Lady` counts cuccos by position — it walks the actor list and tallies
the ones standing inside a box around a fixed point in the pen. No text, no
flag to remember, no instruction that has to survive in a child's memory.

Five of the six errands are that same primitive with different nouns, so the
implementation is one generic "carry N things to a place" driver plus a table,
not six bespoke quests.

## Status

| Piece | State |
| --- | --- |
| Player character | Saria over child Link, recoloured blonde + pink (stand-in; no playable Zelda model exists for SoH) |
| Safety rails | Infinite health, frozen midday, instant text, no forced dialog |
| Starting save | Child, Kakariko front gate, midday |
| Mod slot | `Zeldas Errands 0.1.o2r` loads and registers in the mod menu |
| Quest engine | Built: generic "carry N things to a place" driver, compiles and boots |
| Cucco Round-Up | Wired to the engine with coordinates read from `En_Niw_Lady` |
| Sparkle markers | Built: a pulse on the drop-off, a burst on completion |
| Completion + reward | Built: sticky per-errand CVar, chime and sparkle burst |
| Survey mode | Built: logs the player's position so drop-offs can be measured, not guessed |
| The other five errands | Table rows awaiting surveyed coordinates |
| Festival finale | Not built |

## Build target

Shipwright fork, built for aarch64 via the upstream CI's `ubuntu-22.04-arm`
runner, installed as a separate port so PortMaster does not overwrite it and
the existing Ship of Harkinian install stays untouched.


## Implementation

`src/ZeldasErrands.cpp` registers two hooks through `GameInteractor`:
`OnSceneInit` to reset per-visit state, and `OnGameFrameUpdate` to run the
driver while the player is in Kakariko (scene `0x52`).

The driver counts qualifying actors inside a box around the drop-off, exactly
as `En_Niw_Lady` does. An errand is one row:

```cpp
{ "cuccos", ACTOR_EN_NIW, 330.0f, 1610.0f, 90.0f, 190.0f, 7 },
```

Only coordinates verified from the decompiled source appear in that table. The
cucco pen's centre and tolerances come straight out of `En_Niw_Lady`'s counting
loop. Further errands need their drop-offs measured in-game first — enable
`gEnhancements.ZeldasErrandsSurvey`, walk to the spot, and read the position
out of the log. Guessed coordinates put objects inside walls.

## Settings

| CVar | Effect |
| --- | --- |
| `gEnhancements.ZeldasErrands` | Master switch |
| `gEnhancements.ZeldasErrandsSurvey` | Log the player's position twice a second |
| `gEnhancements.ZE_<key>` | Per-errand completion, sticky |

## Building

`.github/workflows/build-aarch64.yml` builds on a native `ubuntu-22.04-arm`
runner: it clones Shipwright pinned to the commit this was developed against,
grafts `src/` into `soh/soh/Enhancements/ZeldasErrands/`, builds, and fails the
job if the mod's object file is missing from the build. Artifacts are
`soh.elf` and `soh.o2r`.

Pinned upstream: Shipwright `d4bf6528`, libultraship `da6daf1a`.
