#ifndef GUARD_ENCOUNTER_TRACKER_H
#define GUARD_ENCOUNTER_TRACKER_H

// The encounter tracker: every area that can produce a wild Pokemon, what the
// player met there under rules 3 and 4, and what else that area could have
// offered.
//
// The area list and the encounter tables are read straight out of
// gWildMonHeaders rather than restated in a table of their own, so the screen
// cannot disagree with what the game actually rolls. Only the outcome per area
// is stored, in SaveBlock1's nuzlockeAreaRecords.
//
// Reached from the START menu. Returns through gMain.savedCallback.
void CB2_InitEncounterTracker(void);

#endif // GUARD_ENCOUNTER_TRACKER_H
