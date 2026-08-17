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

#endif // GUARD_NUZLOCKE_H
