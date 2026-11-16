/*
 * Zelda's Errands - the party in Kakariko Village.
 *
 * She was told there is a party waiting for her in Kakariko. This makes it so,
 * every visit:
 *
 *   arrival   the title card reads "Welcome, Harvie!", the sign over the gate
 *             reads WELCOME HARVIE (an alt texture in the mod archive), and
 *             Zelda's Theme plays. Through the gate, adult Princess Zelda is
 *             waiting with a rainbow fairy over her head.
 *   surprise  when Harvie reaches her: a chime, a shower of confetti, a flock
 *             of cuccos appears all around her, and the music turns to
 *             Saria's Song.
 *   party     festival poles with lanterns and bunting, a maypole in the
 *             plaza, Darunia dancing, Malon singing, Ruto, a Zora and a
 *             Goron, two puppies, fairy lights, butterflies, and fireworks.
 *
 * And always, everywhere: cuccos never turn into an angry flock.
 *
 * Performance: this handheld is fill-rate bound, so alpha effects are rationed
 * (see the k* rates below) and every model is an ordinary culled actor.
 */

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "ZEParty.h"
#include "ZETextures.h"
#include "ZEQuests.h"

#include <chrono>
#include <cmath>
#include <thread>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Niw/z_en_niw.h"
extern PlayState* gPlayState;
}

namespace ZEFrameRate {
bool AtLowestRate();
}

namespace ZEParty {

// ---- tuning ----------------------------------------------------------------
static const u32 kConfettiEvery = 14;   // frames between ambient confetti specks
static const u32 kFireworkEvery = 70;   // frames between firework bursts
static const int kFireworkSparks = 14;
static const int kSurpriseCuccos = 6;
static const f32 kSurpriseRing = 110.0f;

struct Placement {
    u8 ch, anim;
    f32 x, y, z; // y is a hint; guests snap to the floor
    s16 yaw;
};

// Who is where. Positions were chosen from the floor map and checked in
// screenshots; yaw is the direction a guest faces until Harvie comes near.
static const Placement kPlacements[] = {
    // in the meadow just inside the gate, facing the gate
    { GUEST_ZELDA, 0, -1880.0f, 110.0f, 1000.0f, (s16)0xC000 },
    { GUEST_DOG, 0, -1915.0f, 110.0f, 1075.0f, (s16)0xC000 },
    { GUEST_DOG, 0x80 | 1, -1915.0f, 110.0f, 925.0f, (s16)0xC000 }, // brown
    // around the maypole in the plaza
    { GUEST_DARUNIA, 0, -380.0f, 0.0f, 300.0f, (s16)0xC000 },
    { GUEST_MALON, 0, -800.0f, 0.0f, 330.0f, (s16)0x4000 },
    { GUEST_RUTO, 0, -760.0f, 0.0f, 610.0f, (s16)0x6000 },
    { GUEST_ZORA, 0, -400.0f, 0.0f, 620.0f, (s16)0xA000 },
    { GUEST_GORON, 0, -580.0f, 0.0f, 170.0f, (s16)0x0000 },
};
// Which guests carry a rainbow fairy. Only Zelda: it makes her easy to spot
// through the gate, and every fairy costs a model, a sparkle stream and two
// point lights that the guests around it must then be lit by (measured: the
// fairies were most of the party's CPU cost).
static bool HasFairy(u8 ch) {
    return ch == GUEST_ZELDA;
}

static const Vec3f kButterflies[] = {
    { -860.0f, 40.0f, 520.0f }, { -820.0f, 50.0f, 470.0f }, { -1950.0f, 160.0f, 820.0f }, { -1950.0f, 160.0f, 1180.0f },
};

static const Color_RGBA8 kSparkColors[] = {
    { 255, 90, 110, 255 }, { 255, 210, 60, 255 }, { 110, 200, 255, 255 },
    { 200, 120, 255, 255 }, { 120, 255, 150, 255 }, { 255, 160, 220, 255 },
};

// ---- on-device performance governor ----------------------------------------
// The game advances at 20 Hz. If the handheld cannot hold that in Kakariko,
// shed the optional extras in steps rather than let the whole village stutter:
//   level 1: no fireworks or confetti (alpha effects: the GPU's weak spot)
//   level 2: also no butterflies or drifting fairy light
// The level only ever rises during a session, so it cannot oscillate.
static const double kGameFrameMs = 50.0;
static int sQuality = 0;
static double sAvgMs = kGameFrameMs;
static u32 sSlowFrames = 0;
static std::chrono::steady_clock::time_point sLastFrameTime;
static double sLogAccum = 0;
static u32 sLogFrames = 0;

static void ShedExtras() {
    if (sQuality >= 2) {
        for (int cat : { ACTORCAT_ITEMACTION, ACTORCAT_MISC, ACTORCAT_NPC }) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
                if (a->id == ACTOR_EN_BUTTE || (a->id == GuestActorId() && (u16)a->params == 0xF000)) {
                    Actor_Kill(a);
                }
            }
        }
    }
}

static void Govern(u32 frame) {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double, std::milli>(now - sLastFrameTime).count();
    sLastFrameTime = now;
    if (frame < 40 || dt > 1000.0) {
        return; // loading hitches and pauses are not the party's fault
    }
    sAvgMs = sAvgMs * 0.9 + dt * 0.1;
    sLogAccum += dt;
    if (++sLogFrames == 600) {
        LUSLOG_INFO("[ZeldasErrands] Kakariko frame %.1f ms (target %.0f), party quality level %d",
                    sLogAccum / sLogFrames, kGameFrameMs, sQuality);
        sLogAccum = 0;
        sLogFrames = 0;
    }
    // the frame rate steps down first (60 -> 30 -> 20); extras go only if 20 fps still can't keep up
    if (sAvgMs > kGameFrameMs * 1.15 && sQuality < 2 && ZEFrameRate::AtLowestRate()) {
        if (++sSlowFrames >= 40) {
            sQuality++;
            sSlowFrames = 0;
            sAvgMs = kGameFrameMs;
            LUSLOG_INFO("[ZeldasErrands] frames slow, party quality level now %d", sQuality);
            ShedExtras();
        }
    } else {
        sSlowFrames = 0;
    }
}

// ---- per-visit state -----------------------------------------------------
static u32 sFrame = 0;
static bool sSetUp = false;
static bool sSurprised = false;

bool HasFairyFor(u8 ch) {
    return HasFairy(ch);
}

static void Spark(const Vec3f& pos, const Vec3f& vel, const Vec3f& accel, const Color_RGBA8& c, s16 scale, s32 life) {
    Vec3f p = pos, v = vel, a = accel;
    // a pale tint of the colour for the core, the colour itself for the glow
    Color_RGBA8 prim = { (u8)((c.r + 255) / 2), (u8)((c.g + 255) / 2), (u8)((c.b + 255) / 2), 255 };
    Color_RGBA8 env = c;
    EffectSsKiraKira_SpawnDispersed(gPlayState, &p, &v, &a, &prim, &env, scale, life);
}

static void Confetti(const Vec3f& around, f32 spread) {
    Vec3f pos = { around.x + Rand_CenteredFloat(spread), around.y + 50.0f + Rand_ZeroFloat(60.0f),
                  around.z + Rand_CenteredFloat(spread) };
    Spark(pos, { 0.0f, -0.6f, 0.0f }, { 0.0f, -0.02f, 0.0f },
          kSparkColors[(int)Rand_ZeroFloat(5.99f)], 900, 40);
}

static void Firework() {
    // high in the sky, in front of the camera so she always sees it
    Vec3f eye = gPlayState->view.eye, at = gPlayState->view.lookAt;
    f32 dx = at.x - eye.x, dz = at.z - eye.z, len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        return;
    }
    dx /= len;
    dz /= len;
    f32 side = Rand_CenteredFloat(360.0f);
    // the camera looks slightly down, so "high" is only ~150-250 above the eye
    Vec3f c = { eye.x + dx * 520.0f - dz * side, eye.y + 130.0f + Rand_ZeroFloat(90.0f),
                eye.z + dz * 520.0f + dx * side };
    const Color_RGBA8& col = kSparkColors[(int)Rand_ZeroFloat(5.99f)];
    for (int i = 0; i < kFireworkSparks; i++) {
        s16 yaw = (s16)(i * 0x10000 / kFireworkSparks);
        f32 up = Rand_CenteredFloat(1.6f);
        Spark(c, { 5.0f * Math_SinS(yaw), 2.0f + up * 3.0f, 5.0f * Math_CosS(yaw) }, { 0.0f, -0.15f, 0.0f }, col, 3600,
              30);
    }
}

static void ShowWelcomeCard() {
    TitleCardContext* t = &gPlayState->actorCtx.titleCtx;
    t->texture = (void*)gZEWelcomeCardTex;
    t->isBossCard = false;
    t->hasTranslation = false;
    t->x = 160;
    t->y = 120;
    t->width = ZE_WELCOME_CARD_W;
    t->height = ZE_WELCOME_CARD_H;
    t->durationTimer = 110;
    t->delayTimer = 15;
    t->alpha = 0;
    t->intensityR = t->intensityG = t->intensityB = 0;
}

static void SetUpParty() {
    EnsureActorsRegistered();
    if (int ch = CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestLineup"), 0)) {
        SpawnLineup((u8)(ch - 1)); // test harness only: a row of one character instead of the party
        return;
    }
    ActorContext* ac = &gPlayState->actorCtx;
    if (!CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestNoDecor"), 0)) {
        Actor_Spawn(ac, gPlayState, DecorActorId(), 0, 0, 0, 0, 0, 0, 0);
    }
    for (const Placement& p : kPlacements) {
        if (CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestNoGuests"), 0)) {
            break;
        }
        // the brown puppy only sits by Zelda once she has been found and brought home
        if (p.ch == GUEST_DOG && (p.anim & 0x80) && ZEQuests::GetPuppyState() != ZEQuests::PUPPY_HOME) {
            continue;
        }
        Actor_Spawn(ac, gPlayState, GuestActorId(), p.x, p.y, p.z, 0, p.yaw, 0, GuestParams(p.ch, p.anim));
    }
    if (sQuality >= 2) {
        return; // the governor has already learned this device needs fewer extras
    }
    // a fairy light circling the maypole
    Actor_Spawn(ac, gPlayState, GuestActorId(), -580.0f, 220.0f, 430.0f, 0, 0, 0, (s16)0xF000);
    for (const Vec3f& b : kButterflies) {
        if (CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestNoButte"), 0)) {
            break;
        }
        Actor_Spawn(ac, gPlayState, ACTOR_EN_BUTTE, b.x, b.y, b.z, 0, (s16)Rand_ZeroFloat(65535.0f), 0, 0);
    }
}

static void Surprise() {
    sSurprised = true;
    Player* player = GET_PLAYER(gPlayState);
    Vec3f p = player->actor.world.pos;

    Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_SARIA_THEME);
    for (int i = 0; i < 40; i++) {
        Confetti(p, 240.0f);
    }
    for (int i = 0; i < kSurpriseCuccos; i++) {
        s16 ang = (s16)(i * 0x10000 / kSurpriseCuccos);
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_NIW, p.x + Math_SinS(ang) * kSurpriseRing, p.y + 8.0f,
                    p.z + Math_CosS(ang) * kSurpriseRing, 0, ang, 0, 0);
    }
    Firework();
    LUSLOG_INFO("[ZeldasErrands] surprise! frame %u", sFrame);
}

static void OnFrame() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == NULL ||
        gPlayState->sceneNum != SCENE_KAKARIKO_VILLAGE) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return;
    }
    sFrame++;
    if (int ms = CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestSlowMs"), 0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms)); // test harness: a slow device
    }
    Govern(sFrame);

    if (!sSetUp) {
        sSetUp = true;
        SetUpParty();
        // Arriving through the front gate (from the field, or loading her save)
        // always gets her card, even where vanilla shows none.
        if (gSaveContext.entranceIndex == ENTR_KAKARIKO_VILLAGE_FRONT_GATE) {
            ShowWelcomeCard();
        }
        Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_ZELDA_THEME);
        LUSLOG_INFO("[ZeldasErrands] party set up in Kakariko");
        return;
    }

    // Replace a pending "Kakariko Village" card with hers - only when a card
    // was going to show anyway, so stepping out of a house stays quiet. Checked
    // for a while because the card can also be raised later (by a cutscene).
    if (sFrame < 600) {
        TitleCardContext* t = &gPlayState->actorCtx.titleCtx;
        if (t->texture != NULL && t->texture != (void*)gZEWelcomeCardTex && t->alpha == 0 && t->durationTimer > 0) {
            ShowWelcomeCard();
        }
    }

    if (int q = CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestQuality"), 0); q > sQuality) {
        sQuality = q; // test harness: pretend the device is slow
        ShedExtras();
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestLineup"), 0)) {
        return; // test harness lineup: no party logic
    }
    if (!sSurprised) {
        // Arriving any way other than the front gate (a house, the graveyard,
        // Death Mountain) still gets the party, just without the walk to Zelda.
        f32 dx = player->actor.world.pos.x - gZeldaPos.x, dz = player->actor.world.pos.z - gZeldaPos.z;
        bool farFromZelda = (dx * dx + dz * dz) > 1500.0f * 1500.0f;
        if (gZeldaGreeted || (sFrame > 40 && farFromZelda)) {
            Surprise();
        } else if ((sFrame % 12) == 0) {
            // a twinkle around Zelda so she is easy to spot
            Confetti(gZeldaPos, 60.0f);
        }
        return;
    }

    // Zelda twinkles whenever she has something new to say
    if (ZEQuests::ZeldaHasNews() && (sFrame % 12) == 0) {
        Confetti(gZeldaPos, 60.0f);
    }
    if (sQuality >= 1) {
        return;
    }
    if ((sFrame % kConfettiEvery) == 0) {
        Confetti(player->actor.world.pos, 300.0f);
    }
    if ((sFrame % kFireworkEvery) == 0) {
        Firework();
    }
}

static void OnSceneInit(int16_t sceneNum) {
    sFrame = 0;
    sSlowFrames = 0;
    sAvgMs = kGameFrameMs;
    sLastFrameTime = std::chrono::steady_clock::now();
    sSetUp = false;
    sSurprised = false;
    gZeldaGreeted = false;
    gZeldaPos = { -1880.0f, 110.0f, 1000.0f };
}

// Cuccos count hits in unk_2A4 and summon the angry flock when it reaches
// zero. Topping it up after every update means that never happens.
static void KeepCuccoCalm(void* actor) {
    EnNiw* niw = (EnNiw*)actor;
    if (niw->unk_2A4 < 5) {
        niw->unk_2A4 = 5;
    }
}

void RegisterParty() {
    static HOOK_ID frameHook = 0, sceneHook = 0, cuccoHook = 0;
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameFrameUpdate>(frameHook);
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(sceneHook);
    GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnActorUpdate>(cuccoHook);
    frameHook = sceneHook = cuccoHook = 0;
    if (!CVarGetInteger(CVAR_ENHANCEMENT("ZeldasErrands"), 0)) {
        return;
    }
    frameHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    sceneHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    cuccoHook = GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnActorUpdate>(ACTOR_EN_NIW,
                                                                                             KeepCuccoCalm);
}

} // namespace ZEParty
