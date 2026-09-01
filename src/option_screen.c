#include "global.h"
#include "option_screen.h"
#include "international_string_util.h"
#include "text.h"
#include "window.h"
#include "constants/characters.h"

void OptionScreen_DrawChoice(u8 windowId, const u8 *text, u8 x, u8 y, bool32 selected)
{
    u8 dst[16];
    u16 i;

    for (i = 0; *text != EOS && i < ARRAY_COUNT(dst) - 1; i++)
        dst[i] = *(text++);

    // Indices 2 and 5 are the colour bytes of the {COLOR}{SHADOW} pair every
    // choice string opens with.
    if (selected)
    {
        dst[2] = TEXT_COLOR_RED;
        dst[5] = TEXT_COLOR_LIGHT_RED;
    }

    dst[i] = EOS;
    AddTextPrinterParameterized(windowId, FONT_NORMAL, dst, x, y + 1, TEXT_SKIP_DRAW, NULL);
}

void OptionScreen_DrawChoiceRow(u8 windowId, const u8 *const *texts, u32 count, u32 selection, u8 y)
{
    s32 widths[OPTION_SCREEN_MAX_CHOICES];
    s32 total = 0, gap, x;
    u32 i;

    for (i = 0; i < count; i++)
    {
        widths[i] = GetStringWidth(FONT_NORMAL, texts[i], 0);
        total += widths[i];
    }

    gap = (count > 1)
        ? ((OPTION_SCREEN_CHOICES_RIGHT - OPTION_SCREEN_CHOICES_LEFT) - total) / (s32)(count - 1)
        : 0;
    if (gap < 0)
        gap = 0;

    x = OPTION_SCREEN_CHOICES_LEFT;
    for (i = 0; i < count; i++)
    {
        // The last one is right-aligned rather than placed by the running total,
        // so the row ends flush however the division rounded.
        if (i == count - 1)
            x = GetStringRightAlignXOffset(FONT_NORMAL, texts[i], OPTION_SCREEN_CHOICES_RIGHT);

        OptionScreen_DrawChoice(windowId, texts[i], x, y, i == selection);
        x += widths[i] + gap;
    }
}
