#include "global.h"
#include "nuzlocke.h"
#include "event_data.h"
#include "naming_screen.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "string_util.h"
#include "constants/pokemon.h"
#include "constants/species.h"

// The nuzlockeDead flag steals a bit from PokemonSubstruct3's unusedRibbons.
// If that split ever grows the substruct past 12 bytes, NUM_SUBSTRUCT_BYTES
// changes and the save format silently breaks -- fail the build instead.
STATIC_ASSERT(sizeof(struct PokemonSubstruct3) == 12, NuzlockeSubstruct3SizeUnchanged);
STATIC_ASSERT(sizeof(struct BoxPokemon) == 80, NuzlockeBoxPokemonSizeUnchanged);

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
