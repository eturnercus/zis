function esc(s) {
  return String(s ?? "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function rich(s) {
  return esc(s)
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\b([A-Za-z0-9][A-Za-z0-9._-]*\.(?:html|md))\b/g, '<a href="$1">$1</a>');
}

function isSafeMedia(src) {
  const t = String(src || "").replace(/^\.\//, "");
  return /^(?:\/)?img\/[A-Za-z0-9._-]+\.(png|jpe?g|webp|gif)$/i.test(t) || /^(?:\/)?news-media\/[A-Za-z0-9._-]+$/.test(t);
}

function mediaSrc(src) {
  const t = String(src || "").replace(/^\.\//, "");
  if (!t) return "";
  return t.charAt(0) === "/" ? t : "/" + t;
}

function wrapTables(root) {
  (root || document).querySelectorAll("table.table").forEach((t) => {
    if (t.parentElement && t.parentElement.classList.contains("table-wrap")) return;
    const wrap = document.createElement("div");
    wrap.className = "table-wrap";
    t.parentNode.insertBefore(wrap, t);
    wrap.appendChild(t);
  });
}

if (!document.querySelector('link[rel="icon"]')) {
  const icon = document.createElement("link");
  icon.rel = "icon";
  icon.href = "img/crest.png";
  icon.type = "image/png";
  document.head.appendChild(icon);
}
if (!document.querySelector('meta[name="theme-color"]')) {
  const theme = document.createElement("meta");
  theme.name = "theme-color";
  theme.content = "#07040c";
  document.head.appendChild(theme);
}

const bar = document.querySelector(".bar");
if (bar) {
  let btn = bar.querySelector(".menu");
  if (!btn) {
    btn = document.createElement("button");
    btn.className = "menu";
    btn.type = "button";
    btn.setAttribute("aria-label", "Меню");
    btn.innerHTML = "<span></span><span></span><span></span>";
    bar.insertBefore(btn, bar.querySelector(".dl") || null);
  }
  btn.addEventListener("click", () => bar.classList.toggle("open"));
}

const here = (location.pathname.split("/").pop() || "index.html").toLowerCase();
document.querySelectorAll(".bar nav a").forEach((a) => {
  const href = (a.getAttribute("href") || "").toLowerCase();
  if (href === here) a.classList.add("active");
  if (here.startsWith("wiki") && href === "wiki.html") a.classList.add("active");
});

wrapTables();

function setTicker(phrases) {
  const tracks = document.querySelectorAll(".ticker-track");
  if (!tracks.length || !phrases || !phrases.length) return;
  const bits = [];
  for (let r = 0; r < 4; r++) {
    phrases.forEach((p) => {
      bits.push("<span>" + esc(p) + "</span>");
      bits.push("<i>✠</i>");
    });
  }
  tracks.forEach((el) => {
    el.innerHTML = bits.join("");
  });
}

function setBr(el, v) {
  if (!el || !v) return;
  el.innerHTML = esc(v).replace(/\n/g, "<br>");
}

function wikiById(c, id) {
  const pages = Array.isArray(c.wiki) ? c.wiki : [];
  return pages.find((p) => p && p.id === id) || null;
}

function wikiFilled(p) {
  if (!p) return false;
  return Boolean(
    (p.title && String(p.title).trim()) ||
      (p.lede && String(p.lede).trim()) ||
      (p.quote && String(p.quote).trim()) ||
      (p.eyebrow && String(p.eyebrow).trim()) ||
      (Array.isArray(p.cards) && p.cards.length) ||
      (Array.isArray(p.list) && p.list.length) ||
      (Array.isArray(p.tables) && p.tables.length)
  );
}

function setCaption(text) {
  const el = document.getElementById("hero-caption");
  if (!el) return;
  const t = String(text || "").trim();
  if (!t) {
    el.hidden = true;
    el.textContent = "";
    return;
  }
  el.hidden = false;
  el.innerHTML = esc(t).replace(/\n/g, "<br>");
}

function showCrest(fig) {
  fig.classList.remove("is-video", "is-playing");
  fig.classList.add("is-ph");
  fig.innerHTML = '<img src="img/crest.png" alt="Dynasty of Rot">';
}

function fmtTime(sec) {
  const n = Math.max(0, Math.floor(Number(sec) || 0));
  const m = Math.floor(n / 60);
  const s = n % 60;
  return m + ":" + String(s).padStart(2, "0");
}

function bindHeroPlayer(fig, v) {
  const playBtn = fig.querySelector(".hero-play");
  const muteBtn = fig.querySelector(".hero-mute");
  const seek = fig.querySelector(".hero-seek");
  const timeEl = fig.querySelector(".hero-time");
  let wantMute = true;
  let drag = false;

  function syncPlay() {
    playBtn.textContent = v.paused ? "Играть" : "Пауза";
    playBtn.setAttribute("aria-label", v.paused ? "Играть" : "Пауза");
    fig.classList.toggle("is-playing", !v.paused);
  }
  function syncMute() {
    muteBtn.textContent = v.muted || v.volume === 0 ? "Звук" : "Без звука";
    muteBtn.classList.toggle("on", !(v.muted || v.volume === 0));
  }
  function syncSeek() {
    if (!timeEl) return;
    const dur = Number.isFinite(v.duration) ? v.duration : 0;
    timeEl.textContent = fmtTime(v.currentTime) + " / " + fmtTime(dur);
    if (!drag && dur > 0) seek.value = String(Math.round((v.currentTime / dur) * 1000));
  }
  function tryPlay() {
    const p = v.play();
    if (p && p.catch) p.catch(() => {});
  }
  function applySeek() {
    const dur = Number.isFinite(v.duration) ? v.duration : 0;
    if (dur > 0) v.currentTime = (Number(seek.value) / 1000) * dur;
  }

  v.muted = true;
  v.volume = 1;
  v.setAttribute("playsinline", "");
  v.setAttribute("webkit-playsinline", "");
  tryPlay();

  playBtn.onclick = (ev) => {
    ev.stopPropagation();
    if (v.paused) tryPlay();
    else v.pause();
  };
  muteBtn.onclick = (ev) => {
    ev.stopPropagation();
    wantMute = !wantMute;
    v.muted = wantMute;
    if (!wantMute) {
      v.removeAttribute("muted");
      v.volume = 1;
      tryPlay();
    } else {
      v.setAttribute("muted", "");
    }
    syncMute();
  };
  seek.addEventListener("pointerdown", () => {
    drag = true;
  });
  seek.addEventListener("pointerup", () => {
    drag = false;
    applySeek();
  });
  seek.addEventListener("input", applySeek);
  v.addEventListener("play", syncPlay);
  v.addEventListener("pause", syncPlay);
  v.addEventListener("timeupdate", syncSeek);
  v.addEventListener("loadedmetadata", syncSeek);
  v.addEventListener("volumechange", syncMute);
  v.addEventListener("click", (ev) => {
    ev.preventDefault();
    if (v.paused) tryPlay();
    else v.pause();
  });
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden && !v.paused) tryPlay();
  });
  syncPlay();
  syncMute();
  syncSeek();
}

function clampNum(v, lo, hi, fallback) {
  const n = Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(lo, Math.min(hi, Math.round(n)));
}

function applyHeroSize(c) {
  const hero = document.querySelector(".hero");
  if (!hero) return;
  const w = clampNum(c.hero_width, 20, 70, 36);
  const h = clampNum(c.hero_height, 200, 720, 560);
  hero.style.setProperty("--hero-col", w + "%");
  hero.style.setProperty("--hero-h", h + "px");
}

function mountHero(c) {
  applyHeroSize(c);
  const fig = document.querySelector(".hero-art");
  if (!fig) return;
  const stage = fig.closest(".hero-stage");
  setCaption(c.hero_caption);
  const src = String(c.hero_media || "");
  if (!isSafeMedia(src)) {
    if (stage) stage.classList.remove("has-video");
    showCrest(fig);
    return;
  }
  const url = mediaSrc(src);
  const kind = c.hero_kind === "video" || /\.(mp4|webm)$/i.test(url) ? "video" : "image";
  if (kind === "video") {
    if (stage) stage.classList.add("has-video");
    fig.classList.remove("is-ph");
    fig.classList.add("is-video", "is-playing");
    fig.innerHTML =
      '<div class="hero-frame">' +
      '<video src="' +
      esc(url) +
      '" autoplay muted loop playsinline webkit-playsinline preload="auto"></video>' +
      '<div class="hero-controls">' +
      '<button type="button" class="hero-play">Пауза</button>' +
      '<input class="hero-seek" type="range" min="0" max="1000" value="0" step="1" aria-label="Перемотка">' +
      '<span class="hero-time">0:00 / 0:00</span>' +
      '<button type="button" class="hero-mute">Звук</button>' +
      "</div></div>";
    bindHeroPlayer(fig, fig.querySelector("video"));
  } else {
    if (stage) stage.classList.remove("has-video");
    fig.classList.remove("is-video", "is-playing");
    fig.classList.toggle("is-ph", /crest\.png$/i.test(url));
    fig.innerHTML = '<img src="' + esc(url) + '" alt="Dynasty of Rot">';
  }
}

function mountCodex(c) {
  const cx = c.codex;
  if (!cx) return;
  const rulesEl = document.getElementById("codex-rules");
  if (!rulesEl) return;
  const has =
    (Array.isArray(cx.rules) && cx.rules.length) ||
    (cx.title && String(cx.title).trim()) ||
    (cx.lede && String(cx.lede).trim());
  if (!has) return;
  const eb = document.getElementById("codex-eyebrow");
  if (eb && cx.eyebrow) eb.textContent = cx.eyebrow;
  setBr(document.getElementById("codex-title"), cx.title);
  const lede = document.getElementById("codex-lede");
  if (lede && cx.lede) lede.textContent = cx.lede;
  if (Array.isArray(cx.rules) && cx.rules.length) {
    rulesEl.innerHTML = cx.rules.map((r) => "<li>" + esc(r) + "</li>").join("");
  }
}

function renderTables(tables) {
  return (tables || [])
    .map((t) => {
      const title = t.title ? '<h2 class="wiki-h">' + esc(t.title) + "</h2>" : "";
      const headers = (t.headers || ["", ""]).slice(0, 2);
      const head =
        "<tr><th>" +
        esc(headers[0] || "") +
        "</th><th>" +
        esc(headers[1] || "") +
        "</th></tr>";
      const rows = (t.rows || [])
        .map((r) => {
          const a = Array.isArray(r) ? r[0] : r && r[0];
          const b = Array.isArray(r) ? r[1] : r && r[1];
          return "<tr><td>" + esc(a || "") + "</td><td>" + esc(b || "") + "</td></tr>";
        })
        .join("");
      return title + '<table class="table">' + head + rows + "</table>";
    })
    .join("");
}

function mountWiki(c) {
  const root = document.querySelector("[data-wiki]");
  if (!root) return;
  const id = root.getAttribute("data-wiki");
  const p = wikiById(c, id);
  if (!wikiFilled(p)) return;
  const eb = document.getElementById("wiki-eyebrow");
  if (eb && p.eyebrow) eb.textContent = p.eyebrow;
  setBr(document.getElementById("wiki-title"), p.title);
  const lede = document.getElementById("wiki-lede");
  if (lede) {
    if (p.lede) lede.innerHTML = rich(p.lede);
    else if (id === "mods") lede.innerHTML = "";
  }
  const quote = document.getElementById("wiki-quote");
  if (quote && p.quote) quote.textContent = p.quote;
  const cards = document.getElementById("wiki-cards");
  if (cards && Array.isArray(p.cards) && p.cards.length) {
    cards.innerHTML = p.cards
      .map(
        (card) =>
          '<article class="card"><h3>' +
          esc(card.title) +
          "</h3><p>" +
          rich(card.text) +
          "</p></article>"
      )
      .join("");
  }
  const list = document.getElementById("wiki-list");
  if (list && Array.isArray(p.list) && p.list.length) {
    list.innerHTML = p.list.map((x) => "<li>" + rich(x) + "</li>").join("");
  }
  const tables = document.getElementById("wiki-tables");
  if (tables && Array.isArray(p.tables) && p.tables.length) {
    tables.innerHTML = renderTables(p.tables);
    wrapTables(tables);
  }
  if (p.nav) {
    const map = { overview: "wiki.html", join: "wiki-join.html", mods: "wiki-mods.html" };
    const href = map[id];
    if (href) {
      document.querySelectorAll(".subnav a").forEach((a) => {
        if ((a.getAttribute("href") || "").toLowerCase() === href) a.textContent = p.nav;
      });
    }
  }
}

async function mountHome() {
  try {
    const c = await fetch("/content.json?t=" + Date.now(), { cache: "no-store" }).then((r) => r.json());
    if (c.ticker) setTicker(c.ticker);
    const eb = document.getElementById("hero-eyebrow");
    if (eb && c.hero_eyebrow) eb.textContent = c.hero_eyebrow;
    const ht = document.getElementById("hero-title");
    if (ht && c.hero_title) ht.innerHTML = esc(c.hero_title).replace(/\n/g, "<br>");
    const lede = document.getElementById("home-lede");
    if (lede && c.lede) lede.textContent = c.lede;
    const grid = document.getElementById("home-cards");
    if (grid && Array.isArray(c.cards) && c.cards.length) {
      grid.innerHTML = c.cards
        .map((card) => "<article class=\"card\"><h3>" + esc(card.title) + "</h3><p>" + esc(card.text) + "</p></article>")
        .join("");
    }
    mountHero(c);
    mountCodex(c);
    mountWiki(c);
  } catch (e) {}
}

async function mountNews(selector) {
  const root = document.querySelector(selector);
  if (!root) return;
  try {
    const data = await fetch("news.json", { cache: "no-store" }).then((r) => r.json());
    const items = data.items || [];
    if (!items.length) {
      root.innerHTML = '<article class="card"><h3>Новости</h3><p>Новостей пока нет.</p></article>';
      return;
    }
    root.innerHTML = items
      .map((it) => {
        let media = "";
        const src = String(it.image || "");
        if (/^\/news-media\/[A-Za-z0-9._-]+$/.test(src)) {
          media = '<img src="' + src + '" alt="">';
        }
        return (
          '<article class="card">' +
          media +
          "<h3>" +
          esc(it.date) +
          " · " +
          esc(it.title) +
          "</h3><p>" +
          esc(it.text) +
          "</p></article>"
        );
      })
      .join("");
  } catch (e) {
    root.innerHTML =
      '<article class="card"><h3>Новости</h3><p>Сейчас не удалось загрузить ленту. Обновите страницу чуть позже.</p></article>';
  }
}
mountNews("#news-feed");
mountHome();
