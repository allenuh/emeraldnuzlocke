#include "global.h"
#include "nuzlocke.h"
#include "event_data.h"
#include "naming_screen.h"
#include "overworld.h"
#include "battle.h"
#include "data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "region_map.h"
#include "string_util.h"
#include "constants/battle_string_ids.h"
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
void MarkMonAsNuzlockeDead(struct Pokemon *mon)
{
    u8 dead = TRUE;

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
    u16 species = GetMonData(&gEnemyParty[0], MON_DATA_SPECIES, NULL);

    sEncounterMapSec = gMapHeader.regionMapSectionId;

    if (IsAreaEncounterSpent(sEncounterMapSec))
        sEncounterStatus = NUZLOCKE_ENCOUNTER_AREA_SPENT;
    else if (IsSpeciesFamilyOwned(species))
        sEncounterStatus = NUZLOCKE_ENCOUNTER_DUPLICATE;
    else
        sEncounterStatus = NUZLOCKE_ENCOUNTER_COUNTS;
}

u8 GetNuzlockeEncounterStatus(void)
{
    return sEncounterStatus;
}

bool32 CanThrowBallAtCurrentEncounter(void)
{
    return sEncounterStatus == NUZLOCKE_ENCOUNTER_EXEMPT
        || sEncounterStatus == NUZLOCKE_ENCOUNTER_COUNTS;
}

// Buffers the reason into gStringVar1 and returns which message to print, so
// the three places that can refuse a throw all word it the same way.
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

// Every outcome spends the chance -- caught, fainted, fled, or ran -- so the
// battle result is deliberately not consulted. Resetting to EXEMPT also stops a
// later scripted battle from inheriting this battle's state.
void NuzlockeFinishWildEncounter(void)
{
    if (sEncounterStatus == NUZLOCKE_ENCOUNTER_COUNTS)
        MarkAreaEncounterSpent(sEncounterMapSec);

    sEncounterStatus = NUZLOCKE_ENCOUNTER_EXEMPT;
    sEncounterMapSec = MAPSEC_NONE;
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
