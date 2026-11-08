#!/usr/bin/env bash
# install.sh — copy the release binary into PREFIX and register a launcher icon.
#
# Usage (normally via the Makefile):
#   PREFIX=/usr/local DESTDIR= APPDIR= scripts/install.sh
#   PREFIX=/usr/local DESTDIR= APPDIR= scripts/install.sh --uninstall
#
# DESTDIR is a packaging staging root and is not written into .desktop Exec
# paths or the PATH symlink. Empty DESTDIR installs onto the live system.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE=install

while [[ $# -gt 0 ]]; do
    case "$1" in
        --uninstall)
            MODE=uninstall
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--uninstall]"
            echo "Environment: PREFIX DESTDIR APPDIR (see README.md)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
UNAME_S="$(uname -s)"
BIN_NAME="mitr"
APP_NAME="mitr.app"

stage_root() {
    # Join DESTDIR and an absolute path without doubling slashes.
    local abs="$1"
    if [[ -z "$DESTDIR" ]]; then
        printf '%s\n' "$abs"
        return
    fi
    printf '%s%s\n' "${DESTDIR%/}" "$abs"
}

BINDIR="${PREFIX%/}/bin"
DATADIR="${PREFIX%/}/share"
BINDST="$(stage_root "$BINDIR")"
DATADST="$(stage_root "$DATADIR")"

if [[ "$UNAME_S" == Darwin ]]; then
    if [[ -n "${APPDIR:-}" ]]; then
        APPDIR="${APPDIR%/}"
    elif [[ "$PREFIX" == "$HOME"* ]]; then
        APPDIR="$HOME/Applications"
    else
        APPDIR="/Applications"
    fi
    APPDST="$(stage_root "$APPDIR")"
fi

BINARY="$ROOT/build/mitr"
ICON_PNG="$ROOT/assets/icons/mitr.png"
ICON_SVG="$ROOT/assets/icons/mitr.svg"
DESKTOP_IN="$ROOT/assets/linux/mitr.desktop"
PLIST_IN="$ROOT/assets/macos/Info.plist"

need_file() {
    local path="$1"
    if [[ ! -e "$path" ]]; then
        echo "Missing $path" >&2
        exit 1
    fi
}

ensure_dir() {
    install -d -m 755 "$1"
}

install_binary() {
    need_file "$BINARY"
    if [[ ! -x "$BINARY" ]]; then
        echo "Not an executable: $BINARY (run: make release)" >&2
        exit 1
    fi
    ensure_dir "$BINDST"
    install -m 755 "$BINARY" "$BINDST/$BIN_NAME"
    echo "Installed $BINDST/$BIN_NAME"
}

remove_binary() {
    rm -f "$BINDST/$BIN_NAME"
}

install_linux_launcher() {
    need_file "$DESKTOP_IN"
    need_file "$ICON_SVG"
    need_file "$ICON_PNG"

    local apps="$DATADST/applications"
    local icons="$DATADST/icons/hicolor"
    ensure_dir "$apps"
    ensure_dir "$icons/scalable/apps"
    ensure_dir "$icons/512x512/apps"

    sed "s|@BINDIR@|$BINDIR|g" "$DESKTOP_IN" >"$apps/mitr.desktop"
    chmod 644 "$apps/mitr.desktop"
    install -m 644 "$ICON_SVG" "$icons/scalable/apps/mitr.svg"
    install -m 644 "$ICON_PNG" "$icons/512x512/apps/mitr.png"
    echo "Installed $apps/mitr.desktop"

    if [[ -z "$DESTDIR" ]]; then
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database "$DATADIR/applications" 2>/dev/null || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -f -t "$DATADIR/icons/hicolor" 2>/dev/null || true
        fi
    fi
}

remove_linux_launcher() {
    rm -f "$DATADST/applications/mitr.desktop"
    rm -f "$DATADST/icons/hicolor/scalable/apps/mitr.svg"
    rm -f "$DATADST/icons/hicolor/512x512/apps/mitr.png"
}

make_icns() {
    local png="$1"
    local icns="$2"
    if ! sips -s format icns "$png" --out "$icns" >/dev/null; then
        echo "sips failed; cannot build $icns" >&2
        exit 1
    fi
}

install_macos_launcher() {
    need_file "$BINARY"
    need_file "$PLIST_IN"
    need_file "$ICON_PNG"
    if ! command -v sips >/dev/null 2>&1; then
        echo "macOS install needs sips (Xcode CLT)" >&2
        exit 1
    fi

    local app="$APPDST/$APP_NAME"
    ensure_dir "$app/Contents/MacOS"
    ensure_dir "$app/Contents/Resources"
    install -m 644 "$PLIST_IN" "$app/Contents/Info.plist"
    install -m 755 "$BINARY" "$app/Contents/MacOS/$BIN_NAME"
    make_icns "$ICON_PNG" "$app/Contents/Resources/AppIcon.icns"
    echo "Installed $app"

    # PATH entry: symlink to the bundle so there is one copy of the binary.
    ensure_dir "$BINDST"
    ln -sf "$APPDIR/$APP_NAME/Contents/MacOS/$BIN_NAME" "$BINDST/$BIN_NAME"
    echo "Linked $BINDST/$BIN_NAME -> $APPDIR/$APP_NAME/Contents/MacOS/$BIN_NAME"
}

remove_macos_launcher() {
    rm -rf "$APPDST/$APP_NAME"
    rm -f "$BINDST/$BIN_NAME"
}

writable_or_die() {
    local dir="$1"
    local parent="$dir"
    while [[ ! -d "$parent" && "$parent" != "/" && "$parent" != "." ]]; do
        parent="$(dirname "$parent")"
    done
    if [[ ! -w "$parent" ]]; then
        echo "Cannot write $dir (parent $parent is not writable)." >&2
        echo "Use: sudo make install" >&2
        echo "  or: make install PREFIX=\$HOME/.local" >&2
        if [[ "$UNAME_S" == Darwin ]]; then
            echo "  or: make install PREFIX=\$HOME/.local APPDIR=\$HOME/Applications" >&2
        fi
        exit 1
    fi
}

case "$MODE" in
    install)
        if [[ ! -x "$BINARY" ]]; then
            echo "build/mitr is missing. The Makefile runs 'make release' first." >&2
            exit 1
        fi
        writable_or_die "$BINDST"
        case "$UNAME_S" in
            Linux)
                writable_or_die "$DATADST"
                install_linux_launcher
                install_binary
                ;;
            Darwin)
                writable_or_die "$APPDST"
                install_macos_launcher
                ;;
            *)
                echo "Unknown OS '$UNAME_S': installing the binary only." >&2
                install_binary
                ;;
        esac
        echo "Done. Launch with: $BINDIR/$BIN_NAME"
        if [[ -n "$DESTDIR" ]]; then
            echo "Staged under DESTDIR=$DESTDIR (not on PATH until copied to the live prefix)."
        fi
        ;;
    uninstall)
        case "$UNAME_S" in
            Linux)
                remove_linux_launcher
                remove_binary
                ;;
            Darwin)
                remove_macos_launcher
                ;;
            *)
                remove_binary
                ;;
        esac
        echo "Uninstalled mitr from $PREFIX"
        ;;
esac
