#ifndef GUARD_MAIN_MENU_H
#define GUARD_MAIN_MENU_H

void CB2_InitMainMenu(void);
void CB2_ReinitMainMenu(void);

// Where the nuzlocke rules screen goes when the player starts the run. The Birch
// speech is one of the main menu's tasks rather than a screen of its own, so
// something has to put the menu's callbacks back before it can run.
void CB2_NewGameBirchSpeechAfterNuzlockeRules(void);
void CreateYesNoMenuParameterized(u8 x, u8 y, u16 baseTileNum, u16 baseBlock, u8 yesNoPalNum, u8 winPalNum);

#endif // GUARD_MAIN_MENU_H
