#ifndef GUARD_KEY_SYSTEM_H
#define GUARD_KEY_SYSTEM_H

// The Key System: opt-in modifiers the player may switch on to loosen the run,
// after Black 2 / White 2's keys by way of FRLG-Plus. Everything else in this
// hack restricts the player; these are the one place that gives something back,
// which is why they live on a screen of their own rather than among the options.
//
// Reached from the OPTION screen, and so available both from the main menu and
// mid-run from the START menu.

// EXP. MODIFIER, applied to battle experience.
//
// Zero is deliberately not one of the settings. The field this is stored in was
// filler until now, so every save written before the key system existed -- and
// every freshly cleared SaveBlock2 -- reads back zero, and a run that silently
// switched itself to 0x on load would be a nasty surprise. Zero therefore means
// "never set" and is read as 1x.
//
// The five real settings are contiguous and already in display order, so a menu
// row index is just the value minus KEY_EXP_MODIFIER_0X.
#define KEY_EXP_MODIFIER_UNSET 0

enum
{
    KEY_EXP_MODIFIER_0X = 1,
    KEY_EXP_MODIFIER_HALF,
    KEY_EXP_MODIFIER_1X,
    KEY_EXP_MODIFIER_2X,
    KEY_EXP_MODIFIER_5X,
    KEY_EXP_MODIFIER_COUNT,
};

void CB2_InitKeySystemMenu(void);

// Scales an experience award. Applied after every vanilla multiplier and before
// the level cap clamps and redirects, so the cap still holds at 2x and 5x.
u32 KeySystemApplyExpModifier(u32 exp);

// INF. RARE CANDY: whether the Rare Candy in the bag is bottomless.
bool32 KeySystemInfiniteRareCandy(void);

// Puts the bottomless stack in the bag, or takes back the one the key granted.
// Called when the key is toggled, and again once a new game has wiped the bag.
void KeySystemSyncRareCandy(void);

// INF. TMS: whether teaching a TM leaves it in the bag, the way TMs work from
// Gen 5 on. Nothing is granted, so unlike the Rare Candy key this needs no
// guard against selling.
bool32 KeySystemInfiniteTMs(void);

#endif // GUARD_KEY_SYSTEM_H
