#ifndef GUARD_NUZLOCKE_H
#define GUARD_NUZLOCKE_H

// Nuzlocke rule 1: a Pokémon that faints is dead for good. It can never be
// healed, revived, or sent into battle again, whether it went down in combat
// or to field poison.
//
// Enforcement leans on the fact that vanilla already refuses to use any mon at
// 0 HP. Marking a mon dead pins its HP at 0 permanently (see the MON_DATA_HP
// guard in SetMonData), so every existing "is this mon usable" check keeps it
// out for free.

bool32 IsMonNuzlockeDead(struct Pokemon *mon);
bool32 IsBoxMonNuzlockeDead(struct BoxPokemon *boxMon);
void MarkMonAsNuzlockeDead(struct Pokemon *mon);
void NuzlockeMarkFaintedPartyMons(void);

// Nuzlocke rule 2: every Pokémon must be given a nickname when obtained.
// Script specials used by the gift-Pokémon scripts; strict counterparts of
// ChangePokemonNickname / ChangeBoxPokemonNickname.
void NuzlockeNameReceivedPartyMon(void);
void NuzlockeNameReceivedBoxMon(void);

// Nuzlocke rules 3 & 4: only the first wild Pokémon met in an area may be
// caught, and duplicates of a family you already own don't consume that chance.
//
// How the current wild encounter relates to the rule. Anything not generated
// from a route's encounter table (legendaries, Sudowoodo, Kecleon, gift
// battles) stays EXEMPT and is always catchable.
enum {
    NUZLOCKE_ENCOUNTER_EXEMPT,
    NUZLOCKE_ENCOUNTER_AREA_SPENT, // this area's one chance is already used
    NUZLOCKE_ENCOUNTER_DUPLICATE,  // family already owned; keep looking
    NUZLOCKE_ENCOUNTER_COUNTS,     // the real encounter for this area
};

bool32 IsSpeciesFamilyOwned(u16 species);
bool32 IsAreaEncounterSpent(u8 mapSec);

// Called when a wild battle starts from an encounter table.
void NuzlockeEvaluateWildEncounter(void);
u8 GetNuzlockeEncounterStatus(void);
bool32 CanThrowBallAtCurrentEncounter(void);
u8 NuzlockePrepareBallBlockMessage(void);

// Called once a battle is over: consumes the area's chance if it was used.
void NuzlockeFinishWildEncounter(void);

#endif // GUARD_NUZLOCKE_H
