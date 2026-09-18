#!/bin/sh
# Следит за DynastyLauncher.exe (.sha256) и за папкой cloud (index.json).
# Лаунчер читает index.json, не «что лежит в каталоге».
#
# Из /download/:     sh watch-launcher-sha.sh
# Один проход:       sh watch-launcher-sha.sh --once
#
# ZIS_LAUNCHER_EXE   exe (по умолчанию рядом со скриптом)
# ZIS_CLOUD_DIR      cloud (по умолчанию ./cloud рядом со скриптом)
# ZIS_SHA_INTERVAL   секунды (по умолчанию 60)
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXE="${ZIS_LAUNCHER_EXE:-$SCRIPT_DIR/DynastyLauncher.exe}"
SHA="${ZIS_LAUNCHER_SHA:-${EXE}.sha256}"
CLOUD="${ZIS_CLOUD_DIR:-$SCRIPT_DIR/cloud}"
INTERVAL="${ZIS_SHA_INTERVAL:-60}"
export CLOUD

hash_exe() {
  if [ ! -f "$EXE" ]; then
    echo "нет файла $EXE" >&2
    return 1
  fi
  sha256sum "$EXE" | awk '{print tolower($1)}'
}

is_sha256() {
  echo "$1" | grep -Eq '^[0-9a-f]{64}$'
}

write_sha() {
  got="$1"
  tmp="${SHA}.tmp"
  printf '%s' "$got" > "$tmp"
  mv -f "$tmp" "$SHA"
  chmod 644 "$SHA" 2>/dev/null || true
}

sync_launcher() {
  got=$(hash_exe) || return 1
  if ! is_sha256 "$got"; then
    echo "плохой хеш лаунчера" >&2
    return 1
  fi
  have=""
  if [ -f "$SHA" ]; then
    have=$(tr -d ' \t\r\n' < "$SHA" | tr 'A-F' 'a-f')
  fi
  if [ "$got" = "$have" ]; then
    return 0
  fi
  write_sha "$got"
  echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) обновлён $SHA"
}

sync_cloud() {
  if [ ! -d "$CLOUD" ]; then
    echo "нет папки cloud: $CLOUD" >&2
    return 1
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "нужен python3 для индекса cloud" >&2
    return 1
  fi
  python3 - <<'PY'
import hashlib, json, os, sys, time
from pathlib import Path

root = Path(os.environ["CLOUD"]).resolve()
base = os.environ.get("ZIS_PACK_BASE", "http://zis.inflexus.world/download/cloud/")
stamp_path = root / ".index.stamp"

def listed_files():
    out = []
    mods = root / "mods"
    if mods.is_dir():
        out.extend(sorted(p for p in mods.glob("*.jar")))
    for name in ("options.txt", "servers.dat"):
        p = root / name
        if p.is_file():
            out.append(p)
    arch = root / "archives"
    if arch.is_dir():
        out.extend(sorted(arch.glob("*.zip")))
    jdir = root / "java"
    if jdir.is_dir():
        out.extend(sorted(jdir.glob("*windows*.zip")))
    return out

def fingerprint(paths):
    lines = []
    for p in paths:
        st = p.stat()
        rel = p.relative_to(root).as_posix()
        mtime = getattr(st, "st_mtime_ns", int(st.st_mtime * 1000000000))
        lines.append("%s %s %s" % (rel, st.st_size, mtime))
    return "\n".join(lines) + ("\n" if lines else "")

def sha_size(p: Path):
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    print("HASH", p.relative_to(root).as_posix(), flush=True)
    return h.hexdigest(), p.stat().st_size

paths = listed_files()
fp = fingerprint(paths)
old = stamp_path.read_text(encoding="utf-8") if stamp_path.is_file() else None
if old == fp and (root / "index.json").is_file():
    sys.exit(0)

items = []
java = None
mods = root / "mods"
if mods.is_dir():
    for p in sorted(mods.glob("*.jar")):
        digest, size = sha_size(p)
        items.append({"kind": "file", "path": "mods/" + p.name, "sha256": digest, "size": size})
for name in ("options.txt", "servers.dat"):
    p = root / name
    if p.is_file():
        digest, size = sha_size(p)
        items.append({"kind": "file", "path": name, "sha256": digest, "size": size})
arch = root / "archives"
if arch.is_dir():
    for p in sorted(arch.glob("*.zip")):
        digest, size = sha_size(p)
        items.append({"kind": "archive", "path": "archives/" + p.name, "sha256": digest, "size": size})
jdir = root / "java"
if jdir.is_dir():
    zips = sorted(jdir.glob("*windows*.zip"))
    if zips:
        p = zips[0]
        digest, size = sha_size(p)
        rel = "java/" + p.name
        items.append({"kind": "java", "path": rel, "sha256": digest, "size": size})
        java = {"windows": {"sha256": digest, "size": size, "file": rel}}

if java is None:
    print("нет java/*windows*.zip — индекс не пишу", file=sys.stderr)
    sys.exit(1)

files = {}
archives = {}
for it in items:
    entry = {"sha256": it["sha256"], "size": it["size"]}
    if it["kind"] == "file":
        files[it["path"]] = entry
    elif it["kind"] == "archive":
        archives[it["path"]] = dict(entry, extract_to="")

doc = {
    "schema": 1,
    "generated": int(time.time()),
    "base_url": base,
    "managed_dirs": ["mods"],
    "files": files,
    "archives": archives,
    "java": java,
    "items": items,
}
out = root / "index.json"
tmp = root / "index.json.tmp"
tmp.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
tmp.replace(out)
stamp_path.write_text(fp, encoding="utf-8")
print("WROTE", out, "items=", len(items), "bytes=", out.stat().st_size)
PY
}

sync_once() {
  rc=0
  sync_launcher || rc=1
  sync_cloud || rc=1
  return "$rc"
}

if [ "${1:-}" = "--once" ]; then
  sync_once
  exit $?
fi

echo "слежение exe=$EXE"
echo "слежение cloud=$CLOUD  каждые ${INTERVAL}с  (Ctrl+C стоп)"
while :; do
  sync_once || true
  sleep "$INTERVAL"
done
