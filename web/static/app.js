const PALETTE = ["#5b9dff", "#38d39f", "#ffb454", "#ff6b8a", "#b58cff", "#4fd6e0", "#f08fc0"];
const GEN_TYPES = ["constant", "uniform", "normal", "exponential", "lognormal", "sequence"];
const GEN_FIELDS = {
  constant: ["value"], uniform: ["min", "max"], normal: ["center", "deviation"],
  exponential: ["center"], lognormal: ["center", "deviation"], sequence: ["values"],
};

Chart.defaults.color = "#9aa6bd";
Chart.defaults.borderColor = "rgba(255,255,255,0.06)";
Chart.defaults.font.family = "Inter, -apple-system, Segoe UI, Roboto, sans-serif";

const $ = (id) => document.getElementById(id);
const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };
const num = (id) => Number($(id).value) || 0;
const fmt = (n, d = 1) => n == null ? "—" : Number(n).toLocaleString("ru-RU", { minimumFractionDigits: d, maximumFractionDigits: d });
const color = (i) => PALETTE[i % PALETTE.length];

let ALGORITHMS = [];
const charts = {};

function chart(id, config) {
  charts[id]?.destroy();
  charts[id] = new Chart($(id), { ...config, options: { responsive: true, maintainAspectRatio: false, ...config.options } });
}

function banner(msg, kind = "error") {
  $("banner").innerHTML = msg ? `<div class="${kind}">${msg}</div>` : "";
  if (msg) window.scrollTo({ top: 0, behavior: "smooth" });
}

async function api(path, opts) {
  const res = await fetch(path, opts);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.detail || `HTTP ${res.status}`);
  return data;
}

/* ----------------------------- Генераторы ----------------------------- */

function genRow(label, value) {
  const wrap = el("div", "gen");
  const isArr = Array.isArray(value);
  const type = isArr ? "sequence" : (value?.type || "constant");
  wrap.innerHTML = `<span class="gen__label">${label}</span>
    <select class="gen__type">${GEN_TYPES.map((t) => `<option ${t === type ? "selected" : ""}>${t}</option>`).join("")}</select>
    <span class="gen__fields"></span>`;
  const sel = wrap.querySelector(".gen__type");
  const fields = wrap.querySelector(".gen__fields");
  const initial = isArr ? { values: value.join(", ") } : (value || {});
  const draw = () => {
    fields.innerHTML = GEN_FIELDS[sel.value].map((f) =>
      `<input class="gen__f" data-k="${f}" placeholder="${f}" value="${initial[f] ?? ""}">`).join("");
  };
  sel.onchange = draw; draw();
  return wrap;
}

function readGen(wrap) {
  const type = wrap.querySelector(".gen__type").value;
  if (type === "sequence") {
    const raw = wrap.querySelector('[data-k="values"]').value;
    return raw.split(/[\s,]+/).filter(Boolean).map(Number);
  }
  const g = { type };
  wrap.querySelectorAll(".gen__f").forEach((i) => { g[i.dataset.k] = Number(i.value); });
  return g;
}

/* ----------------------------- Серверы ----------------------------- */

function serverRow(s = {}) {
  const row = el("div", "row");
  row.innerHTML = `
    <label>weight <input class="s-weight" type="number" min="1" value="${s.weight ?? 1}"></label>
    <label>capacity <input class="s-capacity" type="number" step="0.05" min="0" max="1" value="${s.capacity ?? 1}"></label>
    <label>параллелизм <input class="s-parallel" type="number" min="1" value="${s.max_parallel_requests ?? 1}"></label>
    <label>start_at_ms <input class="s-start" type="number" min="0" value="${s.start_at_ms ?? 0}"></label>
    <label>crash_at_ms <input class="s-crash" type="number" min="0" placeholder="∞" value="${s.crash_at_ms ?? ""}"></label>
    <button class="rm" title="убрать">✕</button>`;
  row.append(genRow("background_load", s.background_load ?? { type: "constant", value: 0 }));
  row.querySelector(".rm").onclick = () => { row.remove(); refreshPreview(); };
  return row;
}

function readServers() {
  return [...$("servers").children].map((r) => {
    const crash = r.querySelector(".s-crash").value;
    const s = {
      weight: Number(r.querySelector(".s-weight").value),
      capacity: Number(r.querySelector(".s-capacity").value),
      background_load: readGen(r.querySelector(".gen")),
      max_parallel_requests: Number(r.querySelector(".s-parallel").value),
      start_at_ms: Number(r.querySelector(".s-start").value),
    };
    if (crash !== "") s.crash_at_ms = Number(crash);
    return s;
  });
}

/* ----------------------------- Группы клиентов ----------------------------- */

function groupRow(g = {}) {
  const row = el("div", "row row--group");
  const head = el("div", "row__head");
  head.innerHTML = `
    <label>count <input class="g-count" type="number" min="1" value="${g.count ?? 3}"></label>
    <label>sticky_scope
      <select class="g-sticky">${["none", "client", "group"].map((o) =>
        `<option ${o === (g.sticky_scope || "none") ? "selected" : ""}>${o}</option>`).join("")}</select></label>
    <label>max_requests <input class="g-max" type="number" min="0" value="${g.max_requests ?? 0}"></label>
    <button class="rm" title="убрать">✕</button>`;
  head.querySelector(".rm").onclick = () => { row.remove(); refreshPreview(); };
  row.append(head);
  row.append(genRow("inter_arrival_ms", g.inter_arrival_ms ?? { type: "exponential", center: 100 }));
  row.append(genRow("task_cost", g.task_cost ?? { type: "constant", value: 0.05 }));
  return row;
}

function readGroups() {
  return [...$("groups").children].map((r) => ({
    count: Number(r.querySelector(".g-count").value),
    sticky_scope: r.querySelector(".g-sticky").value,
    max_requests: Number(r.querySelector(".g-max").value),
    inter_arrival_ms: readGen(r.querySelectorAll(".gen")[0]),
    task_cost: readGen(r.querySelectorAll(".gen")[1]),
  }));
}

/* ----------------------------- Сборка пресета ----------------------------- */

function buildPreset() {
  const p = {
    name: $("f-name").value.trim() || "experiment",
    description: $("f-desc").value,
    seed: num("f-seed"),
    duration_ms: num("f-duration"),
    warmup_ms: num("f-warmup"),
    total_request_limit: num("f-limit"),
    manager_workers: num("f-workers"),
    servers: readServers(),
    client_groups: readGroups(),
  };
  const profile = $("f-profile").value.trim();
  if (profile) p.profile = profile;
  return p;
}

function selectedAlgos() {
  return [...document.querySelectorAll(".algo:checked")].map((c) => c.value);
}

function refreshPreview() {
  try { $("presetPreview").textContent = JSON.stringify(buildPreset(), null, 2); }
  catch { $("presetPreview").textContent = ""; }
}

function loadPreset(p) {
  $("f-name").value = p.name ?? "experiment";
  $("f-desc").value = p.description ?? "";
  $("f-profile").value = p.profile ?? "";
  $("f-seed").value = p.seed ?? 42;
  $("f-duration").value = p.duration_ms ?? 30000;
  $("f-warmup").value = p.warmup_ms ?? 0;
  $("f-limit").value = p.total_request_limit ?? 0;
  $("f-workers").value = p.manager_workers ?? 0;
  $("servers").innerHTML = ""; (p.servers ?? []).forEach((s) => $("servers").append(serverRow(s)));
  $("groups").innerHTML = ""; (p.client_groups ?? []).forEach((g) => $("groups").append(groupRow(g)));
  refreshPreview();
}

/* ----------------------------- Действия формы ----------------------------- */

async function run() {
  banner("");
  const algorithms = selectedAlgos();
  if (!algorithms.length) return banner("Выберите хотя бы один алгоритм.");
  $("runBtn").disabled = true; $("runBtn").textContent = "⏳ Запуск…";
  try {
    const batch = await api("/api/run", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ preset: buildPreset(), algorithms }),
    });
    await loadRuns(batch.id);
    showTab("analyze");
  } catch (e) { banner(`Не удалось запустить: ${e.message}`); }
  finally { $("runBtn").disabled = false; $("runBtn").textContent = "▶ Запустить"; }
}

async function savePreset() {
  banner("");
  try {
    const { name } = await api("/api/preset", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(buildPreset()),
    });
    banner(`Пресет сохранён: data/presets/${name}.json`, "notice");
  } catch (e) { banner(`Не удалось сохранить: ${e.message}`); }
}

function exportPreset() {
  const blob = new Blob([JSON.stringify(buildPreset(), null, 2)], { type: "application/json" });
  const a = el("a"); a.href = URL.createObjectURL(blob);
  a.download = `${buildPreset().name || "preset"}.json`; a.click();
  URL.revokeObjectURL(a.href);
}

function importPreset(file) {
  const r = new FileReader();
  r.onload = () => { try { loadPreset(JSON.parse(r.result)); banner(""); } catch (e) { banner(`Битый JSON: ${e.message}`); } };
  r.readAsText(file);
}

/* ----------------------------- Анализ ----------------------------- */

let batch = null;

async function loadRuns(selectId) {
  const runs = await api("/api/runs");
  const sel = $("runSelect");
  sel.innerHTML = runs.map((r) =>
    `<option value="${r.id}">${r.preset_name} · ${r.algorithms.join(", ")}</option>`).join("");
  $("analyzeEmpty").hidden = runs.length > 0;
  $("analyzeBody").hidden = runs.length === 0;
  if (!runs.length) { batch = null; return; }
  if (selectId) sel.value = selectId;
  await showRun(sel.value);
}

async function showRun(id) {
  batch = await api(`/api/runs/${id}`);
  renderAnalyze();
}

const ok = (r) => r.result && !r.error;
let detailAlgo = null;

function renderAnalyze() {
  const body = $("analyzeBody");
  body.innerHTML = "";
  const good = batch.runs.filter(ok);
  const bad = batch.runs.filter((r) => !ok(r));

  if (bad.length) {
    body.append(el("div", "panel error",
      "<b>Ошибки прогонов:</b><br>" + bad.map((r) => `${r.algorithm}: ${r.error}`).join("<br>")));
  }
  if (!good.length) { $("analyzeEmpty").hidden = false; return; }
  $("analyzeEmpty").hidden = true;

  renderCompare(good);
  renderDetailSection(good);
}

function renderDetailSection(good) {
  if (!good.some((r) => r.algorithm === detailAlgo)) detailAlgo = good[0].algorithm;

  const panel = el("section", "panel");
  const head = el("div", "panel__head");
  head.append(el("h2", null, "Детально по алгоритму"));
  const sel = el("select", "detail-select");
  sel.innerHTML = good.map((r) =>
    `<option value="${r.algorithm}" ${r.algorithm === detailAlgo ? "selected" : ""}>${r.algorithm}</option>`).join("");
  head.append(sel);
  panel.append(head);

  const host = el("div"); host.id = "detailHost";
  panel.append(host);
  $("analyzeBody").append(panel);

  sel.onchange = () => { detailAlgo = sel.value; drawDetail(good, host); };
  drawDetail(good, host);
}

function drawDetail(good, host) {
  host.innerHTML = "";
  const r = good.find((x) => x.algorithm === detailAlgo);
  const idx = good.indexOf(r);
  renderDetail(r, idx, host);
}

function summaryRow(r) {
  const t = r.result.totals, l = r.result.latency;
  return {
    algo: r.algorithm,
    throughput: t.throughput_rps, sent: t.requests_sent,
    success: t.successful, failed: t.failed_final, retries: t.retries,
    mean: l.mean, p95: l.percentiles.p95, p99: l.percentiles.p99, max: l.max,
  };
}

function renderCompare(runs) {
  const rows = runs.map(summaryRow);
  const panel = el("section", "panel");
  panel.append(el("h2", null, "Сравнение алгоритмов"));

  const table = el("table");
  table.innerHTML = `<thead><tr>
    <th>Алгоритм</th><th>RPS</th><th>Отправлено</th><th>Успех</th><th>Отказы</th>
    <th>Повторы</th><th>mean, мс</th><th>p95</th><th>p99</th><th>max</th></tr></thead>
    <tbody>${rows.map((r) => `<tr>
      <td>${r.algo}</td><td>${fmt(r.throughput)}</td><td>${r.sent.toLocaleString("ru-RU")}</td>
      <td>${r.success.toLocaleString("ru-RU")}</td><td>${r.failed.toLocaleString("ru-RU")}</td>
      <td>${r.retries.toLocaleString("ru-RU")}</td><td>${fmt(r.mean)}</td>
      <td>${fmt(r.p95)}</td><td>${fmt(r.p99)}</td><td>${fmt(r.max)}</td></tr>`).join("")}</tbody>`;
  const tw = el("div", "table-wrap"); tw.append(table); panel.append(tw);

  const grid = el("div", "grid-2");
  grid.append(canvasPanel("cmpLatency", "Латентность, мс"));
  grid.append(canvasPanel("cmpThroughput", "Пропускная способность и отказы"));
  panel.append(grid);
  $("analyzeBody").append(panel);

  const labels = rows.map((r) => r.algo);
  chart("cmpLatency", {
    type: "bar",
    data: { labels, datasets: [
      { label: "mean", data: rows.map((r) => r.mean), backgroundColor: PALETTE[0], borderRadius: 6 },
      { label: "p95", data: rows.map((r) => r.p95), backgroundColor: PALETTE[2], borderRadius: 6 },
      { label: "p99", data: rows.map((r) => r.p99), backgroundColor: PALETTE[3], borderRadius: 6 },
    ] },
    options: { scales: { y: { beginAtZero: true, title: { display: true, text: "мс" } } } },
  });
  chart("cmpThroughput", {
    type: "bar",
    data: { labels, datasets: [
      { label: "RPS", data: rows.map((r) => r.throughput), backgroundColor: PALETTE[1], borderRadius: 6, yAxisID: "y" },
      { label: "Отказы", data: rows.map((r) => r.failed), backgroundColor: PALETTE[3], borderRadius: 6, yAxisID: "y1" },
    ] },
    options: { scales: {
      y: { beginAtZero: true, position: "left", title: { display: true, text: "rps" } },
      y1: { beginAtZero: true, position: "right", grid: { drawOnChartArea: false }, title: { display: true, text: "отказы" } },
    } },
  });
}

function canvasPanel(id, title) {
  const p = el("div", "subpanel");
  if (title) p.append(el("h3", null, title));
  const box = el("div", "chart-box");
  const cv = el("canvas"); cv.id = id;
  box.append(cv); p.append(box);
  return p;
}

function renderDetail(r, idx, host) {
  const res = r.result;
  const c = color(idx);
  const panel = host;

  const t = res.totals;
  const f = res.failures.by_reason;
  const ru = (n) => (n ?? 0).toLocaleString("ru-RU");

  if (res.experiment || res.config) panel.append(experimentBlock(res));

  panel.append(el("div", "cards", [
    card("Длительность", fmt(t.actual_duration_ms / 1000, 1), "с"),
    card("RPS", fmt(t.throughput_rps), ""),
    card("Отправлено", ru(t.requests_sent), ""),
    card("Успешно", ru(t.successful), "", "card--accent"),
    card("Отказы", ru(t.failed_final), "", "card--danger"),
    card("Повторы", ru(t.retries), ""),
  ].join("")));

  panel.append(latencyBlock(res.latency));

  panel.append(el("div", "subhint",
    `Отказы (${ru(res.failures.total_final_failures)}): crashed ${ru(f.server_crashed)} · ` +
    `overloaded ${ru(f.server_overloaded)} · timeout ${ru(f.timeout)}`));

  const grid = el("div", "grid-2");
  const ids = [`tl-${idx}`, `tlreq-${idx}`, `hist-${idx}`, `srvload-${idx}`, `srvreq-${idx}`];
  grid.append(canvasPanel(ids[0], "Таймлайн: латентность и соединения"));
  grid.append(canvasPanel(ids[1], "Таймлайн: запросы и нагрузка"));
  grid.append(canvasPanel(ids[2], "Гистограмма латентности"));
  grid.append(canvasPanel(ids[3], "Нагрузка серверов (avg/peak)"));
  grid.append(canvasPanel(ids[4], "Запросы по серверам"));
  panel.append(grid);

  panel.append(serverTable(res.servers));
  if (res.client_groups?.length) panel.append(groupTable(res.client_groups));

  const tl = res.timeline || [];
  drawTimeline(ids[0], tl, c);
  drawTimelineLoad(ids[1], tl, c);
  drawHistogram(ids[2], res.latency.histogram, res.latency.min, c);
  drawServerLoad(ids[3], res.servers);
  drawServerReq(ids[4], res.servers, c);
}

function drawTimelineLoad(id, tl, c) {
  chart(id, {
    type: "line",
    data: { labels: tl.map((p) => (p.t_ms / 1000).toFixed(0)), datasets: [
      { label: "новые запросы", data: tl.map((p) => p.new_requests ?? null), borderColor: c, backgroundColor: c + "22", fill: true, tension: 0.3, pointRadius: 0, yAxisID: "y" },
      { label: "отказы/с", data: tl.map((p) => p.failed_this_second ?? null), borderColor: PALETTE[3], tension: 0.3, pointRadius: 0, yAxisID: "y" },
      { label: "в полёте", data: tl.map((p) => p.requests_in_flight ?? null), borderColor: PALETTE[2], tension: 0.3, pointRadius: 0, yAxisID: "y" },
      { label: "ср. нагрузка", data: tl.map((p) => p.total_load_avg ?? null), borderColor: PALETTE[1], tension: 0.3, pointRadius: 0, yAxisID: "y1" },
    ] },
    options: {
      interaction: { mode: "index", intersect: false },
      scales: {
        x: { title: { display: true, text: "время, с" }, grid: { display: false } },
        y: { beginAtZero: true, position: "left", title: { display: true, text: "запросов/с" } },
        y1: { beginAtZero: true, max: 1, position: "right", grid: { drawOnChartArea: false }, title: { display: true, text: "нагрузка" } },
      },
    },
  });
}

function card(label, value, unit, cls = "") {
  return `<div class="card ${cls}"><div class="card__label">${label}</div>
    <div class="card__value">${value}<span class="card__unit">${unit}</span></div></div>`;
}

function drawTimeline(id, tl, c) {
  chart(id, {
    type: "line",
    data: { labels: tl.map((p) => (p.t_ms / 1000).toFixed(0)), datasets: [
      { label: "avg латентность, мс", data: tl.map((p) => p.avg_latency_ms ?? null), borderColor: c, backgroundColor: c + "22", fill: true, tension: 0.3, pointRadius: 0, yAxisID: "y" },
      { label: "p95, мс", data: tl.map((p) => p.p95_latency_ms ?? null), borderColor: PALETTE[2], tension: 0.3, pointRadius: 0, yAxisID: "y" },
      { label: "соединения", data: tl.map((p) => p.active_connections ?? null), borderColor: PALETTE[1], tension: 0.3, pointRadius: 0, yAxisID: "y1" },
    ] },
    options: {
      interaction: { mode: "index", intersect: false },
      scales: {
        x: { title: { display: true, text: "время, с" }, grid: { display: false } },
        y: { beginAtZero: true, position: "left", title: { display: true, text: "мс" } },
        y1: { beginAtZero: true, position: "right", grid: { drawOnChartArea: false }, title: { display: true, text: "conn" } },
      },
    },
  });
}

function drawHistogram(id, h, min, c) {
  const size = h?.bucket_size_ms || 1;
  const buckets = h?.buckets || [];
  const base = min ?? 0;
  chart(id, {
    type: "bar",
    data: { labels: buckets.map((_, i) => `${(base + i * size).toFixed(0)}–${(base + (i + 1) * size).toFixed(0)}`),
      datasets: [{ label: "запросов", data: buckets, backgroundColor: c }] },
    options: { plugins: { legend: { display: false } },
      scales: { x: { title: { display: true, text: "латентность, мс" }, grid: { display: false } },
        y: { beginAtZero: true } } },
  });
}

function drawServerLoad(id, servers) {
  chart(id, {
    type: "bar",
    data: { labels: servers.map((s) => `#${s.id}`), datasets: [
      { label: "avg_load", data: servers.map((s) => s.avg_load ?? 0), backgroundColor: PALETTE[0], borderRadius: 6 },
      { label: "peak_load", data: servers.map((s) => s.peak_load ?? 0), backgroundColor: PALETTE[3], borderRadius: 6 },
    ] },
    options: { scales: { x: { grid: { display: false } }, y: { beginAtZero: true, max: 1 } } },
  });
}

function drawServerReq(id, servers, c) {
  chart(id, {
    type: "bar",
    data: { labels: servers.map((s) => `#${s.id}`),
      datasets: [{ label: "requests_received", data: servers.map((s) => s.requests_received ?? 0),
        backgroundColor: servers.map((_, i) => color(i)), borderRadius: 6 }] },
    options: { plugins: { legend: { display: false } },
      scales: { x: { grid: { display: false } }, y: { beginAtZero: true } } },
  });
}

const SMP_LABELS = {
  task_load_factor: "task_load_factor", connection_load_factor: "connection_load_factor",
  load_slowdown_factor: "load_slowdown_factor", load_recovery_rate: "load_recovery_rate",
  overload_reject_factor: "overload_reject_factor", reject_threshold_seconds: "reject_threshold_seconds",
  min_task_seconds: "min_task_seconds", min_weight_factor: "min_weight_factor",
};

function kvList(pairs) {
  const dl = el("div", "kv");
  dl.innerHTML = pairs.filter(([, v]) => v != null && v !== "")
    .map(([k, v]) => `<div class="kv__k">${k}</div><div class="kv__v">${v}</div>`).join("");
  return dl;
}

function experimentBlock(res) {
  const sp = el("div", "subpanel");
  sp.append(el("h3", null, "Параметры эксперимента"));
  const cfg = res.config || {};
  const exp = res.experiment || {};
  const grid = el("div", "kv-grid");
  grid.append(kvList([
    ["имя", cfg.name ?? exp.name],
    ["алгоритм", cfg.algorithm ?? exp.algorithm],
    ["профиль", exp.profile],
    ["seed", cfg.seed],
    ["duration_ms", cfg.duration_ms],
    ["warmup_ms", cfg.warmup_ms],
    ["total_request_limit", cfg.total_request_limit],
    ["manager_workers", cfg.manager_workers],
    ["preset_file", exp.preset_file],
    ["описание", cfg.description],
  ]));
  const smp = cfg.server_model_params;
  if (smp) grid.append(kvList(Object.keys(SMP_LABELS).map((k) => [SMP_LABELS[k], smp[k]])));
  sp.append(grid);
  return sp;
}

function latencyBlock(l) {
  if (!l) return el("div");
  const p = l.percentiles || {};
  const u = l.unit || "ms";
  const sp = el("div", "subpanel");
  sp.append(el("h3", null, `Латентность (${u}) · ${(l.count ?? 0).toLocaleString("ru-RU")} запросов`));
  sp.append(kvList([
    ["min", fmt(l.min)], ["mean", fmt(l.mean)], ["stddev", fmt(l.stddev)],
    ["p50", fmt(p.p50)], ["p95", fmt(p.p95)], ["p99", fmt(p.p99)], ["max", fmt(l.max)],
  ]));
  return sp;
}

function serverTable(servers) {
  const wrap = el("div", "table-wrap");
  const t = el("table");
  t.innerHTML = `<thead><tr>
    <th>Сервер</th><th>weight</th><th>capacity</th><th>паралл.</th><th>start_at</th><th>crash_at</th>
    <th>получено</th><th>успех</th><th>отказы</th>
    <th>min, мс</th><th>avg, мс</th><th>max, мс</th><th>processing, мс</th>
    <th>avg load</th><th>peak load</th><th>падений</th></tr></thead>
    <tbody>${servers.map((s) => `<tr>
      <td>#${s.id}</td><td>${s.weight}</td><td>${fmt(s.capacity, 2)}</td>
      <td>${s.max_parallel_requests ?? "—"}</td><td>${(s.start_at_ms ?? 0).toLocaleString("ru-RU")}</td>
      <td>${s.crash_at_ms == null ? "∞" : s.crash_at_ms.toLocaleString("ru-RU")}</td>
      <td>${(s.requests_received ?? 0).toLocaleString("ru-RU")}</td>
      <td>${(s.successful ?? 0).toLocaleString("ru-RU")}</td>
      <td>${(s.failed ?? 0).toLocaleString("ru-RU")}</td>
      <td>${fmt(s.min_time_ms)}</td><td>${fmt(s.avg_time_ms)}</td><td>${fmt(s.max_time_ms)}</td>
      <td>${fmt(s.total_time_processing_ms)}</td>
      <td>${fmt(s.avg_load ?? 0, 2)}</td><td>${fmt(s.peak_load ?? 0, 2)}</td>
      <td>${s.crashes ?? 0}</td></tr>`).join("")}</tbody>`;
  wrap.append(t);
  return wrap;
}

function groupTable(groups) {
  const wrap = el("div", "table-wrap");
  const t = el("table");
  t.innerHTML = `<thead><tr>
    <th>Группа</th><th>sticky</th><th>клиентов</th><th>отправлено</th><th>успех</th>
    <th>отказы</th><th>повторы</th><th>mean, мс</th><th>p95</th><th>max</th></tr></thead>
    <tbody>${groups.map((g) => `<tr>
      <td>#${g.group_index}</td><td>${g.sticky_scope}</td><td>${g.num_clients}</td>
      <td>${(g.requests_sent ?? 0).toLocaleString("ru-RU")}</td>
      <td>${(g.successful ?? 0).toLocaleString("ru-RU")}</td>
      <td>${(g.failed_final ?? 0).toLocaleString("ru-RU")}</td>
      <td>${(g.retries ?? 0).toLocaleString("ru-RU")}</td>
      <td>${fmt(g.latency.mean)}</td><td>${fmt(g.latency.p95)}</td><td>${fmt(g.latency.max)}</td></tr>`).join("")}</tbody>`;
  wrap.append(t);
  return wrap;
}

/* ----------------------------- Вкладки и инициализация ----------------------------- */

function showTab(name) {
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("tab--on", t.dataset.tab === name));
  document.querySelectorAll(".view").forEach((v) => { v.hidden = v.id !== name; });
}

async function init() {
  try {
    const meta = await api("/api/meta");
    ALGORITHMS = meta.algorithms;
    $("binInfo").innerHTML = meta.binary
      ? `бинарь: <b>${meta.binary.split("/").pop()}</b>`
      : `<span class="warn-text">бинарь не собран — just build</span>`;
  } catch { ALGORITHMS = ["RoundRobin", "WeightRoundRobin", "LeastConnections", "ConsistentHashing"]; }

  $("algos").innerHTML = ALGORITHMS.map((a) =>
    `<label class="algo-opt"><input class="algo" type="checkbox" value="${a}" checked> ${a}</label>`).join("");

  $("servers").append(serverRow({ weight: 1 }), serverRow({ weight: 2, capacity: 0.8 }), serverRow({ weight: 3, capacity: 0.6 }));
  $("groups").append(groupRow());

  document.querySelectorAll(".tab").forEach((t) => { t.onclick = () => showTab(t.dataset.tab); });
  $("addServer").onclick = () => { $("servers").append(serverRow()); refreshPreview(); };
  $("addGroup").onclick = () => { $("groups").append(groupRow()); refreshPreview(); };
  $("runBtn").onclick = run;
  $("saveBtn").onclick = savePreset;
  $("exampleBtn").onclick = () => loadExample();
  $("exportBtn").onclick = exportPreset;
  $("importBtn").onclick = () => $("importFile").click();
  $("importFile").onchange = (e) => e.target.files[0] && importPreset(e.target.files[0]);
  document.querySelector(".container").addEventListener("input", refreshPreview);
  document.querySelector(".container").addEventListener("change", refreshPreview);

  $("refreshRuns").onclick = () => loadRuns();
  $("runSelect").onchange = (e) => showRun(e.target.value);
  $("deleteRun").onclick = deleteRun;

  refreshPreview();
  loadRuns().catch(() => {});
}

async function loadExample() {
  try {
    const presets = await api("/api/presets");
    const ex = presets.find((p) => p.name === "example") || presets[0];
    if (ex) return loadPreset(ex);
  } catch {}
  loadPreset({
    name: "example", seed: 42, duration_ms: 30000,
    servers: [{ weight: 1, capacity: 1, max_parallel_requests: 4 },
      { weight: 2, capacity: 0.8, max_parallel_requests: 4 },
      { weight: 4, capacity: 1, max_parallel_requests: 8, crash_at_ms: 20000 }],
    client_groups: [{ count: 6, inter_arrival_ms: { type: "exponential", center: 80 }, task_cost: { type: "normal", center: 0.08, deviation: 0.02, min: 0.01 } }],
  });
}

async function deleteRun() {
  const id = $("runSelect").value;
  if (!id || !confirm(`Удалить прогон ${id}?`)) return;
  await api(`/api/runs/${id}`, { method: "DELETE" });
  await loadRuns();
}

init();
