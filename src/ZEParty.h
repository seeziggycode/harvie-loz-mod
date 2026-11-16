#pragma once
/*
 * Zelda's Errands - the Kakariko party. Shared declarations.
 */
// GameInteractor.h first (it pulls in the C++ standard headers), then the
// game's C headers with C linkage - the order the rest of SoH uses.
#include "soh/Enhancements/game-interactor/GameInteractor.h"
extern "C" {
#include "z64.h"
}

namespace ZEParty {

// Characters a guest can be. Order matters: it is the guest params' high byte.
enum GuestChar : u8 {
    GUEST_ZELDA,   // adult Princess Zelda - greets her at the gate
    GUEST_DARUNIA, // dances, like he does to Saria's Song
    GUEST_RUTO,
    GUEST_MALON,   // sings
    GUEST_ZORA,
    GUEST_GORON,
    GUEST_DOG,
    GUEST_CHAR_MAX,
    GUEST_WISP = 0xF0, // invisible anchor that carries a rainbow fairy
};

// Registers the custom actors with SoH's ActorDB the first time it is called.
void EnsureActorsRegistered();
s16 GuestActorId();
s16 DecorActorId();

// params = (char << 8) | animIndex  for guests
// params = 0xF000 | orbitIndex      for an orbiting fairy wisp
// params = 0xF0FF                   for a wisp that hovers over its parent guest
inline s16 GuestParams(u8 ch, u8 anim) {
    return (s16)((ch << 8) | anim);
}

int GuestAnimCount(u8 ch);
const char* GuestName(u8 ch);

// Set by adult Zelda's actor when Harvie reaches her; read by the party director.
extern bool gZeldaGreeted;
extern Vec3f gZeldaPos;

bool HasFairyFor(u8 ch);
// Test harness: spawn every animation of one character in rows (see ZETest).
void SpawnLineup(u8 ch);
void SetLineupAnim(int idx);

// The party director registers its hooks here.
void RegisterParty();

} // namespace ZEParty
