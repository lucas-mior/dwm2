#!/bin/sh -e

# shellcheck disable=SC2086

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. "./cbase/common.sh"

VERSION="6.5"
program=$(common_get_program "$0")
SRC=main.c

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
FREETYPEINC="/usr/include/freetype2"
HBINC="/usr/include/harfbuzz"

cbase="cbase"
cd "$dir" || exit
script=$(basename "$0")

targets=$(cat <<'EOF_TARGETS'
build
debug
fast_feedback
install
uninstall
test
check
release
clean
EOF_TARGETS
)

common_build_parse_args "$@"
common_build_validate_mode "$script" "$targets"

common_build_print_invocation "$script"

XINERAMALIBS="-lXinerama"
XINERAMAFLAGS="-DXINERAMA"
FREETYPELIBS="-lfontconfig -lXft -lharfbuzz"

CC=$(common_get_compiler "$mode")

LDFLAGS="$LDFLAGS -lX11 ${XINERAMALIBS} ${FREETYPELIBS} -lXrender -lImlib2 -lm"

CPPFLAGS="$CPPFLAGS -DVERSION=$VERSION ${XINERAMAFLAGS}"
CPPFLAGS="$CPPFLAGS -I$dir/$cbase -I${FREETYPEINC} -I${HBINC}"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Werror=all -Werror=extra"
CFLAGS="$CFLAGS -Werror"  # Only uncomment occasionally, keep this line
CFLAGS="$CFLAGS -Wno-deprecated-declarations"

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-cast-align"
    CFLAGS="$CFLAGS -Wno-cast-qual"
    CFLAGS="$CFLAGS -Wno-constant-logical-operand"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-declaration-after-statement"
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-float-equal"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-unused-macros"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
fi

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

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
    set +e
    CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" "$0" build

    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    CC=clang CFLAGS="$CFLAGS" "$0" build

    exit
    ;;
debug)
    CFLAGS="$CFLAGS -g3 -Og -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
release|build)
    CFLAGS="$CFLAGS -O2 -flto -march=native"
    ;;
clean)
    echo "Cleaning..."
    rm -rf bin/ tags .tags.vim
    exit
    ;;
esac

case "$mode" in
uninstall)
    trace_on
    rm -f "${DESTDIR}${PREFIX}/bin/${program}"
    rm -f "${DESTDIR}${PREFIX}/man/man1/${program}.1"
    trace_off
    exit
    ;;
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
*)
    trace_on
    common_build_tags

    $CC $CPPFLAGS $CFLAGS $SRC -o "${exe}" $LDFLAGS

    trace_off
    ;;
esac
