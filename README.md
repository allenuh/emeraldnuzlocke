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
