# Pokémon Emerald — Nuzlocke Soul Link

A Pokémon Emerald ROM hack that enforces nuzlocke rules as built-in game mechanics, rather than
leaving them to the honor system. Built on the [pret/pokeemerald](https://github.com/pret/pokeemerald)
decompilation.

## Rules implemented

### Faint = permanently unusable

A Pokémon that faints is dead for good. It can never be healed, revived, or sent into battle again —
whether it went down in combat or to field poison. The Pokémon Center won't touch it, items have no
effect on it, and it can't be laundered back to life through the PC.

**How it works.** Vanilla Emerald already refuses to send out, switch to, or count as alive any
Pokémon at 0 HP. So rather than patching dozens of call sites, the rule guarantees one invariant:
*a dead Pokémon's HP can never become non-zero again.* Every existing usability check then enforces
the rule for free.

The dead state is a `nuzlockeDead` bit taken from the unused `unusedRibbons` field in
`PokemonSubstruct3`. It sits inside `BoxPokemon`, so it survives PC deposit, daycare, and trading —
which also closes the vanilla Gen 3 quirk where boxing a fainted Pokémon and withdrawing it heals it
to full. The substruct stays exactly 12 bytes, so **the save format is unchanged** and ordinary
Emerald saves still load.

Deliberate exemptions: Battle Frontier and Battle Tower faints don't count, since those modes save
and restore the party around a challenge. Link battles are excluded by an explicit guard.

Relevant code: [`src/nuzlocke.c`](src/nuzlocke.c), [`include/nuzlocke.h`](include/nuzlocke.h), and
hooks in `src/pokemon.c`, `src/battle_script_commands.c`, `src/battle_main.c`, `src/field_poison.c`,
`src/party_menu.c`, and `src/script_pokemon_util.c`.

### Level caps

No Pokémon may be raised past the level of the next boss trainer's strongest Pokémon. The cap starts
at 15 (Roxanne's Nosepass) and rises with each badge — 19, 24, 29, 31, 33, 42, 46 — then sits at 55
for the whole Pokémon League, since the hardcore rule caps League entry at the *final* Elite Four
member and there is no way back out to train once the gauntlet starts. Beating the Champion raises
it to 78 for Steven's rematch in Meteor Falls, after which it is lifted entirely.

**How it works.** Rather than punishing the player for over-levelling after the fact, the cap simply
refuses the experience. A Pokémon standing on the cap earns nothing, and the exp it would have taken
is handed to a party member that still has room — whether or not that Pokémon fought. If the whole
party is capped, the exp is lost. Awards are also clamped, so a Pokémon one level below the cap
receives exactly enough to land on it and never overshoots.

The whole party's share is worked out in a single pass before any of it is handed out
(`NuzlockeComputeExpAwards`). Vanilla's exp loop walks the party one slot at a time, so deciding a
redirect on the fly would mean reaching backwards into slots it had already passed. Settling the
table up front also means the clamp happens *after* the Lucky Egg, trainer-battle and traded-Pokémon
multipliers, so a redirect can't quietly push its recipient back over the cap.

Effort values are deliberately left alone: exp moves between Pokémon under this rule, EVs do not.
Rare Candy and the Day Care are capped too — the Day Care by clamping the experience itself, which
also keeps its "grew N levels" preview and its price quote honest.

A Pokémon *obtained* above the cap (a Lv 70 Rayquaza caught while the cap is 55) is benched: it
can't be sent out or switched to, and can't fill the second slot of a double battle, until the cap
rises past it. Unlike the permadeath rule this can't lean on the HP-pinned-at-0 invariant — that
would kill it outright — so those refusals are spelled out at the three places a Pokémon gets sent
into battle. Battle Frontier, Battle Tower and link battles are exempt, as they are for permadeath.

The cap is derived entirely from badge and story flags that already exist, so **no save data was
added** for this rule.

Relevant code: [`src/nuzlocke.c`](src/nuzlocke.c), with hooks in `src/battle_script_commands.c`
(`Cmd_getexp`), `src/battle_controllers.c`, `src/battle_controller_player.c`,
`src/battle_controller_player_partner.c`, `src/party_menu.c`, `src/pokemon.c`, and `src/daycare.c`.

### Planned

- **Soul Link** — Pokémon caught in the same area are linked; when one dies, its partner dies too.

## Building

Requires a Linux/WSL environment with devkitARM. See [INSTALL.md](INSTALL.md) for full toolchain
setup, then:

```bash
./build.sh
```

That wraps `make modern -j$(nproc)` and produces `pokeemerald_modern.gba`, plus a copy named
`Pokemon Emerald Nuzlocke.gba` one directory up. The ROM runs in any GBA emulator; it's been tested
in mGBA.

`build.sh` exists because devkitPro's `devkit-env.sh` only adds `$DEVKITPRO/tools/bin` to `PATH`,
not `devkitARM/bin`, so invoking the toolchain directly fails without the extra path setup.

> **Note:** this repository contains source code only. No ROM is distributed — you build one
> yourself. Nothing here contains copyrighted Nintendo/Game Freak assets.

## Credit

The entire decompilation is the work of the **[pret](https://github.com/pret)** team and its
contributors — an enormous reverse-engineering effort that made this hack possible. This repository
retains pret's full commit history, and upstream remains available as a remote:

```bash
git remote -v                      # upstream -> github.com/pret/pokeemerald
git fetch upstream
git diff upstream/master           # exactly what this hack changes
git merge upstream/master          # pull in upstream fixes
```

Everything outside of the nuzlocke code listed above is pret's work, not mine.

---

## Original pokeemerald README

*Preserved verbatim from [pret/pokeemerald](https://github.com/pret/pokeemerald).*

> # Pokémon Emerald
>
> This is a decompilation of Pokémon Emerald.
>
> It builds the following ROM:
>
> * [**pokeemerald.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=1961) `sha1: f3ae088181bf583e55daf962a92bb46f4f1d07b7`
>
> To set up the repository, see [INSTALL.md](INSTALL.md).
>
> For contacts and other pret projects, see [pret.github.io](https://pret.github.io/).
