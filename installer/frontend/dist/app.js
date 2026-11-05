// The installer window. Backend: window.go.main.App (app.go); progress: the "progress" event.
const $ = (s) => document.querySelector(s);
const api = () => window.go.main.App;
let lang = "en", status = null, caps = null, banksDirty = false, busy = false, tab = "captures";

function t(key, vars = {}) {
  let s = (I18N[lang] && I18N[lang][key]) || I18N.en[key] || key;
  for (const [k, v] of Object.entries(vars)) s = s.replaceAll("{" + k + "}", v);
  return s;
}
function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v);
  }
  for (const k of kids) e.append(k);
  return e;
}

// ---------------------------------------------------------------- language
function applyLang() {
  document.documentElement.lang = lang === "pt" ? "pt-BR" : "en";
  document.querySelectorAll("[data-t]").forEach((n) => (n.textContent = t(n.dataset.t)));
  document.querySelectorAll(".lang button").forEach((b) => b.classList.toggle("on", b.dataset.lang === lang));
  renderStatus();
  renderCaps();
  renderAppUpdate();
}
document.querySelectorAll(".lang button").forEach((b) =>
  b.addEventListener("click", () => { lang = b.dataset.lang; api().SetLang(lang); applyLang(); }));

// ------------------------------------------------------------------ modal
function modal({ title, body = "", actions = [], progress = false, warn = false }) {
  $("#mTitle").textContent = title;
  const mb = $("#mBody");
  mb.replaceChildren();
  if (typeof body === "string") mb.textContent = body; else mb.append(body);
  $("#mBarWrap").hidden = !progress;
  $("#mBar").style.width = "0";
  $("#mWarn").hidden = !warn;
  const ma = $("#mActions");
  ma.replaceChildren(...actions.map((a) => el("button", { class: a.primary ? "primary" : "", onclick: a.run }, a.label)));
  $("#overlay").hidden = false;
}
function closeModal() { $("#overlay").hidden = true; }
function ask(title, body, okLabel) {
  return new Promise((res) => modal({ title, body, actions: [
    { label: t("cancel"), run: () => { closeModal(); res(false); } },
    { label: okLabel || t("proceed"), primary: true, run: () => { closeModal(); res(true); } },
  ] }));
}
function errText(e) {
  const s = String(e && e.message ? e.message : e);
  const m = s.match(/^(E\d+|ESTOCK|ENOTJB)\b/);
  return m ? t(m[1]) : s;
}
function showError(e) {
  modal({ title: t("errTitle"), body: errText(e), actions: [{ label: t("close"), primary: true, run: closeModal }] });
}

// run a long operation with the progress dialog; onError may handle a failure itself (true)
async function run(title, fn, onError) {
  busy = true;
  modal({ title, body: t("pStart"), progress: true, warn: true });
  let failed = null;
  try {
    await fn();
    closeModal();
  } catch (e) {
    failed = e;
  }
  if (failed) {
    closeModal();
    busy = false;
    if (!(onError && (await onError(failed)))) showError(failed);
  }
  busy = false;
  caps = null;
  status = null;            /* the pedal restarted: look again, reload its captures */
  renderStatus();
  setTimeout(poll, 1500);
}
const STEP = { download: "pDownload", wipe: "pWipe", start: "pStart", check: "pCheck", loader: "pLoader",
  write: "pWrite", reboot: "pReboot", done: "pDone", capture: "pCapture" };
function onProgress(p) {
  if ($("#overlay").hidden) return;
  $("#mBody").textContent = t(STEP[p.step] || p.step, { n: p.n, arg: p.arg });
  if (p.step === "write" || p.step === "capture") $("#mBar").style.width = p.n + "%";
}

// ------------------------------------------------------------------ device
function relNum() { return status && status.product ? parseInt(status.product.split("_")[1], 10) : 0; }

function renderStatus() {
  const st = status ? status.state : "searching";
  const dot = $("#dot"), title = $("#devTitle"), sub = $("#devSub"), acts = $("#devActions");
  dot.className = "dot";
  acts.replaceChildren();
  const rel = status ? status.release : "";
  const vars = { id: status ? status.identity : "", rel };
  const juice = st === "juicebox";
  $("#tabs").hidden = !juice;
  $("#welcome").hidden = juice;
  for (const p of ["captures", "banks", "firmware"]) $("#panel-" + p).hidden = !juice || tab !== p;
  if (st === "searching" || st === "busy") {
    dot.classList.add("pulse");
    title.textContent = t(st === "busy" ? "busyTitle" : "searching");
    sub.textContent = "";
  } else if (st === "none" || st === "nomidi") {
    dot.classList.add("pulse");
    title.textContent = t(st + "Title");
    sub.textContent = t(st + "Sub");
  } else if (st === "stock") {
    dot.classList.add("warn");
    title.textContent = t("stockTitle");
    sub.textContent = t("stockSub", vars);
    if (rel) acts.append(el("button", { class: "primary", onclick: doInstall }, t("install", vars)));
  } else if (st === "loader") {
    dot.classList.add("warn");
    title.textContent = t("loaderTitle");
    sub.textContent = t("loaderSub");
    if (rel) acts.append(el("button", { class: "primary", onclick: doInstall }, t("finish")));
  } else if (juice) {
    dot.classList.add("ok");
    title.textContent = t("jbTitle");
    const v = status.version, r = relNum();
    sub.textContent = t(!r || v === r ? "jbSubCurrent" : v < r ? "jbSubOld" : "jbSubNewer", vars);
    if (r && v < r) acts.append(el("button", { class: "primary", onclick: doInstall }, t("update", vars)));
  } else {
    dot.classList.add("err");
    title.textContent = t("otherTitle");
    sub.textContent = t("otherSub", vars);
  }
  $("#fwJuiceText").textContent = rel ? t("fwJuiceText", vars) : t("fwNoRelease");
  {   /* the same version is already on the pedal: nothing to write (an identical package makes the loader loop) */
    const same = juice && relNum() && status.version === relNum();
    $("#fwInstall").textContent = same ? t("upToDate") : t("reinstall", vars);
    $("#fwInstall").disabled = !rel || same;
  }
}

async function poll() {
  if (busy || !window.go) return;
  try {
    const s = await api().Status();
    if (s.state === "busy") return;
    const changed = !status || s.state !== status.state || s.identity !== status.identity;
    status = s;
    if (changed) { renderStatus(); if (s.state === "juicebox") loadCaps(); }
  } catch (e) { /* the next poll retries */ }
}

async function doInstall() {
  const rel = status.release;
  const body = el("div", {}, el("p", {}, t("confirmInstall")),
    el("p", { class: "risk" }, el("strong", {}, t("riskTitle") + ": "), t("risk")));
  if (!(await ask(t("confirmInstallTitle", { rel }), body, t("riskAccept")))) return;
  await run(t("installTitle"), () => api().InstallJuiceBox(), offerFinish);
}

// after a failed install: if the pedal is in update mode, offer to finish right there
async function offerFinish(e) {
  let s = null;
  for (let i = 0; i < 6 && !(s && s.state === "loader"); i++) {
    try { s = await api().Status(); } catch (x) {}
    if (!s || s.state !== "loader") await new Promise((r) => setTimeout(r, 1000));
  }
  if (!s || s.state !== "loader") return false;
  if (await ask(t("loaderTitle"), t("finishNow"), t("finish"))) {
    await run(t("installTitle"), () => api().InstallJuiceBox());
  }
  return true;
}
async function doRevert() {
  if (!(await ask(t("confirmRevertTitle"), t("confirmRevert"), t("fwRevert").replace("…", "")))) return;
  await run(t("revertTitle"), () => api().RevertStock());
}
$("#fwInstall").addEventListener("click", doInstall);
$("#fwRevert").addEventListener("click", doRevert);

// -------------------------------------------------------------------- tabs
document.querySelectorAll(".tabs button").forEach((b) => b.addEventListener("click", () => {
  tab = b.dataset.tab;
  document.querySelectorAll(".tabs button").forEach((x) => x.classList.toggle("on", x === b));
  renderStatus();
}));

// ---------------------------------------------------------------- captures
async function loadCaps() {
  try { caps = await api().GetCaptures(); banksDirty = false; } catch (e) { caps = null; }
  renderCaps();
}

function renderCaps() {
  const box = $("#slots"), rows = $("#bankRows");
  box.replaceChildren();
  rows.replaceChildren();
  if (!caps) return;
  caps.slots.forEach((s, i) => {
    const n = el("div", { class: "n" }, t("slot", { n: i + 1 }));
    if (s.Valid) {
      box.append(el("div", { class: "slot" }, n, el("div", { class: "t" }, s.Title),
        el("div", { class: "row" }, el("span", { class: "m" }, t("capRate", { rate: s.Rate })),
          el("button", { class: "small ghost", onclick: () => doRemove(i, s.Title) }, t("remove")))));
    } else {
      box.append(el("div", { class: "slot empty" }, n, el("div", { class: "t" }, t("empty")), el("span")));
    }
  });
  const opts = (sel) => {
    const s = el("select");
    s.append(el("option", { value: 127 }, t("none")));
    caps.slots.forEach((x, i) => { if (x.Valid) s.append(el("option", { value: i }, `${i + 1} · ${x.Title}`)); });
    s.value = String(caps.slots[sel] && caps.slots[sel].Valid ? sel : 127);
    return s;
  };
  caps.banks.B.forEach((b, i) => {
    const a = opts(b.Cap[0]), bb = opts(b.Cap[1]);
    const num = (v, min, max) => el("input", { type: "number", step: "0.5", min, max, value: (v / 2).toFixed(1) });
    const inn = num(b.InHD, -12, 12), out = num(b.OutHD, -24, 12);
    const dirty = () => {
      b.Cap = [parseInt(a.value, 10), parseInt(bb.value, 10)];
      const clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, Math.round((parseFloat(x) || 0) * 2)));
      b.InHD = clamp(inn.value, -24, 24);
      b.OutHD = clamp(out.value, -48, 24);
      banksDirty = true;
      $("#saveBanks").disabled = false;
    };
    for (const c of [a, bb, inn, out]) c.addEventListener("change", dirty);
    rows.append(el("tr", {},
      el("td", {}, el("span", { class: "btn-name" }, caps.names[i]), String(i + 1)),
      el("td", {}, a), el("td", {}, bb), el("td", {}, inn), el("td", {}, out)));
  });
  $("#saveBanks").disabled = !banksDirty;
}

async function doRemove(i, title) {
  if (!(await ask(t("remove"), t("confirmRemove", { title, n: i + 1 }), t("remove")))) return;
  await run(t("capsTitle"), () => api().RemoveCapture(i));
}

$("#addCaps").addEventListener("click", async () => {
  if (!caps) return;
  let picked;
  try { picked = await api().PickCaptures(); } catch (e) { return showError(e); }
  if (!picked || !picked.length) return;
  const free = caps.slots.map((s, i) => (s.Valid ? -1 : i)).filter((i) => i >= 0);
  if (!free.length) return modal({ title: t("addTitle"), body: t("addNoRoom"), actions: [{ label: t("ok"), primary: true, run: closeModal }] });
  const plan = [], skipped = [];
  for (const p of picked) {
    const name = p.path.split(/[\\/]/).pop();
    if (p.error) skipped.push([name, t(p.error)]);
    else if (plan.length < free.length) plan.push({ path: p.path, slot: free[plan.length], title: p.title });
    else skipped.push([name, t("addTooMany", { n: free.length })]);
  }
  const body = el("div");
  if (plan.length) {
    body.append(el("p", {}, t("addPlan")));
    const tb = el("table");
    for (const it of plan) tb.append(el("tr", {}, el("td", {}, t("slot", { n: it.slot + 1 })), el("td", {}, it.title)));
    body.append(tb);
  }
  if (skipped.length) {
    body.append(el("p", {}, t("addSkipped")));
    const tb = el("table");
    for (const [n, why] of skipped) tb.append(el("tr", {}, el("td", {}, n), el("td", { class: "e" }, why)));
    body.append(tb);
  }
  modal({ title: t("addTitle"), body, actions: plan.length ? [
    { label: t("cancel"), run: closeModal },
    { label: t("proceed"), primary: true, run: () => run(t("capsTitle"), () => api().AddCaptures(plan)) },
  ] : [{ label: t("ok"), primary: true, run: closeModal }] });
});

$("#saveBanks").addEventListener("click", async () => {
  if (!caps) return;
  await run(t("capsTitle"), () => api().SaveBanks(caps.banks));
});

// the first run: English or Brazilian Portuguese
function chooseLang() {
  return new Promise((res) => {
    const pick = (l) => () => { lang = l; api().SetLang(l); applyLang(); closeModal(); res(); };
    modal({ title: "Language · Idioma", body: el("div", {},
      el("p", {}, I18N.en.langAsk), el("p", {}, I18N.pt.langAsk)), actions: [
      { label: "Português (Brasil)", run: pick("pt") },
      { label: "English", primary: true, run: pick("en") },
    ] });
  });
}

// ------------------------------------------------------- a newer installer
let appUpdate = null;
function renderAppUpdate() {
  const box = $("#appUpdate");
  if (!appUpdate || !appUpdate.available) { box.hidden = true; return; }
  const vars = { latest: appUpdate.latest, current: appUpdate.current || "?" };
  $("#auTitle").textContent = t("auTitle", vars);
  $("#auSub").textContent = t("auSub", vars);
  $("#auGet").textContent = t("auGet");
  $("#auGet").onclick = () => api().OpenReleasePage(appUpdate.url);
  box.hidden = false;
}
async function checkAppUpdate() {
  try { appUpdate = await api().CheckAppUpdate(); } catch (e) { appUpdate = null; }
  renderAppUpdate();
}

// -------------------------------------------------------------------- start
(async function start() {
  for (let i = 0; i < 50 && !window.go; i++) await new Promise((r) => setTimeout(r, 100));
  if (window.runtime) window.runtime.EventsOn("progress", onProgress);
  let saved = "";
  try { saved = await api().Lang(); } catch (e) {}
  lang = saved || "en";                      /* English is the default */
  applyLang();
  if (!saved) await chooseLang();            /* first run: ask */
  poll();
  setInterval(poll, 2500);
  checkAppUpdate();
  setInterval(checkAppUpdate, 6 * 3600 * 1000);   /* a window left open checks again every 6 hours */
})();
