/*
 * Zelda's Errands - a gentle quest layer over Kakariko Village.
 *
 * Built for a five-year-old who cannot read. The whole design copies one
 * mechanic that already works for her: the vanilla Cucco round-up. Reading
 * En_Niw_Lady, that quest counts cuccos by POSITION - it walks the actor list
 * and tallies the ones standing inside a box around a fixed point in the pen.
 * No text, no flag to remember, no instruction that has to survive in a
 * child's memory. The goal is a place.
 *
 * So this is one generic "carry N things to a place" driver plus a table.
 * Adding an errand is a table row, not new code.
 */

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "ZEParty.h"
#include "ZEQuests.h"

#include <array>
#include <cmath>
#include <cstdio>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

#define CVAR_ERRANDS_ON CVAR_ENHANCEMENT("ZeldasErrands")
#define CVAR_ERRANDS_SURVEY CVAR_ENHANCEMENT("ZeldasErrandsSurvey")

namespace ZeldasErrands {

// An errand is: carry `count` actors of type `actorId` into a box around
// `dropX/dropZ`. Tolerances are half-extents, matching how En_Niw_Lady tests
// the pen (fabsf(pos - centre) < tolerance).
struct Errand {
    const char* key;      // completion is stored at CVAR_ENHANCEMENT("ZE_<key>")
    s16 actorId;          // what she carries
    f32 dropX, dropZ;     // centre of the drop-off
    f32 tolX, tolZ;       // half-extents of the drop-off box
    u8 count;             // how many are needed
};

// Only coordinates verified from the decompiled source appear here. The cucco
// pen's centre and tolerances are read straight out of En_Niw_Lady's counting
// loop, which tests x within 90.0f of 330.0f and z within 190.0f of 1610.0f.
// Further errands need their drop-offs surveyed in-game first - see the survey
// mode below. Guessing coordinates puts objects inside walls.
static const std::array<Errand, 1> kErrands = { {
    { "cuccos", ACTOR_EN_NIW, 330.0f, 1610.0f, 90.0f, 190.0f, 7 },
} };

static bool sWasComplete[kErrands.size()] = { false };
static u32 sFrame = 0;

static bool IsComplete(const Errand& e) {
    char cvar[64];
    snprintf(cvar, sizeof(cvar), CVAR_ENHANCEMENT("ZE_%s"), e.key);
    return CVarGetInteger(cvar, 0) != 0;
}

static void SetComplete(const Errand& e) {
    char cvar[64];
    snprintf(cvar, sizeof(cvar), CVAR_ENHANCEMENT("ZE_%s"), e.key);
    CVarSetInteger(cvar, 1);
}

// How many qualifying actors are standing in the drop-off box right now.
static u8 CountInDropoff(const Errand& e) {
    u8 n = 0;
    Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head;
    while (a != NULL) {
        if (a->id == e.actorId && fabsf(a->world.pos.x - e.dropX) < e.tolX &&
            fabsf(a->world.pos.z - e.dropZ) < e.tolZ) {
            n++;
        }
        a = a->next;
    }
    return n;
}

// The whole instruction set, with no words in it: a sparkle sits on the place
// things go. She sees where, not a sentence telling her where.
static void SparkleAt(f32 x, f32 y, f32 z) {
    Vec3f pos = { x, y, z };
    Vec3f vel = { 0.0f, 0.6f, 0.0f };
    Vec3f accel = { 0.0f, -0.02f, 0.0f };
    EffectSsKiraKira_SpawnSmallYellow(gPlayState, &pos, &vel, &accel);
}


static void OnFrame() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == NULL) {
        return;
    }
    if (gPlayState->sceneNum != SCENE_KAKARIKO_VILLAGE) {
        return;
    }
    sFrame++;

    // Survey mode: print where the player is standing, so a drop-off can be
    // measured in-game instead of guessed. Walk to a spot, read the log.
    if (CVarGetInteger(CVAR_ERRANDS_SURVEY, 0) && (sFrame % 30) == 0) {
        Player* player = GET_PLAYER(gPlayState);
        if (player != NULL) {
            LUSLOG_INFO("[ZeldasErrands] survey: scene=%d x=%.1f y=%.1f z=%.1f",
                        gPlayState->sceneNum, player->actor.world.pos.x, player->actor.world.pos.y,
                        player->actor.world.pos.z);
        }
    }

    for (size_t i = 0; i < kErrands.size(); i++) {
        const Errand& e = kErrands[i];
        if (IsComplete(e)) {
            continue;
        }

        // A slow pulse rather than a constant stream: readable, and cheap on a
        // handheld that is already fill-rate bound.
        if ((sFrame % 10) == 0) {
            SparkleAt(e.dropX, 0.0f, e.dropZ);
        }

        u8 n = CountInDropoff(e);
        if (n >= e.count && !sWasComplete[i]) {
            sWasComplete[i] = true;
            SetComplete(e);
            Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
            for (int b = 0; b < 12; b++) {
                SparkleAt(e.dropX, 20.0f + b * 6.0f, e.dropZ);
            }
            LUSLOG_INFO("[ZeldasErrands] errand %s complete (%d of %d)", e.key, (int)n, (int)e.count);
        }
    }
}

static void OnSceneLoad(int16_t sceneNum) {
    // Completion is sticky, but the "already fired" latch is per-visit so the
    // reward cannot double-fire within one load.
    for (size_t i = 0; i < kErrands.size(); i++) {
        sWasComplete[i] = IsComplete(kErrands[i]);
    }
    sFrame = 0;
}

} // namespace ZeldasErrands

void RegisterZeldasErrands() {
    static HOOK_ID frameHook = 0;
    static HOOK_ID sceneHook = 0;
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameFrameUpdate>(frameHook);
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(sceneHook);
    frameHook = 0;
    sceneHook = 0;

    if (CVarGetInteger(CVAR_ERRANDS_ON, 0)) {
        frameHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
            ZeldasErrands::OnFrame);
        sceneHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(ZeldasErrands::OnSceneLoad);
    }
    // The Kakariko party shares the mod's on/off switch.
    ZEParty::RegisterParty();
    ZEQuests::Register();
}

static RegisterShipInitFunc initFuncZeldasErrands(RegisterZeldasErrands, { CVAR_ERRANDS_ON });
