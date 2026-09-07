#include "global.h"
#include "encounter_tracker.h"
#include "bg.h"
#include "data.h"
#include "decompress.h"
#include "graphics.h"
#include "gpu_regs.h"
#include "international_string_util.h"
#include "list_menu.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "nuzlocke.h"
#include "overworld.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon_icon.h"
#include "region_map.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "wild_encounter.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/wild_encounter.h"

enum
{
    WIN_HEADER,
    WIN_LIST
};

// The list window is 14 tiles tall and rows are the font's own 16px.
#define ROWS_ON_SCREEN 7

// The list window's own top edge, and where a row sits inside it.
#define LIST_TOP_Y   40
#define ROW_HEIGHT   16

// A mon icon is 32x32 -- two rows tall -- but a species row is always at least
// two rows from the next one, because every species carries at least one method
// row, so icons can never reach each other. Centred on its own row, an icon
// spills 8px into the rows above and below, which is only ever the icon column;
// the text beside it starts at item_X. At the very top and bottom of the list it
// spills past the window, and the frame hides that for free: sMonIconOamData is
// priority 1 and the frames are on bg1 at priority 0, so the frame wins.
#define ICON_CENTER_X 40

// The scroll arrows need tags of their own. Passing TAG_NONE takes a special
// path in AddScrollIndicatorArrowPair that loads the arrow palette straight into
// OBJ slot palNum -- slot 0 by default -- which is where LoadMonIconPalettes has
// already put the first icon palette. Every species using that palette then
// renders in the arrows' reds. Clear of POKE_ICON_BASE_PAL_TAG (56000) and of
// the icons' own anonymous tile allocations.
#define TAG_SCROLL_ARROW 2000
#define TAG_POKEBALL     2001

// The party menu stands its icons in front of a Poke Ball at these offsets --
// slot coords {104, 18, ...} put the icon at (104, 18) and the ball at (102, 25)
// -- and the same pairing is what makes a row here read as a party slot.
#define BALL_OFFSET_X (-2)
#define BALL_OFFSET_Y   7

// Those coords are the *selected* geometry. AnimatePartySlot marks the cursor's
// slot by moving the icon and never the ball: the selected icon sits on the base
// x and bobs, every other one is pushed 4px left onto a resting offset, and the
// ball underneath simply swaps frames. Holding the ball still is also what keeps
// the resting icons clear of the cursor column, since the one row wearing a
// cursor is the one whose icon has stepped back out from under it.
#define ICON_UNSELECTED_X2 (-4)

// The selected icon's bob, from SpriteCB_BouncePartyMonIcon: one step of the
// two-frame walk cycle up, the other barely down.
#define ICON_BOUNCE_UP_Y2   (-3)
#define ICON_BOUNCE_DOWN_Y2   1

enum
{
    BALL_ANIM_CLOSED,
    BALL_ANIM_OPEN,
};

// Lower is nearer the front, so the icon covers the ball. The party menu uses
// exactly these two.
#define ICON_SUBPRIORITY 4
#define BALL_SUBPRIORITY 8

// One per map section shown in the area list. This is a display bound and no
// longer the save bound: gWildMonHeaders alone already fills exactly
// NUZLOCKE_AREA_RECORD_COUNT sections, and the areas that hold only a non-wild
// encounter come on top of that. They need no record of their own, because
// nothing found there ever spends an area's chance -- GetAreaEncounterRecord is
// keyed by map section and simply has nothing to return for them.
#define MAX_AREAS 80

// Every species in an area, and under each of them a row per method it turns up
// by. The Safari Zone sets the bound: 38 species over its four sections comes to
// 80 rows. Victory Road is next at 36, and nothing else is close.
#define MAX_DETAIL_SPECIES 48
#define MAX_DETAIL_ROWS    96

#define MAX_ROWS (MAX_DETAIL_ROWS > MAX_AREAS ? MAX_DETAIL_ROWS : MAX_AREAS)

// A map name or a species name, the six bytes of {COLOR}{SHADOW} in front of it,
// and the terminator.
#define ROW_TEXT_LEN 32

// The fishing table is one list of ten slots split between the three rods.
#define OLD_ROD_FIRST_SLOT   0
#define GOOD_ROD_FIRST_SLOT  NUM_FISHING_MONS_OLD_ROD_ENCOUNTER_SLOTS
#define SUPER_ROD_FIRST_SLOT (GOOD_ROD_FIRST_SLOT + NUM_FISHING_MONS_GOOD_ROD_ENCOUNTER_SLOTS)

// Text colours, as indices into sTrackerText_Pal. Written into the row strings
// as {COLOR n}{SHADOW n} rather than set through the list's template, because a
// template colour applies to the whole list and an itemPrintFunc cannot tell one
// heading from another -- every heading's item id is LIST_HEADER.
// Every pair here is two tones of one hue, and every one of them is drawn the
// same way round: the pale half is the text and the saturated half is its
// shadow. So the colour names below say which tone they are, not which job they
// do -- the job is always the same.
#define COLOR_ACCENT      2  // the orange a row still worth acting on wears
#define SHADOW_ACCENT     3
#define COLOR_MUTED       7  // pale grey: a row that no longer matters
#define SHADOW_MUTED      6
#define RED_LIGHT         4
#define RED_DARK          5
#define GREEN_LIGHT       9
#define GREEN_DARK        8
#define BLUE_LIGHT       11
#define BLUE_DARK        10
#define BROWN_LIGHT      13
#define BROWN_DARK       12
#define PURPLE_LIGHT     15
#define PURPLE_DARK      14

// Which of the header's four WildPokemonInfo pointers a method reads. They sit
// consecutively, so one routine serves all four rather than being written out
// four times. METHOD_NONE is the rows that read no table at all -- the ones
// filled from sSpecialEncounters below.
enum
{
    METHOD_LAND,
    METHOD_WATER,
    METHOD_ROCK_SMASH,
    METHOD_FISHING,
    METHOD_NONE,
};

// The rows a species can be found under, in the order they are gathered and
// emitted. Rock Smash shares Old Rod's brown and Trade shares Good Rod's purple:
// the palette has no second tone of either, and no two rows sharing a hue ever
// appear close enough together to be confused. Static wears the same grey a row
// with nothing left to do wears, which is what was asked for and reads correctly
// besides -- a static is never the encounter an area's chance is spent on.
enum
{
    METHOD_ROW_GRASS,
    METHOD_ROW_SURF,
    METHOD_ROW_ROCK_SMASH,
    METHOD_ROW_OLD_ROD,
    METHOD_ROW_GOOD_ROD,
    METHOD_ROW_SUPER_ROD,
    METHOD_ROW_FOSSIL,
    METHOD_ROW_STATIC,
    METHOD_ROW_GIFT,
    METHOD_ROW_TRADE,
    METHOD_ROW_COUNT,
};

// The rows that come from sSpecialEncounters rather than from an encounter
// table. A species carrying only these is not part of rules 3 and 4 at all,
// which is what IsSpeciesSpecialOnly is for.
#define SPECIAL_METHOD_BITS ((1 << METHOD_ROW_FOSSIL) | (1 << METHOD_ROW_STATIC) \
                           | (1 << METHOD_ROW_GIFT)   | (1 << METHOD_ROW_TRADE))

static const struct
{
    const u8 *label;
    u8 light;
    u8 dark;
    u8 method;
    u8 firstSlot;
    u8 lastSlot;
} sMethodRows[] =
{
    [METHOD_ROW_GRASS]      = { gText_EncounterGrass,     GREEN_LIGHT,  GREEN_DARK,   METHOD_LAND,       0, NUM_LAND_MONS_ENCOUNTER_SLOTS - 1 },
    [METHOD_ROW_SURF]       = { gText_EncounterSurf,      BLUE_LIGHT,   BLUE_DARK,    METHOD_WATER,      0, NUM_WATER_MONS_ENCOUNTER_SLOTS - 1 },
    [METHOD_ROW_ROCK_SMASH] = { gText_EncounterRockSmash, BROWN_LIGHT,  BROWN_DARK,   METHOD_ROCK_SMASH, 0, NUM_ROCK_SMASH_MONS_ENCOUNTER_SLOTS - 1 },
    [METHOD_ROW_OLD_ROD]    = { gText_EncounterOldRod,    BROWN_LIGHT,  BROWN_DARK,   METHOD_FISHING,    OLD_ROD_FIRST_SLOT,   GOOD_ROD_FIRST_SLOT - 1 },
    [METHOD_ROW_GOOD_ROD]   = { gText_EncounterGoodRod,   PURPLE_LIGHT, PURPLE_DARK,  METHOD_FISHING,    GOOD_ROD_FIRST_SLOT,  SUPER_ROD_FIRST_SLOT - 1 },
    [METHOD_ROW_SUPER_ROD]  = { gText_EncounterSuperRod,  RED_LIGHT,    RED_DARK,     METHOD_FISHING,    SUPER_ROD_FIRST_SLOT, NUM_FISHING_MONS_ENCOUNTER_SLOTS - 1 },
    [METHOD_ROW_FOSSIL]     = { gText_EncounterFossil,    BROWN_LIGHT,  BROWN_DARK,   METHOD_NONE,       0, 0 },
    [METHOD_ROW_STATIC]     = { gText_EncounterStatic,    COLOR_MUTED,  SHADOW_MUTED, METHOD_NONE,       0, 0 },
    [METHOD_ROW_GIFT]       = { gText_EncounterGift,      BLUE_LIGHT,   BLUE_DARK,    METHOD_NONE,       0, 0 },
    [METHOD_ROW_TRADE]      = { gText_EncounterTrade,     PURPLE_LIGHT, PURPLE_DARK,  METHOD_NONE,       0, 0 },
};

// Everything a Hoenn run can obtain that no encounter table mentions.
//
// This is the second source of truth the rest of the screen is built to avoid,
// and it has to be one: these exist only as script commands and trade table
// entries, with no equivalent of gWildMonHeaders to walk. Every row below was
// read off the scripts rather than from memory, and can be re-derived with
//
//     grep -rn "givemon SPECIES_\|giveegg SPECIES_\|setwildbattle SPECIES_" data/maps
//
// plus sIngameTrades in src/data/trade.h for the four trades. The area is the
// map's own region map section -- the same location the summary screen shows a
// Pokemon as having been met at -- so Registeel is filed under Ancient Tomb
// rather than under the route it is entered from, and Castform under Route 119
// rather than a Weather Institute of its own.
//
// Deliberately not listed: Sudowoodo, Groudon, Kyogre, the Battle Frontier
// Meowth trade, the post-game Johto starters, the Mystery Gift event legendaries
// and the roaming Latias/Latios.
//
// Feebas is the one entry that is not a special method. Its Route 119 spots are
// picked by their own system rather than from the fishing table, but it is an
// ordinary wild encounter in every way that matters here -- it can be fished by
// any of the three rods and it does spend the route's chance -- so it carries
// the rod rows and behaves like the rest of the table everywhere downstream.
static const struct
{
    u8 mapSec;
    u16 species;
    u16 methods;
} sSpecialEncounters[] =
{
    { MAPSEC_LITTLEROOT_TOWN, SPECIES_TREECKO,   1 << METHOD_ROW_GIFT },
    { MAPSEC_LITTLEROOT_TOWN, SPECIES_TORCHIC,   1 << METHOD_ROW_GIFT },
    { MAPSEC_LITTLEROOT_TOWN, SPECIES_MUDKIP,    1 << METHOD_ROW_GIFT },
    { MAPSEC_LAVARIDGE_TOWN,  SPECIES_WYNAUT,    1 << METHOD_ROW_GIFT },
    { MAPSEC_RUSTBORO_CITY,   SPECIES_LILEEP,    1 << METHOD_ROW_FOSSIL },
    { MAPSEC_RUSTBORO_CITY,   SPECIES_ANORITH,   1 << METHOD_ROW_FOSSIL },
    { MAPSEC_RUSTBORO_CITY,   SPECIES_SEEDOT,    1 << METHOD_ROW_TRADE },
    { MAPSEC_FORTREE_CITY,    SPECIES_PLUSLE,    1 << METHOD_ROW_TRADE },
    { MAPSEC_PACIFIDLOG_TOWN, SPECIES_HORSEA,    1 << METHOD_ROW_TRADE },
    { MAPSEC_ROUTE_119,       SPECIES_FEEBAS,    (1 << METHOD_ROW_OLD_ROD) | (1 << METHOD_ROW_GOOD_ROD) | (1 << METHOD_ROW_SUPER_ROD) },
    { MAPSEC_ROUTE_119,       SPECIES_CASTFORM,  1 << METHOD_ROW_GIFT },
    { MAPSEC_ROUTE_119,       SPECIES_KECLEON,   1 << METHOD_ROW_STATIC },
    { MAPSEC_ROUTE_120,       SPECIES_KECLEON,   1 << METHOD_ROW_STATIC },
    { MAPSEC_NEW_MAUVILLE,    SPECIES_VOLTORB,   1 << METHOD_ROW_STATIC },
    { MAPSEC_AQUA_HIDEOUT,    SPECIES_ELECTRODE, 1 << METHOD_ROW_STATIC },
    { MAPSEC_DESERT_RUINS,    SPECIES_REGIROCK,  1 << METHOD_ROW_STATIC },
    { MAPSEC_ISLAND_CAVE,     SPECIES_REGICE,    1 << METHOD_ROW_STATIC },
    { MAPSEC_ANCIENT_TOMB,    SPECIES_REGISTEEL, 1 << METHOD_ROW_STATIC },
    { MAPSEC_SKY_PILLAR,      SPECIES_RAYQUAZA,  1 << METHOD_ROW_STATIC },
};

struct EncounterTracker
{
    u8 areaMapSecs[MAX_AREAS];
    // What colour each area's row is drawn in, worked out once when the screen
    // opens. IsAreaFullyOwned is by far the most expensive thing on this screen
    // -- a walk of every encounter header per method per area, with a scan of the
    // whole evolution table inside it -- and recomputing it on the way back from
    // a detail view cost a visible hitch. Nothing that feeds it can change while
    // the tracker is up, so once is enough.
    u8 areaColors[MAX_AREAS];
    u8 areaCount;
    u8 areasCaught;
    u8 listTaskId;
    u8 scrollArrowTaskId;
    // Where the list on screen currently sits. Kept live every frame because the
    // scroll arrows dereference it that often to decide which of them to show.
    u16 scrollOffset;
    u16 selectedRow;
    // Where the area list was when the detail view opened. A separate pair, since
    // the live one belongs to whichever list is up and the detail view would
    // otherwise scribble over the position being held for the way back.
    u16 savedScrollOffset;
    u16 savedSelectedRow;
    // The detail view is up; the area list is the other state.
    bool8 inDetail;
    u8 detailMapSec;
    u16 detailRowCount;
    // The species in the area being shown, gathered before any row is written.
    struct
    {
        u16 species;
        u16 methods; // a bit per index into sMethodRows
    } species[MAX_DETAIL_SPECIES];
    u8 speciesCount;
    // Which visible row each icon belongs to, and the scroll offset they were
    // built for, so they are only rebuilt when the window actually moves. The
    // ball behind each icon is tracked alongside it and shares its lifetime.
    // iconSelectedRow is the cheaper half of the same idea: the cursor moving
    // between visible rows only restyles two of them.
    u8 iconSpriteIds[ROWS_ON_SCREEN];
    u8 ballSpriteIds[ROWS_ON_SCREEN];
    u16 iconScrollOffset;
    u16 iconSelectedRow;
    struct ListMenuItem items[MAX_ROWS];
    u8 rowText[MAX_ROWS][ROW_TEXT_LEN];
};

static EWRAM_DATA struct EncounterTracker *sTracker = NULL;

static void MainCB2(void);
static void VBlankCB(void);
static void Task_FadeIn(u8 taskId);
static void Task_HandleInput(u8 taskId);
static void Task_FadeOutToField(u8 taskId);
static void ShowAreaList(void);
static void ShowAreaDetail(u8 mapSec);
static void DestroyIcons(void);
static void RefreshIcons(void);
static void ApplyIconSelection(void);
static void SpriteCB_BounceIcon(struct Sprite *sprite);
static u8 *WriteRowColor(u8 *dest, u8 fg, u8 shadow);
static void DrawHeader(void);
static void DrawBgWindowFrames(void);
static u8 GetAreaColor(u8 mapSec);

// A superset of the option screens' palette: indices 0-7 are theirs verbatim,
// so the header and frames look the same, and the eight slots they leave empty
// carry the method colours below.
static const u16 sTrackerText_Pal[] = INCGFX_U16("graphics/interface/encounter_tracker_text.pal", ".gbapal");
static const u16 sTrackerBg_Pal[] = {RGB(17, 18, 31)};

static const struct WindowTemplate sWinTemplates[] =
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
    [WIN_LIST] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 5,
        .width = 26,
        .height = 14,
        .paletteNum = 1,
        .baseBlock = 0x36
    },
    DUMMY_WIN_TEMPLATE
};

static const struct OamData sOamData_Pokeball =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x32),
    .x = 0,
    .size = SPRITE_SIZE(32x32),
    .tileNum = 0,
    // The same priority the icons use, so the window frame still hides whatever
    // hangs past the top and bottom of the list.
    .priority = 1,
    .paletteNum = 0,
};

// The sheet's two frames, paired the way sSpriteAnimTable_MenuPokeball pairs
// them: closed for the rows the cursor is not on, open for the row it is.
static const union AnimCmd sAnim_PokeballClosed[] =
{
    ANIMCMD_FRAME(0, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_PokeballOpen[] =
{
    ANIMCMD_FRAME(16, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sAnimTable_Pokeball[] =
{
    [BALL_ANIM_CLOSED] = sAnim_PokeballClosed,
    [BALL_ANIM_OPEN]   = sAnim_PokeballOpen,
};

static const struct CompressedSpriteSheet sSpriteSheet_Pokeball =
{
    gPartyMenuPokeball_Gfx, 0x400, TAG_POKEBALL
};

static const struct CompressedSpritePalette sSpritePalette_Pokeball =
{
    gPartyMenuPokeball_Pal, TAG_POKEBALL
};

static const struct SpriteTemplate sSpriteTemplate_Pokeball =
{
    .tileTag = TAG_POKEBALL,
    .paletteTag = TAG_POKEBALL,
    .oam = &sOamData_Pokeball,
    .anims = sAnimTable_Pokeball,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

static const struct BgTemplate sBgTemplates[] =
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

//
// The area list, read out of gWildMonHeaders.
//

// The map section a header's map belongs to. Areas are keyed by section rather
// than by map because that is the granularity rule 3 spends a catch at: Granite
// Cave's four floors are one area with one chance between them.
static u8 GetHeaderMapSec(const struct WildPokemonHeader *header)
{
    const struct MapHeader *mapHeader = Overworld_GetMapHeaderByGroupAndId(header->mapGroup, header->mapNum);

    if (mapHeader == NULL)
        return MAPSEC_NONE;

    return mapHeader->regionMapSectionId;
}

static const struct WildPokemonInfo *GetHeaderMethodInfo(const struct WildPokemonHeader *header, u32 method)
{
    switch (method)
    {
    case METHOD_LAND:
        return header->landMonsInfo;
    case METHOD_WATER:
        return header->waterMonsInfo;
    case METHOD_ROCK_SMASH:
        return header->rockSmashMonsInfo;
    default:
        return header->fishingMonsInfo;
    }
}

static bool32 HeaderHasEncounters(const struct WildPokemonHeader *header)
{
    return header->landMonsInfo != NULL
        || header->waterMonsInfo != NULL
        || header->rockSmashMonsInfo != NULL
        || header->fishingMonsInfo != NULL;
}

// Adds a map section to the area list if it is not already there, keeping the
// list in section order -- which already runs in roughly the order the player
// travels.
static void AddArea(u8 mapSec)
{
    u32 i, insertAt;

    if (mapSec >= MAPSEC_NONE || sTracker->areaCount >= MAX_AREAS)
        return;

    for (i = 0; i < sTracker->areaCount; i++)
    {
        if (sTracker->areaMapSecs[i] == mapSec)
            return;
    }

    for (insertAt = 0; insertAt < sTracker->areaCount; insertAt++)
    {
        if (sTracker->areaMapSecs[insertAt] > mapSec)
            break;
    }

    for (i = sTracker->areaCount; i > insertAt; i--)
        sTracker->areaMapSecs[i] = sTracker->areaMapSecs[i - 1];

    sTracker->areaMapSecs[insertAt] = mapSec;
    sTracker->areaCount++;
}

// Walks gWildMonHeaders the way GetCurrentMapWildMonHeaderId does, then adds the
// areas that only a non-wild encounter puts on the map.
static void BuildAreaList(void)
{
    u32 i;

    sTracker->areaCount = 0;
    sTracker->areasCaught = 0;

    for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        if (HeaderHasEncounters(&gWildMonHeaders[i]))
            AddArea(GetHeaderMapSec(&gWildMonHeaders[i]));
    }

    // The areas that hold only a gift, a trade, a fossil or a static have no
    // encounter header to have been picked up above, so they are added here.
    for (i = 0; i < ARRAY_COUNT(sSpecialEncounters); i++)
        AddArea(sSpecialEncounters[i].mapSec);

    for (i = 0; i < sTracker->areaCount; i++)
    {
        const struct NuzlockeAreaRecord *record = GetAreaEncounterRecord(sTracker->areaMapSecs[i]);

        if (record != NULL && record->outcome == NUZLOCKE_AREA_OUTCOME_CAUGHT)
            sTracker->areasCaught++;
    }

    // Worked out once, here, rather than on every return from a detail view --
    // see the comment on areaColors.
    for (i = 0; i < sTracker->areaCount; i++)
        sTracker->areaColors[i] = GetAreaColor(sTracker->areaMapSecs[i]);
}

//
// Building the two lists.
//

// Whether every species an area can offer is already spoken for by rule 4. Such
// an area still has its catch to spend, but there is nothing left there to spend
// it on, so it reads the same as one already settled.
//
// Walked live rather than cached: the answer changes with the party and the box,
// and the list is only built when the screen opens.
static bool32 IsAreaFullyOwned(u8 mapSec)
{
    u32 m, i, slot;
    bool32 anySpecies = FALSE;

    for (m = 0; m < ARRAY_COUNT(sMethodRows); m++)
    {
        if (sMethodRows[m].method == METHOD_NONE)
            continue;

        for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
        {
            const struct WildPokemonInfo *info = GetHeaderMethodInfo(&gWildMonHeaders[i], sMethodRows[m].method);

            if (info == NULL || GetHeaderMapSec(&gWildMonHeaders[i]) != mapSec)
                continue;

            for (slot = sMethodRows[m].firstSlot; slot <= sMethodRows[m].lastSlot; slot++)
            {
                u16 species = info->wildPokemon[slot].species;

                if (species == SPECIES_NONE)
                    continue;

                if (!IsSpeciesFamilyOwned(species))
                    return FALSE;

                anySpecies = TRUE;
            }
        }
    }

    // An area with nothing in its tables is not "fully owned", it is unknown.
    return anySpecies;
}

// An area with no encounter tables at all -- one that only holds a gift, a
// trade, a fossil or a static. Nothing there is part of rules 3 and 4, so there
// is no catch to spend and nothing for the accent colour to be urging.
static bool32 AreaHasWildEncounters(u8 mapSec)
{
    u32 i;

    for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        if (HeaderHasEncounters(&gWildMonHeaders[i]) && GetHeaderMapSec(&gWildMonHeaders[i]) == mapSec)
            return TRUE;
    }

    return FALSE;
}

// Each area is coloured by whether its catch is still worth going for: green
// once something was kept, grey once the chance was spent for nothing, once
// every name there is already owned, or once it turns out there was never a
// wild catch to make there, and the accent orange while it is still there to be
// taken. Returns the light half of the pair; the dark half follows from it.
static u8 GetAreaColor(u8 mapSec)
{
    const struct NuzlockeAreaRecord *record = GetAreaEncounterRecord(mapSec);

    if (record != NULL)
        return (record->outcome == NUZLOCKE_AREA_OUTCOME_CAUGHT) ? GREEN_LIGHT : COLOR_MUTED;

    if (!AreaHasWildEncounters(mapSec) || IsAreaFullyOwned(mapSec))
        return COLOR_MUTED;

    return COLOR_ACCENT;
}

static void BuildAreaListRows(void)
{
    u32 i;

    for (i = 0; i < sTracker->areaCount; i++)
    {
        u8 fg = sTracker->areaColors[i];
        u8 shadow;
        u8 *dest;

        if (fg == GREEN_LIGHT)
            shadow = GREEN_DARK;
        else if (fg == COLOR_MUTED)
            shadow = SHADOW_MUTED;
        else
            shadow = SHADOW_ACCENT;

        dest = WriteRowColor(sTracker->rowText[i], fg, shadow);
        GetMapName(dest, sTracker->areaMapSecs[i], 0);
        sTracker->items[i].name = sTracker->rowText[i];
        sTracker->items[i].id = i;
    }
}

// The right-hand column of the area list: what was met there, and whether it was
// kept. An area with no record is left blank rather than labelled, so the eye
// goes to the ones that have been settled.
static void PrintAreaOutcome(u8 windowId, u32 itemId, u8 y)
{
    const struct NuzlockeAreaRecord *record;
    const u8 *text;

    if (itemId >= sTracker->areaCount)
        return;

    record = GetAreaEncounterRecord(sTracker->areaMapSecs[itemId]);
    if (record == NULL)
        return;

    text = (record->outcome == NUZLOCKE_AREA_OUTCOME_CAUGHT)
         ? gSpeciesNames[record->species]
         : gText_EncounterGotAway;

    AddTextPrinterParameterized(windowId, FONT_NARROW, text,
                                GetStringRightAlignXOffset(FONT_NARROW, text, 196), y, TEXT_SKIP_DRAW, NULL);
}

// Writes {COLOR fg}{SHADOW shadow} at the front of a row, and returns where the
// text itself goes. The background is deliberately left alone, so every row sits
// on the window's own fill.
static u8 *WriteRowColor(u8 *dest, u8 fg, u8 shadow)
{
    *dest++ = EXT_CTRL_CODE_BEGIN;
    *dest++ = EXT_CTRL_CODE_COLOR;
    *dest++ = fg;
    *dest++ = EXT_CTRL_CODE_BEGIN;
    *dest++ = EXT_CTRL_CODE_SHADOW;
    *dest++ = shadow;

    return dest;
}

static void AddMethodRow(const u8 *label, u8 light, u8 dark)
{
    u8 *dest;

    if (sTracker->detailRowCount >= MAX_DETAIL_ROWS)
        return;

    // Light on dark, like every other row: what tells a method row from the
    // species row above it is the hue and the indent, not the pairing.
    dest = WriteRowColor(sTracker->rowText[sTracker->detailRowCount], light, dark);
    StringCopy(dest, label);
    sTracker->items[sTracker->detailRowCount].name = sTracker->rowText[sTracker->detailRowCount];
    // LIST_HEADER draws the row at header_X and makes a scroll step pass over
    // it. It does not keep the cursor off it: a cursor moving between rows that
    // are already on screen lands on one like any other row, which is why
    // header_X has to clear the cursor column rather than relying on this.
    sTracker->items[sTracker->detailRowCount].id = LIST_HEADER;
    sTracker->detailRowCount++;
}

static void AddSpeciesRow(u16 species, bool32 muted)
{
    u8 *dest;

    if (sTracker->detailRowCount >= MAX_DETAIL_ROWS)
        return;

    // The indent is item_X's job, not a couple of padding spaces on the string.
    dest = WriteRowColor(sTracker->rowText[sTracker->detailRowCount],
                         muted ? COLOR_MUTED : COLOR_ACCENT,
                         muted ? SHADOW_MUTED : SHADOW_ACCENT);
    StringCopy(dest, gSpeciesNames[species]);
    sTracker->items[sTracker->detailRowCount].name = sTracker->rowText[sTracker->detailRowCount];
    sTracker->items[sTracker->detailRowCount].id = species;
    sTracker->detailRowCount++;
}

// Whether a species row should be drawn muted -- which is to say, whether there
// is no longer any point going after it here. Once the area's encounter has
// happened this view is a record rather than a plan: a catch leaves only what was
// caught picked out and a miss leaves nothing picked out. While the chance is
// still unspent, the names stay accented except the ones rule 4 would refuse
// anyway, because the family is already owned.
// Whether a species in the area being shown is reachable only by a gift, a
// trade, a fossil or a static -- which is to say, whether rules 3 and 4 have
// anything to say about it at all. Feebas is deliberately not one of these: it
// carries the rod rows, because it really is Route 119's wild catch if taken.
static bool32 IsSpeciesSpecialOnly(u16 species)
{
    u32 entry;

    for (entry = 0; entry < sTracker->speciesCount; entry++)
    {
        if (sTracker->species[entry].species == species)
            return (sTracker->species[entry].methods & ~SPECIAL_METHOD_BITS) == 0;
    }

    return FALSE;
}

static bool32 IsDetailSpeciesMuted(u16 species)
{
    const struct NuzlockeAreaRecord *record;

    // A gift, a trade, a fossil or a static never spends the area's chance, so
    // spending it elsewhere in the area says nothing about them. Only already
    // owning one takes it off the list.
    if (IsSpeciesSpecialOnly(species))
        return IsSpeciesFamilyOwned(species);

    record = GetAreaEncounterRecord(sTracker->detailMapSec);

    if (record != NULL)
    {
        if (record->outcome == NUZLOCKE_AREA_OUTCOME_CAUGHT)
            return record->species != species;

        return TRUE;
    }

    return IsSpeciesFamilyOwned(species);
}

// Records that a species can be found here by the given rows, merging into the
// entry already present if there is one. First appearance sets the order, so
// the land species stay at the top where they are most likely to matter.
static void AddGatheredSpecies(u16 species, u16 methods)
{
    u32 entry;

    for (entry = 0; entry < sTracker->speciesCount; entry++)
    {
        if (sTracker->species[entry].species == species)
            break;
    }

    if (entry == sTracker->speciesCount)
    {
        if (sTracker->speciesCount >= MAX_DETAIL_SPECIES)
            return;

        sTracker->species[entry].species = species;
        sTracker->species[entry].methods = 0;
        sTracker->speciesCount++;
    }

    sTracker->species[entry].methods |= methods;
}

// Every method a species turns up under in this area, gathered before any row is
// written. It has to be a pass of its own: a species found in the grass and
// again under a rock is one entry with two bits, and that cannot be known until
// every method has been walked.
static void GatherAreaSpecies(u8 mapSec)
{
    u32 m, i, slot;

    sTracker->speciesCount = 0;

    for (m = 0; m < ARRAY_COUNT(sMethodRows); m++)
    {
        if (sMethodRows[m].method == METHOD_NONE)
            continue;

        for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
        {
            const struct WildPokemonInfo *info = GetHeaderMethodInfo(&gWildMonHeaders[i], sMethodRows[m].method);

            if (info == NULL || GetHeaderMapSec(&gWildMonHeaders[i]) != mapSec)
                continue;

            for (slot = sMethodRows[m].firstSlot; slot <= sMethodRows[m].lastSlot; slot++)
            {
                u16 species = info->wildPokemon[slot].species;

                if (species == SPECIES_NONE)
                    continue;

                AddGatheredSpecies(species, 1 << m);
            }
        }
    }

    // The non-wild encounters last, so they sit below the species the area's
    // tables can actually roll. One of them may name a species already gathered,
    // in which case it only adds its row to the entry already there.
    for (i = 0; i < ARRAY_COUNT(sSpecialEncounters); i++)
    {
        if (sSpecialEncounters[i].mapSec == mapSec)
            AddGatheredSpecies(sSpecialEncounters[i].species, sSpecialEncounters[i].methods);
    }
}

// One species, then a row per method it can be found by.
static void BuildDetailRows(u8 mapSec)
{
    u32 entry, m;

    sTracker->detailRowCount = 0;
    GatherAreaSpecies(mapSec);

    for (entry = 0; entry < sTracker->speciesCount; entry++)
    {
        u16 species = sTracker->species[entry].species;

        AddSpeciesRow(species, IsDetailSpeciesMuted(species));

        for (m = 0; m < ARRAY_COUNT(sMethodRows); m++)
        {
            if (sTracker->species[entry].methods & (1 << m))
                AddMethodRow(sMethodRows[m].label, sMethodRows[m].light, sMethodRows[m].dark);
        }
    }
}

// The right-hand column of the detail view. OWNED is the thing rule 4 refuses,
// and it is derived live rather than stored, so it follows the party.
static void PrintDetailStatus(u8 windowId, u32 itemId, u8 y)
{
    const struct NuzlockeAreaRecord *record;
    const u8 *text;
    u8 color[3] = { TEXT_COLOR_TRANSPARENT, COLOR_ACCENT, SHADOW_ACCENT };

    if (itemId == (u32)LIST_HEADER || itemId == SPECIES_NONE)
        return;

    // A species reachable only by a gift, a trade, a fossil or a static is never
    // what an area's record is about, even if the two happen to name the same
    // one, so OWNED is the only thing it can say.
    record = IsSpeciesSpecialOnly(itemId) ? NULL : GetAreaEncounterRecord(sTracker->detailMapSec);

    if (record != NULL && record->species == itemId)
        text = (record->outcome == NUZLOCKE_AREA_OUTCOME_CAUGHT) ? gText_EncounterCaught : gText_EncounterGotAway;
    else if (IsSpeciesFamilyOwned(itemId))
        text = gText_EncounterOwned;
    else
        return;

    // Drawn in the same colour as the name it sits beside, so a greyed row is
    // greyed all the way across.
    if (IsDetailSpeciesMuted(itemId))
        color[1] = COLOR_MUTED, color[2] = SHADOW_MUTED;

    AddTextPrinterParameterized3(windowId, FONT_NARROW,
                                 GetStringRightAlignXOffset(FONT_NARROW, text, 196), y,
                                 color, TEXT_SKIP_DRAW, text);
}

//
// The species icons down the left of the detail view.
//

static void DestroyIcons(void)
{
    u32 i;

    for (i = 0; i < ROWS_ON_SCREEN; i++)
    {
        if (sTracker->iconSpriteIds[i] != SPRITE_NONE)
        {
            FreeAndDestroyMonIconSprite(&gSprites[sTracker->iconSpriteIds[i]]);
            sTracker->iconSpriteIds[i] = SPRITE_NONE;
        }

        if (sTracker->ballSpriteIds[i] != SPRITE_NONE)
        {
            DestroySprite(&gSprites[sTracker->ballSpriteIds[i]]);
            sTracker->ballSpriteIds[i] = SPRITE_NONE;
        }
    }
}

// Rebuilds the icons for whatever is on screen now. Torn down and recreated
// rather than repointed, because the set of species on screen changes with every
// scroll step and a sprite is cheap: the tiles come from gMonIconTable through
// sprite->images, so there is no per-species VRAM to shuffle.
static void RefreshIcons(void)
{
    u32 i;

    DestroyIcons();
    sTracker->iconScrollOffset = sTracker->scrollOffset;

    if (!sTracker->inDetail)
        return;

    for (i = 0; i < ROWS_ON_SCREEN; i++)
    {
        u32 row = sTracker->scrollOffset + i;
        s32 id;

        if (row >= sTracker->detailRowCount)
            break;

        // Method rows carry LIST_HEADER; only a species row has an icon.
        id = sTracker->items[row].id;
        if (id == LIST_HEADER || id == SPECIES_NONE)
            continue;

        {
            s16 x = ICON_CENTER_X;
            s16 y = LIST_TOP_Y + (i * ROW_HEIGHT) + (ROW_HEIGHT / 2);

            // The ball first, so it is behind: same priority, higher
            // subpriority. It is created even if the icon fails, since a bare
            // ball reads better than a gap.
            sTracker->ballSpriteIds[i] = CreateSprite(&sSpriteTemplate_Pokeball,
                                                      x + BALL_OFFSET_X, y + BALL_OFFSET_Y,
                                                      BALL_SUBPRIORITY);
            // x is the base -- the selected position. Everything else is a
            // resting offset ApplyIconSelection puts on afterwards.
            sTracker->iconSpriteIds[i] = CreateMonIconNoPersonality(id, SpriteCB_MonIcon, x, y,
                                                                    ICON_SUBPRIORITY, FALSE);
        }
    }

    ApplyIconSelection();
}

// SpriteCB_BouncePartyMonIcon, which the party menu keeps to itself. The icon's
// own two-frame walk cycle drives the bob, so it stays in step with the
// animation rather than running off a timer of its own.
static void SpriteCB_BounceIcon(struct Sprite *sprite)
{
    u8 animCmd = UpdateMonIconFrame(sprite);

    if (animCmd != 0)
    {
        if (animCmd & 1)
            sprite->y2 = ICON_BOUNCE_UP_Y2;
        else
            sprite->y2 = ICON_BOUNCE_DOWN_Y2;
    }
}

// Dresses one visible row the way AnimatePartySlot dresses a slot: the ball
// opens, and the icon comes forward off its resting offset and starts to bob.
static void SetIconSelected(u32 i, bool32 selected)
{
    if (sTracker->ballSpriteIds[i] != SPRITE_NONE)
        StartSpriteAnim(&gSprites[sTracker->ballSpriteIds[i]],
                        selected ? BALL_ANIM_OPEN : BALL_ANIM_CLOSED);

    if (sTracker->iconSpriteIds[i] != SPRITE_NONE)
    {
        struct Sprite *icon = &gSprites[sTracker->iconSpriteIds[i]];

        icon->x2 = selected ? 0 : ICON_UNSELECTED_X2;
        icon->y2 = 0;
        icon->callback = selected ? SpriteCB_BounceIcon : SpriteCB_MonIcon;
    }
}

// Only ever one row is the cursor's, so this is cheap enough to redo wholesale
// rather than tracking which row is losing the selection.
static void ApplyIconSelection(void)
{
    u32 i;

    sTracker->iconSelectedRow = sTracker->selectedRow;

    for (i = 0; i < ROWS_ON_SCREEN; i++)
        SetIconSelected(i, i == sTracker->selectedRow);
}

//
// The two views.
//

static void DestroyCurrentList(void)
{
    if (sTracker->listTaskId != TASK_NONE)
    {
        DestroyListMenuTask(sTracker->listTaskId, NULL, NULL);
        sTracker->listTaskId = TASK_NONE;
    }

    if (sTracker->scrollArrowTaskId != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sTracker->scrollArrowTaskId);
        sTracker->scrollArrowTaskId = TASK_NONE;
    }
}

static void CreateList(u16 totalItems, void (*printFunc)(u8, u32, u8), u16 scrollOffset, u16 selectedRow)
{
    struct ListMenuTemplate template;

    template.items = sTracker->items;
    template.moveCursorFunc = ListMenuDefaultCursorMoveFunc;
    template.itemPrintFunc = printFunc;
    template.totalItems = totalItems;
    template.maxShowed = (totalItems < ROWS_ON_SCREEN) ? totalItems : ROWS_ON_SCREEN;
    template.windowId = WIN_LIST;
    // Headings share the area list's left edge and species sit in from them.
    // Both clear cursor_X: the cursor is printed into this same window at that
    // x, so anything drawn at 0 has the arrow painted over it whenever the
    // cursor is on that row -- which, for a heading, is any time the player
    // arrows onto one.
    // Only the detail view has icons to make room for. There the cursor takes
    // the first 8px, a 32px icon is centred on ICON_CENTER_X, and the text
    // starts clear of it, with method rows indented again under their species.
    // The area list has no icons, so it keeps the tighter left edge.
    template.header_X = sTracker->inDetail ? 54 : 8;
    template.item_X = sTracker->inDetail ? 42 : 8;
    template.cursor_X = 0;
    template.upText_Y = 1;
    template.cursorPal = 2;
    template.fillValue = 1;
    template.cursorShadowPal = 3;
    template.lettersSpacing = 0;
    template.itemVerticalPadding = 0;
    // Left and right jump a full screen of rows; up and down still step by one.
    template.scrollMultiple = LIST_MULTIPLE_SCROLL_DPAD;
    template.fontId = FONT_NORMAL;
    template.cursorKind = CURSOR_BLACK_ARROW;

    sTracker->listTaskId = ListMenuInit(&template, scrollOffset, selectedRow);

    // ListMenuInit loads the row tiles but not the tilemap that points at them --
    // it copies with COPYWIN_GFX. Without this the rows sit in VRAM with nothing
    // on screen referring to them, which looks exactly like an empty window.
    CopyWindowToVram(WIN_LIST, COPYWIN_MAP);

    // Seeded here rather than left to the input task, so the arrows are right on
    // the first frame instead of one behind.
    sTracker->scrollOffset = scrollOffset;
    sTracker->selectedRow = selectedRow;

    if (totalItems > ROWS_ON_SCREEN)
    {
        // Centred on the list box and just outside its top and bottom edges,
        // which is where the bag and the marts put theirs. Centring also keeps
        // them off the right-hand status column.
        sTracker->scrollArrowTaskId = AddScrollIndicatorArrowPairParameterized(
            SCROLL_ARROW_UP, 120, 36, 156, totalItems - ROWS_ON_SCREEN,
            TAG_SCROLL_ARROW, TAG_SCROLL_ARROW, &sTracker->scrollOffset);
    }
}

static void ShowAreaList(void)
{
    DestroyCurrentList();
    sTracker->inDetail = FALSE;
    BuildAreaListRows();
    DrawHeader();
    CreateList(sTracker->areaCount, PrintAreaOutcome, sTracker->savedScrollOffset, sTracker->savedSelectedRow);
    // The area list has no icons, so this only ever clears the detail view's.
    RefreshIcons();
}

static void ShowAreaDetail(u8 mapSec)
{
    DestroyCurrentList();
    sTracker->inDetail = TRUE;
    sTracker->detailMapSec = mapSec;
    BuildDetailRows(mapSec);
    DrawHeader();
    CreateList(sTracker->detailRowCount, PrintDetailStatus, 0, 0);
    RefreshIcons();
}

// The count of areas settled over the list, the area's own name over its detail.
static void DrawHeader(void)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));

    if (sTracker->inDetail)
    {
        GetMapName(gStringVar1, sTracker->detailMapSec, 0);
        AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gStringVar1, 8, 1, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        u8 *end;

        AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gText_EncounterTracker, 8, 1, TEXT_SKIP_DRAW, NULL);

        end = ConvertIntToDecimalStringN(gStringVar1, sTracker->areasCaught, STR_CONV_MODE_LEFT_ALIGN, 2);
        end = StringCopy(end, gText_EncounterSlash);
        ConvertIntToDecimalStringN(end, sTracker->areaCount, STR_CONV_MODE_LEFT_ALIGN, 2);
        AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gStringVar1,
                                    GetStringRightAlignXOffset(FONT_NORMAL, gStringVar1, 200), 1, TEXT_SKIP_DRAW, NULL);
    }

    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

void CB2_InitEncounterTracker(void)
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
        InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        InitWindows(sWinTemplates);
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        ShowBg(0);
        ShowBg(1);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        ScanlineEffect_Stop();
        ResetTasks();
        ResetSpriteData();
        FreeAllSpritePalettes();
        gMain.state++;
        break;
    case 3:
        LoadBgTiles(1, GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->tiles, 0x120, 0x1A2);
        LoadPalette(sTrackerBg_Pal, BG_PLTT_ID(0), sizeof(sTrackerBg_Pal));
        LoadPalette(GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        LoadPalette(sTrackerText_Pal, BG_PLTT_ID(1), sizeof(sTrackerText_Pal));
        gMain.state++;
        break;
    case 4:
    {
        u32 i;

        sTracker = AllocZeroed(sizeof(struct EncounterTracker));
        if (sTracker == NULL)
        {
            // Nowhere to build the lists, so there is nothing to show. Go back
            // rather than running the rest of the screen off a null pointer.
            SetMainCallback2(gMain.savedCallback);
            return;
        }
        sTracker->listTaskId = TASK_NONE;
        sTracker->scrollArrowTaskId = TASK_NONE;
        // AllocZeroed leaves these at 0, which is a real sprite id.
        for (i = 0; i < ROWS_ON_SCREEN; i++)
        {
            sTracker->iconSpriteIds[i] = SPRITE_NONE;
            sTracker->ballSpriteIds[i] = SPRITE_NONE;
        }
        LoadMonIconPalettes();
        LoadCompressedSpriteSheet(&sSpriteSheet_Pokeball);
        LoadCompressedSpritePalette(&sSpritePalette_Pokeball);
        BuildAreaList();
        gMain.state++;
        break;
    }
    case 5:
        PutWindowTilemap(WIN_HEADER);
        PutWindowTilemap(WIN_LIST);
        FillWindowPixelBuffer(WIN_LIST, PIXEL_FILL(1));
        DrawBgWindowFrames();
        gMain.state++;
        break;
    case 6:
        ShowAreaList();
        CreateTask(Task_FadeIn, 0);
        gMain.state++;
        break;
    case 7:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(MainCB2);
        return;
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

static void Task_FadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_HandleInput;
}

static void Task_HandleInput(u8 taskId)
{
    s32 selected = ListMenu_ProcessInput(sTracker->listTaskId);

    // The scroll arrows read this every frame, so it has to be told every frame
    // where the list actually is.
    ListMenuGetScrollAndRow(sTracker->listTaskId, &sTracker->scrollOffset, &sTracker->selectedRow);

    // The icons belong to the rows on screen, so they follow the window rather
    // than the cursor: moving within the visible rows only changes which of them
    // is wearing the selection, and RefreshIcons sets that itself on the way out.
    if (sTracker->inDetail)
    {
        if (sTracker->scrollOffset != sTracker->iconScrollOffset)
            RefreshIcons();
        else if (sTracker->selectedRow != sTracker->iconSelectedRow)
            ApplyIconSelection();
    }

    if (JOY_NEW(A_BUTTON) && !sTracker->inDetail && selected != LIST_NOTHING_CHOSEN)
    {
        PlaySE(SE_SELECT);
        // Held aside so backing out of the detail lands on the area it was opened
        // from rather than at the top of the list.
        sTracker->savedScrollOffset = sTracker->scrollOffset;
        sTracker->savedSelectedRow = sTracker->selectedRow;
        ShowAreaDetail(sTracker->areaMapSecs[selected]);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);

        if (sTracker->inDetail)
        {
            ShowAreaList();
        }
        else
        {
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].func = Task_FadeOutToField;
        }
    }
}

static void Task_FadeOutToField(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyCurrentList();
        DestroyIcons();
        FreeMonIconPalettes();
        FreeSpriteTilesByTag(TAG_POKEBALL);
        FreeSpritePaletteByTag(TAG_POKEBALL);
        DestroyTask(taskId);
        FreeAllWindowBuffers();
        Free(sTracker);
        sTracker = NULL;
        SetMainCallback2(gMain.savedCallback);
    }
}

#define TILE_TOP_CORNER_L 0x1A2
#define TILE_TOP_EDGE     0x1A3
#define TILE_TOP_CORNER_R 0x1A4
#define TILE_LEFT_EDGE    0x1A5
#define TILE_RIGHT_EDGE   0x1A7
#define TILE_BOT_CORNER_L 0x1A8
#define TILE_BOT_EDGE     0x1A9
#define TILE_BOT_CORNER_R 0x1AA

// The same two boxes the option and key system screens draw, so the three read
// as one family.
static void DrawBgWindowFrames(void)
{
    //                     bg, tile,              x, y, width, height, palNum
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  0, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2,  3, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28,  3,  1,  1,  7);

    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  4,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  4, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  4,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  5,  1, 14,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  5,  1, 14,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, 19,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, 19, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, 19,  1,  1,  7);

    CopyBgTilemapBufferToVram(1);
}
