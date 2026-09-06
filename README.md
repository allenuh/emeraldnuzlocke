# Pokémon Emerald — Hardcore Nuzlocke

A Pokémon Emerald ROM hack that enforces the hardcore nuzlocke ruleset as built-in game
mechanics, rather than leaving it to the honor system. Built on the
[pret/pokeemerald](https://github.com/pret/pokeemerald) decompilation.

Nine rules are enforced in code — you cannot heal a dead Pokémon, catch a second Pokémon on a
route, or over-level past the next gym leader, because the game will not let you. Alongside
them are the conveniences a forced solo run needs: trade evolutions without a second player,
free EV-lowering berries, a summary page for the IVs and EVs you are stuck with, and an
optional set of assists for pace and grind.

See **[FEATURES.md](FEATURES.md)** for the full documentation.

## What's in it

- **Nine nuzlocke rules** enforced as mechanics — permadeath, mandatory nicknames, one catch
  per area with the duplicate clause, SET battles, no bag items, level caps, whiteout ends
  the run, and a shiny clause.
- **A rules screen at new-game time** — five of the nine can be switched on or off per save,
  chosen before the run starts and fixed for its lifetime.
- **A KEY SYSTEM menu** under OPTION — an experience multiplier (`0×` to `5×`), an infinite
  Rare Candy, and reusable TMs, for players who want to loosen the grind.
- **Quality-of-life changes** — instant text, running indoors, a "use another?" prompt when
  a Repel wears off, cut trees that stay cut, Pickup announcing its finds, and an indicator
  on the enemy's health bar telling you whether it counts as the route's catch.
- **A STATS page in the summary** — a fifth page showing a Pokémon's IVs and EVs, neither of
  which vanilla ever displays, plus stat names coloured by what its nature raises and lowers.
- **LINK STONE** — an evolution stone that stands in for a trade, so Alakazam, Machamp,
  Gengar and the rest are reachable in a solo run.
- **A Surplus Berries box** outside the Berry Master's house, giving away the EV-lowering
  berries for free.

Save compatibility was a hard constraint throughout: every rule and setting was fitted into
existing unused save fields, so ordinary Emerald saves still load.

## Building

Requires a Linux/WSL environment with devkitARM. See [INSTALL.md](INSTALL.md) for full
toolchain setup, then:

```bash
./build.sh
```

That wraps `make modern -j$(nproc)` and produces `pokeemerald_modern.gba`, plus a copy named
`Pokemon Emerald Nuzlocke.gba` one directory up. The ROM runs in any GBA emulator; it's been
tested in mGBA.

`build.sh` exists because devkitPro's `devkit-env.sh` only adds `$DEVKITPRO/tools/bin` to
`PATH`, not `devkitARM/bin`, so invoking the toolchain directly fails without the extra path
setup.

> **Note:** this repository contains source code only. No ROM is distributed — you build one
> yourself. Nothing here contains copyrighted Nintendo/Game Freak assets.

## Credit

The entire decompilation is the work of the **[pret](https://github.com/pret)** team and its
contributors — an enormous reverse-engineering effort that made this hack possible. This
repository retains pret's full commit history, and everything outside the hack code
documented in [FEATURES.md](FEATURES.md) is their work, not mine.
