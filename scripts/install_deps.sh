#!/usr/bin/env bash

set -euo pipefail

USE_VENDOR="${USE_VENDOR:-1}"
DEPS_DEV="${DEPS_DEV:-0}"
DRY_RUN="${DEPS_DRY_RUN:-0}"

if [[ "$USE_VENDOR" != "0" && "$USE_VENDOR" != "1" ]]; then
    echo "USE_VENDOR must be 0 or 1" >&2
    exit 2
fi

if [[ "$DEPS_DEV" != "0" && "$DEPS_DEV" != "1" ]]; then
    echo "DEPS_DEV must be 0 or 1" >&2
    exit 2
fi

print_command() {
    printf '  '
    printf '%q ' "$@"
    printf '\n'
}

run_command() {
    print_command "$@"
    if [[ "$DRY_RUN" != "1" ]]; then
        "$@"
    fi
}

run_as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        run_command "$@"
        return
    fi

    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required to install system packages" >&2
        exit 1
    fi
    run_command sudo "$@"
}

install_apt() {
    local packages=(
        build-essential cmake autoconf automake libtool pkg-config
        libx11-dev libxext-dev libxrandr-dev libxi-dev libxfixes-dev
        libxcursor-dev
    )
    if [[ "$USE_VENDOR" == "0" ]]; then
        packages+=(libsdl2-dev libsdl2-ttf-dev libyaml-dev libfontconfig1-dev)
    else
        packages+=(python3 gettext autopoint)
    fi
    if [[ "$DEPS_DEV" == "1" ]]; then
        packages+=(clang clang-format clang-tidy)
    fi

    run_as_root apt-get update
    run_as_root apt-get install -y "${packages[@]}"
}

install_dnf() {
    local packages=(
        gcc make cmake autoconf automake libtool pkgconf-pkg-config
        libX11-devel libXext-devel libXrandr-devel libXi-devel libXfixes-devel
        libXcursor-devel
    )
    if [[ "$USE_VENDOR" == "0" ]]; then
        packages+=(SDL2-devel SDL2_ttf-devel libyaml-devel fontconfig-devel)
    else
        packages+=(gcc-c++ python3 gettext gettext-devel)
    fi
    if [[ "$DEPS_DEV" == "1" ]]; then
        packages+=(clang clang-tools-extra)
    fi

    run_as_root dnf install -y "${packages[@]}"
}

install_pacman() {
    local packages=(
        base-devel cmake autoconf automake libtool pkgconf
        libx11 libxext libxrandr libxi libxfixes libxcursor
    )
    if [[ "$USE_VENDOR" == "0" ]]; then
        packages+=(sdl2 sdl2_ttf libyaml fontconfig)
    else
        packages+=(python gettext)
    fi
    if [[ "$DEPS_DEV" == "1" ]]; then
        packages+=(clang)
    fi

    run_as_root pacman -S --needed "${packages[@]}"
}

install_linux() {
    if command -v apt-get >/dev/null 2>&1; then
        install_apt
    elif command -v dnf >/dev/null 2>&1; then
        install_dnf
    elif command -v pacman >/dev/null 2>&1; then
        install_pacman
    else
        echo "Unsupported Linux package manager; install dependencies from README.md" >&2
        exit 1
    fi
}

install_macos() {
    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew is required; install it from https://brew.sh" >&2
        exit 1
    fi

    local packages=(cmake autoconf automake libtool pkg-config)
    if [[ "$USE_VENDOR" == "0" ]]; then
        packages+=(sdl2 sdl2_ttf libyaml)
    fi
    if [[ "$DEPS_DEV" == "1" ]]; then
        packages+=(clang-format)
    fi
    run_command brew install "${packages[@]}"
}

echo "Installing mitr system dependencies (USE_VENDOR=$USE_VENDOR, DEPS_DEV=$DEPS_DEV)..."
case "$(uname -s)" in
    Linux)
        install_linux
        ;;
    Darwin)
        install_macos
        ;;
    *)
        echo "Unsupported operating system; install dependencies from README.md" >&2
        exit 1
        ;;
esac

if [[ "$DRY_RUN" == "1" ]]; then
    echo "Dependency installation dry run complete."
else
    echo "System dependencies installed."
fi
