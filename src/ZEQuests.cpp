/*
 * Zelda's Errands - the quest line (see ZEQuests.h).
 *
 * Dialogue is one or two short lines per box, a few boxes at most: enough to
 * encourage a five-year-old to read along without overwhelming her. Every
 * goal also has something to see - a twinkle, a sparkle, a chime - so the
 * quest still works for her before she can read every word.
 *
 * Progress is kept in CVars (this handheld is hers alone).
 */

#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/log/luslog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/ShipInit.hpp"
#include "ZEQuests.h"

#include <cmath>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

#define CV(name) CVAR_ENHANCEMENT("ZE_" name)

namespace ZEQuests {

// ---- text --------------------------------------------------------------------
// '^' starts a new box. Lines are short on purpose.
enum : u16 {
    TEXT_BASE = 0x7E00,
    T_ASK_CUCCOS = TEXT_BASE,
    T_REMIND_CUCCOS,
    T_THANKS_CUCCOS, // ...and asks for the path
    T_REMIND_PATH,
    T_THANKS_PATH, // ...and asks for the puppy
    T_REMIND_PUPPY,
    T_THANKS_PUPPY,
    T_DONE,
    TEXT_END
};

static const char* kText[] = {
    /* ASK_CUCCOS    */ "Hi, Harvie!^The cuccos ran away!&Can you bring them home?",
    /* REMIND_CUCCOS */ "The cuccos go in their pen.&Look for the sparkles!",
    /* THANKS_CUCCOS */ "Yay! The cuccos are home!^Oh no! Monsters are&by the big steps.^Take this sword.&You can do it!",
    /* REMIND_PATH   */ "The monsters are&down the big steps.",
    /* THANKS_PATH   */ "You did it!&You are so brave!^Oh! My puppy is lost&in the big field.^Can you find her?",
    /* REMIND_PUPPY  */ "My puppy is in the big field,&by the water.",
    /* THANKS_PUPPY  */ "You found her!&Thank you, Harvie!^You are a hero!",
    /* DONE          */ "Thank you, Harvie!&Let's party!",
};

static void BuildText(uint16_t* textId, bool* loadFromMessageTable) {
    if (*textId < TEXT_BASE || *textId >= TEXT_END) {
        return;
    }
    CustomMessage msg(kText[*textId - TEXT_BASE]);
    msg.AutoFormat();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

// ---- state ---------------------------------------------------------------------
int GetStage() {
    return CVarGetInteger(CV("Stage"), STAGE_CUCCOS);
}
static void SetStage(int s) {
    CVarSetInteger(CV("Stage"), s);
    LUSLOG_INFO("[ZeldasErrands] quest stage %d", s);
}
static bool Flag(const char* name) {
    return CVarGetInteger(name, 0) != 0;
}
static void SetFlag(const char* name) {
    CVarSetInteger(name, 1);
}
static bool CuccosDone() {
    return Flag(CV("cuccos"));
}
static bool PathDone() {
    return Flag(CV("PathDone"));
}

PuppyState GetPuppyState() {
    if (GetStage() < STAGE_PUPPY) {
        return PUPPY_HIDDEN;
    }
    if (Flag(CV("PuppyHome")) || GetStage() >= STAGE_DONE) {
        return PUPPY_HOME;
    }
    return Flag(CV("PuppyFollowing")) ? PUPPY_FOLLOWING : PUPPY_LOST;
}

u16 ZeldaTextId() {
    switch (GetStage()) {
        case STAGE_CUCCOS:
            if (CuccosDone()) {
                return T_THANKS_CUCCOS;
            }
            return Flag(CV("AskedCuccos")) ? T_REMIND_CUCCOS : T_ASK_CUCCOS;
        case STAGE_PATH:
            return PathDone() ? T_THANKS_PATH : T_REMIND_PATH;
        case STAGE_PUPPY:
            return GetPuppyState() == PUPPY_HOME ? T_THANKS_PUPPY : T_REMIND_PUPPY;
        default:
            return T_DONE;
    }
}

bool ZeldaHasNews() {
    u16 t = ZeldaTextId();
    return t == T_ASK_CUCCOS || t == T_THANKS_CUCCOS || t == T_THANKS_PATH || t == T_THANKS_PUPPY;
}

static void Celebrate(const Vec3f& at, u16 sfx) {
    Sfx_PlaySfxCentered(sfx);
    for (int i = 0; i < 24; i++) {
        Vec3f pos = { at.x + Rand_CenteredFloat(120.0f), at.y + 40.0f + Rand_ZeroFloat(60.0f),
                      at.z + Rand_CenteredFloat(120.0f) };
        Vec3f vel = { 0.0f, 1.0f + Rand_ZeroOne(), 0.0f };
        Vec3f accel = { 0.0f, -0.05f, 0.0f };
        EffectSsKiraKira_SpawnSmallYellow(gPlayState, &pos, &vel, &accel);
    }
}

static void GiveSword() {
    Item_Give(gPlayState, ITEM_SWORD_KOKIRI);
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
    gSaveContext.equips.equipment &= (u16) ~(0xF << (EQUIP_TYPE_SWORD * 4));
    gSaveContext.equips.equipment |= EQUIP_VALUE_SWORD_KOKIRI << (EQUIP_TYPE_SWORD * 4);
    Interface_LoadItemIcon1(gPlayState, 0);
    Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
    SetFlag(CV("SwordGiven"));
    LUSLOG_INFO("[ZeldasErrands] gave the Kokiri Sword");
}

void OnZeldaTalkDone(u16 textId) {
    Player* p = GET_PLAYER(gPlayState);
    switch (textId) {
        case T_ASK_CUCCOS:
            SetFlag(CV("AskedCuccos"));
            break;
        case T_THANKS_CUCCOS:
            SetStage(STAGE_PATH);
            GiveSword();
            break;
        case T_THANKS_PATH:
            SetStage(STAGE_PUPPY);
            break;
        case T_THANKS_PUPPY:
            SetStage(STAGE_DONE);
            Rupees_ChangeBy(50);
            Celebrate(p->actor.world.pos, NA_SE_SY_CORRECT_CHIME);
            break;
        default:
            break;
    }
    // A sword is the one thing she needs for the path, so make sure she has it
    // even if this save got here another way.
    if (GetStage() >= STAGE_PATH && !CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI)) {
        GiveSword();
    }
}

void OnPuppyFound() {
    SetFlag(CV("PuppyFollowing"));
    LUSLOG_INFO("[ZeldasErrands] puppy found, following");
}

void OnPuppyHome() {
    SetFlag(CV("PuppyHome"));
    Celebrate(GET_PLAYER(gPlayState)->actor.world.pos, NA_SE_SY_CORRECT_CHIME);
    LUSLOG_INFO("[ZeldasErrands] puppy home");
}

// ---- the field below Kakariko's steps ----------------------------------------
struct Spot {
    f32 x, y, z;
};
// Open grass at the bottom of the big steps, in sight as she comes down.
static const Spot kBabas[] = { { 3330.0f, -20.0f, 760.0f }, { 3440.0f, -20.0f, 900.0f }, { 3200.0f, -20.0f, 930.0f } };
// By the river, a short walk from the steps.
static const Spot kPuppyLost = { 2900.0f, -20.0f, 1060.0f };

static u32 sFrame = 0;
static bool sBabasSpawned = false;

static f32 Floor(f32 x, f32 y, f32 z) {
    Vec3f probe = { x, y + 200.0f, z };
    CollisionPoly* poly;
    f32 fy = BgCheck_EntityRaycastFloor1(&gPlayState->colCtx, &poly, &probe);
    return fy > BGCHECK_Y_MIN ? fy : y;
}

static int CountBabas() {
    int n = 0;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != NULL; a = a->next) {
        n += (a->id == ACTOR_EN_DEKUBABA);
    }
    return n;
}

static void SpawnPuppy(f32 x, f32 y, f32 z) {
    ZEParty::EnsureActorsRegistered();
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ZEParty::GuestActorId(), x, y, z, 0, 0, 0,
                ZEParty::GuestParams(ZEParty::GUEST_DOG, 0x80 | 0x40)); // brown, the quest puppy
}

static void OnSceneInit(int16_t sceneNum) {
    sFrame = 0;
    sBabasSpawned = false;
}

static void OnFrame() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == NULL) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return;
    }
    sFrame++;
    const s16 scene = gPlayState->sceneNum;

    if (sFrame == 2) {
        // The puppy, once found, comes along to the field and the village.
        if (GetPuppyState() == PUPPY_FOLLOWING &&
            (scene == SCENE_HYRULE_FIELD || scene == SCENE_KAKARIKO_VILLAGE)) {
            Vec3f p = player->actor.world.pos;
            SpawnPuppy(p.x - 40.0f * Math_SinS(player->actor.shape.rot.y), p.y + 10.0f,
                       p.z - 40.0f * Math_CosS(player->actor.shape.rot.y));
        }
        if (scene == SCENE_HYRULE_FIELD && GetPuppyState() == PUPPY_LOST) {
            SpawnPuppy(kPuppyLost.x, Floor(kPuppyLost.x, kPuppyLost.y, kPuppyLost.z), kPuppyLost.z);
        }
        if (scene == SCENE_HYRULE_FIELD && GetStage() == STAGE_PATH && !PathDone()) {
            for (const Spot& s : kBabas) {
                Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_DEKUBABA, s.x, Floor(s.x, s.y, s.z), s.z,
                            0, (s16)Rand_ZeroFloat(65535.0f), 0, 0);
            }
            sBabasSpawned = true;
            LUSLOG_INFO("[ZeldasErrands] path: %d Deku Babas waiting", CountBabas());
        }
    }

    if (scene == SCENE_HYRULE_FIELD && sBabasSpawned && !PathDone() && sFrame > 10 && CountBabas() == 0) {
        SetFlag(CV("PathDone"));
        Celebrate(player->actor.world.pos, NA_SE_SY_CORRECT_CHIME);
        LUSLOG_INFO("[ZeldasErrands] path cleared");
    }
    // A gentle sparkle over the monsters so she knows where to go.
    if (scene == SCENE_HYRULE_FIELD && sBabasSpawned && !PathDone() && (sFrame % 15) == 0) {
        const Spot& s = kBabas[(sFrame / 15) % 3];
        Vec3f pos = { s.x, Floor(s.x, s.y, s.z) + 80.0f, s.z }, vel = { 0, 0.4f, 0 }, accel = { 0, -0.02f, 0 };
        EffectSsKiraKira_SpawnSmallYellow(gPlayState, &pos, &vel, &accel);
    }
}

void Register() {
    static HOOK_ID frameHook = 0, sceneHook = 0, textHook = 0;
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameFrameUpdate>(frameHook);
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(sceneHook);
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnOpenText>(textHook);
    frameHook = sceneHook = textHook = 0;
    if (!CVarGetInteger(CVAR_ENHANCEMENT("ZeldasErrands"), 0)) {
        return;
    }
    frameHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    sceneHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    textHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(BuildText);
}

} // namespace ZEQuests
