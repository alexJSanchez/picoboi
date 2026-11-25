#!/bin/sh
# Downloads free test ROMs into tests/roms/ (untracked, see .gitignore).
# These are NOT committed. Each file is verified against a pinned SHA-256.
set -eu

dir="$(cd "$(dirname "$0")/.." && pwd)/tests/roms"
mkdir -p "$dir"

fetch() {  # name url sha256
  name="$1"; url="$2"; want="$3"
  out="$dir/$name"
  if [ -f "$out" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" = "$want" ]; then
    echo "ok (cached)  $name"
    return
  fi
  curl -sSL -o "$out.tmp" "$url"
  got="$(sha256sum "$out.tmp" | cut -d' ' -f1)"
  if [ "$got" != "$want" ]; then
    rm -f "$out.tmp"
    echo "CHECKSUM MISMATCH for $name: expected $want, got $got" >&2
    exit 1
  fi
  mv "$out.tmp" "$out"
  echo "ok           $name"
}

# Blargg's cpu_instrs (combined ROM), from the retrio/gb-test-roms mirror.
fetch cpu_instrs.gb \
  "https://raw.githubusercontent.com/retrio/gb-test-roms/master/cpu_instrs/cpu_instrs.gb" \
  8c5e12f41e0ba5bbca796944f92ffe6de28809198682c4332e38d1b3cf56fcf2

# dmg-acid2 v1.0 by Matt Currie (MIT).
fetch dmg-acid2.gb \
  "https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb" \
  464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1
