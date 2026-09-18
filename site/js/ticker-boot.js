(function () {
  var PERIOD = 32000;
  var KEY = "dorTickerT0";
  var t0;
  try {
    t0 = sessionStorage.getItem(KEY);
    if (!t0) {
      t0 = String(Date.now());
      sessionStorage.setItem(KEY, t0);
    }
  } catch (e) {
    t0 = String(Date.now());
  }
  t0 = Number(t0) || Date.now();

  function progress() {
    return ((Date.now() - t0) % PERIOD) / PERIOD;
  }

  function apply(el) {
    el.style.animation = "none";
    el.style.transform = "translate3d(" + (-progress() * 25) + "%,0,0)";
    el.classList.add("is-on");
  }

  function tick() {
    var nodes = document.querySelectorAll(".ticker-track");
    for (var i = 0; i < nodes.length; i++) apply(nodes[i]);
    requestAnimationFrame(tick);
  }

  var nodes = document.querySelectorAll(".ticker-track");
  for (var i = 0; i < nodes.length; i++) apply(nodes[i]);
  requestAnimationFrame(tick);
})();
