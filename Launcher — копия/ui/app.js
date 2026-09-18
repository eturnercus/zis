const $ = (s) => document.querySelector(s);
const logEl = $("#log");

document.querySelectorAll(".nav button").forEach((b) => {
  b.addEventListener("click", () => {
    document.querySelectorAll(".nav button").forEach((x) => x.classList.remove("on"));
    b.classList.add("on");
    ["news", "rules", "settings"].forEach((id) => {
      document.getElementById(id).classList.toggle("hidden", id !== b.dataset.tab);
    });
  });
});

async function api(path, opts) {
  const r = await fetch(path, opts);
  return r.json();
}

function line(t) {
  logEl.textContent += "\n" + t;
  logEl.scrollTop = logEl.scrollHeight;
}

async function refresh() {
  try {
    const s = await api("/api/state");
    $("#nick").value = s.nick || $("#nick").value;
    $("#ram").value = s.ram || 8192;
    $("#gamedir").value = s.gameDir || "";
    $("#serverHint").textContent = s.server || "";
    $("#status").textContent = s.online ? "двор в сети" : "адрес из конфига";
    $("#status").classList.toggle("ok", !!s.online);
  } catch (e) {
    $("#status").textContent = "нет связи с хостом";
  }
}

$("#play").addEventListener("click", async () => {
  const nick = $("#nick").value.trim();
  if (nick.length < 3) {
    $("#hint").textContent = "Ник от 3 символов. Это пропуск, не манифест.";
    return;
  }
  $("#play").disabled = true;
  $("#play").classList.add("busy");
  $("#play").textContent = "Загрузка";
  $("#hint").textContent = "Готовим сборку. Окно не закрывать.";
  try {
    const s = await api("/api/play", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        nick,
        ram: Number($("#ram").value),
        gameDir: $("#gamedir").value
      })
    });
    line(s.message || "Запущено.");
    $("#play").textContent = "В игре";
    $("#hint").textContent = "Не закрывайте лаунчер, пока играете — иначе двор решит, что вас нет.";
  } catch (e) {
    line(String(e));
    $("#play").disabled = false;
    $("#play").classList.remove("busy");
    $("#play").textContent = "Играть";
    $("#hint").textContent = "Не вышло. Смотрите лог выше.";
  }
});

setInterval(async () => {
  try {
    const s = await api("/api/log");
    if (s.text) logEl.textContent = s.text;
  } catch (_) {}
}, 1500);

refresh();
