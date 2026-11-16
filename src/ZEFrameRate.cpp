/*
 * Zelda's Errands - adaptive frame rate.
 *
 * Goal: 60 fps wherever the handheld can hold it, and a steady 30 (or 20)
 * where it can't - never the judder in between.
 *
 * The game advances in 50 ms ticks; at 60 fps the renderer draws three
 * interpolated frames inside each tick. So if ticks take longer than 50 ms,
 * the current frame rate is too ambitious for this place. We step down
 * 60 -> 30 -> 20, remember the step for this scene for the rest of the
 * session, and start every new scene at 60 again.
 *
 * Logs one line every 30 s (scene, rate, mean tick) so on-device behaviour
 * can be read back from the port's log.txt.
 */

#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/log/luslog.h>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include <chrono>
#include <map>

extern "C" {
#include "z64.h"
extern PlayState* gPlayState;
}

#define CVAR_ADAPTIVE CVAR_ENHANCEMENT("ZE_AdaptiveFPS")
#define CVAR_FPS CVAR_SETTING("InterpolationFPS")

namespace ZEFrameRate {

static const int kRates[] = { 60, 30, 20 };
static const double kTickMs = 50.0;
static const double kSlowFactor = 1.08; // ~54 ms: more than a frame's slack lost per tick
static const u32 kSettleTicks = 40;     // ignore loading hitches after entering a scene
static const u32 kWindowTicks = 40;     // judge over ~2 s

static std::map<s16, int> sSceneLevel; // scene -> index into kRates, for this session
static int sLevel = 0;
static s16 sScene = -1;
static u32 sTicks = 0, sWindow = 0, sLogTicks = 0;
static double sWindowMs = 0, sLogMs = 0;
static std::chrono::steady_clock::time_point sLast;

int CurrentRate() {
    return kRates[sLevel];
}
bool AtLowestRate() {
    return sLevel == (int)(sizeof(kRates) / sizeof(kRates[0])) - 1;
}

static void Apply() {
    if (CVarGetInteger(CVAR_FPS, 20) != kRates[sLevel]) {
        CVarSetInteger(CVAR_FPS, kRates[sLevel]);
    }
}

static void OnSceneInit(int16_t sceneNum) {
    sScene = sceneNum;
    auto it = sSceneLevel.find(sceneNum);
    sLevel = it != sSceneLevel.end() ? it->second : 0;
    sTicks = sWindow = 0;
    sWindowMs = 0;
    Apply();
}

static void OnTick() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double, std::milli>(now - sLast).count();
    sLast = now;
    if (gPlayState == NULL || ++sTicks < kSettleTicks || dt > 500.0) {
        return; // loading, pausing and menus are not frame-rate problems
    }
    sLogMs += dt;
    if (++sLogTicks == 600) {
        LUSLOG_INFO("[ZeldasErrands] fps: scene 0x%02X at %d fps, mean tick %.1f ms (%.0f%% of budget)", sScene,
                    kRates[sLevel], sLogMs / sLogTicks, 100.0 * (sLogMs / sLogTicks) / kTickMs);
        sLogMs = 0;
        sLogTicks = 0;
    }
    sWindowMs += dt;
    if (++sWindow < kWindowTicks) {
        return;
    }
    double mean = sWindowMs / sWindow;
    sWindow = 0;
    sWindowMs = 0;
    if (mean > kTickMs * kSlowFactor && !AtLowestRate()) {
        sLevel++;
        sSceneLevel[sScene] = sLevel;
        Apply();
        LUSLOG_INFO("[ZeldasErrands] fps: scene 0x%02X too slow at %.1f ms/tick, now %d fps", sScene, mean,
                    kRates[sLevel]);
    }
}

static void Register() {
    bool on = CVarGetInteger(CVAR_ENHANCEMENT("ZeldasErrands"), 0) && CVarGetInteger(CVAR_ADAPTIVE, 1);
    COND_HOOK(OnSceneInit, on, OnSceneInit);
    COND_HOOK(OnGameFrameUpdate, on, OnTick);
}

static RegisterShipInitFunc initFunc(Register, { CVAR_ENHANCEMENT("ZeldasErrands"), CVAR_ADAPTIVE });

} // namespace ZEFrameRate
