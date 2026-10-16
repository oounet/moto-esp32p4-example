#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
library=${MOTO_SDK_HOST_LIBRARY:?Set MOTO_SDK_HOST_LIBRARY to a matching host-platform libmoto_sdk.a}
include=${MOTO_SDK_HOST_INCLUDE:-"$root/vendor/moto-sdk/include"}
[ -f "$library" ] && [ -f "$include/moto/vector_map.h" ] || { echo "Matching host SDK library and public headers are required" >&2; exit 1; }
folder=$(mktemp -d /tmp/moto-vector-tests.XXXXXX)
trap 'rm -rf "$folder"' EXIT
for name in store view; do
  extra=""
  if [ "$name" = store ]; then extra="$root/main/route_map_compression.c"; fi
  clang -DMOTO_VECTOR_STORE_HOST_TEST -std=c99 -Wall -Wextra -Werror -Wno-unused-variable -fsanitize=address,undefined \
    -I "$root/tests" -I "$root/main" -I "$include" \
    "$root/tests/test_route_vector_$name.c" "$root/main/route_vector_$name.c" $extra \
    "$library" -lm -lz -o "$folder/$name"
  "$folder/$name"
done
