#!/usr/bin/env bash
# Builds updateAll.exe, the Windows program, with mingw-w64:
#
#   updateAll/win/build.sh      ->  updateAll/win/build/updateAll.exe
#
# Needs x86_64-w64-mingw32-gcc and x86_64-w64-mingw32-windres (Debian/Ubuntu:
# apt install mingw-w64). The program links only Windows' own DLLs, so the
# exe is all a user needs. ../updateAll.json is built in.
set -euo pipefail
cd "$(dirname "$0")"

CROSS=${CROSS:-x86_64-w64-mingw32-}
CC="${CROSS}gcc"
WINDRES="${CROSS}windres"
for tool in "$CC" "$WINDRES"; do
    command -v "$tool" >/dev/null || { echo "ERROR: $tool not found (apt install mingw-w64)" >&2; exit 1; }
done

# The program's own version, which is not the version of the bootloader. Raise it
# in VERSION whenever the program changes: the program offers itself as an update
# by this number, and a release stops if the program changed and it did not.
# UA_VERSION gives another one for a test build.
if [ -n "${UA_VERSION:-}" ]; then
    VERSION=$UA_VERSION
else
    VERSION=$(tr -d '[:space:]' < VERSION)
    [[ $VERSION =~ ^v[0-9]{1,9}\.[0-9]{1,9}(\.[0-9]{1,9})?$ ]] ||
        { echo "ERROR: VERSION must hold a version such as v1.0 or v1.0.1, not '$VERSION'" >&2; exit 1; }
fi
if [[ $VERSION =~ ^v?([0-9]+)\.([0-9]+)(\.([0-9]+))? ]]; then
    NUM="${BASH_REMATCH[1]},${BASH_REMATCH[2]},${BASH_REMATCH[4]:-0},0"
else
    NUM="0,0,0,0"
fi

rm -rf build
mkdir -p build
cat > build/version.h <<EOF
#define UA_VERSION "$VERSION"
#define UA_VERSION_NUM $NUM
EOF

# Only the zip reader and inflate of miniz are needed.
CFLAGS="-std=gnu99 -Os -ffunction-sections -fdata-sections -DMINIZ_NO_DEFLATE_APIS
        -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_TIME"

"$WINDRES" -I build -O coff updateAll.rc -o build/updateAll.res
"$CC" $CFLAGS -w -c third_party/miniz/miniz.c -o build/miniz.o
"$CC" $CFLAGS -w -c third_party/cJSON/cJSON.c -o build/cJSON.o
for src in ua_core.c ua_win32.c ua_gui.c; do
    "$CC" $CFLAGS -Wall -Wextra -municode -include build/version.h -c "$src" -o "build/${src%.c}.o"
done
"$CC" -municode -mwindows -static -s -Wl,--gc-sections -o build/updateAll.exe \
    build/ua_core.o build/ua_win32.o build/ua_gui.o build/miniz.o build/cJSON.o build/updateAll.res \
    -lwinhttp -lcomctl32 -luxtheme -lole32 -lshell32 -luuid

echo "built $(pwd)/build/updateAll.exe ($VERSION, $(stat -c%s build/updateAll.exe) bytes)"
