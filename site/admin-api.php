<?php
declare(strict_types=1);
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
header('Content-Type: application/json; charset=utf-8');

const COOKIE = 'zis_admin';
const SESSION_SEC = 28800;
const MAX_ITEMS = 20;
const MAX_STR = 2000;

function out(int $code, array $obj, ?string $cookie = null, bool $clear = false): void {
    http_response_code($code);
    if ($cookie !== null) {
        header('Set-Cookie: ' . COOKIE . '=' . $cookie . '; HttpOnly; Path=/; SameSite=Strict; Max-Age=' . SESSION_SEC, false);
    }
    if ($clear) {
        header('Set-Cookie: ' . COOKIE . '=; HttpOnly; Path=/; SameSite=Strict; Max-Age=0', false);
    }
    echo json_encode($obj, JSON_UNESCAPED_UNICODE);
    exit;
}

function clean($s, int $n): string {
    $t = str_replace(["\0", "\r"], '', (string)$s);
    $t = preg_replace('/[^\P{C}\n]+/u', '', $t) ?? '';
    if (strlen($t) > $n) $t = substr($t, 0, $n);
    return trim($t);
}

function cfg(): array {
    $cands = [
        getenv('ZIS_PRIVATE') ? (rtrim((string)getenv('ZIS_PRIVATE'), '/\\') . '/admin.json') : '',
        '/var/lib/zis-admin/admin.json',
        dirname(__DIR__) . '/private/admin.json',
        __DIR__ . '/.admin.json',
    ];
    foreach ($cands as $p) {
        if ($p !== '' && is_file($p)) {
            $j = json_decode((string)file_get_contents($p), true);
            if (is_array($j)) return $j;
        }
    }
    out(500, ['ok' => false, 'error' => 'write_failed']);
}

function body(): array {
    $raw = file_get_contents('php://input') ?: '{}';
    if (strlen($raw) > 48234496) out(400, ['ok' => false, 'error' => 'bad_json']);
    $j = json_decode($raw, true);
    return is_array($j) ? $j : [];
}

function verify(string $login, string $password): bool {
    $c = cfg();
    $wantLogin = (string)($c['login'] ?? '');
    $salt = @hex2bin((string)($c['salt_hex'] ?? ''));
    $want = @hex2bin((string)($c['hash_hex'] ?? ''));
    $iters = (int)($c['iters'] ?? 210000);
    if (($c['kdf'] ?? '') !== 'pbkdf2_sha256' || $wantLogin === '' || $salt === false || $want === false) return false;
    if (!hash_equals($wantLogin, $login)) return false;
    $got = hash_pbkdf2('sha256', $password, $salt, $iters, 32, true);
    return hash_equals($want, $got);
}

function hmac_key(): string {
    $c = cfg();
    return (string)($c['hash_hex'] ?? '') . (string)($c['salt_hex'] ?? '');
}

function mint(string $login): string {
    $payload = rtrim(strtr(base64_encode(json_encode(['u' => $login, 'e' => time() + SESSION_SEC])), '+/', '-_'), '=');
    return $payload . '.' . hash_hmac('sha256', $payload, hmac_key());
}

function session_user(): ?string {
    $raw = $_COOKIE[COOKIE] ?? '';
    $parts = explode('.', $raw, 2);
    if (count($parts) !== 2) return null;
    [$payload, $sig] = $parts;
    if (!hash_equals(hash_hmac('sha256', $payload, hmac_key()), $sig)) return null;
    $pad = strlen($payload) % 4;
    if ($pad) $payload .= str_repeat('=', 4 - $pad);
    $j = json_decode(base64_decode(strtr($payload, '-_', '+/')), true);
    if (!is_array($j) || empty($j['u']) || (int)($j['e'] ?? 0) < time()) return null;
    return (string)$j['u'];
}

function rate_ok(): bool {
    $ip = $_SERVER['HTTP_X_REAL_IP'] ?? $_SERVER['REMOTE_ADDR'] ?? '0';
    $f = sys_get_temp_dir() . '/zis-admin-' . hash('sha256', $ip);
    $hits = [];
    if (is_file($f)) {
        $hits = [];
        foreach (explode("\n", (string)file_get_contents($f)) as $line) {
            $t = (int)$line;
            if ($t > time() - 900) $hits[] = $t;
        }
    }
    if (count($hits) >= 8) return false;
    return true;
}

function rate_hit(): void {
    $ip = $_SERVER['HTTP_X_REAL_IP'] ?? $_SERVER['REMOTE_ADDR'] ?? '0';
    $f = sys_get_temp_dir() . '/zis-admin-' . hash('sha256', $ip);
    $hits = [];
    if (is_file($f)) {
        $hits = [];
        foreach (explode("\n", (string)file_get_contents($f)) as $line) {
            $t = (int)$line;
            if ($t > time() - 900) $hits[] = $t;
        }
    }
    $hits[] = time();
    @file_put_contents($f, implode("\n", $hits));
}

function as_list($raw): array {
    if ($raw === null || $raw === false) return [];
    if (is_string($raw)) return $raw === '' ? [] : [$raw];
    if (!is_array($raw)) return [];
    if ($raw === []) return [];
    if (array_keys($raw) !== range(0, count($raw) - 1)) return [$raw];
    return $raw;
}

function safe_image($s): string {
    $t = clean($s, 240);
    if (strpos($t, 'img/') === 0) $t = '/' . $t;
    if (preg_match('#^/img/[A-Za-z0-9._-]+\.(png|jpe?g|webp|gif)$#i', $t)) return $t;
    if (!preg_match('#^/news-media/[A-Za-z0-9._-]+$#', $t)) return '';
    return $t;
}

function hero_kind(string $path): string {
    $low = strtolower($path);
    if (substr($low, -4) === '.mp4' || substr($low, -5) === '.webm') return 'video';
    return 'image';
}

function sanitize_cards($raw, int $n = 12): array {
    $cards = [];
    foreach (as_list($raw) as $c) {
        if (count($cards) >= $n || !is_array($c)) continue;
        $title = clean($c['title'] ?? '', 120);
        $text = clean($c['text'] ?? '', 800);
        if ($title !== '' || $text !== '') $cards[] = ['title' => $title, 'text' => $text];
    }
    return $cards;
}

function sanitize_codex($raw): array {
    if (!is_array($raw)) $raw = [];
    $rules = [];
    foreach (as_list($raw['rules'] ?? []) as $x) {
        if (count($rules) >= 20) break;
        $s = clean($x, 400);
        if ($s !== '') $rules[] = $s;
    }
    return [
        'eyebrow' => clean($raw['eyebrow'] ?? '', 160),
        'title' => clean($raw['title'] ?? '', 400),
        'lede' => clean($raw['lede'] ?? '', MAX_STR),
        'rules' => $rules,
    ];
}

function sanitize_table($raw): ?array {
    if (!is_array($raw)) return null;
    $headers = [];
    foreach (as_list($raw['headers'] ?? []) as $h) {
        if (count($headers) >= 6) break;
        $s = clean($h, 80);
        if ($s !== '') $headers[] = $s;
    }
    while (count($headers) < 2) $headers[] = '';
    $headers = array_slice($headers, 0, 2);
    $rows = [];
    foreach (as_list($raw['rows'] ?? []) as $r) {
        if (count($rows) >= 24) break;
        if (is_array($r) && array_keys($r) !== range(0, count($r) - 1)) {
            $cells = [$r['0'] ?? $r['a'] ?? '', $r['1'] ?? $r['b'] ?? ''];
        } elseif (is_array($r)) {
            $cells = $r;
        } else {
            continue;
        }
        $a = clean($cells[0] ?? '', 160);
        $b = clean($cells[1] ?? '', 500);
        if ($a !== '' || $b !== '') $rows[] = [$a, $b];
    }
    $title = clean($raw['title'] ?? '', 160);
    if (!$rows && $title === '') return null;
    return ['title' => $title, 'headers' => $headers, 'rows' => $rows];
}

function sanitize_wiki($raw): array {
    $src = [];
    if (is_array($raw) && isset($raw['id']) && is_string($raw['id'])) $src = [$raw];
    elseif (is_array($raw) && isset($raw['pages']) && is_array($raw['pages'])) $src = as_list($raw['pages']);
    else $src = as_list($raw);
    $allowed = ['overview' => true, 'join' => true, 'mods' => true];
    $byId = [];
    foreach ($src as $p) {
        if (!is_array($p)) continue;
        $pid = strtolower(clean($p['id'] ?? '', 32));
        if (!isset($allowed[$pid])) continue;
        $lst = [];
        foreach (as_list($p['list'] ?? []) as $x) {
            if (count($lst) >= 20) break;
            $s = clean($x, 400);
            if ($s !== '') $lst[] = $s;
        }
        $tables = [];
        foreach (as_list($p['tables'] ?? []) as $t) {
            if (count($tables) >= 4) break;
            $st = sanitize_table($t);
            if ($st !== null) $tables[] = $st;
        }
        $page = [
            'id' => $pid,
            'nav' => clean($p['nav'] ?? '', 40),
            'eyebrow' => clean($p['eyebrow'] ?? '', 160),
            'title' => clean($p['title'] ?? '', 400),
            'lede' => clean($p['lede'] ?? '', MAX_STR),
            'quote' => clean($p['quote'] ?? '', 400),
            'cards' => sanitize_cards($p['cards'] ?? [], 12),
            'list' => $lst,
            'tables' => $tables,
        ];
        if ($page['nav'] !== '' || $page['eyebrow'] !== '' || $page['title'] !== '' || $page['lede'] !== '' || $page['quote'] !== '' || $page['cards'] || $page['list'] || $page['tables']) {
            $byId[$pid] = $page;
        }
    }
    $out = [];
    foreach (['overview', 'join', 'mods'] as $id) {
        if (isset($byId[$id])) $out[] = $byId[$id];
    }
    return $out;
}

function sanitize_news($raw): array {
    $itemsIn = [];
    if (is_array($raw) && isset($raw['items']) && is_array($raw['items'])) $itemsIn = $raw['items'];
    elseif (is_array($raw) && $raw !== [] && array_keys($raw) === range(0, count($raw) - 1)) $itemsIn = $raw;
    $items = [];
    foreach ($itemsIn as $it) {
        if (count($items) >= MAX_ITEMS || !is_array($it)) continue;
        $date = clean($it['date'] ?? '', 80);
        $title = clean($it['title'] ?? '', 200);
        $text = clean($it['text'] ?? '', MAX_STR);
        $image = safe_image($it['image'] ?? '');
        if ($title === '' && $text === '' && $image === '') continue;
        $row = ['date' => $date, 'title' => $title, 'text' => $text];
        if ($image !== '') $row['image'] = $image;
        $items[] = $row;
    }
    return ['feed' => 'dynasty', 'updated' => gmdate('Y-m-d'), 'items' => $items];
}

function int_range($v, int $lo, int $hi, int $def): int {
    if (!is_numeric($v)) return $def;
    $n = (int)$v;
    if ($n < $lo) return $lo;
    if ($n > $hi) return $hi;
    return $n;
}

function sanitize_content($raw): array {
    if (!is_array($raw)) $raw = [];
    $ticker = [];
    foreach (as_list($raw['ticker'] ?? []) as $x) {
        if (count($ticker) >= 12) break;
        $s = clean($x, 80);
        if ($s !== '') $ticker[] = $s;
    }
    if (!$ticker) $ticker = ['DYNASTY OF ROT'];
    $media = safe_image($raw['hero_media'] ?? $raw['hero_image'] ?? '');
    return [
        'ticker' => $ticker,
        'hero_eyebrow' => clean($raw['hero_eyebrow'] ?? '', 160),
        'hero_title' => clean($raw['hero_title'] ?? '', 400),
        'hero_media' => $media,
        'hero_kind' => $media !== '' ? hero_kind($media) : 'image',
        'hero_caption' => clean($raw['hero_caption'] ?? '', 400),
        'hero_width' => int_range($raw['hero_width'] ?? 36, 20, 70, 36),
        'hero_height' => int_range($raw['hero_height'] ?? 560, 200, 720, 560),
        'lede' => clean($raw['lede'] ?? '', MAX_STR),
        'cards' => sanitize_cards($raw['cards'] ?? [], 8),
        'codex' => sanitize_codex($raw['codex'] ?? null),
        'wiki' => sanitize_wiki($raw['wiki'] ?? null),
    ];
}

function sniff(string $raw): string {
    if (substr($raw, 0, 6) === 'GIF87a' || substr($raw, 0, 6) === 'GIF89a') return '.gif';
    if (substr($raw, 0, 8) === "\x89PNG\r\n\x1a\n") return '.png';
    if (substr($raw, 0, 3) === "\xff\xd8\xff") return '.jpg';
    if (strlen($raw) >= 12 && substr($raw, 0, 4) === 'RIFF' && substr($raw, 8, 4) === 'WEBP') return '.webp';
    if (substr($raw, 0, 4) === "\x1a\x45\xdf\xa3") return '.webm';
    $head = substr($raw, 0, 512);
    if (strpos($head, 'ftyp') !== false) return '.mp4';
    if (strlen($raw) >= 8) {
        $box = substr($raw, 4, 4);
        if ($box === 'mdat' || $box === 'moov') return '.mp4';
    }
    return '';
}

function need_user(): string {
    $u = session_user();
    if ($u === null) out(401, ['ok' => false, 'error' => 'need_auth']);
    return $u;
}

$r = clean($_GET['r'] ?? '', 32);
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

if ($method === 'GET' && $r === 'me') {
    $u = session_user();
    if ($u === null) out(401, ['ok' => false, 'error' => 'need_auth']);
    out(200, ['ok' => true, 'login' => $u]);
}
if ($method === 'GET' && $r === 'content') {
    need_user();
    $news = json_decode((string)@file_get_contents(__DIR__ . '/news.json'), true) ?: ['items' => []];
    $content = json_decode((string)@file_get_contents(__DIR__ . '/content.json'), true) ?: [];
    out(200, ['ok' => true, 'news' => $news, 'content' => $content]);
}
if ($method === 'POST' && $r === 'login') {
    if (!rate_ok()) out(429, ['ok' => false, 'error' => 'rate_limited']);
    $d = body();
    $login = clean($d['login'] ?? '', 64);
    $password = (string)($d['password'] ?? '');
    if (strlen($password) > 128) $password = substr($password, 0, 128);
    if (!verify($login, $password)) {
        rate_hit();
        usleep(250000);
        out(401, ['ok' => false, 'error' => 'bad_login']);
    }
    out(200, ['ok' => true], mint($login));
}
if ($method !== 'POST') out(405, ['ok' => false, 'error' => 'method_denied']);
need_user();
if ($r === 'logout') out(200, ['ok' => true], null, true);
if ($r === 'upload') {
    $ctype = strtolower(trim(explode(';', (string)($_SERVER['CONTENT_TYPE'] ?? ''))[0]));
    $raw = '';
    if ($ctype === 'application/octet-stream' || $ctype === 'video/mp4' || $ctype === 'video/webm' || $ctype === 'application/mp4') {
        $n = (int)($_SERVER['CONTENT_LENGTH'] ?? 0);
        if ($n < 8 || $n > 32 * 1024 * 1024) out(400, ['ok' => false, 'error' => $n > 32 * 1024 * 1024 ? 'too_large' : 'bad_type']);
        $raw = (string)file_get_contents('php://input');
    } else {
        $d = body();
        $b64 = (string)($d['data'] ?? '');
        if (strpos($b64, ',') !== false) $b64 = explode(',', $b64, 2)[1];
        $decoded = base64_decode($b64, true);
        $raw = ($decoded === false) ? '' : $decoded;
    }
    if ($raw === '' || strlen($raw) > 32 * 1024 * 1024) out(400, ['ok' => false, 'error' => strlen($raw) > 32 * 1024 * 1024 ? 'too_large' : 'bad_type']);
    $ext = sniff($raw);
    if ($ext === '') out(400, ['ok' => false, 'error' => 'bad_type']);
    $name = substr(hash('sha256', $raw), 0, 16) . $ext;
    $dir = __DIR__ . '/news-media';
    if (!is_dir($dir) && !mkdir($dir, 0755, true)) out(500, ['ok' => false, 'error' => 'write_failed']);
    if (file_put_contents($dir . '/' . $name, $raw) === false) out(500, ['ok' => false, 'error' => 'write_failed']);
    out(200, ['ok' => true, 'image' => '/news-media/' . $name, 'media' => '/news-media/' . $name]);
}
if ($r === 'save') {
    $d = body();
    $news = sanitize_news($d['news'] ?? null);
    $content = sanitize_content($d['content'] ?? null);
    $n = json_encode($news, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT) . "\n";
    $c = json_encode($content, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT) . "\n";
    if (file_put_contents(__DIR__ . '/news.json', $n) === false || file_put_contents(__DIR__ . '/content.json', $c) === false) {
        out(500, ['ok' => false, 'error' => 'write_failed']);
    }
    out(200, ['ok' => true, 'news' => $news, 'content' => $content]);
}
out(404, ['ok' => false, 'error' => 'not_found']);
