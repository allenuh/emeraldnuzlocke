#include "global.h"
#include "key_system.h"
#include "bg.h"
#include "gpu_regs.h"
#include "international_string_util.h"
#include "item.h"
#include "main.h"
#include "menu.h"
#include "option_menu.h"
#include "option_screen.h"
#include "overworld.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/items.h"
#include "constants/rgb.h"

// The three key fields were carved out of SaveBlock2's filler_90[0x8]. If that
// split ever changes the total size, every field after it shifts and existing
// save files break -- fail the build instead of corrupting saves.
STATIC_ASSERT(offsetof(struct SaveBlock2, localTimeOffset) == 0x98, KeySystemSaveBlock2LayoutUnchanged);

#define tMenuSelection data[0]
#define tExpModifier   data[1]
#define tRareCandy     data[2]
#define tInfiniteTMs   data[3]
#define tNoFlash       data[4]

enum
{
    MENUITEM_EXP_MODIFIER,
    MENUITEM_RARE_CANDY,
    MENUITEM_INFINITE_TMS,
    MENUITEM_NO_FLASH,
    MENUITEM_CANCEL,
    MENUITEM_COUNT,
};

enum
{
    WIN_HEADER,
    WIN_OPTIONS
};

#define YPOS_EXP_MODIFIER (MENUITEM_EXP_MODIFIER * 16)
#define YPOS_RARE_CANDY   (MENUITEM_RARE_CANDY * 16)
#define YPOS_INFINITE_TMS (MENUITEM_INFINITE_TMS * 16)
#define YPOS_NO_FLASH     (MENUITEM_NO_FLASH * 16)

static void Task_KeySystemMenuFadeIn(u8 taskId);
static void Task_KeySystemMenuProcessInput(u8 taskId);
static void Task_KeySystemMenuSave(u8 taskId);
static void Task_KeySystemMenuFadeOut(u8 taskId);
static void HighlightKeySystemMenuItem(u8 selection);
static u8 ExpModifier_ProcessInput(u8 selection);
static void ExpModifier_DrawChoices(u8 selection);
static u8 RareCandy_ProcessInput(u8 selection);
static void RareCandy_DrawChoices(u8 selection);
static u8 InfiniteTMs_ProcessInput(u8 selection);
static void InfiniteTMs_DrawChoices(u8 selection);
static u8 NoFlash_ProcessInput(u8 selection);
static void NoFlash_DrawChoices(u8 selection);
static void DrawHeaderText(void);
static void DrawKeySystemMenuTexts(void);
static void DrawBgWindowFrames(void);

EWRAM_DATA static bool8 sArrowPressed = FALSE;

// The same palette the option menu uses, so the green/red choice colouring and
// the window frame match between the two screens exactly.
static const u16 sKeySystemMenuText_Pal[] = INCGFX_U16("graphics/interface/option_menu_text.pal", ".gbapal");

static const u8 *const sKeySystemMenuItemsNames[MENUITEM_COUNT] =
{
    [MENUITEM_EXP_MODIFIER] = gText_ExpModifier,
    [MENUITEM_RARE_CANDY]   = gText_InfiniteRareCandy,
    [MENUITEM_INFINITE_TMS] = gText_InfiniteTMs,
    [MENUITEM_NO_FLASH]     = gText_NoFlash,
    [MENUITEM_CANCEL]       = gText_OptionMenuCancel,
};

// Indexed by the setting minus KEY_EXP_MODIFIER_0X, which is why the enum runs
// in display order.
static const u8 *const sExpModifierChoices[KEY_EXP_MODIFIER_COUNT - KEY_EXP_MODIFIER_0X] =
{
    gText_ExpModifier0x,
    gText_ExpModifierHalf,
    gText_ExpModifier1x,
    gText_ExpModifier2x,
    gText_ExpModifier5x,
};

// The option menu's own ON/OFF strings. They are already exactly these words
// with the colour prefix OptionScreen_DrawChoice expects, so there is nothing to
// gain by declaring a second pair. OFF is first because it is the zero value.
static const u8 *const sOffOnChoices[] =
{
    gText_BattleSceneOff,
    gText_BattleSceneOn,
};

static const struct WindowTemplate sKeySystemMenuWinTemplates[] =
{
    [WIN_HEADER] = {
        .bg = 1,
        .tilemapLeft = 2,
        .tilemapTop = 1,
        .width = 26,
        .height = 2,
        .paletteNum = 1,
        .baseBlock = 2
    },
    // Three rows rather than the option menu's seven, so the list box is sized
    // to what it holds instead of leaving two thirds of the screen empty.
    [WIN_OPTIONS] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 5,
        .width = 26,
        .height = MENUITEM_COUNT * 2,
        .paletteNum = 1,
        .baseBlock = 0x36
    },
    DUMMY_WIN_TEMPLATE
};

static const struct BgTemplate sKeySystemMenuBgTemplates[] =
{
    {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
    {
        .bg = 0,
        .charBaseIndex = 1,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0
    }
};

static const u16 sKeySystemMenuBg_Pal[] = {RGB(17, 18, 31)};

// KEY_EXP_MODIFIER_UNSET is what a save written before the key system existed
// reads back, and it is not one of the five settings -- treat it as 1x. The
// range check also covers a byte corrupted into something out of range.
static u8 GetExpModifierSetting(void)
{
    u8 setting = gSaveBlock2Ptr->keyExpModifier;

    if (setting < KEY_EXP_MODIFIER_0X || setting >= KEY_EXP_MODIFIER_COUNT)
        return KEY_EXP_MODIFIER_1X;

    return setting;
}

u32 KeySystemApplyExpModifier(u32 exp)
{
    switch (GetExpModifierSetting())
    {
    case KEY_EXP_MODIFIER_0X:
        return 0;
    // Floored at 1 rather than allowed to round down to nothing, so a low-yield
    // knockout still reads as experience gained -- the same floor vanilla puts
    // on the shares it divides up in Cmd_getexp.
    case KEY_EXP_MODIFIER_HALF:
        return (exp > 1) ? exp / 2 : exp;
    case KEY_EXP_MODIFIER_2X:
        return exp * 2;
    case KEY_EXP_MODIFIER_5X:
        return exp * 5;
    default:
        return exp;
    }
}

bool32 KeySystemInfiniteRareCandy(void)
{
    return gSaveBlock2Ptr->keyInfiniteRareCandy != 0;
}

bool32 KeySystemInfiniteTMs(void)
{
    return gSaveBlock2Ptr->keyInfiniteTMs != 0;
}

bool32 KeySystemNoFlash(void)
{
    return gSaveBlock2Ptr->keyNoFlash != 0;
}

void KeySystemSyncRareCandy(void)
{
    if (KeySystemInfiniteRareCandy())
    {
        // One is enough: it is never spent, so the count cannot fall. Nothing is
        // added when the player already has some -- those simply become the
        // bottomless stack, rather than gaining a redundant one beside them.
        if (!CheckBagHasItem(ITEM_RARE_CANDY, 1) && AddBagItem(ITEM_RARE_CANDY, 1) == TRUE)
            gSaveBlock2Ptr->keyRareCandyGranted = TRUE;
    }
    else if (gSaveBlock2Ptr->keyRareCandyGranted)
    {
        // Take back exactly what the key handed over. The flag is only ever set
        // when the key added one itself, so a candy the player found or bought
        // is never taken. It may already be gone -- tossed, or wiped by a new
        // game -- which is why the removal is conditional but the flag is not.
        if (CheckBagHasItem(ITEM_RARE_CANDY, 1))
            RemoveBagItem(ITEM_RARE_CANDY, 1);

        gSaveBlock2Ptr->keyRareCandyGranted = FALSE;
    }
}

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_InitKeySystemMenu(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        gMain.state++;
        break;
    case 1:
        DmaClearLarge16(3, (void *)(VRAM), VRAM_SIZE, 0x1000);
        DmaClear32(3, OAM, OAM_SIZE);
        DmaClear16(3, PLTT, PLTT_SIZE);
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sKeySystemMenuBgTemplates, ARRAY_COUNT(sKeySystemMenuBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        ChangeBgX(2, 0, BG_COORD_SET);
        ChangeBgY(2, 0, BG_COORD_SET);
        ChangeBgX(3, 0, BG_COORD_SET);
        ChangeBgY(3, 0, BG_COORD_SET);
        InitWindows(sKeySystemMenuWinTemplates);
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_WIN0H, 0);
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG0);
        SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG0 | WINOUT_WIN01_BG1 | WINOUT_WIN01_CLR);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        SetGpuReg(REG_OFFSET_BLDY, 4);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        ShowBg(0);
        ShowBg(1);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        ScanlineEffect_Stop();
        ResetTasks();
        ResetSpriteData();
        gMain.state++;
        break;
    case 3:
        LoadBgTiles(1, GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->tiles, 0x120, 0x1A2);
        gMain.state++;
        break;
    case 4:
        LoadPalette(sKeySystemMenuBg_Pal, BG_PLTT_ID(0), sizeof(sKeySystemMenuBg_Pal));
        LoadPalette(GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        gMain.state++;
        break;
    case 5:
        LoadPalette(sKeySystemMenuText_Pal, BG_PLTT_ID(1), sizeof(sKeySystemMenuText_Pal));
        gMain.state++;
        break;
    case 6:
        PutWindowTilemap(WIN_HEADER);
        DrawHeaderText();
        gMain.state++;
        break;
    case 7:
        PutWindowTilemap(WIN_OPTIONS);
        DrawKeySystemMenuTexts();
        gMain.state++;
        break;
    case 8:
        DrawBgWindowFrames();
        gMain.state++;
        break;
    case 9:
    {
        u8 taskId = CreateTask(Task_KeySystemMenuFadeIn, 0);

        gTasks[taskId].tMenuSelection = 0;
        gTasks[taskId].tExpModifier = GetExpModifierSetting();
        gTasks[taskId].tRareCandy = KeySystemInfiniteRareCandy();
        gTasks[taskId].tInfiniteTMs = KeySystemInfiniteTMs();
        gTasks[taskId].tNoFlash = KeySystemNoFlash();

        ExpModifier_DrawChoices(gTasks[taskId].tExpModifier);
        RareCandy_DrawChoices(gTasks[taskId].tRareCandy);
        InfiniteTMs_DrawChoices(gTasks[taskId].tInfiniteTMs);
        NoFlash_DrawChoices(gTasks[taskId].tNoFlash);
        HighlightKeySystemMenuItem(gTasks[taskId].tMenuSelection);

        CopyWindowToVram(WIN_OPTIONS, COPYWIN_FULL);
        gMain.state++;
        break;
    }
    case 10:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(MainCB2);
        return;
    }
}

static void Task_KeySystemMenuFadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_KeySystemMenuProcessInput;
}

static void Task_KeySystemMenuProcessInput(u8 taskId)
{
    if (JOY_NEW(A_BUTTON))
    {
        if (gTasks[taskId].tMenuSelection == MENUITEM_CANCEL)
            gTasks[taskId].func = Task_KeySystemMenuSave;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        gTasks[taskId].func = Task_KeySystemMenuSave;
    }
    else if (JOY_NEW(DPAD_UP))
    {
        if (gTasks[taskId].tMenuSelection > 0)
            gTasks[taskId].tMenuSelection--;
        else
            gTasks[taskId].tMenuSelection = MENUITEM_CANCEL;
        HighlightKeySystemMenuItem(gTasks[taskId].tMenuSelection);
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        if (gTasks[taskId].tMenuSelection < MENUITEM_CANCEL)
            gTasks[taskId].tMenuSelection++;
        else
            gTasks[taskId].tMenuSelection = 0;
        HighlightKeySystemMenuItem(gTasks[taskId].tMenuSelection);
    }
    else
    {
        u8 previousOption;

        switch (gTasks[taskId].tMenuSelection)
        {
        case MENUITEM_EXP_MODIFIER:
            previousOption = gTasks[taskId].tExpModifier;
            gTasks[taskId].tExpModifier = ExpModifier_ProcessInput(gTasks[taskId].tExpModifier);

            if (previousOption != gTasks[taskId].tExpModifier)
                ExpModifier_DrawChoices(gTasks[taskId].tExpModifier);
            break;
        case MENUITEM_RARE_CANDY:
            previousOption = gTasks[taskId].tRareCandy;
            gTasks[taskId].tRareCandy = RareCandy_ProcessInput(gTasks[taskId].tRareCandy);

            if (previousOption != gTasks[taskId].tRareCandy)
                RareCandy_DrawChoices(gTasks[taskId].tRareCandy);
            break;
        case MENUITEM_INFINITE_TMS:
            previousOption = gTasks[taskId].tInfiniteTMs;
            gTasks[taskId].tInfiniteTMs = InfiniteTMs_ProcessInput(gTasks[taskId].tInfiniteTMs);

            if (previousOption != gTasks[taskId].tInfiniteTMs)
                InfiniteTMs_DrawChoices(gTasks[taskId].tInfiniteTMs);
            break;
        case MENUITEM_NO_FLASH:
            previousOption = gTasks[taskId].tNoFlash;
            gTasks[taskId].tNoFlash = NoFlash_ProcessInput(gTasks[taskId].tNoFlash);

            if (previousOption != gTasks[taskId].tNoFlash)
                NoFlash_DrawChoices(gTasks[taskId].tNoFlash);
            break;
        default:
            return;
        }

        if (sArrowPressed)
        {
            sArrowPressed = FALSE;
            CopyWindowToVram(WIN_OPTIONS, COPYWIN_GFX);
        }
    }
}

static void Task_KeySystemMenuSave(u8 taskId)
{
    gSaveBlock2Ptr->keyExpModifier = gTasks[taskId].tExpModifier;
    gSaveBlock2Ptr->keyInfiniteRareCandy = gTasks[taskId].tRareCandy;
    gSaveBlock2Ptr->keyInfiniteTMs = gTasks[taskId].tInfiniteTMs;
    gSaveBlock2Ptr->keyNoFlash = gTasks[taskId].tNoFlash;

    // Acts on the values just written, so the bag matches the key the moment the
    // player leaves this screen rather than at the next load.
    KeySystemSyncRareCandy();

    // A map's darkness is decided by SetDefaultFlashLevel when it loads, so a key
    // toggled inside a cave would otherwise not take effect until the player left
    // it. Recomputing here lets the scanline effect come back at the new level
    // when the field is redrawn. Only on a cave map: anywhere else the level was
    // set by something other than this function -- Dewford Gym sets its own from
    // a script -- and recomputing would throw that away.
    if (gMapHeader.cave)
        SetDefaultFlashLevel();

    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_KeySystemMenuFadeOut;
}

static void Task_KeySystemMenuFadeOut(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        FreeAllWindowBuffers();

        // Always back to the option menu, which is the only way in. Its own
        // gMain.savedCallback -- the main menu or the field -- was never touched
        // on the way here, so it still knows where to go afterwards. The state
        // must be rewound by hand: CB2_InitOptionMenu leaves it at the end of
        // its own staircase, and re-entering with that value walks off the end.
        gMain.state = 0;
        SetMainCallback2(CB2_InitOptionMenu);
    }
}

static void HighlightKeySystemMenuItem(u8 index)
{
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(16, DISPLAY_WIDTH - 16));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(index * 16 + 40, index * 16 + 56));
}

static u8 ExpModifier_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_RIGHT))
    {
        if (selection < KEY_EXP_MODIFIER_COUNT - 1)
            selection++;
        else
            selection = KEY_EXP_MODIFIER_0X;

        sArrowPressed = TRUE;
    }
    if (JOY_NEW(DPAD_LEFT))
    {
        if (selection > KEY_EXP_MODIFIER_0X)
            selection--;
        else
            selection = KEY_EXP_MODIFIER_COUNT - 1;

        sArrowPressed = TRUE;
    }
    return selection;
}

static void ExpModifier_DrawChoices(u8 selection)
{
    OptionScreen_DrawChoiceRow(WIN_OPTIONS, sExpModifierChoices, ARRAY_COUNT(sExpModifierChoices),
                               selection - KEY_EXP_MODIFIER_0X, YPOS_EXP_MODIFIER);
}

static u8 RareCandy_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }

    return selection;
}

static void RareCandy_DrawChoices(u8 selection)
{
    OptionScreen_DrawChoiceRow(WIN_OPTIONS, sOffOnChoices, ARRAY_COUNT(sOffOnChoices), selection, YPOS_RARE_CANDY);
}

static u8 InfiniteTMs_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }

    return selection;
}

static void InfiniteTMs_DrawChoices(u8 selection)
{
    OptionScreen_DrawChoiceRow(WIN_OPTIONS, sOffOnChoices, ARRAY_COUNT(sOffOnChoices), selection, YPOS_INFINITE_TMS);
}

static u8 NoFlash_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }

    return selection;
}

static void NoFlash_DrawChoices(u8 selection)
{
    OptionScreen_DrawChoiceRow(WIN_OPTIONS, sOffOnChoices, ARRAY_COUNT(sOffOnChoices), selection, YPOS_NO_FLASH);
}

static void DrawHeaderText(void)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gText_KeySystem, 8, 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

static void DrawKeySystemMenuTexts(void)
{
    u8 i;

    FillWindowPixelBuffer(WIN_OPTIONS, PIXEL_FILL(1));
    for (i = 0; i < MENUITEM_COUNT; i++)
        AddTextPrinterParameterized(WIN_OPTIONS, FONT_NORMAL, sKeySystemMenuItemsNames[i], 8, (i * 16) + 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_OPTIONS, COPYWIN_FULL);
}

#define TILE_TOP_CORNER_L 0x1A2
#define TILE_TOP_EDGE     0x1A3
#define TILE_TOP_CORNER_R 0x1A4
#define TILE_LEFT_EDGE    0x1A5
#define TILE_RIGHT_EDGE   0x1A7
#define TILE_BOT_CORNER_L 0x1A8
#define TILE_BOT_EDGE     0x1A9
#define TILE_BOT_CORNER_R 0x1AA

// The list box is as tall as the list, so its bottom edge is derived from the
// item count rather than hardcoded the way option_menu.c's full-screen one is.
#define LIST_TOP    4
#define LIST_HEIGHT (MENUITEM_COUNT * 2)
#define LIST_BOTTOM (LIST_TOP + LIST_HEIGHT + 1)

static void DrawBgWindowFrames(void)
{
    //                     bg, tile,              x, y, width, height, palNum
    // Draw title window frame
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  0, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2,  3, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28,  3,  1,  1,  7);

    // Draw key list window frame
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1, LIST_TOP,      1,  1,           7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2, LIST_TOP,     26,  1,           7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28, LIST_TOP,      1,  1,           7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1, LIST_TOP + 1,  1, LIST_HEIGHT,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28, LIST_TOP + 1,  1, LIST_HEIGHT,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, LIST_BOTTOM,   1,  1,           7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, LIST_BOTTOM,  26,  1,           7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, LIST_BOTTOM,   1,  1,           7);

    CopyBgTilemapBufferToVram(1);
}
