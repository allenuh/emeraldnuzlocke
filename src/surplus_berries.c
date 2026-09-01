#include "global.h"
#include "surplus_berries.h"
#include "shop.h"
#include "constants/items.h"

// The six berries that lower EVs. Nothing in vanilla sells them and they grow
// only from planted trees, so correcting a spread is a multi-day farming chore
// -- which a nuzlocke, where a mis-trained Pokemon cannot simply be replaced,
// makes worse. The box outside the Berry Master's house hands them out instead.
//
// Read as a shop stock list, so it terminates the way SetShopItemsForSale
// expects rather than by a count.
static const u16 sSurplusBerries[] =
{
    ITEM_POMEG_BERRY,
    ITEM_KELPSY_BERRY,
    ITEM_QUALOT_BERRY,
    ITEM_HONDEW_BERRY,
    ITEM_GREPA_BERRY,
    ITEM_TAMATO_BERRY,
    ITEM_NONE
};

bool32 IsSurplusBerry(u16 itemId)
{
    u32 i;

    // ITEM_NONE terminates the list and is not a berry, so it must not match an
    // empty bag slot on its way through the sell menu.
    if (itemId == ITEM_NONE)
        return FALSE;

    for (i = 0; i < ARRAY_COUNT(sSurplusBerries); i++)
    {
        if (sSurplusBerries[i] == itemId)
            return TRUE;
    }

    return FALSE;
}

void OpenSurplusBerryShop(void)
{
    CreateFreeMartMenu(sSurplusBerries);
}
