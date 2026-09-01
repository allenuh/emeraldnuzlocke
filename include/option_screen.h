#ifndef GUARD_OPTION_SCREEN_H
#define GUARD_OPTION_SCREEN_H

// Drawing shared by the option-menu-shaped screens -- the OPTION menu itself,
// KEY SYSTEM, and NUZLOCKE RULES. They each keep their own window and background
// setup, which differs; what they share is how a row of choices is laid out and
// how the selected one is coloured.
//
// The choices on every such row live between these two x coordinates, whatever
// screen they are on, so a player moving between them sees one column.
#define OPTION_SCREEN_CHOICES_LEFT  104
#define OPTION_SCREEN_CHOICES_RIGHT 198

// The widest row any of these screens has: the key system's exp modifier.
#define OPTION_SCREEN_MAX_CHOICES 5

// Prints one choice, in red when selected and green when not.
//
// The colour is not a parameter but an overwrite: every choice string starts
// with {COLOR GREEN}{SHADOW LIGHT_GREEN}, and this replaces the two colour bytes
// in a copy of it. So a string passed here MUST carry that prefix, and must be
// short enough to survive the 16-byte buffer -- six bytes of prefix leaves nine
// characters.
void OptionScreen_DrawChoice(u8 windowId, const u8 *text, u8 x, u8 y, bool32 selected);

// Places a whole row of choices across the span above, measuring each one and
// spreading the slack evenly between them. option_menu.c places its two- and
// three-choice rows with hand-tuned offsets instead; that does not survive five
// choices, and there is nothing to be gained by placing any of them by eye.
void OptionScreen_DrawChoiceRow(u8 windowId, const u8 *const *texts, u32 count, u32 selection, u8 y);

#endif // GUARD_OPTION_SCREEN_H
