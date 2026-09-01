#!/bin/sh -e

# shellcheck disable=SC2086

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. ./cbase/common.sh

VERSION="6.5"
program=$(common_get_program "$0")
SRC=main.c
script=$(basename "$0")
common_build_parse_args "$@"

case "$mode" in
build|check|clean|debug|debug-fast|fast_feedback|install|release|test|uninstall)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

common_build_print_invocation "$script"
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
exe="bin/$program"
mkdir -p "$(dirname "$exe")"
CC=$(common_get_compiler "$mode")

case "$mode" in
clean)
    echo "Cleaning..."
    rm -rf bin/ tags .tags.vim
    exit
    ;;
uninstall)
    trace_on
    rm -f "${DESTDIR}${PREFIX}/bin/${program}"
    rm -f "${DESTDIR}${PREFIX}/man/man1/${program}.1"
    trace_off
    exit
    ;;
esac

CPPFLAGS="$CPPFLAGS -DVERSION=$VERSION -DXINERAMA"
CPPFLAGS="$CPPFLAGS -I."
CPPFLAGS="$CPPFLAGS -Icbase"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags x11)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags xinerama)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags xft)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags fontconfig)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags freetype2)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags harfbuzz)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags xrender)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags imlib2)"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"

LDFLAGS="$LDFLAGS -lm"
LDFLAGS="$LDFLAGS $(pkg-config --libs x11)"
LDFLAGS="$LDFLAGS $(pkg-config --libs xinerama)"
LDFLAGS="$LDFLAGS $(pkg-config --libs xft)"
LDFLAGS="$LDFLAGS $(pkg-config --libs fontconfig)"
LDFLAGS="$LDFLAGS $(pkg-config --libs freetype2)"
LDFLAGS="$LDFLAGS $(pkg-config --libs harfbuzz)"
LDFLAGS="$LDFLAGS $(pkg-config --libs xrender)"
LDFLAGS="$LDFLAGS $(pkg-config --libs imlib2)"

case "$mode" in
test)
    TEST_EXCLUDE_PATTERN='(^|/)cbase/' common_test "$target"
    exit
    ;;
fast_feedback)
    trace_on
    $CC $CPPFLAGS $CFLAGS main.c -o "$exe" $LDFLAGS
    trace_off
    exit
    ;;
check)
    common_build_run_analyzers build
    ;;
debug)
    CFLAGS="$CFLAGS -g3 -Og"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/$program"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -Wno-error -g2 -O2 -flto -march=native -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
build)
    CFLAGS="$CFLAGS -Wno-error -O2 -flto -march=native -ftree-vectorize"
    ;;
check|clean|debug|debug-fast|fast_feedback|install|release|test|uninstall)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

case "$mode" in
install)
    if [ ! -f "$exe" ]; then
        $0 build
    fi
    trace_on
    install -Dm755 "$exe" "${DESTDIR}${PREFIX}/bin/${program}"
    install -Dm644 "${program}.1" "${DESTDIR}${PREFIX}/man/man1/${program}.1"
    trace_off
    exit
    ;;
build|debug|debug-fast|release)
    common_build_tags cbase .

    trace_on
    $CC $CPPFLAGS $CFLAGS $SRC -o "${exe}" $LDFLAGS
    trace_off

    ;;
esac
