#include "global.h"
#include "nuzlocke.h"
#include "pokemon.h"
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
