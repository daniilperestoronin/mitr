#!/usr/bin/env bash
# build_vendor.sh - Build all vendored dependencies as static libraries
#
# Usage:
#   ./scripts/build_vendor.sh           # Build all dependencies (skip already built)
#   ./scripts/build_vendor.sh --clean   # Clean and rebuild all
#   ./scripts/build_vendor.sh --only sdl2   # Build only a specific library

set -eo pipefail  # Exit on errors, including source-copy pipelines

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project root (absolute path)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$PROJECT_ROOT/vendor"
BUILD_DIR="$PROJECT_ROOT/build/vendor"

# Detect number of CPU cores for parallel build
if [[ "$OSTYPE" == "darwin"* ]]; then
    NCPU=$(sysctl -n hw.ncpu)
else
    NCPU=$(nproc)
fi

echo -e "${BLUE}=== mitr Vendor Build ===${NC}"
echo "Project root: $PROJECT_ROOT"
echo "Vendor dir: $VENDOR_DIR"
echo "Build dir: $BUILD_DIR"
echo "CPU cores: $NCPU"
echo ""

# Parse arguments
CLEAN=0
ONLY=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=1
            shift
            ;;
        --only)
            ONLY="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--clean] [--only <library>]"
            exit 1
            ;;
    esac
done

# Clean if requested
if [ $CLEAN -eq 1 ]; then
    echo -e "${YELLOW}Cleaning build artifacts...${NC}"
    rm -rf "$BUILD_DIR"
    rm -rf "$VENDOR_DIR"/*/build_static
    echo -e "${GREEN}Clean complete${NC}"
    echo ""
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Check if library is already built
is_built() {
    local lib=$1
    [ -f "$BUILD_DIR/lib/lib${lib}.a" ]
}

is_tool_built() {
    local tool=$1
    [ -x "$BUILD_DIR/bin/${tool}" ]
}

# Bootstrap copies so generated Autotools files never change vendored sources.
prepare_autotools_source() {
    local library=$1
    local source_dir="$BUILD_DIR/src/$library"
    rm -rf "$source_dir"
    mkdir -p "$source_dir"
    tar -C "$VENDOR_DIR/$library" --exclude=build_static -cf - . |
        tar -C "$source_dir" -xf -
    cd "$source_dir"
}

# Build zlib first; FreeType and Fontconfig link against it.
build_zlib() {
    if [ -n "$ONLY" ] && [ "$ONLY" != "zlib" ]; then
        return
    fi

    if is_built "z" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ zlib already built, skipping${NC}"
        return
    fi

    echo -e "${BLUE}Building zlib...${NC}"
    cd "$VENDOR_DIR/zlib"

    cmake -B build_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$BUILD_DIR" \
        -DZLIB_BUILD_SHARED=OFF \
        -DZLIB_BUILD_TESTING=OFF

    cmake --build build_static --parallel "$NCPU"
    cmake --install build_static

    echo -e "${GREEN}✓ zlib built successfully${NC}"
    echo ""
}

# Build Expat (Fontconfig uses it to parse the system XML configuration)
build_expat() {
    if [[ "$OSTYPE" != "linux"* ]]; then
        return
    fi
    if [ -n "$ONLY" ] && [ "$ONLY" != "expat" ]; then
        return
    fi

    if is_built "expat" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ Expat already built, skipping${NC}"
        return
    fi

    echo -e "${BLUE}Building Expat...${NC}"
    cd "$VENDOR_DIR/expat"

    cmake -B build_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$BUILD_DIR" \
        -DEXPAT_SHARED_LIBS=OFF \
        -DEXPAT_BUILD_DOCS=OFF \
        -DEXPAT_BUILD_EXAMPLES=OFF \
        -DEXPAT_BUILD_TESTS=OFF \
        -DEXPAT_BUILD_TOOLS=OFF

    cmake --build build_static --parallel "$NCPU"
    cmake --install build_static

    echo -e "${GREEN}✓ Expat built successfully${NC}"
    echo ""
}

# Build gperf, which Fontconfig uses to generate static lookup tables.
build_gperf() {
    if [[ "$OSTYPE" != "linux"* ]]; then
        return
    fi
    if [ -n "$ONLY" ] && [ "$ONLY" != "gperf" ]; then
        return
    fi

    if is_tool_built "gperf" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ gperf already built, skipping${NC}"
        return
    fi

    echo -e "${BLUE}Building gperf...${NC}"
    prepare_autotools_source gperf
    (cd lib && autoreconf -fi)
    mkdir -p build_static
    cd build_static

    ../configure --prefix="$BUILD_DIR" --disable-dependency-tracking
    make -C lib -j"$NCPU"
    make -C src -j"$NCPU"
    make -C src install

    echo -e "${GREEN}✓ gperf built successfully${NC}"
    echo ""
}

# Build FreeType (SDL2_ttf and Fontconfig depend on it)
build_freetype() {
    if [ -n "$ONLY" ] && [ "$ONLY" != "freetype" ]; then
        return
    fi
    
    if is_built "freetype" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ FreeType already built, skipping${NC}"
        return
    fi

    if ! is_built "z"; then
        echo -e "${RED}FreeType requires vendored zlib.${NC}"
        echo "Build the full vendor set first with: $0"
        exit 1
    fi
    
    echo -e "${BLUE}Building FreeType...${NC}"
    cd "$VENDOR_DIR/freetype"
    
    cmake -B build_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$BUILD_DIR" \
        -DZLIB_ROOT="$BUILD_DIR" \
        -DFT_DISABLE_BZIP2=ON \
        -DFT_DISABLE_BROTLI=ON \
        -DFT_DISABLE_HARFBUZZ=ON \
        -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_ZLIB=OFF
    
    cmake --build build_static --parallel "$NCPU"
    cmake --install build_static
    
    echo -e "${GREEN}✓ FreeType built successfully${NC}"
    echo ""
}

# Build Fontconfig against the vendored Expat and FreeType.  The library is
# vendored, but it intentionally reads the host's /etc/fonts configuration and
# /var/cache/fontconfig cache so font selection stays consistent with the OS.
build_fontconfig() {
    if [[ "$OSTYPE" != "linux"* ]]; then
        return
    fi
    if [ -n "$ONLY" ] && [ "$ONLY" != "fontconfig" ]; then
        return
    fi

    if is_built "fontconfig" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ Fontconfig already built, skipping${NC}"
        return
    fi

    if ! is_built "z" || ! is_built "expat" || ! is_built "freetype" ||
       ! is_tool_built "gperf"; then
        echo -e "${RED}Fontconfig requires vendored zlib, Expat, FreeType, and gperf.${NC}"
        echo "Build the full vendor set first with: $0"
        exit 1
    fi

    echo -e "${BLUE}Building Fontconfig...${NC}"
    prepare_autotools_source fontconfig
    PATH="$BUILD_DIR/bin:$PATH" AUTORECONF_FLAGS="-fi" \
        bash autogen.sh --noconfigure
    mkdir -p build_static
    cd build_static

    PATH="$BUILD_DIR/bin:$PATH" \
    GPERF="$BUILD_DIR/bin/gperf" \
    PKG_CONFIG_PATH="$BUILD_DIR/lib/pkgconfig" \
    FREETYPE_CFLAGS="-I$BUILD_DIR/include/freetype2" \
    FREETYPE_LIBS="-L$BUILD_DIR/lib -lfreetype -lz" \
    EXPAT_CFLAGS="-I$BUILD_DIR/include" \
    EXPAT_LIBS="-L$BUILD_DIR/lib -lexpat" \
    ../configure \
        --prefix="$BUILD_DIR" \
        --sysconfdir=/etc \
        --localstatedir=/var \
        --datadir=/usr/share \
        --enable-static \
        --disable-shared \
        --disable-docs \
        --disable-cache-build

    make -j"$NCPU"
    make -C src install-libLTLIBRARIES
    make -C fontconfig install-fontconfigincludeHEADERS
    make install-pkgconfigDATA

    echo -e "${GREEN}✓ Fontconfig built successfully${NC}"
    echo ""
}

# Build SDL2
build_sdl2() {
    if [ -n "$ONLY" ] && [ "$ONLY" != "sdl2" ]; then
        return
    fi
    
    if is_built "SDL2" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ SDL2 already built, skipping${NC}"
        return
    fi
    
    echo -e "${BLUE}Building SDL2...${NC}"
    cd "$VENDOR_DIR/SDL2"
    
    cmake -B build_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$BUILD_DIR" \
        -DSDL_STATIC=ON \
        -DSDL_SHARED=OFF \
        -DSDL_TEST=OFF
    
    cmake --build build_static --parallel "$NCPU"
    cmake --install build_static
    
    echo -e "${GREEN}✓ SDL2 built successfully${NC}"
    echo ""
}

# Build SDL2_ttf
build_sdl2_ttf() {
    if [ -n "$ONLY" ] && [ "$ONLY" != "sdl2_ttf" ]; then
        return
    fi
    
    if is_built "SDL2_ttf" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ SDL2_ttf already built, skipping${NC}"
        return
    fi
    
    echo -e "${BLUE}Building SDL2_ttf...${NC}"
    cd "$VENDOR_DIR/SDL2_ttf"
    
    cmake -B build_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$BUILD_DIR" \
        -DCMAKE_PREFIX_PATH="$BUILD_DIR" \
        -DSDL2TTF_VENDORED=OFF \
        -DSDL2TTF_SAMPLES=OFF
    
    cmake --build build_static --parallel "$NCPU"
    cmake --install build_static
    
    echo -e "${GREEN}✓ SDL2_ttf built successfully${NC}"
    echo ""
}

# Build libyaml
build_libyaml() {
    if [ -n "$ONLY" ] && [ "$ONLY" != "libyaml" ]; then
        return
    fi
    
    if is_built "yaml" && [ $CLEAN -eq 0 ]; then
        echo -e "${GREEN}✓ libyaml already built, skipping${NC}"
        return
    fi
    
    echo -e "${BLUE}Building libyaml...${NC}"
    cd "$VENDOR_DIR/libyaml"
    
    # Note: libyaml release tarball uses autotools, not CMake
    # Create separate build directory
    mkdir -p build_static
    cd build_static
    
    # Use system aclocal/automake (avoids "aclocal-1.15: command not found" on Ubuntu)
    export ACLOCAL="${ACLOCAL:-aclocal}"
    export AUTOMAKE="${AUTOMAKE:-automake}"
    
    # Configure with autotools (static library)
    ../configure \
        --prefix="$BUILD_DIR" \
        --enable-static \
        --disable-shared \
        --silent
    
    # Build and install
    make -j"$NCPU"
    make install
    
    echo -e "${GREEN}✓ libyaml built successfully${NC}"
    echo ""
}

# Main build sequence
# Order matters: FreeType uses zlib; Fontconfig needs Expat, gperf, and
# FreeType; SDL2_ttf needs FreeType.
build_zlib
build_expat
build_gperf
build_freetype
build_fontconfig
build_sdl2
build_sdl2_ttf
build_libyaml

# Summary
echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}Vendor build complete!${NC}"
echo -e "${GREEN}==================================${NC}"
echo ""
echo "Static libraries installed to:"
echo "  $BUILD_DIR/lib/"
echo ""
echo "Headers installed to:"
echo "  $BUILD_DIR/include/"
echo ""
echo "You can now build mitr:"
echo "  make debug"
