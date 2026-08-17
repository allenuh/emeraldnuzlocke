#ifndef GUARD_NAMING_SCREEN_H
#define GUARD_NAMING_SCREEN_H

#include "main.h"

enum {
    NAMING_SCREEN_PLAYER,
    NAMING_SCREEN_BOX,
    NAMING_SCREEN_CAUGHT_MON,
    NAMING_SCREEN_NICKNAME,
    // Nuzlocke: same screen as NAMING_SCREEN_NICKNAME, but the player cannot
    // confirm an empty name. Used wherever a Pokemon is obtained (eggs, gifts,
    // the starter). NAMING_SCREEN_NICKNAME itself stays lenient so the Name
    // Rater can still be backed out of.
    NAMING_SCREEN_NICKNAME_REQUIRED,
    NAMING_SCREEN_WALDA,
};

void DoNamingScreen(u8 templateNum, u8 *destBuffer, u16 monSpecies, u16 monGender, u32 monPersonality, MainCallback returnCallback);

#endif // GUARD_NAMING_SCREEN_H
