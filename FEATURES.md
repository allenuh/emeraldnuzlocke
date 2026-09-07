# Features

Everything this hack changes from vanilla Pokémon Emerald. For a short overview and build
instructions, see [README.md](README.md).

---

## 1. The nuzlocke rules

Nine rules, all enforced by the game rather than by the player. Four are core and always on;
the other five are chosen once, on the [rules screen](#2-choosing-your-rules), before a run
starts.

| # | Rule | |
| --- | --- | --- |
| 1 | A Pokémon that faints is dead for good | core |
| 2 | Every Pokémon must be given a nickname | core |
| 3 | Only the first wild Pokémon met in an area may be caught | core |
| 4 | Duplicates of a family you already own don't count | core |
| 5 | Battle mode is locked to SET | optional |
| 6 | No bag items in battle except Poké Balls | optional |
| 7 | No Pokémon may pass the next boss trainer's level | optional |
| 8 | Whiting out ends the run | optional |
| 9 | Shiny clause — a shiny may always be caught | optional |

Most of the enforcement lives in [`src/nuzlocke.c`](src/nuzlocke.c) and
[`include/nuzlocke.h`](include/nuzlocke.h), with hooks in the battle, party and field code.

### Grace period

Rules 1 and 3/4 are dormant until the rival hands over the five Poké Balls in Birch's Lab.
Before that, Routes 101 and 103 are the only reachable areas and the starter is the only
Pokémon owned, so both rules would only punish a player with no way to obey them — a wild
encounter would silently burn a route's one catch, and the Route 103 rival battle could end
the run before it began. Those routes keep their catch for when you come back able to use it.

Rule 2 is deliberately *not* gated: the starter arrives before that point and must still be
nicknamed.

### Rule 1 — Faint is permanent

A Pokémon that faints is dead. It can never be healed, revived, or sent into battle again,
whether it went down in combat or to field poison. The Pokémon Center won't touch it, items
have no effect, and it can't be laundered back to life through the PC.

Vanilla already refuses to send out, switch to, or count as alive any Pokémon at 0 HP. So
rather than patching dozens of call sites, the rule guarantees a single invariant — *a dead
Pokémon's HP can never become non-zero again* — and every existing usability check enforces
it for free.

The dead state is one bit taken from the unused `unusedRibbons` field in `PokemonSubstruct3`.
It lives inside `BoxPokemon`, so it survives PC deposit, the Day Care and trading, which also
closes the vanilla Gen 3 quirk where boxing a fainted Pokémon and withdrawing it healed it to
full.

### Rule 2 — Mandatory nicknames

Naming is compulsory and covers every way a Pokémon is obtained: wild catches (including the
Safari Zone), the starter, gift Pokémon and hatched eggs. The Yes/No "do you want to give it
a nickname?" prompts are replaced with statements that lead straight into the naming screen.

Confirming a blank name used to be the way out — the naming screen only copies typed text
when it holds a non-space character, so an empty confirm silently kept the species name. A
strict `NAMING_SCREEN_NICKNAME_REQUIRED` template now rejects that. The lenient template
still exists for the Name Rater, where confirming blank is how you back out of him.

### Rules 3 & 4 — One catch per area, with the duplicate clause

Only the first wild Pokémon met in an area may be caught. Every outcome spends the area's
chance — caught, fainted, fled or ran — so there is no rerolling by running away.

A Pokémon whose evolutionary family you already own doesn't count as that encounter: the ball
is refused, but the area keeps its chance. Ownership is checked across the whole family in
both directions, so a Poochyena in the box makes a wild Mightyena a duplicate.

Areas are region map sections. Anything not generated from a route's encounter table —
legendaries, Sudowoodo, Kecleon, gift battles — is exempt and always catchable.

The enemy's health bar tells you which case you're in before you throw; see
[the encounter indicator](#encounter-indicator).

### Rule 5 — SET battles

The "want to switch?" prompt after knocking out an opponent's Pokémon never appears. SHIFT
hands out a free switch every time the opponent sends something in, which removes most of the
risk the other rules exist to create.

Switchable — a run can be started on SHIFT from the rules screen.

### Rule 6 — No bag items in battle

Potions, Revives, X items and the rest are unusable in battle; only Poké Balls can be
selected. Held items are untouched, since they never go through the bag. The bag simply
offers CANCEL, which is the game's own idiom for "not usable here".

Switchable.

### Rule 7 — Level caps

No Pokémon may be raised past the level of the next boss trainer's strongest Pokémon.

| Badges | Cap | Next boss |
| --- | --- | --- |
| 0 | 15 | Roxanne |
| 1 | 19 | Brawly |
| 2 | 24 | Wattson |
| 3 | 29 | Flannery |
| 4 | 31 | Norman |
| 5 | 33 | Winona |
| 6 | 42 | Tate & Liza |
| 7 | 46 | Juan |
| 8 | 55 | Drake — the whole League |
| Champion | 78 | Steven, Meteor Falls |
| After Steven | — | lifted |

The League sits at a single cap rather than stepping through the Elite Four, because the
hardcore rule caps entry on the *final* member and there is no way back out to train once the
gauntlet starts.

Rather than punishing over-levelling after the fact, the cap refuses the experience. A
Pokémon standing on the cap earns nothing, and the exp it would have taken is handed to a
party member that still has room — whether or not that Pokémon fought. If the whole party is
capped, the exp is lost. Awards are clamped, so a Pokémon one level below the cap lands
exactly on it and never overshoots.

The whole party's share is settled in one pass before any of it is handed out. Vanilla's exp
loop walks the party one slot at a time, so deciding a redirect on the fly would mean reaching
backwards into slots it had already passed. Settling up front also puts the clamp *after* the
Lucky Egg, trainer-battle and traded-Pokémon multipliers, so a redirect can't quietly push its
recipient back over the cap.

Effort values are left alone: experience moves between Pokémon under this rule, EVs do not —
they stay with whoever actually fought. Rare Candy and the Day Care are capped too, the Day
Care by clamping the experience itself, which keeps its "grew N levels" preview and its price
quote honest.

A Pokémon *obtained* above the cap — a Lv 70 Rayquaza caught while the cap is 55 — is benched:
it can't be sent out, switched to, or fill the second slot of a double battle until the cap
rises past it.

Switchable. The cap is derived entirely from badge and story flags that already exist, so no
save data was added for it.

### Rule 8 — Whiting out ends the run

Losing your last Pokémon ends the save for good. It can no longer be continued; the main menu
offers NEW GAME only. The save is committed before anything is drawn, so a whiteout you have
already seen cannot be undone by resetting the console.

Battle Frontier, Pyramid, Pike, Trainer Hill and Secret Base losses never reach the whiteout
path and so are never fatal.

Switchable, and **off by default** — the permissive reading, where you box what fell and carry
on with what's left, is a legitimate way to play.

### Rule 9 — Shiny clause

A shiny wild Pokémon may be caught whatever would otherwise have refused it — the area's catch
already spent, or a family already owned — and catching it spends nothing, so the area keeps
its chance.

A shiny rescued this way is a **trophy**: caught and kept, but permanently fainted and never
registered in the Pokédex. It never paid an area's encounter for its place, so it earns none
of the things one buys — no battling, no experience, no evolution family claimed, no dex
progress. A shiny that *was* the area's first encounter did pay, and is an ordinary Pokémon.

Switchable, and on by default.

### Standing exemptions

The level cap is not applied in Battle Frontier, Battle Tower, Trainer Hill, Safari Zone,
e-Reader, link or recorded-link battles. Those facilities swap in their own party, whose
levels are their business — benching a Lv 100 rental against a cap of 78 would make a
challenge unplayable.

Permadeath likewise ignores Battle Frontier and Battle Tower faints, since those modes save
and restore the party around a challenge, and link battles are excluded by an explicit guard.

---

## 2. Choosing your rules

Selecting NEW GAME opens a **NUZLOCKE RULES** screen before Professor Birch's introduction.
Five rows, then START:

| Row | Choices | Default |
| --- | --- | --- |
| SHINY CLAUSE | ON / OFF | ON |
| WHITE OUT | END RUN / CONTINUE | CONTINUE |
| BATTLE MODE | SET / SHIFT | SET |
| BAG ITEMS | ALLOWED / BANNED | BANNED |
| LEVEL CAPS | ON / OFF | ON |

A on START commits the choices and begins the run; B backs out to the main menu with nothing
saved, so a mis-press on NEW GAME costs nothing.

**The choice is fixed for the life of the save.** There is no way to change a run's rules
afterwards — that is what makes them rules rather than options, and it is why the screen sits
where it does. Rules 1-4 are not on the screen because they are the nuzlocke.

Source: [`src/nuzlocke_rules_menu.c`](src/nuzlocke_rules_menu.c).

---

## 3. Quality of life

### Instant text

A fourth text speed, **INSTANT**, which prints a whole page in a single frame. The options
menu now offers `FAST / INSTANT` only. SLOW and MID still work — a save that holds one keeps
printing at that speed — but the menu shows FAST for them and will snap the save to it the
next time you leave the options screen.

Text stops for exactly the reasons it always did — a pause, a prompt, a line scroll, the end
of the message — so nothing you had to read or acknowledge is skipped. The Berry Crush link
minigame deliberately stays at FAST, since its text speed is shared with the other players.

### Running indoors

The Running Shoes work inside buildings. Only the metatile can still refuse, so doorways,
warps and stairs keep the walking pace they need.

### Repel reuse prompt

When a Repel wears off, the game offers you another instead of just telling you, the way
Black 2 / White 2 does. It offers the same kind you last used if you still have one, and
otherwise falls back to the weakest available — so it never spends a Max Repel you were
saving. If you have none at all, you get the plain "wore off" message.

### Cut trees stay cut

22 cuttable trees across 10 maps stay gone once cut. Vanilla gives them temporary flags that
the next map change clears, which is the only reason they grow back.

Petalburg Woods, Routes 103, 104, 111, 116, 117, 118, 120, 121 and 123. Trick House puzzle 1
is deliberately excluded — its eleven trees *are* the puzzle, and cutting them for good would
leave it solved.

### Pickup announcements

A Pokémon with the Pickup ability now tells you what it found, in a message after the battle,
instead of leaving you to notice the held item later.

### Visible Feebas spots

Six of Route 119's 447 fishing spots hold Feebas, and vanilla gives you nothing to go on —
you fish a tile, get a Carvanha, and move one step. The six spots are now drawn as **visibly
darker water**, so you can see where to cast.

They still move. The spots are picked from the Dewford trend phrase, so they reroll whenever
the trend does, and the marks are recalculated every time you enter the route. Everything else
is untouched: the darker tiles are the same water to surf, fish and dive on, and Feebas still
appears only half the time you fish a correct one.

### The Pokémon Center nurse

Healing is three button presses shorter. The welcome — "Hello, and welcome to the POKéMON
CENTER" and "We restore your tired POKéMON to full health" — is gone, so talking to the nurse
opens on "Would you like to rest your POKéMON?", and the receipt afterwards is gone too, so the
heal ends on "We hope to see you again!".

You are also turned to face away from the counter as the last message appears. Holding A
through a heal used to run straight into the next conversation; now it stops when the exchange
does. Answering **no** leaves you facing the nurse, so changing your mind costs nothing.

### Encounter indicator

The Poké Ball marker on a wild Pokémon's health bar tells you what the encounter is worth
under rules 3 and 4, before you commit a ball:

| | |
| --- | --- |
| **White ball** | This is the area's first encounter — free to catch |
| **Red ball** (vanilla) | A family you already own; the ball will be refused |
| **Dark ball** | This area's chance is already spent |
| **No ball** | Not part of the rules — legendaries, scripted battles, a species you haven't caught |

It reuses the slot and silhouette of vanilla's "already caught" ball, so the three read as one
set. Source: [`src/battle_interface.c`](src/battle_interface.c).

### The STATS page

A fifth page in a Pokémon's summary, between POKéMON SKILLS and BATTLE MOVES, reached with
L/R or the D-pad like the others. It borrows the SKILLS page's layout and fills it with two
stat boxes instead of one: **IV** on top, **EV** below, each showing all six stats.

Neither number is visible anywhere in vanilla. In a nuzlocke you keep what you caught, so the
IV spread of a route's one encounter is a fact you plan around rather than one you re-roll,
and EVs matter more once the Surplus Berries make a spread correctable.

The page is read-only and costs no save space — the values come straight off the Pokémon.
Eggs cannot reach it, as with every page but the egg's own.

### Nature-marked stats

On both the SKILLS page and the new STATS page, the stat names are coloured by what the
Pokémon's nature does to them:

| | |
| --- | --- |
| **Red** | The nature raises this stat (×1.1) |
| **Blue** | The nature lowers this stat (×0.9) |
| **White** | Unaffected — and every stat of a neutral nature |

HP is never coloured, since no nature touches it. The five neutral natures (HARDY, DOCILE,
SERIOUS, BASHFUL, QUIRKY) leave the page looking exactly as it did.

Vanilla prints the stat names once when the summary screen loads, which would have frozen the
colours on whichever Pokémon you opened the screen with. They are now redrawn by the page
itself, so the colours follow the Pokémon as you scroll the party with ↑/↓.

Source: [`src/pokemon_summary_screen.c`](src/pokemon_summary_screen.c).

---

## 4. Menus

### OPTION

BATTLE STYLE is gone — it moved to the rules screen, where it belongs to the run rather than
to the player's preferences. A **KEY SYSTEM** row takes its place. The list scrolls if it ever
grows past seven rows.

### KEY SYSTEM

Under OPTION, reachable both from the title screen and mid-run from the START menu. Four
opt-in assists, the first three borrowed from Black 2 / White 2 by way of FRLG-Plus:

| Key | Options | |
| --- | --- | --- |
| **EXP. MODIFIER** | `0× / ½× / 1× / 2× / 5×` | Scales battle experience |
| **INF. RARE CANDY** | ON / OFF | Puts a Rare Candy in the bag that is never used up |
| **INF. TMS** | ON / OFF | TMs are not consumed when taught, as in Gen 5 onwards |
| **NO FLASH** | ON / OFF | Dark caves are lit, so no party slot has to carry the HM |

At `0×`, Pokémon **still gain EVs normally** — it is a "train stats, don't level" mode, not a
switch that turns off progression entirely.

**NO FLASH** exists because in a nuzlocke a party slot spent on an HM mule is a slot the run
cannot spare. Only four maps are actually dark — Granite Cave B1F and B2F, and Victory Road B1F
and B2F — and with the key on they are lit and Flash is refused in the party menu. Two places
that look like they should be affected are not: **Brawly's gym** keeps its darkness and its
lights-up-as-you-win puzzle, and **Ancient Tomb** still accepts Flash, because there the move
opens Registeel's chamber rather than lighting anything. The Battle Pyramid's darkness is a
separate system and is untouched. Toggling the key while standing in a dark cave takes effect
as soon as you close the menu.

The level cap still applies on top of the first three. `5×` experience is still clamped at the cap
and redirected to a party member with room, and Rare Candy still refuses to work at the cap.
Switching a key off is clean: turning INF. RARE CANDY off takes back the candy it granted and
leaves any you found or bought alone.

Neither the bottomless Rare Candy nor the free berries can be sold, since either would be an
unlimited money press.

Source: [`src/key_system.c`](src/key_system.c).

### ROUTES

Under START, once the adventure has begun. Rules 3 and 4 have been enforced since the beginning,
but the player could only ever see their consequences — which routes still had a catch to spend,
and what was met on the ones that no longer did, were things the game knew and never said. This
is the notebook, kept by the game.

The first screen lists every area that can produce a Pokémon, coloured by whether its catch is
still worth going for — **orange** still to be taken, **green** something kept, **grey** nothing
left to do — with what was met there down the right-hand side. Selecting one opens that area's
table: every species it can offer, the ways each turns up, and where each stands under the rules.

The method under each species is colour-coded:

| | | | |
| --- | --- | --- | --- |
| **GRASS** green | **SURF** blue | **ROCK SMASH** brown | **OLD ROD** brown |
| **GOOD ROD** purple | **SUPER ROD** red | **FOSSIL** brown | **STATIC** grey |
| **GIFT** blue | **TRADE** purple | | |

The wild lists are read out of the game's own encounter tables rather than restated, so the
screen cannot disagree with what the game actually rolls. The last four methods have no table to
read — the starters, the fossils, the gift mons, the NPC trades and the statics exist only as
script commands — so those are the one hand-written list on this screen, and they are marked
under the area the summary screen would call the Pokémon's met location. Registeel is therefore
filed under ANCIENT TOMB rather than the route it is entered from.

**None of them spend an area's chance.** A gift or a static is always catchable, so it stays
picked out even in an area whose wild encounter is long gone, and it only greys once you own the
family. Route 119's **Feebas** is the exception that proves it: its spots are chosen by their own
system rather than from the fishing table, but it is an ordinary wild catch in every other way,
so it is listed under all three rods and does spend the route's chance.

Source: [`src/encounter_tracker.c`](src/encounter_tracker.c).

---

## 5. Items and places

### LINK STONE

A forced solo nuzlocke can never trade, which put twelve species out of reach. The LINK STONE
is an evolution stone that stands in for the trade: **Alakazam, Machamp, Golem, Gengar,
Politoed, Slowking, Steelix, Kingdra, Scizor, Porygon2, Huntail and Gorebyss**.

Sold at the **Lilycove Department Store, 2F, for ¥2100** — behind most of the game without
being gated on a story flag.

It replaces the trade, not the held item: a trade-item evolution still requires its item and
still consumes it, so an Onix holding nothing does not evolve. The stone reads the evolution
table's existing trade rows directly rather than restating them, so there is only ever one
statement of which species evolve by trading.

### The Surplus Berries box

A box outside the **Berry Master's house on Route 123**, marked by a sign, giving away the six
EV-lowering berries — **Pomeg, Kelpsy, Qualot, Hondew, Grepa and Tamato** — free and in any
quantity.

Vanilla sells these nowhere and they grow only from planted trees, so correcting a spread was
a multi-day farming chore. In a nuzlocke, where a mis-trained Pokémon can't simply be
replaced, that matters more than usual.

It uses the ordinary Poké Mart buy screen with its prices zeroed, so it behaves exactly like
shopping. Poké Marts will not buy these berries back.

---

## 6. Balance changes

Mauville Game Corner TM prices, in coins:

| TM | Was | Now |
| --- | --- | --- |
| Double Team | 1500 | 1000 |
| Psychic | 3500 | 1500 |
| Flamethrower | 4000 | 1500 |
| Thunderbolt | 4000 | 1500 |
| Ice Beam | 4000 | 1500 |

---

## 7. Save compatibility

**Ordinary Emerald saves still load, and a save made before any given feature keeps playing
the way it was.** That was a hard constraint on every change here, not an afterthought.

Nothing was appended to a save structure. Every field was carved out of existing filler and
the totals kept identical, so no offset moved:

- The permadeath bit comes from `PokemonSubstruct3`'s unused ribbon field — the substruct
  stays exactly 12 bytes.
- The per-area catch bitfield and the rule flags come from SaveBlock1's `unused_3598`.
- The key system's settings come from SaveBlock2's `filler_90`.
- `FLAG_CUT_TREE_*` and `VAR_LAST_REPEL_USED` take unused flag and var ids.

`STATIC_ASSERT`s pin each of those layouts, so a change that would move an offset fails the
build rather than silently corrupting saves.

Every new setting is also defined so that **zero means the behaviour an older save was played
under**. An existing save reads zero for a field that didn't exist when it was written, so
level caps, SET battles and the bag-item ban all stay in force on it, the exp modifier reads
`1×`, and the keys read off.

---

## Planned

- **Soul Link** — Pokémon caught in the same area are linked; when one dies, its partner dies
  too. Not implemented.
