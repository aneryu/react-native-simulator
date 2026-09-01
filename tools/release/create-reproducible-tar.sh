#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: create-reproducible-tar.sh PARENT DIRECTORY OUTPUT" >&2
  exit 2
fi

parent=$1
directory=$2
output=$3
project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
source_date_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$project_root" show -s --format=%ct HEAD)}
timestamp=$(date -u -r "$source_date_epoch" '+%Y%m%d%H%M.%S')

if [ ! -d "$parent/$directory" ]; then
  echo "Archive input does not exist: $parent/$directory" >&2
  exit 1
fi

# Normalize mtimes and archive ownership. gzip -n removes its own filename and
# timestamp header so the same clean tag produces byte-identical assets.
find "$parent/$directory" -exec touch -h -t "$timestamp" {} +
COPYFILE_DISABLE=1 tar -cf - --format ustar --uid 0 --gid 0 --numeric-owner \
  -C "$parent" "$directory" | gzip -n -9 >"$output"
