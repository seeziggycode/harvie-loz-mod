/*
 * Zelda's Errands - test hooks.
 *
 * None of this runs on Harvie's device: every hook is gated on a CVar that is
 * only ever set in the headless test harness. It exists so changes can be seen
 * and measured before she sees them.
 *
 *   ZE_TestBoot   entrance index to boot straight into (skips title + file select)
 *   ZE_TestMap    1 = raycast the scene's floor on a grid and log it
 *   ZE_TestTour   1 = teleport the player to "x y z yaw" read from /tmp/ze_tour
 *   ZE_TestPerf   1 = log mean frame time, actor and effect counts
 */

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "ZEParty.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <string>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
extern GameState* gGameState;
void Sram_InitDebugSave(void);
extern EffectSsInfo sEffectSsInfo;
}

#define CVAR_TEST_BOOT CVAR_ENHANCEMENT("ZE_TestBoot")
#define CVAR_TEST_MAP CVAR_ENHANCEMENT("ZE_TestMap")
#define CVAR_TEST_TOUR CVAR_ENHANCEMENT("ZE_TestTour")
#define CVAR_TEST_PERF CVAR_ENHANCEMENT("ZE_TestPerf")

namespace ZETest {

// --- boot straight into a scene as child Link at noon -----------------------
static void BootInto(s32 entrance) {
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.fileNum = 0xFE;
    Sram_InitDebugSave();
    gSaveContext.fileNum = 0xFF;
    gSaveContext.sceneLayer = 0;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.linkAge = 0;
    gSaveContext.nightFlag = 0;
    gSaveContext.skyboxTime = gSaveContext.dayTime = 0x8000;
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.buttonStatus); i++) {
        gSaveContext.buttonStatus[i] = BTN_ENABLED;
    }
    gSaveContext.nextHudVisibilityMode = gSaveContext.hudVisibilityMode = gSaveContext.hudVisibilityModeTimer = 0;
    gSaveContext.entranceIndex = entrance;
    gSaveContext.respawnFlag = 0;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = 0;
    gGameState->running = false;
    SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
}

// --- floor heightmap ---------------------------------------------------------
static const s32 kMapMin = -2400, kMapMax = 2400, kMapStep = 25;
static s32 sMapRow = 0;
static bool sMapDone = false;

static void MapStep() {
    if (sMapDone) {
        return;
    }
    // one row per frame keeps the game responsive while the map is taken
    const s32 z = kMapMin + sMapRow * kMapStep;
    std::string line = "ZEMAP " + std::to_string(z);
    for (s32 x = kMapMin; x <= kMapMax; x += kMapStep) {
        Vec3f pos = { (f32)x, 3000.0f, (f32)z };
        CollisionPoly* poly;
        f32 y = BgCheck_EntityRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
        line += " " + std::to_string((s32)y);
    }
    LUSLOG_INFO("%s", line.c_str());
    if (z >= kMapMax) {
        sMapDone = true;
        LUSLOG_INFO("ZEMAP done");
    }
    sMapRow++;
}

static void DumpActors() {
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
            LUSLOG_INFO("ZEACTOR cat=%d id=0x%04X name=%s pos=%.0f,%.0f,%.0f rotY=%d params=0x%04X", cat,
                        (u16)a->id, ActorDB::Instance->RetrieveEntry(a->id).name.c_str(), a->world.pos.x, a->world.pos.y,
                        a->world.pos.z, (int)a->shape.rot.y, (u16)a->params);
        }
    }
}

// --- teleport tour ----------------------------------------------------------
static std::string sLastTour;

static std::string sLastAnim;

static void AnimStep() {
    std::ifstream f("/tmp/ze_anim");
    std::string s;
    if (!f || !std::getline(f, s) || s == sLastAnim) {
        return;
    }
    sLastAnim = s;
    ZEParty::SetLineupAnim(atoi(s.c_str()));
}

static void TourStep() {
    AnimStep();
    std::ifstream f("/tmp/ze_tour");
    std::string s;
    if (!f || !std::getline(f, s) || s == sLastTour) {
        return;
    }
    sLastTour = s;
    float x, y, z;
    int yaw;
    if (sscanf(s.c_str(), "%f %f %f %d", &x, &y, &z, &yaw) != 4) {
        return;
    }
    Player* p = GET_PLAYER(gPlayState);
    p->actor.world.pos = p->actor.home.pos = p->actor.prevPos = { x, y, z };
    p->actor.shape.rot.y = p->actor.world.rot.y = p->yaw = (s16)yaw;
    Camera_InitPlayerSettings(Play_GetCamera(gPlayState, CAM_ID_MAIN), p);
    LUSLOG_INFO("ZETOUR at %.0f %.0f %.0f yaw %d", x, y, z, yaw);
}

// --- frame timing -------------------------------------------------------------
static std::chrono::steady_clock::time_point sLast;
static double sAccum = 0, sCpuAccum = 0, sCpuLast = -1;
static int sFrames = 0;

// Process CPU time: includes the software rasteriser's worker threads, so it
// stands in for GPU fill cost on the handheld, unlike wall time (pinned at the
// game's 20 Hz frame cap).
static double ProcessCpuMs() {
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void PerfStep() {
    auto now = std::chrono::steady_clock::now();
    double cpu = ProcessCpuMs();
    if (sFrames > 0 || sAccum > 0) {
        sAccum += std::chrono::duration<double, std::milli>(now - sLast).count();
        sCpuAccum += cpu - sCpuLast;
    }
    sLast = now;
    sCpuLast = cpu;
    if (++sFrames == 120) {
        int actors = 0;
        for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
            actors += gPlayState->actorCtx.actorLists[cat].length;
        }
        int effects = 0;
        for (int i = 0; i < sEffectSsInfo.tableSize; i++) {
            effects += (sEffectSsInfo.table[i].life > -1);
        }
        LUSLOG_INFO("ZEPERF mean_frame_ms=%.2f cpu_ms_per_frame=%.2f actors=%d effects=%d bgm=0x%02X",
                    sAccum / 119.0, sCpuAccum / 119.0, actors, effects, func_800FA0B4(SEQ_PLAYER_BGM_MAIN) & 0xFF);
        sAccum = 0;
        sCpuAccum = 0;
        sFrames = 0;
    }
}

static bool sDumped = false;

static void Register() {
    COND_HOOK(OnZTitleUpdate, CVarGetInteger(CVAR_TEST_BOOT, 0) != 0,
              [](void*) { BootInto(CVarGetInteger(CVAR_TEST_BOOT, 0)); });

    COND_HOOK(OnSceneSpawnActors, CVarGetInteger(CVAR_TEST_MAP, 0) != 0, []() {
        sMapRow = 0;
        sMapDone = false;
        sDumped = false;
    });

    COND_HOOK(OnGameFrameUpdate,
              CVarGetInteger(CVAR_TEST_MAP, 0) || CVarGetInteger(CVAR_TEST_TOUR, 0) ||
                  CVarGetInteger(CVAR_TEST_PERF, 0),
              []() {
                  if (!GameInteractor::IsSaveLoaded(true)) {
                      return;
                  }
                  if (CVarGetInteger(CVAR_TEST_MAP, 0)) {
                      if (!sDumped) {
                          sDumped = true;
                          Player* p = GET_PLAYER(gPlayState);
                          LUSLOG_INFO("ZESPAWN scene=0x%02X player=%.0f,%.0f,%.0f yaw=%d",
                                      gPlayState->sceneNum, p->actor.world.pos.x, p->actor.world.pos.y,
                                      p->actor.world.pos.z, (int)p->actor.shape.rot.y);
                          DumpActors();
                      }
                      MapStep();
                  }
                  if (CVarGetInteger(CVAR_TEST_TOUR, 0)) {
                      TourStep();
                  }
                  if (CVarGetInteger(CVAR_TEST_PERF, 0)) {
                      PerfStep();
                  }
              });
}

static RegisterShipInitFunc initFunc(Register, { CVAR_TEST_BOOT, CVAR_TEST_MAP, CVAR_TEST_TOUR, CVAR_TEST_PERF });

} // namespace ZETest
