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
//
// EXEMPT, COUNTS and SHINY allow a ball; AREA_SPENT and DUPLICATE refuse one.
// That split is what CanThrowBallAtCurrentEncounter encodes, and it is the
// thing to get right when adding a status -- only the refusing ones reach
// NuzlockePrepareBallBlockMessage.
enum {
    NUZLOCKE_ENCOUNTER_EXEMPT,
    NUZLOCKE_ENCOUNTER_AREA_SPENT, // this area's one chance is already used
    NUZLOCKE_ENCOUNTER_DUPLICATE,  // family already owned; keep looking
    NUZLOCKE_ENCOUNTER_COUNTS,     // the real encounter for this area
    NUZLOCKE_ENCOUNTER_SHINY,      // rule 9 rescued it; catchable, spends nothing
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

// Nuzlocke rule 7: no Pokémon may be raised past the level of the next boss
// trainer's strongest Pokémon.
//
// Enforcement is mostly by refusal rather than punishment: a Pokémon standing on
// the cap simply earns nothing, and the experience it would have taken is handed
// to a party member that still has room. Only a Pokémon *obtained* above the cap
// (a Lv 70 Rayquaza caught while the cap is 55) has to be benched, and that
// cannot ride rule 1's "HP pinned at 0" trick without killing it for good, so it
// needs explicit checks at the few places a Pokémon is sent out.
//
// The cap is derived entirely from flags that already exist, so no save data was
// added for this rule.
u8 NuzlockeGetLevelCap(void);
bool32 IsMonOverLevelCap(struct Pokemon *mon);
bool32 NuzlockePartyHasMonUnderLevelCap(void);
bool32 NuzlockeLevelCapAppliesToBattle(void);

// Truncates an experience total so it cannot represent a level past the cap.
// Takes a species rather than a Pokémon so it serves boxed ones too.
u32 NuzlockeClampExpToLevelCap(u16 species, u32 exp);

// Decides, in one pass over the party, exactly how much experience each slot
// takes from a knockout -- including the redirect away from capped Pokémon.
// Call once per knockout, then read the result back per slot.
void NuzlockeComputeExpAwards(u32 participantExp, u32 shareExp, u32 sentInPokes);
u16 NuzlockeGetExpAward(u8 partySlot);
bool32 NuzlockeMonEarnedExpNormally(u8 partySlot, u32 sentInPokes);

// Nuzlocke rule 8: whiting out ends the run for good -- the save can no longer
// be continued and the player has to start over.
//
// This is the first *optional* rule. Rules 1-7 are core and always on; whether a
// whiteout is fatal is a matter of taste, and the permissive reading (box the
// Pokémon that fell and carry on with what is left in the PC) already works.
// So the rule is stored per save rather than compiled in, ready for the planned
// options menu at new-game time to choose between the two.
#define NUZLOCKE_RULE_RESTART_ON_WHITEOUT (1 << 0)

// Nuzlocke rule 9 (shiny clause): a shiny wild Pokémon may be caught whatever
// would otherwise have refused it -- the area's one catch already spent, or a
// family already owned -- and catching it spends nothing, so the area keeps its
// chance. A shiny that IS the area's first encounter is still that encounter
// and spends it like any other.
//
// Optional rather than core for the same reason rule 8 is: players disagree on
// whether a run that turns down a shiny is playing the spirit of the rules.
#define NUZLOCKE_RULE_SHINY_CLAUSE (1 << 1)

// A shiny the clause rescued is a trophy: caught and kept, but permanently
// fainted, and never registered in the Pokédex. It never paid an area's
// encounter for its place, so it earns none of the things one buys -- no
// battling, no experience, no evolution family claimed, no dex progress.
//
// A shiny that WAS the area's encounter did pay, and is an ordinary Pokémon.
// The test is therefore trophy-ness, not shininess.
bool32 NuzlockeIsTrophyCatch(void);
void NuzlockeFaintTrophyCatch(struct Pokemon *mon);

// Future optional rules claim (1 << 2), (1 << 3), ... here.
#define NUZLOCKE_RULES_DEFAULT (NUZLOCKE_RULE_RESTART_ON_WHITEOUT | NUZLOCKE_RULE_SHINY_CLAUSE)

bool32 NuzlockeRuleEnabled(u32 rule);

// TRUE once a whiteout has ended this save. Read by the main menu, which then
// offers NEW GAME only.
bool32 NuzlockeIsRunOver(void);

// Called at the whiteout choke point. Returns FALSE to let the vanilla whiteout
// proceed; TRUE means the run is over and the caller must not continue.
bool32 NuzlockeTryEndRunOnWhiteOut(void);

// The options menu will run during the Birch speech, but NewGameInitData's
// ClearSav1 wipes all of SaveBlock1 afterwards -- so choices are staged in EWRAM
// and copied into the save once the wipe is done.
void NuzlockeStageRuleFlags(u8 flags);
u8 NuzlockeGetStagedRuleFlags(void);
void NuzlockeInitRulesForNewGame(void);

#endif // GUARD_NUZLOCKE_H
