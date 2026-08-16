#!/bin/bash
# Builds the nuzlocke ROM. Run from WSL:
#   bash "/mnt/e/Pokemon ROM Hacks/Pokemon Emerald Nuzlocke Soul Link/pokeemerald/build.sh"
#
# Note: devkit-env.sh only puts $DEVKITPRO/tools/bin on PATH, not devkitARM/bin,
# so add the latter explicitly for anything that invokes the toolchain directly.
set -e

export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

cd "$(dirname "$0")"

make modern -j"$(nproc)" "$@"

# Keep a clearly-named copy at the project root for loading into mGBA.
cp pokeemerald_modern.gba "../Pokemon Emerald Nuzlocke.gba"

echo
echo "Built: $(pwd)/pokeemerald_modern.gba"
echo "Copy:  $(cd .. && pwd)/Pokemon Emerald Nuzlocke.gba"
