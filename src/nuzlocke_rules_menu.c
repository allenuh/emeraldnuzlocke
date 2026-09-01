#include "global.h"
#include "nuzlocke_rules_menu.h"
#include "bg.h"
#include "gpu_regs.h"
#include "main.h"
#include "main_menu.h"
#include "menu.h"
#include "nuzlocke.h"
#include "option_screen.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/global.h"
#include "constants/rgb.h"
#include "constants/songs.h"

#define tMenuSelection data[0]
#define tShinyClause   data[1]
#define tWhiteOut      data[2]
#define tBattleMode    data[3]
#define tBagItems      data[4]
#define tLevelCaps     data[5]

enum
{
    MENUITEM_SHINY_CLAUSE,
    MENUITEM_WHITE_OUT,
    MENUITEM_BATTLE_MODE,
    MENUITEM_BAG_ITEMS,
    MENUITEM_LEVEL_CAPS,
    MENUITEM_START,
    MENUITEM_COUNT,
};

enum
{
    WIN_HEADER,
    WIN_OPTIONS
};

// Every row here is a two-choice row, and each one's task value is the index of
// the choice that is lit -- not the rule bit, which for three of the five is
// stored the other way round. The two are reconciled in one place, in
// StageChosenRuleFlags, rather than at each row.
enum
{
    CHOICE_LEFT,
    CHOICE_RIGHT,
};

#define YPOS_OF(item) ((item) * 16)

static void Task_NuzlockeRulesMenuFadeIn(u8 taskId);
static void Task_NuzlockeRulesMenuProcessInput(u8 taskId);
static void Task_NuzlockeRulesMenuStart(u8 taskId);
static void Task_NuzlockeRulesMenuCancel(u8 taskId);
static void Task_NuzlockeRulesMenuFadeOutToBirchSpeech(u8 taskId);
static void Task_NuzlockeRulesMenuFadeOutToMainMenu(u8 taskId);
static void HighlightNuzlockeRulesMenuItem(u8 selection);
static void DrawRowChoices(u8 taskId, u32 item);
static void DrawHeaderText(void);
static void DrawNuzlockeRulesMenuTexts(void);
static void DrawBgWindowFrames(void);

EWRAM_DATA static bool8 sArrowPressed = FALSE;

// The same palette the option and key system screens use, so all three read as
// one family of menus.
static const u16 sNuzlockeRulesMenuText_Pal[] = INCGFX_U16("graphics/interface/option_menu_text.pal", ".gbapal");

static const u8 *const sNuzlockeRulesMenuItemsNames[MENUITEM_COUNT] =
{
    [MENUITEM_SHINY_CLAUSE] = gText_ShinyClause,
    [MENUITEM_WHITE_OUT]    = gText_WhiteOut,
    [MENUITEM_BATTLE_MODE]  = gText_BattleMode,
    [MENUITEM_BAG_ITEMS]    = gText_BagItems,
    [MENUITEM_LEVEL_CAPS]   = gText_LevelCaps,
    [MENUITEM_START]        = gText_NuzlockeStart,
};

// Left choice first. The ON/OFF and SET/SHIFT pairs are the option menu's own
// strings, which are already these words carrying the colour prefix the draw
// routine overwrites.
static const u8 *const sShinyClauseChoices[] = { gText_BattleSceneOn, gText_BattleSceneOff };
static const u8 *const sWhiteOutChoices[]    = { gText_WhiteOutEndRun, gText_WhiteOutContinue };
static const u8 *const sBattleModeChoices[]  = { gText_BattleStyleSet, gText_BattleStyleShift };
static const u8 *const sBagItemsChoices[]    = { gText_BagItemsAllowed, gText_BagItemsBanned };
static const u8 *const sLevelCapsChoices[]   = { gText_BattleSceneOn, gText_BattleSceneOff };

static const u8 *const *const sRowChoices[MENUITEM_COUNT] =
{
    [MENUITEM_SHINY_CLAUSE] = sShinyClauseChoices,
    [MENUITEM_WHITE_OUT]    = sWhiteOutChoices,
    [MENUITEM_BATTLE_MODE]  = sBattleModeChoices,
    [MENUITEM_BAG_ITEMS]    = sBagItemsChoices,
    [MENUITEM_LEVEL_CAPS]   = sLevelCapsChoices,
    [MENUITEM_START]        = NULL, // not a setting; A begins the run
};

static const struct WindowTemplate sNuzlockeRulesMenuWinTemplates[] =
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

static const struct BgTemplate sNuzlockeRulesMenuBgTemplates[] =
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

static const u16 sNuzlockeRulesMenuBg_Pal[] = {RGB(17, 18, 31)};

// Seeds the rows from NUZLOCKE_RULES_DEFAULT. Deliberately the constant and not
// NuzlockeGetStagedRuleFlags: the staged value lives in ewram_data, which is a
// NOLOAD section, so it holds nothing meaningful until this screen writes it.
// Reading the constant also means backing out and coming back gives the player a
// clean slate rather than half-remembered choices.
static void SetRowsToDefaults(u8 taskId)
{
    u32 defaults = NUZLOCKE_RULES_DEFAULT;

    gTasks[taskId].tShinyClause = (defaults & NUZLOCKE_RULE_SHINY_CLAUSE) ? CHOICE_LEFT : CHOICE_RIGHT;
    gTasks[taskId].tWhiteOut    = (defaults & NUZLOCKE_RULE_RESTART_ON_WHITEOUT) ? CHOICE_LEFT : CHOICE_RIGHT;
    gTasks[taskId].tBattleMode  = (defaults & NUZLOCKE_RULE_BATTLE_STYLE_SHIFT) ? CHOICE_RIGHT : CHOICE_LEFT;
    gTasks[taskId].tBagItems    = (defaults & NUZLOCKE_RULE_ALLOW_BAG_ITEMS) ? CHOICE_LEFT : CHOICE_RIGHT;
    gTasks[taskId].tLevelCaps   = (defaults & NUZLOCKE_RULE_NO_LEVEL_CAPS) ? CHOICE_RIGHT : CHOICE_LEFT;
}

// The one place the rows and the stored bits are reconciled. Three of the five
// rules are stored as the departure from the strict reading -- see the bit
// definitions in nuzlocke.h -- so their left-hand choice clears the bit rather
// than setting it.
static void StageChosenRuleFlags(u8 taskId)
{
    u8 flags = 0;

    if (gTasks[taskId].tShinyClause == CHOICE_LEFT)   // ON
        flags |= NUZLOCKE_RULE_SHINY_CLAUSE;
    if (gTasks[taskId].tWhiteOut == CHOICE_LEFT)      // END RUN
        flags |= NUZLOCKE_RULE_RESTART_ON_WHITEOUT;
    if (gTasks[taskId].tBattleMode == CHOICE_RIGHT)   // SHIFT
        flags |= NUZLOCKE_RULE_BATTLE_STYLE_SHIFT;
    if (gTasks[taskId].tBagItems == CHOICE_LEFT)      // ALLOWED
        flags |= NUZLOCKE_RULE_ALLOW_BAG_ITEMS;
    if (gTasks[taskId].tLevelCaps == CHOICE_RIGHT)    // OFF
        flags |= NUZLOCKE_RULE_NO_LEVEL_CAPS;

    NuzlockeStageRuleFlags(flags);
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

void CB2_InitNuzlockeRulesMenu(void)
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
        InitBgsFromTemplates(0, sNuzlockeRulesMenuBgTemplates, ARRAY_COUNT(sNuzlockeRulesMenuBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        ChangeBgX(2, 0, BG_COORD_SET);
        ChangeBgY(2, 0, BG_COORD_SET);
        ChangeBgX(3, 0, BG_COORD_SET);
        ChangeBgY(3, 0, BG_COORD_SET);
        InitWindows(sNuzlockeRulesMenuWinTemplates);
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
        LoadPalette(sNuzlockeRulesMenuBg_Pal, BG_PLTT_ID(0), sizeof(sNuzlockeRulesMenuBg_Pal));
        LoadPalette(GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        gMain.state++;
        break;
    case 5:
        LoadPalette(sNuzlockeRulesMenuText_Pal, BG_PLTT_ID(1), sizeof(sNuzlockeRulesMenuText_Pal));
        gMain.state++;
        break;
    case 6:
        PutWindowTilemap(WIN_HEADER);
        DrawHeaderText();
        gMain.state++;
        break;
    case 7:
        PutWindowTilemap(WIN_OPTIONS);
        DrawNuzlockeRulesMenuTexts();
        gMain.state++;
        break;
    case 8:
        DrawBgWindowFrames();
        gMain.state++;
        break;
    case 9:
    {
        u32 item;
        u8 taskId = CreateTask(Task_NuzlockeRulesMenuFadeIn, 0);

        gTasks[taskId].tMenuSelection = 0;
        SetRowsToDefaults(taskId);

        for (item = 0; item < MENUITEM_COUNT; item++)
            DrawRowChoices(taskId, item);
        HighlightNuzlockeRulesMenuItem(gTasks[taskId].tMenuSelection);

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

static void Task_NuzlockeRulesMenuFadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_NuzlockeRulesMenuProcessInput;
}

static void Task_NuzlockeRulesMenuProcessInput(u8 taskId)
{
    if (JOY_NEW(A_BUTTON))
    {
        if (gTasks[taskId].tMenuSelection == MENUITEM_START)
        {
            PlaySE(SE_SELECT);
            gTasks[taskId].func = Task_NuzlockeRulesMenuStart;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        // Backing out of NEW GAME entirely. Nothing is staged, so a mis-press on
        // the main menu costs the player nothing.
        PlaySE(SE_SELECT);
        gTasks[taskId].func = Task_NuzlockeRulesMenuCancel;
    }
    else if (JOY_NEW(DPAD_UP))
    {
        if (gTasks[taskId].tMenuSelection > 0)
            gTasks[taskId].tMenuSelection--;
        else
            gTasks[taskId].tMenuSelection = MENUITEM_START;
        HighlightNuzlockeRulesMenuItem(gTasks[taskId].tMenuSelection);
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        if (gTasks[taskId].tMenuSelection < MENUITEM_START)
            gTasks[taskId].tMenuSelection++;
        else
            gTasks[taskId].tMenuSelection = 0;
        HighlightNuzlockeRulesMenuItem(gTasks[taskId].tMenuSelection);
    }
    else if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        u32 item = gTasks[taskId].tMenuSelection;

        // Every rule row has exactly two choices, so left and right both toggle
        // and there is nothing to wrap. START has no value to change.
        if (sRowChoices[item] != NULL)
        {
            gTasks[taskId].data[1 + item] ^= 1;
            DrawRowChoices(taskId, item);
            sArrowPressed = TRUE;
        }
    }

    if (sArrowPressed)
    {
        sArrowPressed = FALSE;
        CopyWindowToVram(WIN_OPTIONS, COPYWIN_GFX);
    }
}

static void Task_NuzlockeRulesMenuStart(u8 taskId)
{
    StageChosenRuleFlags(taskId);

    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_NuzlockeRulesMenuFadeOutToBirchSpeech;
}

static void Task_NuzlockeRulesMenuCancel(u8 taskId)
{
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_NuzlockeRulesMenuFadeOutToMainMenu;
}

// Both exits rewind gMain.state by hand: these CB2s leave it at the end of their
// own staircase, and re-entering one with a stale value walks off the end of its
// switch. gMain.savedCallback is not used by this screen -- neither destination
// is "wherever I came from".
static void Task_NuzlockeRulesMenuFadeOutToBirchSpeech(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        FreeAllWindowBuffers();
        gMain.state = 0;
        SetMainCallback2(CB2_NewGameBirchSpeechAfterNuzlockeRules);
    }
}

static void Task_NuzlockeRulesMenuFadeOutToMainMenu(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        FreeAllWindowBuffers();
        // CB2_ReinitMainMenu rather than CB2_InitMainMenu: it fades in from
        // black, matching the fade out above, and restores the cursor to NEW
        // GAME. Its "returning from options" behaviour needs OPTION_MENU_FLAG,
        // which only the options menu ever sets.
        gMain.state = 0;
        SetMainCallback2(CB2_ReinitMainMenu);
    }
}

static void HighlightNuzlockeRulesMenuItem(u8 index)
{
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(16, DISPLAY_WIDTH - 16));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(index * 16 + 40, index * 16 + 56));
}

static void DrawRowChoices(u8 taskId, u32 item)
{
    if (sRowChoices[item] == NULL)
        return;

    // The rule rows are data[1] onwards, in menu order, which is what lets the
    // input handler toggle a row without a switch over all five.
    OptionScreen_DrawChoiceRow(WIN_OPTIONS, sRowChoices[item], 2,
                               gTasks[taskId].data[1 + item], YPOS_OF(item));
}

static void DrawHeaderText(void)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gText_NuzlockeRules, 8, 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

static void DrawNuzlockeRulesMenuTexts(void)
{
    u8 i;

    FillWindowPixelBuffer(WIN_OPTIONS, PIXEL_FILL(1));
    for (i = 0; i < MENUITEM_COUNT; i++)
        AddTextPrinterParameterized(WIN_OPTIONS, FONT_NORMAL, sNuzlockeRulesMenuItemsNames[i], 8, (i * 16) + 1, TEXT_SKIP_DRAW, NULL);
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

    // Draw rules list window frame
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
