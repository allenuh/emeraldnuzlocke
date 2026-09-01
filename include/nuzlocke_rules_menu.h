#ifndef GUARD_NUZLOCKE_RULES_MENU_H
#define GUARD_NUZLOCKE_RULES_MENU_H

// The screen that decides which nuzlocke rules a run is played under. It sits
// between NEW GAME and the Birch speech, and is the only place the rules are
// ever set: they are fixed for the life of the save, which is what makes them
// rules rather than options.
//
// START stages the choices with NuzlockeStageRuleFlags and hands over to
// CB2_NewGameBirchSpeechAfterNuzlockeRules; B abandons the new game and returns
// to the main menu with nothing staged.
void CB2_InitNuzlockeRulesMenu(void);

#endif // GUARD_NUZLOCKE_RULES_MENU_H
