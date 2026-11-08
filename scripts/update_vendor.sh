#!/usr/bin/env bash
# update_vendor.sh - Update vendored dependency to a new version
#
# Usage:
#   ./scripts/update_vendor.sh sdl2 2.30.11
#   ./scripts/update_vendor.sh freetype 2.13.4
#   ./scripts/update_vendor.sh libyaml 0.2.6
#   ./scripts/update_vendor.sh sdl2_ttf 2.22.1
#   ./scripts/update_vendor.sh expat 2.8.5
#   ./scripts/update_vendor.sh fontconfig 2.18.3
#   ./scripts/update_vendor.sh gperf 3.3
#   ./scripts/update_vendor.sh zlib 1.3.2

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$PROJECT_ROOT/vendor"

if [ $# -ne 2 ]; then
    echo -e "${RED}Usage: $0 <library> <version>${NC}"
    echo ""
    echo "Examples:"
    echo "  $0 sdl2 2.30.11"
    echo "  $0 freetype 2.13.4"
    echo "  $0 libyaml 0.2.6"
    echo "  $0 sdl2_ttf 2.22.1"
    exit 1
fi

LIBRARY=$1
VERSION=$2
PRESERVE=""

echo -e "${BLUE}=== Updating $LIBRARY to version $VERSION ===${NC}"
echo ""

case $LIBRARY in
    sdl2)
        DIR="SDL2"
        URL="https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL2-$VERSION.tar.gz"
        EXTRACT_DIR="SDL2-$VERSION"
        CLEANUP="test docs android-project Xcode Xcode-iOS VisualC VisualC-GDK VisualC-WinRT"
        ;;
    sdl2_ttf)
        DIR="SDL2_ttf"
        URL="https://github.com/libsdl-org/SDL_ttf/releases/download/release-$VERSION/SDL2_ttf-$VERSION.tar.gz"
        EXTRACT_DIR="SDL2_ttf-$VERSION"
        CLEANUP="Xcode VisualC external"
        ;;
    freetype)
        DIR="freetype"
        URL="https://download.savannah.gnu.org/releases/freetype/freetype-$VERSION.tar.xz"
        EXTRACT_DIR="freetype-$VERSION"
        CLEANUP="docs"
        # LICENSE.TXT names these two files. Keep them when the rest of docs/ goes.
        PRESERVE="docs/FTL.TXT docs/GPLv2.TXT"
        ;;
    libyaml)
        DIR="libyaml"
        URL="https://github.com/yaml/libyaml/releases/download/$VERSION/yaml-$VERSION.tar.gz"
        EXTRACT_DIR="yaml-$VERSION"
        CLEANUP=""
        ;;
    expat)
        DIR="expat"
        URL="https://github.com/libexpat/libexpat/releases/download/R_${VERSION//./_}/expat-$VERSION.tar.xz"
        EXTRACT_DIR="expat-$VERSION"
        CLEANUP=""
        ;;
    fontconfig)
        DIR="fontconfig"
        URL="https://gitlab.freedesktop.org/api/v4/projects/890/packages/generic/fontconfig/$VERSION/fontconfig-$VERSION.tar.xz"
        EXTRACT_DIR="fontconfig-$VERSION"
        CLEANUP=""
        ;;
    gperf)
        DIR="gperf"
        URL="https://ftp.gnu.org/gnu/gperf/gperf-$VERSION.tar.gz"
        EXTRACT_DIR="gperf-$VERSION"
        CLEANUP=""
        ;;
    zlib)
        DIR="zlib"
        URL="https://zlib.net/fossils/zlib-$VERSION.tar.gz"
        EXTRACT_DIR="zlib-$VERSION"
        CLEANUP=""
        ;;
    *)
        echo -e "${RED}Unknown library: $LIBRARY${NC}"
        echo "Supported: sdl2, sdl2_ttf, freetype, libyaml, expat, fontconfig, gperf, zlib"
        exit 1
        ;;
esac

# Check if old version exists
if [ ! -d "$VENDOR_DIR/$DIR" ]; then
    echo -e "${YELLOW}Warning: $VENDOR_DIR/$DIR not found${NC}"
    echo "This will be a fresh install"
fi

# Create temp directory
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

echo -e "${BLUE}Downloading $LIBRARY $VERSION...${NC}"
cd "$TMP_DIR"

if [[ $URL == *.tar.xz ]]; then
    curl -L "$URL" | tar xJ
elif [[ $URL == *.tar.gz ]]; then
    curl -L "$URL" | tar xz
else
    echo -e "${RED}Unknown archive format${NC}"
    exit 1
fi

if [ ! -d "$EXTRACT_DIR" ]; then
    echo -e "${RED}Error: extracted directory $EXTRACT_DIR not found${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Downloaded${NC}"

# Cleanup unnecessary files
if [ -n "$CLEANUP" ]; then
    echo -e "${BLUE}Removing unnecessary files...${NC}"
    cd "$EXTRACT_DIR"
    preserved=""
    if [ -n "$PRESERVE" ]; then
        preserved="$TMP_DIR/preserved"
        mkdir -p "$preserved"
        for item in $PRESERVE; do
            if [ ! -f "$item" ]; then
                echo -e "${RED}Required file missing from archive: $item${NC}"
                exit 1
            fi
            mkdir -p "$preserved/$(dirname "$item")"
            cp "$item" "$preserved/$item"
        done
    fi
    for item in $CLEANUP; do
        rm -rf "$item" 2>/dev/null || true
    done
    if [ -n "$preserved" ]; then
        cp -R "$preserved/." .
    fi
    cd "$TMP_DIR"
    echo -e "${GREEN}✓ Cleaned up${NC}"
fi

# Get size
SIZE=$(du -sh "$EXTRACT_DIR" | cut -f1)

# Remove old version
if [ -d "$VENDOR_DIR/$DIR" ]; then
    OLD_SIZE=$(du -sh "$VENDOR_DIR/$DIR" | cut -f1)
    echo -e "${YELLOW}Removing old version (size: $OLD_SIZE)${NC}"
    rm -rf "$VENDOR_DIR/$DIR"
fi

# Move new version
echo -e "${BLUE}Installing new version...${NC}"
mv "$EXTRACT_DIR" "$VENDOR_DIR/$DIR"
echo -e "${GREEN}✓ Installed to $VENDOR_DIR/$DIR (size: $SIZE)${NC}"

echo ""
echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}Update complete!${NC}"
echo -e "${GREEN}==================================${NC}"
echo ""
echo -e "${YELLOW}TODO:${NC}"
echo "1. Update vendor/VERSIONS.md with new version and date"
echo "2. Review changes: git diff vendor/$DIR"
echo "3. Test build: make vendor-clean && make vendor"
echo "4. Commit: git add vendor/$DIR vendor/VERSIONS.md"
echo "            git commit -m \"vendor: update $LIBRARY to $VERSION\""
