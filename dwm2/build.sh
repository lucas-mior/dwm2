#!/bin/sh -e

# shellcheck disable=SC2086

set -e

error () {
    >&2 printf "$@"
    return
}

if [ -n "$BASH_VERSION" ]; then
    # shellcheck disable=SC3044
    shopt -s expand_aliases
fi

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

VERSION="6.5"
program=$(basename "$(readlink -f "$(dirname "$0")")")
SRC="main.c"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
FREETYPEINC="/usr/include/freetype2"
HBINC="/usr/include/harfbuzz"

dir=$(dirname "$(readlink -f "$0")")
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

target="${1:-build}"
if ! printf '%s\n' "$targets" | grep -qx "$target"; then
    echo "usage: $script <targets>"
    printf '%s\n' "$targets"
    exit 1
fi

XINERAMALIBS="-lXinerama"
XINERAMAFLAGS="-DXINERAMA"
FREETYPELIBS="-lfontconfig -lXft -lharfbuzz"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
# CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Wno-padded"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-unused-variable"

LDFLAGS="$LDFLAGS -lX11 ${XINERAMALIBS} ${FREETYPELIBS} -lXrender -lImlib2 -lm"

CPPFLAGS="-D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L"
CPPFLAGS="$CPPFLAGS -DVERSION=$VERSION ${XINERAMAFLAGS}"
CPPFLAGS="$CPPFLAGS -I$dir/$cbase -I${FREETYPEINC} -I${HBINC}"

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CC="${CC:-tcc}"

option_remove() {
    echo "$1" | sed -E "s| *$2 +| |g"
}

with_other () {
    compiler="$1"
    compiler_macro=$(echo "$compiler" | tr '[:lower:]' '[:upper:]' | tr ' ' '_')
    compiler_macro="__${compiler_macro}__"
    shift
    args="$*"
    trace_on
    while ! problem=$($compiler "-D${compiler_macro}" $args 2>&1); do
        trace_off
        problem=$(echo "$problem" | head -n 1 | tr -d "'")
        sleep 0.4
        if echo "$problem" | grep -Eq "unknown (argument|option)"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            args=$(option_remove "$args" "$arg")
        else
            printf "\nError compiling with $compiler:\n%s\n" "$problem"
            return 1
        fi
        trace_on
    done
    return 0
}

case "$target" in
"test")
    exit
    ;;
"fast_feedback")
    trace_on
    clang $CPPFLAGS $CFLAGS -Werror main.c -o "$exe" $LDFLAGS
    trace_off
    exit
    ;;
"check")
    CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" "$0" build
    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    CC=clang CFLAGS="$CFLAGS" "$0" build
    exit
    ;;
"debug")
    CFLAGS="$CFLAGS -g3 -O0 -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
"release"|"build")
    CFLAGS="$CFLAGS -O2 -flto -march=native"
    ;;
"clean")
    echo "Cleaning..."
    rm -rf bin/ tags .tags.vim
    exit
    ;;
esac

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-incompatible-pointer-types-discards-qualifiers"
    CFLAGS="$CFLAGS -Wno-cast-qual"
    CFLAGS="$CFLAGS -Wno-gnu-union-cast"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-float-equal"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-cast-align"
    CFLAGS="$CFLAGS -Wno-declaration-after-statement"
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
    CFLAGS="$CFLAGS -Wno-documentation-unknown-command"
    CFLAGS="$CFLAGS -Wno-documentation"
    CFLAGS="$CFLAGS -Wno-reserved-identifier"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
fi

case "$target" in
"uninstall")
    trace_on
    rm -f "${DESTDIR}${PREFIX}/bin/${program}"
    rm -f "${DESTDIR}${PREFIX}/man/man1/${program}.1"
    trace_off
    exit
    ;;
"install")
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
    ctags --kinds-C=+l+d *.c *.h cbase/*.c cbase/*.h 2> /dev/null || true
    if [ -f tags ]; then
        vtags.sed tags | sort | uniq > .tags.vim 2> /dev/null || true
    fi
    
    if [ "$CC" = "chibicc" ] || [ "$CC" = "cproc" ]; then
        with_other "$CC" $CPPFLAGS $CFLAGS $LDFLAGS -o "${exe}" $SRC
    else
        $CC $CPPFLAGS $CFLAGS $SRC -o "${exe}" $LDFLAGS
    fi
    
    if [ "$target" = "debug" ]; then
        trace_on

        DISPLAY=:0 Xephyr -br -ac -noreset -screen 1280x720 :1 &
        xephyr=$!
        sleep 1
        DISPLAY=:1 gdb bin/dwm2_debug -ex run

        kill -s KILL $xephyr

        trace_off
    fi
    trace_off
    ;;
esac
