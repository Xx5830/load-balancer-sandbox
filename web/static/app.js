const PALETTE = ["#5b9dff", "#38d39f", "#ffb454", "#ff6b8a", "#b58cff", "#4fd6e0", "#f08fc0"];
const [ACCENT, ACCENT2, DANGER] = PALETTE;

Chart.defaults.color = "#9aa6bd";
Chart.defaults.borderColor = "rgba(255,255,255,0.06)";
Chart.defaults.font.family = "Inter, -apple-system, Segoe UI, Roboto, sans-serif";

const $ = (id) => document.getElementById(id);
const fmt = (n, d = 1) =>
  n == null ? "—" : Number(n).toLocaleString("ru-RU", { minimumFractionDigits: d, maximumFractionDigits: d });
const axis = (text, extra = {}) => ({ beginAtZero: true, title: { display: true, text }, ...extra });

let results = null;
let policy = null;
const charts = {};

function chart(id, config) {
  charts[id]?.destroy();
  charts[id] = new Chart($(id), {
    ...config,
    options: { responsive: true, maintainAspectRatio: false, ...config.options },
  });
}

async function fetchResults() {
  const res = await fetch("/api/results");
  if (res.ok) return res.json();
  const err = await res.json().catch(() => ({}));
  throw new Error(err.detail || `HTTP ${res.status}`);
}

function renderMeta() {
  const m = results.meta;
  $("meta").innerHTML = `
    <span>Серверов: <b>${m.servers.length}</b></span>
    <span>Тасок: <b>${m.total_tasks.toLocaleString("ru-RU")}</b></span>
    <span>Длительность: <b>${fmt(m.duration_ms / 1000, 0)} с</b></span>
    <span>Сгенерировано: <b>${new Date(m.generated_at).toLocaleString("ru-RU")}</b></span>`;
  $("srcLabel").textContent = m.source;
  $("policySelect").innerHTML = results.policies.map((p) => `<option>${p.name}</option>`).join("");
}

function renderCards() {
  const s = policy.summary;
  const cards = [
    ["Среднее время", fmt(s.avg_latency_ms), "мс", "card--accent"],
    ["Пиковое время", fmt(s.peak_latency_ms), "мс", "card--danger"],
    ["P95 латентность", fmt(s.p95_latency_ms), "мс", "card--warn"],
    ["Пропускная способность", fmt(s.throughput_rps, 0), "rps", ""],
  ];
  $("cards").innerHTML = cards
    .map(([label, value, unit, cls]) => `<div class="card ${cls}">
      <div class="card__label">${label}</div>
      <div class="card__value">${value}<span class="card__unit">${unit}</span></div>
    </div>`)
    .join("");
}

function renderCompare() {
  chart("compareChart", {
    type: "bar",
    data: {
      labels: results.policies.map((p) => p.name),
      datasets: [
        { label: "Среднее, мс", data: results.policies.map((p) => p.summary.avg_latency_ms), backgroundColor: ACCENT, borderRadius: 6 },
        { label: "Пиковое, мс", data: results.policies.map((p) => p.summary.peak_latency_ms), backgroundColor: DANGER, borderRadius: 6 },
      ],
    },
    options: { scales: { x: { grid: { display: false } }, y: axis("мс") } },
  });
}

function renderTimeline() {
  $("tlPolicy").textContent = policy.name;
  const line = (label, key, color, fill, yAxisID) => ({
    label, data: policy.timeline.map((p) => p[key]), borderColor: color,
    backgroundColor: fill, fill: !!fill, tension: 0.35, pointRadius: 0, yAxisID,
  });
  chart("timelineChart", {
    type: "line",
    data: {
      labels: policy.timeline.map((p) => (p.t_ms / 1000).toFixed(1)),
      datasets: [
        line("Латентность, мс", "latency_ms", ACCENT, "rgba(91,157,255,0.12)", "y"),
        line("Активные соединения", "active_connections", ACCENT2, null, "y1"),
      ],
    },
    options: {
      interaction: { mode: "index", intersect: false },
      scales: {
        x: axis("время, с", { beginAtZero: false, grid: { display: false } }),
        y: axis("мс", { position: "left" }),
        y1: axis("соединения", { position: "right", grid: { drawOnChartArea: false } }),
      },
    },
  });
}

function renderLoad() {
  $("loadPolicy").textContent = policy.name;
  chart("loadChart", {
    type: "bar",
    data: {
      labels: policy.servers.map((s) => `srv #${s.id}`),
      datasets: [{
        label: "Запросов", data: policy.servers.map((s) => s.requests),
        backgroundColor: policy.servers.map((_, i) => PALETTE[i % PALETTE.length]), borderRadius: 6,
      }],
    },
    options: {
      plugins: {
        legend: { display: false },
        tooltip: { callbacks: { afterLabel: (ctx) => {
          const s = policy.servers[ctx.dataIndex];
          return `вес ${s.weight} · свободность ${s.capacity} · ${s.share_pct}%`;
        } } },
      },
      scales: { x: { grid: { display: false } }, y: axis("запросов") },
    },
  });
}

function renderTable() {
  const max = Math.max(...policy.servers.map((s) => s.requests), 1);
  $("serverTable").querySelector("tbody").innerHTML = policy.servers
    .map((s) => `<tr>
      <td>srv #${s.id}</td>
      <td>${s.weight}</td>
      <td>${fmt(s.capacity, 2)}</td>
      <td class="bar-cell"><span class="bar" style="width:${((s.requests / max) * 100).toFixed(1)}%"></span><span>${s.requests.toLocaleString("ru-RU")}</span></td>
      <td>${fmt(s.share_pct)}</td>
      <td>${fmt(s.avg_time_ms)}</td>
      <td>${s.total_time_ms.toLocaleString("ru-RU")}</td>
    </tr>`)
    .join("");
}

function render() {
  policy = results.policies.find((p) => p.name === $("policySelect").value) || results.policies[0];
  renderCards();
  renderCompare();
  renderTimeline();
  renderLoad();
  renderTable();
}

async function init() {
  try {
    results = await fetchResults();
  } catch (e) {
    document.querySelector(".container").innerHTML = `<div class="error">Не удалось загрузить данные: ${e.message}</div>`;
    return;
  }
  renderMeta();
  render();
  $("policySelect").addEventListener("change", render);
  $("reloadBtn").addEventListener("click", init);
}

init();
