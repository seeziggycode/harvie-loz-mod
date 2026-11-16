/*
 * Zelda's Errands - party guests.
 *
 * One actor class plays any guest: it borrows a character's skeleton, face
 * textures and an animation, and none of that character's story logic. The
 * real NPC actors are tied to the scenes and flags of their own quests; a
 * guest only stands, animates, blinks, turns to look at Harvie, and is solid
 * so she bumps into them instead of walking through.
 *
 * SoH loads models by resource name, so a guest's object does not need to be
 * in Kakariko's object list (the same reason Enemy Randomizer works).
 */

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "ZEParty.h"
#include "soh/frame_interpolation.h"

#include <cmath>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Elf/z_en_elf.h"
#include "objects/object_zl2/object_zl2.h"
#include "objects/object_zl2_anime2/object_zl2_anime2.h"
#include "objects/object_du/object_du.h"
#include "objects/object_ru1/object_ru1.h"
#include "objects/object_ma1/object_ma1.h"
#include "objects/object_zo/object_zo.h"
#include "objects/object_oF1d_map/object_oF1d_map.h"
#include "objects/object_dog/object_dog.h"
extern PlayState* gPlayState;
}

namespace ZEParty {

// OPEN_DISPS/CLOSE_DISPS declare these at block scope, which inside this
// namespace names ZEParty:: functions. Forward them to the real ones.
void FrameInterpolation_RecordOpenChild(const void* a, int b) {
    ::FrameInterpolation_RecordOpenChild(a, b);
}
void FrameInterpolation_RecordCloseChild() {
    ::FrameInterpolation_RecordCloseChild();
}

bool gZeldaGreeted = false;
Vec3f gZeldaPos = { 0, 0, 0 };

#define A(x) ((AnimationHeader*)(x))

struct GuestDef {
    const char* name;
    FlexSkeletonHeader* skel;
    AnimationHeader* anims[24];
    f32 scale;
    // face: segment 8 = eyes (open, half, closed for blinking), 9/10 = mouth, nose
    void* eyes[3];
    void* seg9;
    void* seg10;
    f32 colRadius, colHeight;
    f32 headHeight; // where the rainbow fairy hovers
    bool turnsToPlayer;
};

static const GuestDef kGuests[GUEST_CHAR_MAX] = {
    // GUEST_ZELDA - candidates are every looping animation En_Zl3 uses
    { "Zelda", (FlexSkeletonHeader*)gZelda2Skel,
      { A(gZelda2Anime2Anim_009FBC), A(gZelda2Anime2Anim_003FF8), A(gZelda2Anime2Anim_008050),
        A(gZelda2Anime2Anim_007C84), A(gZelda2Anime2Anim_008AD0), A(gZelda2Anime2Anim_0099A0),
        A(gZelda2Anime2Anim_00AACC), A(gZelda2Anime2Anim_005A0C), A(gZelda2Anime2Anim_004408),
        A(gZelda2Anime2Anim_0061C4), A(gZelda2Anime2Anim_0054E0), A(gZelda2Anime2Anim_006F04),
        A(gZelda2Anime2Anim_0001D8), A(gZelda2Anime2Anim_0004F4), A(gZelda2Anime2Anim_00210C),
        A(gZelda2Anime2Anim_002710), A(gZelda2Anime2Anim_0014DC), A(gZelda2Anime2Anim_009BE4),
        A(gZelda2Anime2Anim_003D20), A(gZelda2Anime2Anim_007664), NULL },
      0.01f,
      { (void*)gZelda2EyeOpenTex, (void*)gZelda2EyeHalfTex, (void*)gZelda2EyeShutTex },
      NULL, (void*)gZelda2MouthHappyTex, 22.0f, 90.0f, 120.0f, true },
    // GUEST_DARUNIA
    { "Darunia", (FlexSkeletonHeader*)gDaruniaSkel,
      { A(gDaruniaDancingLoop1Anim), A(gDaruniaDancingLoop2Anim), A(gDaruniaDancingLoop3Anim),
        A(gDaruniaDancingLoop4Anim), A(gDaruniaIdleAnim), NULL },
      0.01f,
      { (void*)gDaruniaEyeOpenTex, (void*)gDaruniaEyeClosingTex, (void*)gDaruniaEyeShutTex },
      (void*)gDaruniaMouthHappyTex, (void*)gDaruniaNoseHappyTex, 32.0f, 100.0f, 135.0f, false },
    // GUEST_RUTO
    { "Ruto", (FlexSkeletonHeader*)gRutoChildSkel,
      { A(gRutoChildHoldArmsUpAnim), A(gRutoChildBringArmsUpAnim), A(gRutoChildWaitHandsOnHipsAnim),
        A(gRutoChildWaitHandsBehindBackAnim), A(gRutoChildWait2Anim), A(gRutoChildSquirmAnim), NULL },
      0.01f,
      { (void*)gRutoChildEyeOpenTex, (void*)gRutoChildEyeHalfTex, (void*)gRutoChildEyeClosedTex },
      (void*)gRutoChildMouthOpenTex, NULL, 16.0f, 50.0f, 75.0f, true },
    // GUEST_MALON
    { "Malon", (FlexSkeletonHeader*)gMalonChildSkel,
      { A(gMalonChildSingAnim), A(gMalonChildIdleAnim), A(gMalonChildRaiseHandsAnim), NULL },
      0.01f,
      { (void*)gMalonChildEyeOpenTex, (void*)gMalonChildEyeHalfTex, (void*)gMalonChildEyeClosedTex },
      (void*)gMalonChildSmilingMouthTex, NULL, 16.0f, 50.0f, 75.0f, true },
    // GUEST_ZORA
    { "Zora", (FlexSkeletonHeader*)gZoraSkel,
      { A(gZoraOpenArmsAnim), A(gZoraIdleAnim), A(gZoraHandsOnHipsTappingFootAnim), NULL },
      0.01f,
      { (void*)gZoraEyeOpenTex, (void*)gZoraEyeHalfTex, (void*)gZoraEyeClosedTex },
      NULL, NULL, 20.0f, 80.0f, 110.0f, true },
    // GUEST_GORON - En_Go2's animation table
    { "Goron", (FlexSkeletonHeader*)gGoronSkel,
      { A(gGoronAnim_0029A8), A(gGoronAnim_010590), A(gGoronAnim_003768), A(gGoronAnim_000750),
        A(gGoronAnim_000D5C), A(gGoronAnim_00161C), A(gGoronAnim_001A00), A(gGoronAnim_0021D0),
        A(gGoronAnim_0038E4), A(gGoronAnim_002D80), NULL },
      0.01f,
      { (void*)gGoronCsEyeOpenTex, (void*)gGoronCsEyeHalfTex, (void*)gGoronCsEyeClosedTex },
      (void*)gGoronCsMouthSmileTex, NULL, 30.0f, 80.0f, 110.0f, true },
    // GUEST_DOG
    { "Dog", (FlexSkeletonHeader*)gDogSkel,
      { A(gDogSitAnim), A(gDogBowAnim), A(gDogBow2Anim), A(gDogBarkAnim), A(gDogWalkAnim), NULL },
      0.0075f, { NULL, NULL, NULL }, NULL, NULL, 12.0f, 25.0f, 50.0f, true },
};

int GuestAnimCount(u8 ch) {
    int n = 0;
    while (n < 24 && kGuests[ch].anims[n] != NULL) {
        n++;
    }
    return n;
}

const char* GuestName(u8 ch) {
    return ch < GUEST_CHAR_MAX ? kGuests[ch].name : "Wisp";
}

// Slow circles of fairy light drifting over the village.
struct Orbit {
    f32 x, y, z, radius;
    s16 speed;
};
static const Orbit kOrbits[] = {
    { -580.0f, 230.0f, 430.0f, 190.0f, 0x0120 }, // around the maypole
};

static ColliderCylinderInit sCylInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_ON | OC1_TYPE_ALL, OC2_TYPE_2, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0, 0, 0 }, { 0, 0, 0 }, TOUCH_NONE, BUMP_NONE, OCELEM_ON },
    { 20, 60, 0, { 0, 0, 0 } },
};

typedef struct {
    Actor actor;
    SkelAnime skelAnime;
    ColliderCylinder collider;
    u8 ch;
    u8 anim;
    u8 eye;
    s16 blinkTimer;
    s16 angle;
    bool hasSkel;
    bool alt; // dogs: brown instead of cream
    s16 animTimer;
    u8 phase;
} ZEGuest;

// Adult Zelda's poses, from the En_Zl3 animations checked in the harness lineup
enum { ZELDA_ANIM_WAIT = 0, ZELDA_ANIM_OPEN_ARMS = 17, ZELDA_ANIM_CHEER = 15 };

static bool IsWisp(Actor* a) {
    return ((u16)a->params >> 8) == GUEST_WISP;
}

static void Guest_Init(Actor* thisx, PlayState* play) {
    ZEGuest* self = (ZEGuest*)thisx;
    self->hasSkel = false;
    self->blinkTimer = (s16)Rand_ZeroFloat(60.0f) + 20;
    self->eye = 0;
    self->angle = (s16)Rand_ZeroFloat(65535.0f);

    if (IsWisp(thisx)) {
        thisx->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        Actor_SpawnAsChild(&play->actorCtx, thisx, play, ACTOR_EN_ELF, thisx->world.pos.x, thisx->world.pos.y,
                           thisx->world.pos.z, 0, 0, 0, FAIRY_KOKIRI);
        return;
    }

    self->animTimer = 0;
    self->phase = 0;
    self->ch = (u16)thisx->params >> 8;
    self->anim = thisx->params & 0x7F;
    self->alt = (thisx->params & 0x80) != 0;
    if (self->ch >= GUEST_CHAR_MAX) {
        Actor_Kill(thisx);
        return;
    }
    const GuestDef& g = kGuests[self->ch];
    if (self->anim >= GuestAnimCount(self->ch)) {
        self->anim = 0;
    }

    // stand on the floor, wherever the table put us
    Vec3f probe = { thisx->world.pos.x, thisx->world.pos.y + 150.0f, thisx->world.pos.z };
    CollisionPoly* poly;
    f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &probe);
    if (floorY > BGCHECK_Y_MIN) {
        thisx->world.pos.y = thisx->home.pos.y = floorY;
    }

    Actor_SetScale(thisx, g.scale);
    SkelAnime_InitFlex(play, &self->skelAnime, g.skel, NULL, NULL, NULL, 0);
    Animation_PlayLoop(&self->skelAnime, g.anims[self->anim]);
    // a random start frame so a row of identical guests does not move in lockstep
    self->skelAnime.curFrame = Rand_ZeroFloat(self->skelAnime.endFrame);
    self->hasSkel = true;

    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCylInit);
    self->collider.dim.radius = (s16)g.colRadius;
    self->collider.dim.height = (s16)g.colHeight;
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    thisx->targetMode = 6;
    Actor_SetFocus(thisx, g.headHeight * 0.8f);

    // some guests get a rainbow fairy over their head
    if (HasFairyFor(self->ch)) {
        Actor_SpawnAsChild(&play->actorCtx, thisx, play, GuestActorId(), thisx->world.pos.x,
                           thisx->world.pos.y + g.headHeight, thisx->world.pos.z, 0, 0, 0, (s16)0xF0FF);
    }
    if (self->ch == GUEST_ZELDA) {
        gZeldaPos = thisx->world.pos;
    }
}

static void Guest_Destroy(Actor* thisx, PlayState* play) {
    ZEGuest* self = (ZEGuest*)thisx;
    if (self->hasSkel) {
        Collider_DestroyCylinder(play, &self->collider);
        SkelAnime_Free(&self->skelAnime, play);
    }
}

static void Wisp_Update(ZEGuest* self, PlayState* play) {
    Actor* thisx = &self->actor;
    u8 mode = thisx->params & 0xFF;
    if (mode == 0xFF) {
        Actor* p = thisx->parent;
        if (p == NULL || p->update == NULL) {
            Actor_Kill(thisx);
            return;
        }
        const GuestDef& g = kGuests[((ZEGuest*)p)->ch];
        thisx->world.pos = p->world.pos;
        thisx->world.pos.y += g.headHeight + 6.0f * Math_SinS(self->angle);
        self->angle += 0x300;
        return;
    }
    if (mode >= ARRAY_COUNT(kOrbits)) {
        Actor_Kill(thisx);
        return;
    }
    const Orbit& o = kOrbits[mode];
    self->angle += o.speed;
    thisx->world.pos.x = o.x + o.radius * Math_SinS(self->angle);
    thisx->world.pos.z = o.z + o.radius * Math_CosS(self->angle);
    thisx->world.pos.y = o.y + 20.0f * Math_SinS(self->angle * 3);
}

static void Guest_Update(Actor* thisx, PlayState* play) {
    ZEGuest* self = (ZEGuest*)thisx;
    if (IsWisp(thisx)) {
        Wisp_Update(self, play);
        return;
    }
    const GuestDef& g = kGuests[self->ch];

    SkelAnime_Update(&self->skelAnime);

    // Darunia works through his four dance loops, blending between them
    if (self->ch == GUEST_DARUNIA && ++self->animTimer >= 140) {
        self->animTimer = 0;
        self->anim = (self->anim + 1) % 4;
        AnimationHeader* a = g.anims[self->anim];
        Animation_Change(&self->skelAnime, a, 1.0f, 0.0f, Animation_GetLastFrame(a), ANIMMODE_LOOP, -8.0f);
    }
    // Zelda waits demurely, cheers when Harvie reaches her, then opens her arms
    if (self->ch == GUEST_ZELDA) {
        if (self->phase == 0 && gZeldaGreeted) {
            self->phase = 1;
            self->animTimer = 0;
            AnimationHeader* a = g.anims[ZELDA_ANIM_CHEER];
            Animation_Change(&self->skelAnime, a, 1.0f, 0.0f, Animation_GetLastFrame(a), ANIMMODE_LOOP, -6.0f);
        } else if (self->phase == 1 && ++self->animTimer >= 90) {
            self->phase = 2;
            AnimationHeader* a = g.anims[ZELDA_ANIM_OPEN_ARMS];
            Animation_Change(&self->skelAnime, a, 1.0f, 0.0f, Animation_GetLastFrame(a), ANIMMODE_LOOP, -10.0f);
        }
    }

    // blink: open ... half, closed, half, open
    if (--self->blinkTimer <= 0) {
        self->eye++;
        if (self->eye > 3) {
            self->eye = 0;
            self->blinkTimer = (s16)Rand_ZeroFloat(70.0f) + 30;
        } else {
            self->blinkTimer = 2;
        }
    }

    if (g.turnsToPlayer && thisx->xzDistToPlayer < 320.0f) {
        Math_SmoothStepToS(&thisx->shape.rot.y, thisx->yawTowardsPlayer, 6, 0x500, 0x80);
    } else {
        Math_SmoothStepToS(&thisx->shape.rot.y, thisx->home.rot.y, 10, 0x200, 0x40);
    }
    thisx->world.rot.y = thisx->shape.rot.y;

    if (self->ch == GUEST_ZELDA && !gZeldaGreeted && thisx->xzDistToPlayer < 230.0f) {
        gZeldaGreeted = true;
    }

    Collider_UpdateCylinder(thisx, &self->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
}

// Adult Zelda's head limb carries her hair and headdress strands, which are
// skinned to seven matrices in segment 0x0C. Ported from En_Zl3's override
// (the version without hair physics); without it the strands become one
// enormous polygon across the screen.
static s32 Zelda_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    if (limbIndex == 14) {
        Mtx* mtx = (Mtx*)Graph_Alloc(play->state.gfxCtx, sizeof(Mtx) * 7);
        OPEN_DISPS(play->state.gfxCtx);
        gSPSegment(POLY_OPA_DISP++, 0x0C, mtx);
        CLOSE_DISPS(play->state.gfxCtx);
        Matrix_Push();
        Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_APPLY);
        Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Translate(174.0f, -317.0f, 0.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[0]);
        Matrix_Translate(-410.0f, -184.0f, 0.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[1]);
        Matrix_Translate(-1019.0f, -26.0f, 0.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[2]);
        Matrix_Pop();
        Matrix_Push();
        Matrix_Translate(40.0f, 264.0f, 386.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[3]);
        Matrix_Translate(-446.0f, -52.0f, 84.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[4]);
        Matrix_Pop();
        Matrix_Push();
        Matrix_Translate(40.0f, 264.0f, -386.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[5]);
        Matrix_Translate(-446.0f, -52.0f, -84.0f, MTXMODE_APPLY);
        MATRIX_TOMTX(&mtx[6]);
        Matrix_Pop();
        Matrix_Pop();
    }
    return false;
}

static void Guest_Draw(Actor* thisx, PlayState* play) {
    ZEGuest* self = (ZEGuest*)thisx;
    if (!self->hasSkel) {
        return;
    }
    const GuestDef& g = kGuests[self->ch];
    static const u8 kBlinkSeq[4] = { 0, 1, 2, 1 };

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if (g.eyes[0] != NULL) {
        void* eye = g.eyes[kBlinkSeq[self->eye]];
        gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(eye));
        gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(g.seg9 != NULL ? g.seg9 : eye));
    }
    if (g.seg10 != NULL) {
        gSPSegment(POLY_OPA_DISP++, 0x0A, SEGMENTED_TO_VIRTUAL(g.seg10));
    }
    if (self->ch == GUEST_DOG) {
        // En_Dog tints the fur with the environment colour: cream or brown
        if (self->alt) {
            gDPSetEnvColor(POLY_OPA_DISP++, 150, 100, 50, 255);
        } else {
            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 200, 255);
        }
    } else {
        gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
    }
    gSPSegment(POLY_OPA_DISP++, 0x0B, &D_80116280[2]);
    gSPSegment(POLY_OPA_DISP++, 0x0C, &D_80116280[2]);
    SkelAnime_DrawFlexOpa(play, self->skelAnime.skeleton, self->skelAnime.jointTable, self->skelAnime.dListCount,
                          self->ch == GUEST_ZELDA ? Zelda_OverrideLimbDraw : NULL, NULL, self);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------

static s16 sGuestId = -1;
extern void Decor_Register(); // ZEDecor.cpp
extern s16 sDecorId;

void EnsureActorsRegistered() {
    if (sGuestId >= 0) {
        return;
    }
    ActorDBInit guest = {
        "ZE_Guest",
        "Zelda's Errands party guest",
        -1,
        ACTORCAT_NPC,
        0, // ordinary culling: off-screen guests stop updating (fairy anchors opt back in)
        OBJECT_GAMEPLAY_KEEP,
        sizeof(ZEGuest),
        (ActorFunc)Guest_Init,
        (ActorFunc)Guest_Destroy,
        (ActorFunc)Guest_Update,
        (ActorFunc)Guest_Draw,
        nullptr,
    };
    ActorDB::Instance->AddEntry(guest);
    sGuestId = (s16)ActorDB::Instance->RetrieveId("ZE_Guest");
    Decor_Register();
}

s16 GuestActorId() {
    return sGuestId;
}

void SpawnLineup(u8 ch) {
    // one guest in the middle of the plaza; the harness switches its animation
    EnsureActorsRegistered();
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, sGuestId, -580.0f, 0.0f, 430.0f, 0, 0, 0, GuestParams(ch, 0));
    LUSLOG_INFO("ZELINEUP %s: %d animations", GuestName(ch), GuestAnimCount(ch));
}

void SetLineupAnim(int idx) {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != NULL; a = a->next) {
        if (a->id != sGuestId || IsWisp(a)) {
            continue;
        }
        ZEGuest* g = (ZEGuest*)a;
        if (idx < GuestAnimCount(g->ch)) {
            g->anim = (u8)idx;
            Animation_PlayLoop(&g->skelAnime, kGuests[g->ch].anims[idx]);
            LUSLOG_INFO("ZELINEUP anim %d frames %.0f", idx, g->skelAnime.endFrame);
        }
    }
}

} // namespace ZEParty
