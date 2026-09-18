#!/usr/bin/env bash
# One-shot Ubuntu bring-up: admin API on 127.0.0.1:8744 + nginx proxy.
# Usage: sudo ./start-zis.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SITE_ROOT="${ZIS_SITE_ROOT:-/var/www/zis.inflexus.world}"
PRIV_DIR="${ZIS_PRIVATE:-/var/lib/zis-admin}"
BIND="${ZIS_BIND:-127.0.0.1}"
PORT="${ZIS_PORT:-8744}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Запусти так: sudo $0" >&2
  exit 1
fi
if [[ ! -f "$ROOT/private/admin_api.py" ]]; then
  echo "Нет $ROOT/private/admin_api.py — скрипт должен лежать в корне репозитория zis." >&2
  exit 1
fi
if [[ ! -f "$ROOT/private/admin.json" ]]; then
  echo "Нет $ROOT/private/admin.json. Не запускай python --init на сервере." >&2
  exit 1
fi
if [[ "$BIND" != "127.0.0.1" && "$BIND" != "localhost" && "$BIND" != "::1" ]]; then
  echo "API только на loopback, не $BIND" >&2
  exit 2
fi

command -v python3 >/dev/null || { apt-get update -y && apt-get install -y python3; }
command -v nginx >/dev/null || { echo "nginx не установлен — сначала подними сайт." >&2; exit 1; }

install -d -m 0750 -o www-data -g www-data "$PRIV_DIR"
install -m 0755 -o www-data -g www-data "$ROOT/private/admin_api.py" "$PRIV_DIR/admin_api.py"
install -m 0600 -o www-data -g www-data "$ROOT/private/admin.json" "$PRIV_DIR/admin.json"
if [[ -f "$ROOT/private/zis-admin.service" ]]; then
  install -m 0644 "$ROOT/private/zis-admin.service" /etc/systemd/system/zis-admin.service
fi

if [[ -d "$SITE_ROOT" ]]; then
  html=""
  if [[ -f "$ROOT/site/admin.html" ]]; then html="$ROOT/site/admin.html"; fi
  if [[ -f "$ROOT/admin.html" ]]; then html="$ROOT/admin.html"; fi
  if [[ -n "$html" ]]; then
    install -m 0644 "$html" "$SITE_ROOT/admin.html"
  fi
  touch "$SITE_ROOT/news.json" "$SITE_ROOT/content.json"
  install -d -m 0755 -o www-data -g www-data "$SITE_ROOT/news-media"
  chown www-data:www-data "$SITE_ROOT/news.json" "$SITE_ROOT/content.json"
fi

install -d /etc/nginx/snippets
cat >/etc/nginx/snippets/zis-admin-api.conf <<'EOF'
client_max_body_size 48m;
location ~ /\. {
    deny all;
    return 404;
}
location = /content.json {
    add_header Cache-Control "no-store";
    default_type application/json;
}
location /news-media/ {
    gzip off;
    types {
        video/mp4 mp4;
        video/webm webm;
        image/png png;
        image/jpeg jpg jpeg;
        image/gif gif;
        image/webp webp;
    }
    default_type application/octet-stream;
}
location /admin/api/ {
    client_max_body_size 48m;
    proxy_read_timeout 300s;
    proxy_send_timeout 300s;
    proxy_pass http://127.0.0.1:8744;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header Cookie $http_cookie;
    proxy_hide_header X-Powered-By;
    proxy_pass_header Set-Cookie;
}
EOF

python3 - "$SITE_ROOT" <<'PY'
import pathlib, sys
site_root = sys.argv[1]
needle = "include /etc/nginx/snippets/zis-admin-api.conf;"
roots = [pathlib.Path("/etc/nginx/sites-enabled"), pathlib.Path("/etc/nginx/sites-available")]
files = []
for d in roots:
    if d.is_dir():
        files.extend(sorted(p for p in d.iterdir() if p.is_file() and not p.name.startswith(".")))
target = None
for p in files:
    t = p.read_text(encoding="utf-8", errors="replace")
    if "zis.inflexus.world" in t or site_root in t:
        target = p
        break
if target is None:
    print("nginx: site zis.inflexus.world not found, leave nginx as-is", file=sys.stderr)
    sys.exit(0)
text = target.read_text(encoding="utf-8", errors="replace")
if needle in text:
    print("nginx: snippet include already present, leave vhost as-is")
    sys.exit(0)
if "location /admin/api/" in text:
    if "client_max_body_size 48m" in text:
        print("nginx: /admin/api/ already present, leave file as-is")
        sys.exit(0)
    import re
    new, n = re.subn(
        r"(location /admin/api/\s*\{)",
        r"\1\n        client_max_body_size 48m;",
        text,
        count=1,
    )
    if n:
        bak = target.with_suffix(target.suffix + ".bak-zis-admin")
        if not bak.exists():
            bak.write_text(text, encoding="utf-8")
        target.write_text(new, encoding="utf-8")
        print("nginx: added client_max_body_size 48m to", target)
    else:
        print("nginx: /admin/api/ already present, leave file as-is")
    sys.exit(0)
lines = text.splitlines(True)
out = []
done = False
for line in lines:
    out.append(line)
    if done:
        continue
    if "server_name" in line and "zis.inflexus.world" in line:
        indent = line[: len(line) - len(line.lstrip())]
        out.append(f"{indent}{needle}\n")
        done = True
        continue
    if (not done) and "root" in line and site_root in line:
        indent = line[: len(line) - len(line.lstrip())]
        out.append(f"{indent}{needle}\n")
        done = True
if not done:
    print("nginx: no insert point in", target, file=sys.stderr)
    sys.exit(0)
bak = target.with_suffix(target.suffix + ".bak-zis-admin")
if not bak.exists():
    bak.write_text(text, encoding="utf-8")
target.write_text("".join(out), encoding="utf-8")
print("nginx: inserted include into", target, "(backup", bak, ")")
PY

nginx -t
systemctl daemon-reload
systemctl enable zis-admin >/dev/null
systemctl restart zis-admin
systemctl reload nginx

sleep 1
code="$(curl -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:${PORT}/admin/api/me" || true)"
if [[ "$code" != "401" && "$code" != "200" ]]; then
  echo "API не ответил (HTTP $code). journalctl -u zis-admin -n 50" >&2
  systemctl status zis-admin --no-pager || true
  exit 1
fi
echo "Готово. API http://127.0.0.1:${PORT}/admin/api/  (проверка: HTTP $code)"
echo "Логин: открой /admin на сайте и войди. Nginx проксирует /admin/api/."
