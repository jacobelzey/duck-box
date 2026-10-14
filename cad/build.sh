#!/usr/bin/env bash
# Rebuild every print file from The Duck Spot's original 3MF.
#
#   cad/build.sh path/to/the-downloaded-model.3mf [output dir]
#
# Output goes to build/ by default:
#   build/stl/       the print files, already oriented for the bed
#   build/boxframe/  every part in its assembled position (used by the checks)
#   build/meta.json  dimensions and notes the README refers to
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <original .3mf> [output dir]" >&2
  exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC_3MF="$1"
OUT="${2:-build}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/model" "$WORK/stage1" "$OUT"
unzip -q "$SRC_3MF" -d "$WORK/model"

# Make sure this is the same version of the model the geometry was built
# against. A newer upload from the designer may have moved things.
# sha256sum on Linux, shasum on macOS
if command -v sha256sum >/dev/null; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
if ! (cd "$WORK/model" && $SHA -c --quiet "$HERE/original_model.sha256"); then
  echo
  echo "This 3MF is not the version the scripts were written against."
  echo "The build may still work, but check the results before printing."
  echo "Set DUCKBOX_IGNORE_HASH=1 to carry on anyway."
  [ "${DUCKBOX_IGNORE_HASH:-0}" = "1" ] || exit 1
fi

python3 "$HERE/build_led_parts.py" "$WORK/model" "$WORK/stage1" > "$WORK/stage1.log"
python3 "$HERE/build_print_parts.py" "$WORK/model" "$WORK/stage1" "$OUT" | tee "$OUT.log" | grep -E "CHECKS" || true

echo
echo "== checks"
python3 "$HERE/verify_exports.py" "$OUT" | tail -n 1
python3 "$HERE/verify_assembly.py" "$OUT" | tail -n 1
echo
echo "Print files are in $OUT/stl"
