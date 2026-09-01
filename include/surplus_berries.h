#ifndef GUARD_SURPLUS_BERRIES_H
#define GUARD_SURPLUS_BERRIES_H

// The Surplus Berries box outside the Berry Master's house on Route 123, which
// gives away the EV-lowering berries for nothing.

// Whether an item is one of the six the box hands out. Used by the sell menu:
// they are free to take and worth money at a Mart, which would otherwise be an
// unlimited money press.
bool32 IsSurplusBerry(u16 itemId);

// Script special. Opens the box's shop.
void OpenSurplusBerryShop(void);

#endif // GUARD_SURPLUS_BERRIES_H
