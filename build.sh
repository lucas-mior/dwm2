#!/bin/sh

# --- Configuration (from config.mk) ---
VERSION="6.5"
PREFIX="/usr/local"
MANPREFIX="${PREFIX}/share/man"
X11INC="/usr/X11R6/include"
X11LIB="/usr/X11R6/lib"

# Xinerama
XINERAMALIBS="-lXinerama"
XINERAMAFLAGS="-DXINERAMA"

# Freetype
FREETYPELIBS="-lfontconfig -lXft"
FREETYPEINC="/usr/include/freetype2"

# Compiler and Flags
CC="clang"
INCS="-I${X11INC} -I${FREETYPEINC}"
LIBS="-L${X11LIB} -lX11 ${XINERAMALIBS} ${FREETYPELIBS} -lXrender -lImlib2"

CPPFLAGS="-D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L -DVERSION=\"${VERSION}\" ${XINERAMAFLAGS}"
CFLAGS="-std=c99 -Weverything -Wfatal-errors ${INCS} ${CPPFLAGS} \
        -Wno-unsafe-buffer-usage -Wno-format-nonliteral \
        -Wno-deprecated-declarations -Wno-c23-extensions \
        -Wno-disabled-macro-expansion -Wno-unused-function -Wno-padded \
        -O2 -flto"
LDFLAGS="${LIBS}"

SRC="drw.c dwm.c"

# --- Logic (from Makefile) ---

build_dwm() {
    echo "Building dwm..."
    # Generate tags
    ctags --kinds-C=+l *.h *.c
    vtags.sed tags > .tags.vim
    # Compile
    ${CC} -g ${CFLAGS} -o dwm ${SRC} ${LDFLAGS}
}

clean_dwm() {
    echo "Cleaning..."
    rm -f dwm dwm-${VERSION}.tar.gz tags .tags.vim
}

install_dwm() {
    echo "Installing to ${DESTDIR}${PREFIX}..."
    mkdir -p "${DESTDIR}${PREFIX}/bin"
    cp -f dwm "${DESTDIR}${PREFIX}/bin"
    chmod 755 "${DESTDIR}${PREFIX}/bin/dwm"
    
    mkdir -p "${DESTDIR}${MANPREFIX}/man1"
    sed "s/VERSION/${VERSION}/g" < dwm.1 > "${DESTDIR}${MANPREFIX}/man1/dwm.1"
    chmod 644 "${DESTDIR}${MANPREFIX}/man1/dwm.1"
}

uninstall_dwm() {
    echo "Uninstalling..."
    rm -f "${DESTDIR}${PREFIX}/bin/dwm" "${DESTDIR}${MANPREFIX}/man1/dwm.1"
}

# --- Execution ---

case "$1" in
    "all" | "")
        build_dwm
        ;;
    "clean")
        clean_dwm
        ;;
    "install")
        if [ ! -f dwm ]; then build_dwm; fi
        install_dwm
        ;;
    "uninstall")
        uninstall_dwm
        ;;
    *)
        echo "Usage: $0 {all|clean|install|uninstall}"
        exit 1
        ;;
esac
