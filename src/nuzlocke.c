#include "global.h"
#include "nuzlocke.h"
#include "event_data.h"
#include "item.h"
#include "key_system.h"
#include "main.h"
#include "naming_screen.h"
#include "overworld.h"
#include "battle.h"
#include "data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "region_map.h"
#include "save.h"
#include "string_util.h"
#include "constants/battle_string_ids.h"
#include "constants/flags.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"
#include "constants/species.h"

extern const struct Evolution gEvolutionTable[][EVOS_PER_MON];

// The nuzlockeDead flag steals a bit from PokemonSubstruct3's unusedRibbons.
// If that split ever grows the substruct past 12 bytes, NUM_SUBSTRUCT_BYTES
// changes and the save format silently breaks -- fail the build instead.
STATIC_ASSERT(sizeof(struct PokemonSubstruct3) == 12, NuzlockeSubstruct3SizeUnchanged);
STATIC_ASSERT(sizeof(struct BoxPokemon) == 80, NuzlockeBoxPokemonSizeUnchanged);

// The rule 3 area bitfield was carved out of SaveBlock1's unused_3598 filler.
// If that split ever changes the total size, every field after it shifts and
// existing save files break -- fail the build instead of corrupting saves.
STATIC_ASSERT(offsetof(struct SaveBlock1, trainerHillTimes) == 0x3718, NuzlockeSaveBlock1LayoutUnchanged);
STATIC_ASSERT(NUZLOCKE_AREA_BYTES * 8 >= MAPSEC_COUNT, NuzlockeAreaBitfieldCoversAllAreas);
// Every further nuzlocke field is taken from the same filler, so it is the
// filler running out -- not the offsets moving -- that would bite first.
STATIC_ASSERT(NUZLOCKE_AREA_BYTES + 2 <= 0x180, NuzlockeSaveFieldsFitInFiller);

// The 5 Poké Balls the rival hands over in Birch's Lab are the point where the
// player can first comply with the catching rules at all. Before that, Routes
// 101 and 103 are the only reachable areas and the starter is the only Pokémon
// owned, so rules 1 and 3/4 would only punish a player with no way to obey them.
//
// FLAG_ADVENTURE_STARTED is set on the line directly below those giveitem calls,
// and vanilla already treats it as the "player may have Poké Balls" boundary --
// the Oldale Mart stocks none until it is set -- so no new save data is needed.
//
// Deliberately not applied to the other rules: the starter is received *before*
// this flag and must still be nicknamed (rule 2), and rules 5-7 cannot be
// violated this early. Hence the name -- this asks about the milestone, not
// about whether "the nuzlocke is on".
static bool32 HasPlayerReceivedPokeBalls(void)
{
    return FlagGet(FLAG_ADVENTURE_STARTED);
}

bool32 IsBoxMonNuzlockeDead(struct BoxPokemon *boxMon)
{
    return GetBoxMonData(boxMon, MON_DATA_NUZLOCKE_DEAD, NULL) != 0;
}

bool32 IsMonNuzlockeDead(struct Pokemon *mon)
{
    return GetMonData(mon, MON_DATA_NUZLOCKE_DEAD, NULL) != 0;
}

// Marks a Pokémon as permanently dead. Empty party slots and Eggs are skipped:
// an Egg can be at 0 HP without having fainted, and an empty slot is not a mon.
//
// This is the only function anywhere that sets the dead bit, so gating it here
// is what makes the whole of rule 1 dormant before the Poké Balls -- every faint
// path (Cmd_tryfaintmon, the post-battle net, field poison) runs through it.
// Leaving the bit unset also restores the vanilla whiteout heal for free: the
// HP pin in SetMonData only fires on that bit, so HealPlayerParty works again.
void MarkMonAsNuzlockeDead(struct Pokemon *mon)
{
    u8 dead = TRUE;

    if (!HasPlayerReceivedPokeBalls())
        return;
    if (!GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES, NULL))
        return;
    if (GetMonData(mon, MON_DATA_SANITY_IS_EGG, NULL))
        return;

    SetMonData(mon, MON_DATA_NUZLOCKE_DEAD, &dead);
}

// Safety net for faint paths that don't run through Cmd_tryfaintmon (Destiny
// Bond, Perish Song, the self-KO cases in battle_script_commands.c). Called
// once the battle is over, so anything still at 0 HP fainted during it.
void NuzlockeMarkFaintedPartyMons(void)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL) == 0)
            MarkMonAsNuzlockeDead(&gPlayerParty[i]);
    }
}

//
// Rules 3 & 4: one catch per area, with the duplicate clause.
// Rule 9 (shiny clause) rides on the same classification -- see the tail of
// NuzlockeEvaluateWildEncounter.
//

// Where the current wild encounter stands. Transient -- it only has to survive
// one battle, so it is deliberately not saved. The map section is captured at
// encounter time rather than re-read at battle end, so it cannot go stale.
static EWRAM_DATA u8 sEncounterStatus = NUZLOCKE_ENCOUNTER_EXEMPT;
static EWRAM_DATA u8 sEncounterMapSec = MAPSEC_NONE;

bool32 IsAreaEncounterSpent(u8 mapSec)
{
    if (mapSec >= MAPSEC_NONE)
        return FALSE;

    return (gSaveBlock1Ptr->nuzlockeAreaEncounterSpent[mapSec / 8] >> (mapSec % 8)) & 1;
}

static void MarkAreaEncounterSpent(u8 mapSec)
{
    if (mapSec >= MAPSEC_NONE)
        return;

    gSaveBlock1Ptr->nuzlockeAreaEncounterSpent[mapSec / 8] |= 1 << (mapSec % 8);
}

// Walks back down an evolution line to its first stage. The loop is bounded
// rather than trusting the table to be acyclic.
static u16 GetBaseEvolutionSpecies(u16 species)
{
    u32 step, i, k;

    // Unused slots in the table hold SPECIES_NONE as their target, so searching
    // for SPECIES_NONE would match one of them and walk to a bogus base.
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return species;

    for (step = 0; step < EVOS_PER_MON; step++)
    {
        u16 preEvo = SPECIES_NONE;

        for (i = 1; i < NUM_SPECIES && preEvo == SPECIES_NONE; i++)
        {
            for (k = 0; k < EVOS_PER_MON; k++)
            {
                if (gEvolutionTable[i][k].targetSpecies == species)
                {
                    preEvo = i;
                    break;
                }
            }
        }

        if (preEvo == SPECIES_NONE)
            break;

        species = preEvo;
    }

    return species;
}

// Recurses rather than walking a chain because branching lines exist (Eevee).
static bool32 IsAnyInFamilyCaught(u16 species)
{
    u32 i;

    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return FALSE;

    if (GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_CAUGHT))
        return TRUE;

    for (i = 0; i < EVOS_PER_MON; i++)
    {
        u16 target = gEvolutionTable[species][i].targetSpecies;

        if (target != SPECIES_NONE && IsAnyInFamilyCaught(target))
            return TRUE;
    }

    return FALSE;
}

// Rule 4: owning any member of the line counts, so a Poochyena on hand makes a
// wild Mightyena a duplicate.
bool32 IsSpeciesFamilyOwned(u16 species)
{
    return IsAnyInFamilyCaught(GetBaseEvolutionSpecies(species));
}

// Called from BattleSetup_StartWildBattle, which only ever runs for encounters
// generated from a route's encounter table. Scripted and legendary battles use
// different entry points and so are never classified -- they stay EXEMPT.
void NuzlockeEvaluateWildEncounter(void)
{
    u16 species;

    // Grace period: before the Poké Balls, don't classify the encounter at all.
    // Staying EXEMPT means NuzlockeFinishWildEncounter spends nothing, so Routes
    // 101 and 103 keep their one catch for when the player returns able to use
    // it. Set explicitly rather than assumed, so no stale state can leak in.
    if (!HasPlayerReceivedPokeBalls())
    {
        sEncounterStatus = NUZLOCKE_ENCOUNTER_EXEMPT;
        sEncounterMapSec = MAPSEC_NONE;
        return;
    }

    species = GetMonData(&gEnemyParty[0], MON_DATA_SPECIES, NULL);
    sEncounterMapSec = gMapHeader.regionMapSectionId;

    if (IsAreaEncounterSpent(sEncounterMapSec))
        sEncounterStatus = NUZLOCKE_ENCOUNTER_AREA_SPENT;
    else if (IsSpeciesFamilyOwned(species))
        sEncounterStatus = NUZLOCKE_ENCOUNTER_DUPLICATE;
    else
        sEncounterStatus = NUZLOCKE_ENCOUNTER_COUNTS;

    // Rule 9: a shiny is catchable whatever blocked it -- the area's chance
    // already spent, or a family already owned. Deliberately a pass over the
    // verdict above rather than another branch within it, because the clause
    // only rescues an encounter that was already going to be refused: a shiny
    // that IS the area's first encounter stays COUNTS and spends the chance
    // like any other. Written this way, that case needs no code of its own.
    //
    // The branch above always lands on one of the three classified statuses, so
    // testing COUNTS alone is enough to mean "was going to be refused".
    if (sEncounterStatus != NUZLOCKE_ENCOUNTER_COUNTS
     && NuzlockeRuleEnabled(NUZLOCKE_RULE_SHINY_CLAUSE)
     && IsMonShiny(&gEnemyParty[0]))
        sEncounterStatus = NUZLOCKE_ENCOUNTER_SHINY;
}

u8 GetNuzlockeEncounterStatus(void)
{
    return sEncounterStatus;
}

bool32 CanThrowBallAtCurrentEncounter(void)
{
    return sEncounterStatus == NUZLOCKE_ENCOUNTER_EXEMPT
        || sEncounterStatus == NUZLOCKE_ENCOUNTER_COUNTS
        || sEncounterStatus == NUZLOCKE_ENCOUNTER_SHINY;
}

// Buffers the reason into gStringVar1 and returns which message to print, so
// the three places that can refuse a throw all word it the same way.
//
// Only ever reached for a status this rejects: all three callers sit behind
// !CanThrowBallAtCurrentEncounter(). That is what lets the tail below assume
// AREA_SPENT -- a new *allowed* status is free, but a new *refused* one has to
// be spelled out here, and in the two-way ternary in ItemUseInBattle_PokeBall.
u8 NuzlockePrepareBallBlockMessage(void)
{
    if (sEncounterStatus == NUZLOCKE_ENCOUNTER_DUPLICATE)
    {
        StringCopy(gStringVar1, gSpeciesNames[GetMonData(&gEnemyParty[0], MON_DATA_SPECIES, NULL)]);
        return B_MSG_NUZLOCKE_DUPLICATE;
    }

    GetMapName(gStringVar1, sEncounterMapSec, 0);
    return B_MSG_NUZLOCKE_AREA_SPENT;
}

// Rule 9: whether the Pokémon about to be handed over only got its ball because
// the shiny clause rescued the encounter. Read from the cached status rather
// than re-derived at catch time on purpose: Cmd_trysetcaughtmondexflags runs
// first and has already set the dex caught flag by then, so asking "was this a
// duplicate?" that late would always answer no.
bool32 NuzlockeIsTrophyCatch(void)
{
    return sEncounterStatus == NUZLOCKE_ENCOUNTER_SHINY;
}

// Faints a trophy for good, on its way to the player.
//
// MarkMonAsNuzlockeDead only sets the flag. Rule 1 has only ever called it on
// Pokémon that were already at 0 HP, and the HP pin in SetMonData is reactive --
// it fires on an HP write, it does not sweep -- so a trophy caught in good
// health would otherwise arrive at full HP wearing a dead bit, selectable and
// battle-ready. The faint has to be spelled out.
//
// Called before GiveMonToPlayer so one hook covers both destinations: the party
// path memcpys the whole struct Pokemon and the box path memcpys mon->box, so
// the dead bit rides along either way. In the box case the HP write is simply
// discarded -- BoxPokemon has no HP -- and CalculateMonStats re-zeroes it on
// withdrawal from the same bit, which is the quirk rule 1 already closed.
//
// MarkMonAsNuzlockeDead's Poké Ball gate is never reached from here: before the
// balls the evaluator returns EXEMPT, so SHINY cannot occur.
void NuzlockeFaintTrophyCatch(struct Pokemon *mon)
{
    u16 zeroHp = 0;
    u32 zeroStatus = STATUS1_NONE;

    if (!NuzlockeIsTrophyCatch())
        return;

    MarkMonAsNuzlockeDead(mon);
    SetMonData(mon, MON_DATA_HP, &zeroHp);
    // A Pokémon softened with Sleep or Paralysis before the ball would otherwise
    // sit in the party fainted and still showing SLP.
    SetMonData(mon, MON_DATA_STATUS, &zeroStatus);
}

// Every outcome spends the chance -- caught, fainted, fled, or ran -- so the
// battle result is deliberately not consulted. Resetting to EXEMPT also stops a
// later scripted battle from inheriting this battle's state.
//
// Only COUNTS spends, which is also how rule 9 gets its "a shiny does not count
// as the encounter for that area" for free: a rescued shiny is SHINY, not
// COUNTS, so the area keeps its chance no matter how the battle ends.
void NuzlockeFinishWildEncounter(void)
{
    if (sEncounterStatus == NUZLOCKE_ENCOUNTER_COUNTS)
        MarkAreaEncounterSpent(sEncounterMapSec);

    sEncounterStatus = NUZLOCKE_ENCOUNTER_EXEMPT;
    sEncounterMapSec = MAPSEC_NONE;
}

//
// Rule 7: no Pokémon past the next boss trainer's strongest Pokémon.
//

// Indexed by badge count, so entry N is the cap while gym N+1 is still ahead.
// The last entry is Drake's Salamence rather than Wallace's Milotic: the hardcore
// rule caps the run at the *final Elite Four member* on entering the League, and
// once the gauntlet has begun there is no way back out to train, so a single flat
// cap covers all five battles. (Wallace's own 58 would go here if the cap were
// ever changed to step through the Elite Four one member at a time.)
static const u8 sLevelCaps[NUM_BADGES + 1] =
{
    [0] = 15, // Roxanne
    [1] = 19, // Brawly
    [2] = 24, // Wattson
    [3] = 29, // Flannery
    [4] = 31, // Norman
    [5] = 33, // Winona
    [6] = 42, // Tate & Liza
    [7] = 46, // Juan
    [8] = 55, // Drake, the final Elite Four member
};

#define NUZLOCKE_CAP_METEOR_FALLS 78 // Steven's post-game rematch in Meteor Falls

static u32 GetBadgeCount(void)
{
    u32 i, count = 0;

    for (i = 0; i < NUM_BADGES; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            count++;
    }

    return count;
}

// Returns MAX_LEVEL once nothing is left to cap against, so that every
// "level >= cap" test in this hack also subsumes the vanilla MAX_LEVEL test it
// replaced -- no vanilla behaviour is lost when the cap is lifted.
u8 NuzlockeGetLevelCap(void)
{
    // Turning the rule off is one line because of the promise above: MAX_LEVEL
    // means "nothing left to cap against", and every site in the hack is written
    // to degrade into the vanilla test it replaced when it sees that. So this
    // gate switches off the lead-benching, the rare candy refusal, the exp clamp
    // and the daycare clamp together, without any of them knowing about it.
    if (!NuzlockeLevelCapsEnabled())
        return MAX_LEVEL;

    if (FlagGet(FLAG_DEFEATED_METEOR_FALLS_STEVEN))
        return MAX_LEVEL;
    if (FlagGet(FLAG_SYS_GAME_CLEAR))
        return NUZLOCKE_CAP_METEOR_FALLS;

    return sLevelCaps[GetBadgeCount()];
}

// Standing exactly on the cap is legal -- only exceeding it benches a Pokémon.
// Since experience is clamped, this can only ever be true of one obtained above
// the cap, such as a Lv 70 Rayquaza caught while the League cap is 55.
bool32 IsMonOverLevelCap(struct Pokemon *mon)
{
    if (!GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES, NULL))
        return FALSE;
    if (GetMonData(mon, MON_DATA_SANITY_IS_EGG, NULL))
        return FALSE;

    return GetMonData(mon, MON_DATA_LEVEL, NULL) > NuzlockeGetLevelCap();
}

// Whether anything in the party could actually lead a battle. Fainted and dead
// Pokémon are excluded deliberately: this exists to answer "is benching the
// over-cap ones survivable", and a party of one dead Pokémon and one over-cap
// Pokémon has nothing to fall back on.
bool32 NuzlockePartyHasMonUnderLevelCap(void)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];

        if (!GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES, NULL))
            continue;
        if (GetMonData(mon, MON_DATA_SANITY_IS_EGG, NULL))
            continue;
        if (GetMonData(mon, MON_DATA_HP, NULL) == 0)
            continue;
        if (!IsMonOverLevelCap(mon))
            return TRUE;
    }

    return FALSE;
}

// The same exemptions Cmd_getexp already applies to experience. It matters most
// for the Battle Frontier and Battle Tower: those swap in their own party, whose
// levels are the facility's business, and benching a Lv 100 rental against a cap
// of 78 would make a challenge unplayable.
bool32 NuzlockeLevelCapAppliesToBattle(void)
{
    return !(gBattleTypeFlags & (BATTLE_TYPE_LINK
                               | BATTLE_TYPE_RECORDED_LINK
                               | BATTLE_TYPE_TRAINER_HILL
                               | BATTLE_TYPE_FRONTIER
                               | BATTLE_TYPE_SAFARI
                               | BATTLE_TYPE_BATTLE_TOWER
                               | BATTLE_TYPE_EREADER_TRAINER));
}

static u32 GetExpForLevel(u16 species, u8 level)
{
    return gExperienceTables[gSpeciesInfo[species].growthRate][level];
}

u32 NuzlockeClampExpToLevelCap(u16 species, u32 exp)
{
    u32 capExp = GetExpForLevel(species, NuzlockeGetLevelCap());

    return (exp > capExp) ? capExp : exp;
}

//
// Experience redirection.
//
// gBattleMoveDamage doubles as the exp amount inside Cmd_getexp, and that state
// machine walks the party one slot at a time. Working out a redirect on the fly
// would mean looking backwards at slots it has already passed, so instead the
// whole award table is settled up front, in one pass, and Cmd_getexp just reads
// a finished number out of it.
static EWRAM_DATA u16 sExpAwards[PARTY_SIZE] = {0};

static u8 GetPartyMonHoldEffect(struct Pokemon *mon)
{
    u16 item = GetMonData(mon, MON_DATA_HELD_ITEM, NULL);

    if (item == ITEM_ENIGMA_BERRY)
        return gSaveBlock1Ptr->enigmaBerry.holdEffect;

    return GetItemHoldEffect(item);
}

// Whether this slot would have earned exp in vanilla: it fought, or it is holding
// an Exp Share. Redirect recipients deliberately do not qualify -- experience
// moves between Pokémon under this rule, but EVs do not.
bool32 NuzlockeMonEarnedExpNormally(u8 partySlot, u32 sentInPokes)
{
    if (sentInPokes & (1 << partySlot))
        return TRUE;

    return GetPartyMonHoldEffect(&gPlayerParty[partySlot]) == HOLD_EFFECT_EXP_SHARE;
}

// How much exp this slot can still absorb before it would stand on the cap.
// Zero for anything that cannot take exp at all: an empty slot, an Egg, or a
// Pokémon at 0 HP -- which covers both ordinary fainting and rule 1's dead
// Pokémon, matching the exclusion vanilla already applies when counting shares.
static u32 GetExpHeadroom(struct Pokemon *mon)
{
    u32 currentExp, capExp;

    if (GetMonData(mon, MON_DATA_SPECIES, NULL) == SPECIES_NONE)
        return 0;
    if (GetMonData(mon, MON_DATA_SANITY_IS_EGG, NULL))
        return 0;
    if (GetMonData(mon, MON_DATA_HP, NULL) == 0)
        return 0;

    currentExp = GetMonData(mon, MON_DATA_EXP, NULL);
    capExp = GetExpForLevel(GetMonData(mon, MON_DATA_SPECIES, NULL), NuzlockeGetLevelCap());

    return (currentExp >= capExp) ? 0 : capExp - currentExp;
}

// The multipliers vanilla applies in Cmd_getexp state 2. They are replicated
// here, ahead of the clamp, rather than left where they were: the amount that
// gets redirected has to be the final one, or a Lucky Egg applied afterwards
// would push the recipient straight back over the cap.
static u32 ApplyExpMultipliers(struct Pokemon *mon, u8 partySlot, u32 exp)
{
    if (GetPartyMonHoldEffect(mon) == HOLD_EFFECT_LUCKY_EGG)
        exp = (exp * 150) / 100;
    if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
        exp = (exp * 150) / 100;

    // A traded Pokémon earns more, except for the ones on loan from an in-game
    // partner, which are not really the player's.
    if (IsTradedMon(mon) && !(gBattleTypeFlags & BATTLE_TYPE_INGAME_PARTNER && partySlot >= 3))
        exp = (exp * 150) / 100;

    // The key system's EXP. MODIFIER goes last, so the level cap clamps and
    // redirects the amount the player will actually receive rather than the
    // amount before it. The arithmetic here is u32 even though the vanilla exp
    // value is a u16, so 5x cannot overflow on the way through.
    exp = KeySystemApplyExpModifier(exp);

    return exp;
}

void NuzlockeComputeExpAwards(u32 participantExp, u32 shareExp, u32 sentInPokes)
{
    u32 i, pool = 0;
    u32 headroom[PARTY_SIZE];

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        u32 earned = 0;

        headroom[i] = GetExpHeadroom(mon);

        if (GetMonData(mon, MON_DATA_HP, NULL) != 0)
        {
            if (sentInPokes & (1 << i))
                earned = participantExp;
            if (GetPartyMonHoldEffect(mon) == HOLD_EFFECT_EXP_SHARE)
                earned += shareExp;

            earned = ApplyExpMultipliers(mon, i, earned);
        }

        // A Pokémon standing on the cap has no headroom, so its entire share
        // lands in the pool -- which is the rule.
        if (earned > headroom[i])
        {
            pool += earned - headroom[i];
            earned = headroom[i];
        }

        sExpAwards[i] = earned;
        headroom[i] -= earned;
    }

    // Hand the pool to whoever still has room, in party order. Redirected exp is
    // not multiplied again; it arrives exactly as the donor would have taken it.
    //
    // The redirect is the one part of the rule the MAX_LEVEL trick above does not
    // switch off by itself: with no cap, the only Pokémon that overflows is one
    // at level 100, and vanilla throws that exp away rather than passing it on.
    // Skipping the loop keeps a caps-off run identical to vanilla.
    for (i = 0; i < PARTY_SIZE && pool != 0 && NuzlockeLevelCapsEnabled(); i++)
    {
        u32 give = (pool < headroom[i]) ? pool : headroom[i];

        sExpAwards[i] += give;
        headroom[i] -= give;
        pool -= give;
    }

    // Whatever is still in the pool has nowhere to go, and is lost for good.

    // The controllers round the award through an s16 on its way to the exp bar,
    // so a large pool landing on a low-level Pokémon must not go negative.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (sExpAwards[i] > 0x7FFF)
            sExpAwards[i] = 0x7FFF;
    }
}

u16 NuzlockeGetExpAward(u8 partySlot)
{
    if (partySlot >= PARTY_SIZE)
        return 0;

    return sExpAwards[partySlot];
}

// Rule 2: gift Pokemon must be nicknamed like caught ones.
//
// These mirror ChangePokemonNickname / ChangeBoxPokemonNickname in tv.c, but
// use the strict naming template. They exist as separate specials rather than
// changing the originals because the Name Rater shares those, and confirming a
// blank name is how the player backs out of him -- making them strict would
// trap the player in his house.
//
// The vanilla gift scripts already branch on party-vs-PC and set the relevant
// vars, so these reuse that plumbing unchanged.
static void CB2_SetReceivedPartyMonNickname(void)
{
    SetMonData(&gPlayerParty[gSpecialVar_0x8004], MON_DATA_NICKNAME, gStringVar2);
    CB2_ReturnToFieldContinueScriptPlayMapMusic();
}

void NuzlockeNameReceivedPartyMon(void)
{
    struct Pokemon *mon = &gPlayerParty[gSpecialVar_0x8004];

    GetMonData(mon, MON_DATA_NICKNAME, gStringVar3);
    GetMonData(mon, MON_DATA_NICKNAME, gStringVar2);
    DoNamingScreen(NAMING_SCREEN_NICKNAME_REQUIRED, gStringVar2,
                   GetMonData(mon, MON_DATA_SPECIES, NULL),
                   GetMonGender(mon),
                   GetMonData(mon, MON_DATA_PERSONALITY, NULL),
                   CB2_SetReceivedPartyMonNickname);
}

static void CB2_SetReceivedBoxMonNickname(void)
{
    SetBoxMonNickAt(gSpecialVar_MonBoxId, gSpecialVar_MonBoxPos, gStringVar2);
    CB2_ReturnToFieldContinueScriptPlayMapMusic();
}

// Rule 1 leaves dead Pokemon parked in the party, so receiving a gift on a full
// party -- and therefore naming it in the box -- is a normal case in this hack.
void NuzlockeNameReceivedBoxMon(void)
{
    struct BoxPokemon *boxMon = GetBoxedMonPtr(gSpecialVar_MonBoxId, gSpecialVar_MonBoxPos);

    GetBoxMonData(boxMon, MON_DATA_NICKNAME, gStringVar3);
    GetBoxMonData(boxMon, MON_DATA_NICKNAME, gStringVar2);
    DoNamingScreen(NAMING_SCREEN_NICKNAME_REQUIRED, gStringVar2,
                   GetBoxMonData(boxMon, MON_DATA_SPECIES, NULL),
                   GetBoxMonGender(boxMon),
                   GetBoxMonData(boxMon, MON_DATA_PERSONALITY, NULL),
                   CB2_SetReceivedBoxMonNickname);
}

//
// Rule 8: whiting out ends the run.
//

// The rules a new save will be started under. This is staged in EWRAM rather
// than written straight to the save because the rules menu that sets it runs
// before the Birch speech, and NewGameInitData's ClearSav1 wipes all of
// SaveBlock1 afterwards -- anything written early would be erased moments later.
// NuzlockeInitRulesForNewGame copies it across once the wipe is done.
//
// The initialiser is documentation, not code: ewram_data is a NOLOAD section, so
// nothing is copied into it at boot and this actually starts at zero. It does
// not matter, because the menu always stages a full set of flags before a run
// can begin -- but do not add a path here that relies on the default.
static EWRAM_DATA u8 sPendingRuleFlags = NUZLOCKE_RULES_DEFAULT;

void NuzlockeStageRuleFlags(u8 flags)
{
    sPendingRuleFlags = flags;
}

u8 NuzlockeGetStagedRuleFlags(void)
{
    return sPendingRuleFlags;
}

void NuzlockeInitRulesForNewGame(void)
{
    gSaveBlock1Ptr->nuzlockeRuleFlags = sPendingRuleFlags;
}

// Save files made before the optional rules existed read back 0 here, which is
// exactly right: they keep the permissive behaviour they were played under.
bool32 NuzlockeRuleEnabled(u32 rule)
{
    return (gSaveBlock1Ptr->nuzlockeRuleFlags & rule) != 0;
}

// The three rules whose bits are stored inverted, read the way round they are
// actually thought about. Keeping the inversion behind these means a save with
// no flags at all -- one written before the rules menu existed -- answers TRUE
// to all three, which is the behaviour it was played under.

bool32 NuzlockeBattleStyleIsSet(void)
{
    return !NuzlockeRuleEnabled(NUZLOCKE_RULE_BATTLE_STYLE_SHIFT);
}

bool32 NuzlockeBagItemsAllowedInBattle(void)
{
    return NuzlockeRuleEnabled(NUZLOCKE_RULE_ALLOW_BAG_ITEMS);
}

bool32 NuzlockeLevelCapsEnabled(void)
{
    return !NuzlockeRuleEnabled(NUZLOCKE_RULE_NO_LEVEL_CAPS);
}

bool32 NuzlockeIsRunOver(void)
{
    return gSaveBlock1Ptr->nuzlockeRunOver != 0;
}

// The screen is already black by the time this runs -- CB2_WhiteOut holds it for
// 120 frames before handing over -- so this is just a beat of silence between the
// "whited out!" message and the reset, rather than a fade. The explanation is
// waiting on the main menu.
static void CB2_NuzlockeRunOver(void)
{
    if (++gMain.state >= 40)
        DoSoftReset();
}

// Called from CB2_WhiteOut, the one place every whiteout in the game passes
// through -- battle losses, field poison, and the Mossdeep multi battle alike.
// Battle Frontier, Pyramid, Pike, Trainer Hill and Secret Base losses never
// reach it, so they need no exemption here.
//
// The early-game grace period is the same one rules 1 and 3/4 use: before the
// Poké Balls the player has one Pokémon and no way to build a safety net, so a
// loss on Route 101 heals as it always did. Ending the run there would punish a
// player for a rule they had no means to obey.
//
// The save is committed here, before anything is drawn, so a whiteout the player
// has already seen cannot be undone by resetting the console.
bool32 NuzlockeTryEndRunOnWhiteOut(void)
{
    if (!NuzlockeRuleEnabled(NUZLOCKE_RULE_RESTART_ON_WHITEOUT))
        return FALSE;
    if (!HasPlayerReceivedPokeBalls())
        return FALSE;

    gSaveBlock1Ptr->nuzlockeRunOver = TRUE;
    TrySavingData(SAVE_NORMAL);

    gMain.state = 0;
    SetMainCallback2(CB2_NuzlockeRunOver);
    return TRUE;
}
