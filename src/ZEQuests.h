#pragma once
/*
 * Zelda's Errands - the quest line. Zelda (in Kakariko, just inside the gate)
 * is the one Harvie always goes back to.
 *
 *   1. Cuccos      bring the runaway cuccos back to the pen (ZeldasErrands.cpp)
 *   2. The path    Zelda gives her a sword; three Deku Babas wait at the
 *                  bottom of Kakariko's big steps in Hyrule Field
 *   3. The puppy   the brown puppy is lost in the field by the river; find
 *                  her and she follows Harvie home to Zelda
 */
#include "ZEParty.h"

namespace ZEQuests {

enum Stage { STAGE_CUCCOS = 0, STAGE_PATH = 1, STAGE_PUPPY = 2, STAGE_DONE = 3 };

int GetStage();
// What Zelda says right now, and what happens when she has said it.
u16 ZeldaTextId();
void OnZeldaTalkDone(u16 textId);
// Whether Zelda has something new to say (shown as a twinkle over her).
bool ZeldaHasNews();

// The puppy's state (guest actor asks; quest module decides).
enum PuppyState { PUPPY_HIDDEN, PUPPY_LOST, PUPPY_FOLLOWING, PUPPY_HOME };
PuppyState GetPuppyState();
void OnPuppyFound();
void OnPuppyHome();

void Register();

} // namespace ZEQuests
