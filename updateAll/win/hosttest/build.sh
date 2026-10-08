#!/usr/bin/env bash
# Builds ua_host, the updateAll core on Linux with ASan and UBSan, for compare.sh.
set -euo pipefail
cd "$(dirname "$0")"

MINIZ_DEFS="-DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_TIME"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"

mkdir -p build
for src in ../third_party/miniz/miniz.c ../third_party/cJSON/cJSON.c; do
    gcc -std=gnu99 -g -O1 $SAN $MINIZ_DEFS -w -c "$src" -o "build/$(basename "${src%.c}").o"
done
# -Wno-format-truncation: UBSan's null checks make GCC see a NULL format in vfmt().
gcc -std=gnu99 -g -O1 $SAN $MINIZ_DEFS -Wall -Wextra -Werror -Wno-format-truncation \
    ../ua_core.c ua_posix.c ua_host.c build/miniz.o build/cJSON.o -o build/ua_host
echo "built $(pwd)/build/ua_host"
