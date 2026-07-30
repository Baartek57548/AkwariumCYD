import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const outputDirectory = resolve(scriptDirectory, "..", "figma-import");

const color = {
  canvas: "#07101F",
  surface: "#0D192C",
  card: "#142238",
  elevated: "#1B2D48",
  border: "#29405F",
  text: "#F4F8FF",
  muted: "#91A8C7",
  disabled: "#60748F",
  accent: "#4D8DFF",
  accentDark: "#326FD6",
  info: "#68C5FF",
  success: "#5CDBA2",
  successDark: "#143F35",
  warning: "#FFB45C",
  warningDark: "#49341F",
  danger: "#FF6878",
  dangerDark: "#4B2230"
};

const esc = (value) =>
  String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");

const rect = (x, y, width, height, fill, radius = 0, stroke = "none", strokeWidth = 0) =>
  `<rect x="${x}" y="${y}" width="${width}" height="${height}" rx="${radius}" fill="${fill}" stroke="${stroke}" stroke-width="${strokeWidth}"/>`;

const line = (x1, y1, x2, y2, stroke, width = 1) =>
  `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${stroke}" stroke-width="${width}" stroke-linecap="round"/>`;

const text = (x, y, value, size = 12, fill = color.text, weight = 500, anchor = "start") =>
  `<text x="${x}" y="${y}" fill="${fill}" font-family="Montserrat, Arial, sans-serif" font-size="${size}" font-weight="${weight}" text-anchor="${anchor}">${esc(value)}</text>`;

function statusBar(alarm = false) {
  const healthFill = alarm ? color.dangerDark : color.elevated;
  const healthStroke = alarm ? color.danger : color.accent;
  const healthText = alarm ? "!" : "AQ";
  return [
    rect(0, 0, 320, 25, color.surface),
    line(0, 24.5, 320, 24.5, color.accent),
    rect(6, 4, 24, 17, healthFill, 5, healthStroke, 1),
    text(18, 16.5, healthText, 10, healthStroke, 700, "middle"),
    rect(34, 4, 62, 17, color.elevated, 4),
    text(65, 16.5, "T 25.4°C", 10, color.info, 500, "middle"),
    text(160, 16.5, "18:42  ·  12 s", 10, color.text, 500, "middle"),
    rect(222, 4, 56, 17, color.elevated, 4),
    text(250, 16.5, "WiFi", 10, color.success, 500, "middle"),
    rect(282, 4, 34, 17, color.successDark, 4),
    text(299, 16.5, "RTC", 9, color.success, 500, "middle")
  ].join("");
}

const navigationItems = [
  ["⌂", "Start"],
  ["↻", "Plan"],
  ["＋", "Moduly"],
  ["⌁", "Wykres"],
  ["⚙", "System"]
];

function navigation(active) {
  const items = navigationItems.map(([icon, label], index) => {
    const x = 1 + index * 64;
    const selected = index === active;
    return [
      selected ? rect(x, 207, 62, 31, color.elevated, 8, color.accent, 1) : "",
      text(x + 31, 220, icon, 12, selected ? color.accent : color.muted, 500, "middle"),
      text(x + 31, 233, label, 9, selected ? color.accent : color.muted, 500, "middle")
    ].join("");
  }).join("");
  return rect(0, 205, 320, 35, color.canvas) + line(0, 205, 320, 205, color.border) + items;
}

function card(x, y, width, height, accent = null) {
  return [
    rect(x, y, width, height, color.card, 10, color.border, 1),
    accent ? rect(x, y + 7, 3, height - 14, accent, 2) : ""
  ].join("");
}

function stateChip(x, y, value, active = false, warning = false) {
  const fg = warning ? color.warning : active ? color.success : color.muted;
  return text(x, y + 11.5, `[${value}]`, 9, fg, 700);
}

function deviceCard(x, y, width, title, state, detail, accent, active = false, warning = false) {
  return [
    card(x, y, width, 42, accent),
    text(x + 10, y + 14, "●", 9, accent),
    text(x + 24, y + 14, title, 10, color.text, 600),
    stateChip(x + 7, y + 23, state, active, warning),
    text(x + width - 7, y + 35, detail, 9, color.muted, 500, "end")
  ].join("");
}

function startBody(alarm = false) {
  return [
    card(4, 29, 150, 86, color.info),
    text(16, 44, "WODA", 10, color.muted, 600),
    text(144, 44, "Cel 25.0°C", 9, color.muted, 500, "end"),
    text(16, 80, "25.4", 28, alarm ? color.danger : color.text, 700),
    text(82, 80, "°C", 11, color.info, 600),
    text(16, 106, alarm ? "Za wysoka" : "Stabilna", 10, alarm ? color.danger : color.success, 500),
    card(160, 29, 74, 41, color.success),
    text(171, 44, "pH", 9, color.muted, 500),
    text(171, 63, "7.12", 14, color.text, 700),
    rect(240, 29, 76, 41, color.elevated, 8, color.border, 1),
    text(278, 44, "KARMIJ", 9, color.muted, 600, "middle"),
    text(278, 63, "19:00", 11, color.text, 600, "middle"),
    card(160, 74, 156, 41, color.danger),
    text(172, 90, "SERWIS", 9, color.muted, 600),
    text(172, 107, "Sterowanie reczne", 10, color.text, 500),
    deviceCard(4, 119, 100, "Przednia", "ON", "AUTO DAY", color.info, true),
    deviceCard(110, 119, 100, "Tylna", "ON", "AUTO DAY", color.success, true),
    deviceCard(216, 119, 100, "Filtr", "ON", "AUTO", color.info, true),
    deviceCard(4, 163, 153, "Grzalka", alarm ? "HEAT" : "STBY", "25.0°C", color.warning, false, alarm),
    deviceCard(163, 163, 153, "Powietrze", "OFF", "AUTO", "#A855F7")
  ].join("");
}

function tile(x, y, width, title, detail, icon, active = true) {
  return [
    rect(x, y, width, 74, active ? color.elevated : color.surface, 10, active ? color.accent : color.border, 1),
    text(x + 12, y + 23, icon, 14, color.info, 600),
    text(x + 34, y + 22, title, 11, color.text, 600),
    text(x + 12, y + 57, detail, 10, active ? color.muted : color.disabled, 500)
  ].join("");
}

function planBody() {
  return [
    tile(8, 33, 144, "Przednia", "10:00–20:00 · AUTO", "◉"),
    tile(160, 33, 144, "Tylna", "10:00–20:00 · AUTO", "◉"),
    tile(8, 115, 296, "Filtr", "00:00–23:59 · HARMONOGRAM · ON", "↻")
  ].join("");
}

function modulesBody() {
  return [
    tile(8, 33, 144, "Karmnik", "19:00 · aktywny", "≡"),
    tile(160, 33, 144, "Grzalka", "25.0°C · STBY", "ϟ"),
    tile(8, 115, 144, "pH", "7.12 · poprawny", "◇"),
    tile(160, 115, 144, "Powietrze", "AUTO · OFF", "↻")
  ].join("");
}

function chartBody() {
  const points = [[15, 157], [38, 151], [61, 153], [84, 140], [107, 144], [130, 132], [153, 137], [176, 125], [199, 129], [222, 117], [245, 121], [268, 109], [299, 113]];
  const path = points.map(([x, y], index) => `${index === 0 ? "M" : "L"}${x} ${y}`).join(" ");
  return [
    card(4, 29, 312, 172),
    rect(10, 33, 50, 22, color.accent, 7),
    text(35, 48, "TEMP", 9, color.text, 700, "middle"),
    ...["pH", "LDR", "HEAP"].map((label, index) => {
      const x = 64 + index * 45;
      return rect(x, 33, 42, 22, color.elevated, 7) +
        text(x + 21, 48, label, 9, color.muted, 600, "middle");
    }),
    rect(262, 33, 44, 22, color.successDark, 7),
    text(284, 48, "LIVE", 9, color.success, 700, "middle"),
    text(10, 69, "Cel 25.0°C  ·  H 0.5", 9, color.muted),
    rect(10, 75, 300, 91, color.surface, 6, color.border, 1),
    line(10, 105, 310, 105, color.border),
    line(10, 135, 310, 135, color.border),
    line(10, 150, 310, 150, color.success),
    `<path d="${path}" fill="none" stroke="${color.info}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>`,
    rect(10, 171, 300, 24, color.surface, 5),
    text(24, 181, "MIN", 8, color.muted),
    text(24, 191, "24.8", 9, color.text, 600),
    text(160, 181, "MAX", 8, color.muted, 500, "middle"),
    text(160, 191, "25.6", 9, color.text, 600, "middle"),
    text(296, 181, "TERAZ", 8, color.muted, 500, "end"),
    text(296, 191, "25.4", 9, color.text, 600, "end")
  ].join("");
}

const systemTiles = [
  ["WiFi", "STA / OTA", "⌁", color.info],
  ["Ekran", "Motyw / LDR", "▣", color.accent],
  ["Logi", "Zdarzenia", "≡", color.warning],
  ["Czas", "RTC / NTP", "◷", color.success],
  ["Diag", "Heap / CPU", "⚙", "#A855F7"],
  ["Zasilanie", "Sleep / reset", "⏻", color.danger],
  ["Audio", "Dzwieki", "♫", "#14B8A6"],
  ["Sprzet", "Moduly", "＋", color.muted]
];

function systemBody() {
  return systemTiles.map(([title, subtitle, icon, accent], index) => {
    const column = index % 2;
    const row = Math.floor(index / 2);
    const x = column === 0 ? 4 : 166;
    const y = 29 + row * 44;
    return [
      card(x, y, 150, 38),
      text(x + 10, y + 24, icon, 12, accent, 600),
      text(x + 30, y + 15, title, 10, color.text, 600),
      text(x + 30, y + 30, subtitle, 9, color.muted, 500)
    ].join("");
  }).join("");
}

function frame(name, active, body, alarm = false) {
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240" viewBox="0 0 320 240">
  <title>${esc(name)}</title>
  ${rect(0, 0, 320, 240, color.canvas)}
  ${statusBar(alarm)}
  ${body}
  ${navigation(active)}
</svg>
`;
}

const files = new Map([
  ["01-start.svg", frame("AquaCYD CYD — Start", 0, startBody(false))],
  ["02-plan.svg", frame("AquaCYD CYD — Plan", 1, planBody())],
  ["03-modules.svg", frame("AquaCYD CYD — Moduly", 2, modulesBody())],
  ["04-chart.svg", frame("AquaCYD CYD — Wykres", 3, chartBody())],
  ["05-system.svg", frame("AquaCYD CYD — System", 4, systemBody())],
  ["06-start-alarm.svg", frame("AquaCYD CYD — Alarm", 0, startBody(true), true)]
]);

await mkdir(outputDirectory, { recursive: true });
for (const [name, content] of files) {
  await writeFile(resolve(outputDirectory, name), content, "utf8");
}

console.log(`Wygenerowano ${files.size} ramek CYD w ${outputDirectory}`);
