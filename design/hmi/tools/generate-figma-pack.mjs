import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const outputDirectory = resolve(scriptDirectory, "..", "figma-import");

const colors = {
  canvas: "#07101F",
  surface: "#0D192C",
  card: "#142238",
  elevated: "#1B2D48",
  border: "#29405F",
  text: "#F4F8FF",
  muted: "#91A8C7",
  disabled: "#60748F",
  accent: "#4D8DFF",
  success: "#5CDBA2",
  successDark: "#143F35",
  warning: "#FFB45C",
  warningDark: "#49341F",
  danger: "#FF6878",
  dangerDark: "#4B2230",
  info: "#68C5FF",
};

const nav = [
  ["⌂", "Podgląd"],
  ["●", "Sterowanie"],
  ["◉", "Czujniki"],
  ["!", "Alarmy"],
  ["↻", "Automatyka"],
  ["⚙", "System"],
];

function text(x, y, value, size = 16, fill = colors.text, weight = 400, anchor = "start") {
  const escaped = String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
  return `<text x="${x}" y="${y}" fill="${fill}" font-family="Montserrat, Arial, sans-serif" font-size="${size}" font-weight="${weight}" text-anchor="${anchor}">${escaped}</text>`;
}

function rect(x, y, width, height, fill, radius = 18, stroke = "none", strokeWidth = 0) {
  return `<rect x="${x}" y="${y}" width="${width}" height="${height}" rx="${radius}" fill="${fill}" stroke="${stroke}" stroke-width="${strokeWidth}"/>`;
}

function circle(x, y, radius, fill, stroke = "none", strokeWidth = 0) {
  return `<circle cx="${x}" cy="${y}" r="${radius}" fill="${fill}" stroke="${stroke}" stroke-width="${strokeWidth}"/>`;
}

function multiline(x, y, lines, size = 16, fill = colors.muted, lineHeight = 25, weight = 400) {
  return lines.map((line, index) => text(x, y + index * lineHeight, line, size, fill, weight)).join("");
}

function chip(x, y, width, label, foreground, background) {
  return [
    rect(x, y, width, 38, background, 19, foreground, 1),
    circle(x + 20, y + 19, 6, foreground),
    text(x + 34, y + 25, label, 14, foreground, 700),
  ].join("");
}

function sidebar(activeIndex, syncText = "Teraz") {
  const items = nav
    .map(([icon, label], index) => {
      const y = 96 + index * 58;
      const active = index === activeIndex;
      return [
        rect(12, y, 160, 50, active ? colors.accent : "transparent", 12),
        text(30, y + 31, icon, 18, colors.text, 700),
        text(58, y + 31, label, 15, colors.text, active ? 700 : 500),
      ].join("");
    })
    .join("");
  return [
    rect(0, 0, 184, 600, colors.surface, 0),
    text(20, 38, "AquaCYD", 28, colors.text, 700),
    text(20, 64, "SMART AQUARIUM", 13, colors.info, 700),
    items,
    rect(12, 464, 160, 120, colors.canvas, 14, colors.border, 1),
    text(26, 490, "OSTATNIA SYNCHRONIZACJA", 10, colors.muted, 700),
    text(26, 520, syncText, 15, syncText === "Teraz" ? colors.success : colors.warning, 700),
    text(26, 563, "Automatyka lokalna: CYD", 12, colors.success, 600),
  ].join("");
}

function header(title, linkState = "WSZYSTKO ONLINE", alarmState = "BEZ ALARMÓW", alarm = false) {
  return [
    rect(184, 0, 840, 76, colors.canvas, 0),
    text(208, 32, title, 23, colors.text, 700),
    text(208, 57, "Bezpieczne sterowanie • dane z CYD przez ESP-NOW", 13, colors.muted, 400),
    chip(
      676,
      19,
      166,
      linkState,
      linkState === "WSZYSTKO ONLINE" ? colors.success : colors.warning,
      linkState === "WSZYSTKO ONLINE" ? colors.successDark : colors.warningDark,
    ),
    chip(
      854,
      19,
      154,
      alarmState,
      alarm ? colors.danger : colors.success,
      alarm ? colors.dangerDark : colors.successDark,
    ),
  ].join("");
}

function frame(name, activeIndex, body, options = {}) {
  const {
    linkState = "WSZYSTKO ONLINE",
    alarmState = "BEZ ALARMÓW",
    alarm = false,
    syncText = "Teraz",
    overlay = "",
  } = options;
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="600" viewBox="0 0 1024 600">
  <title>${name}</title>
  <desc>Edytowalna ramka AquaCYD HMI 1024x600 przeznaczona do importu w Figma.</desc>
  <g id="HMI-Screen-${name.replaceAll(" ", "-")}">
    ${rect(0, 0, 1024, 600, colors.canvas, 0)}
    <g id="Navigation">${sidebar(activeIndex, syncText)}</g>
    <g id="TopBar">${header(name, linkState, alarmState, alarm)}</g>
    <g id="Content">${body}</g>
${overlay ? `    ${overlay}` : ""}
  </g>
</svg>
`;
}

function metricCard(x, y, width, title, value, caption, accent = colors.accent) {
  return [
    rect(x, y, width, 136, colors.card, 18, colors.border, 1),
    text(x + 16, y + 26, title, 13, colors.muted, 700),
    text(x + 16, y + 75, value, 31, colors.text, 700),
    text(x + 16, y + 112, caption, 14, accent, 500),
  ].join("");
}

function outputPill(x, y, width, label, enabled) {
  return [
    rect(x, y, width, 48, enabled ? colors.successDark : colors.elevated, 14, enabled ? colors.success : "none", enabled ? 1 : 0),
    circle(x + 20, y + 24, 6, enabled ? colors.success : colors.disabled),
    text(x + 34, y + 30, label, 14, colors.text, 600),
    text(x + width - 18, y + 30, enabled ? "ON" : "OFF", 13, enabled ? colors.success : colors.muted, 700, "end"),
  ].join("");
}

function dashboardBody(critical = false) {
  return [
    rect(204, 88, 800, 82, critical ? colors.dangerDark : colors.successDark, 18, critical ? colors.danger : colors.success, 1),
    circle(232, 129, 7, critical ? colors.danger : colors.success),
    text(252, 120, critical ? "CYD zgłasza alarm" : "System bezpieczny", 20, colors.text, 700),
    text(252, 148, critical ? "Otwórz ekran Alarmy i usuń przyczynę" : "Automatyka i zabezpieczenia działają lokalnie", 14, critical ? colors.danger : colors.success, 500),
    text(970, 141, critical ? "!" : "✓", 34, critical ? colors.danger : colors.success, 700, "end"),
    metricCard(204, 182, 256, "TEMPERATURA", "24.68 °C", "Pomiar prawidłowy", colors.success),
    metricCard(476, 182, 256, "pH", "7.126", "Pomiar prawidłowy", colors.success),
    metricCard(748, 182, 256, "PRZEWODNOŚĆ", "512 µS/cm", "Pomiar prawidłowy", colors.success),
    rect(204, 330, 522, 248, colors.card, 18, colors.border, 1),
    text(222, 360, "Urządzenia", 20, colors.text, 700),
    text(706, 358, "Bieżący stan wyjść sterownika", 13, colors.muted, 400, "end"),
    outputPill(222, 378, 236, "Światło główne", true),
    outputPill(474, 378, 234, "Światło roślinne", false),
    outputPill(222, 438, 236, "Filtr", true),
    outputPill(474, 438, 234, "Napowietrzanie", true),
    outputPill(222, 498, 236, "Grzałka", false),
    rect(742, 330, 262, 248, colors.card, 18, colors.border, 1),
    text(760, 360, "Stan instalacji", 20, colors.text, 700),
    multiline(760, 408, [
      critical ? "Poziom wody   NISKI" : "Poziom wody   OK",
      critical ? "Wyciek         WYKRYTY" : "Wyciek         BRAK",
      critical ? "Fail-safe      ALARM" : "Fail-safe      AKTYWNY",
    ], 14, critical ? colors.danger : colors.muted, 34, 600),
    text(760, 552, "ESP-NOW  −54 dBm", 14, colors.info, 600),
  ].join("");
}

function controlCard(x, y, title, enabled, dangerousOff = false) {
  return [
    rect(x, y, 386, 174, colors.card, 18, colors.border, 1),
    text(x + 18, y + 32, title, 19, colors.text, 700),
    circle(x + 352, y + 25, 6, enabled ? colors.success : colors.disabled),
    text(x + 18, y + 65, enabled ? "Stan: WŁĄCZONE" : "Stan: wyłączone", 14, enabled ? colors.success : colors.muted, 500),
    rect(x + 18, y + 104, 164, 52, colors.accent, 12),
    text(x + 100, y + 136, "Włącz", 15, colors.text, 700, "middle"),
    rect(x + 204, y + 104, 164, 52, dangerousOff ? colors.warningDark : colors.elevated, 12, dangerousOff ? colors.warning : "none", dangerousOff ? 1 : 0),
    text(x + 286, y + 136, "Wyłącz", 15, dangerousOff ? colors.warning : colors.text, 700, "middle"),
  ].join("");
}

function controlsBody() {
  return [
    rect(204, 88, 800, 64, "#102B43", 18, colors.info, 1),
    text(226, 127, "•  Każde ręczne polecenie wygasa po 15 minutach", 15, colors.info, 600),
    controlCard(204, 164, "Światło główne", true),
    controlCard(618, 164, "Światło roślinne", false),
    controlCard(204, 350, "Filtr", true, true),
    controlCard(618, 350, "Napowietrzanie", true),
    rect(204, 536, 244, 52, colors.accent, 12),
    text(326, 568, "▶  Karmienie 10 min", 15, colors.text, 700, "middle"),
    rect(464, 536, 220, 52, colors.elevated, 12),
    text(574, 568, "↻  Odśwież stan", 15, colors.text, 700, "middle"),
    text(1004, 558, "Sterowanie gotowe", 14, colors.success, 600, "end"),
    rect(736, 574, 268, 6, colors.success, 3),
  ].join("");
}

function sensorCard(x, y, title, value, caption, progress, accent = colors.accent) {
  const maximumWidth = 352;
  return [
    rect(x, y, 386, 202, colors.card, 18, colors.border, 1),
    text(x + 18, y + 28, title, 13, colors.muted, 700),
    text(x + 18, y + 78, value, 30, colors.text, 700),
    rect(x + 18, y + 124, maximumWidth, 8, colors.elevated, 4),
    rect(x + 18, y + 124, Math.round(maximumWidth * progress), 8, accent, 4),
    text(x + 18, y + 174, caption, 14, colors.muted, 500),
  ].join("");
}

function sensorsBody() {
  return [
    sensorCard(204, 92, "TEMPERATURA WODY", "24.68 °C", "Aktualny pomiar z CYD", 0.56, colors.success),
    sensorCard(618, 92, "ODCZYN pH", "7.126", "Aktualny pomiar sondy pH", 0.52, colors.info),
    sensorCard(204, 310, "PRZEWODNOŚĆ EC", "512 µS/cm", "Aktualny pomiar przewodności", 0.26, colors.accent),
    sensorCard(618, 310, "ŚWIATŁO LDR", "1840", "Surowy poziom światła otoczenia", 0.44, colors.warning),
    rect(204, 528, 800, 60, colors.card, 18, colors.border, 1),
    text(222, 563, "CZUJNIKI BEZPIECZEŃSTWA", 13, colors.muted, 700),
    text(694, 563, "Poziom wody: OK", 14, colors.success, 700, "end"),
    text(986, 563, "Wyciek: BRAK", 14, colors.success, 700, "end"),
  ].join("");
}

function alarmRow(y, title, action, critical = false) {
  const accent = critical ? colors.danger : colors.warning;
  const background = critical ? colors.dangerDark : colors.warningDark;
  return [
    rect(204, y, 800, 64, background, 14, accent, 1),
    text(222, y + 26, title, 15, accent, 700),
    text(222, y + 50, action, 13, colors.muted, 400),
  ].join("");
}

function alarmsBody() {
  return [
    rect(204, 92, 800, 102, colors.dangerDark, 18, colors.danger, 1),
    text(222, 129, "3 aktywne alarmy", 24, colors.danger, 700),
    text(222, 169, "CYD uruchomił lokalną procedurę bezpieczeństwa.", 15, colors.muted, 400),
    alarmRow(206, "Wykryto wyciek", "Odłącz zasilanie urządzeń i sprawdź instalację.", true),
    alarmRow(280, "Niski poziom wody", "Uzupełnij wodę i sprawdź układ dolewki."),
    alarmRow(354, "Nieaktualne dane czujnika", "Sprawdź magistralę i przewody czujnika."),
    rect(204, 440, 800, 112, colors.card, 18, colors.border, 1),
    text(222, 474, "Zasada bezpieczeństwa", 18, colors.text, 700),
    multiline(222, 505, [
      "Alarm znika dopiero po usunięciu przyczyny i stabilizacji pomiaru.",
      "Panel nie omija blokad, histerezy ani procedur fail-safe sterownika.",
    ], 14, colors.muted, 26),
  ].join("");
}

function automationCard(x, y, title, lines) {
  return [
    rect(x, y, 386, 130, colors.card, 18, colors.border, 1),
    text(x + 18, y + 32, title, 19, colors.text, 700),
    multiline(x + 18, y + 70, lines, 14, colors.muted, 23),
    rect(x + 264, y + 68, 104, 44, colors.accent, 12),
    text(x + 316, y + 96, "Edytuj", 14, colors.text, 700, "middle"),
  ].join("");
}

function automationBody() {
  return [
    rect(204, 92, 800, 74, "#102B43", 18, colors.info, 1),
    text(222, 121, "Automatyka pozostaje w sterowniku CYD", 19, colors.info, 700),
    text(222, 148, "Edycja zapisuje ustawienia atomowo w CYD; panel można wyłączyć.", 13, colors.success, 500),
    automationCard(204, 180, "Światło główne", ["10:00–22:00", "CYKL AUTO"]),
    automationCard(618, 180, "Światło roślinne", ["10:30–20:00", "DAYBREAK"]),
    automationCard(204, 326, "Filtr", ["10:30–20:30", "Harmonogram CYD"]),
    automationCard(618, 326, "Napowietrzanie", ["ZAWSZE WYŁ.", "Czasy pozostają zapisane"]),
    automationCard(204, 468, "Temperatura", ["Cel 25.0 °C", "Histereza 0.5 °C"]),
    rect(618, 468, 386, 130, colors.card, 18, colors.border, 1),
    text(636, 500, "REWIZJA KONFIGURACJI", 13, colors.muted, 700),
    text(636, 544, "1234ABCD", 20, colors.text, 700),
    text(636, 578, "Kontrola konfliktu + pełny ACK", 13, colors.success, 600),
  ].join("");
}

function scheduleEditorOverlay() {
  return [
    `<rect x="0" y="0" width="1024" height="600" fill="#02060C" fill-opacity="0.82"/>`,
    rect(182, 60, 660, 480, colors.surface, 24, colors.border, 1),
    text(206, 98, "Światło główne", 24, colors.text, 700),
    text(206, 128, "Zapis z kontrolą rewizji; krok czasu 15 minut.", 14, colors.muted, 400),
    text(206, 180, "TRYB", 13, colors.muted, 700),
    text(386, 180, "HARMONOGRAM", 19, colors.info, 700),
    rect(698, 151, 104, 44, colors.elevated, 12),
    text(750, 179, "Zmień", 14, colors.text, 700, "middle"),
    text(206, 250, "START", 13, colors.muted, 700),
    text(466, 252, "10:00", 24, colors.text, 700),
    rect(612, 220, 58, 44, colors.elevated, 12),
    text(641, 249, "−", 21, colors.text, 700, "middle"),
    rect(682, 220, 58, 44, colors.elevated, 12),
    text(711, 249, "+", 21, colors.text, 700, "middle"),
    text(206, 312, "KONIEC", 13, colors.muted, 700),
    text(466, 314, "22:00", 24, colors.text, 700),
    rect(612, 282, 58, 44, colors.elevated, 12),
    text(641, 311, "−", 21, colors.text, 700, "middle"),
    rect(682, 282, 58, 44, colors.elevated, 12),
    text(711, 311, "+", 21, colors.text, 700, "middle"),
    text(206, 374, "PROFIL ŚWIATŁA", 13, colors.muted, 700),
    text(466, 376, "CYKL AUTO", 19, colors.info, 700),
    rect(698, 345, 104, 44, colors.elevated, 12),
    text(750, 373, "Zmień", 14, colors.text, 700, "middle"),
    rect(206, 462, 292, 54, colors.elevated, 12),
    text(352, 496, "Anuluj", 16, colors.text, 700, "middle"),
    rect(526, 462, 292, 54, colors.accent, 12),
    text(672, 496, "Zapisz w CYD", 16, colors.text, 700, "middle"),
  ].join("");
}

function temperatureEditorOverlay() {
  return [
    `<rect x="0" y="0" width="1024" height="600" fill="#02060C" fill-opacity="0.82"/>`,
    rect(182, 60, 660, 480, colors.surface, 24, colors.border, 1),
    text(206, 98, "Temperatura", 24, colors.text, 700),
    text(206, 128, "CYD waliduje zakres i zapisuje ustawienia atomowo.", 14, colors.muted, 400),
    text(206, 210, "TEMPERATURA DOCELOWA", 13, colors.muted, 700),
    text(466, 214, "25.0 °C", 24, colors.text, 700),
    rect(612, 181, 58, 44, colors.elevated, 12),
    text(641, 210, "−", 21, colors.text, 700, "middle"),
    rect(682, 181, 58, 44, colors.elevated, 12),
    text(711, 210, "+", 21, colors.text, 700, "middle"),
    text(206, 302, "HISTEREZA", 13, colors.muted, 700),
    text(466, 306, "0.5 °C", 24, colors.text, 700),
    rect(612, 273, 58, 44, colors.elevated, 12),
    text(641, 302, "−", 21, colors.text, 700, "middle"),
    rect(682, 273, 58, 44, colors.elevated, 12),
    text(711, 302, "+", 21, colors.text, 700, "middle"),
    text(206, 386, "TRYB GRZAŁKI", 13, colors.muted, 700),
    text(466, 390, "REGULACJA", 19, colors.info, 700),
    rect(698, 359, 104, 44, colors.elevated, 12),
    text(750, 387, "Zmień", 14, colors.text, 700, "middle"),
    rect(206, 462, 292, 54, colors.elevated, 12),
    text(352, 496, "Anuluj", 16, colors.text, 700, "middle"),
    rect(526, 462, 292, 54, colors.accent, 12),
    text(672, 496, "Zapisz w CYD", 16, colors.text, 700, "middle"),
  ].join("");
}

function systemBody() {
  return [
    rect(204, 92, 386, 242, colors.card, 18, colors.border, 1),
    text(222, 126, "Sterownik CYD", 20, colors.text, 700),
    multiline(222, 166, [
      "Status                         online",
      "Uptime                         184 320 s",
      "Wolna pamięć                   121 840 B",
      "Rewizja                        1234ABCD",
      "ESP-NOW                        −54 dBm",
    ], 14, colors.muted, 33),
    rect(618, 92, 386, 242, colors.card, 18, colors.border, 1),
    text(636, 126, "Panel ESP32-P4", 20, colors.text, 700),
    multiline(636, 166, [
      "Status                         online",
      "Wolna pamięć                   18.6 MB",
      "Wyświetlacz                    1024×600",
      "Interfejs                      LVGL 9.2",
      "BSP                            Waveshare 7B",
    ], 14, colors.muted, 33),
    rect(204, 350, 800, 132, colors.card, 18, colors.border, 1),
    text(222, 384, "Jasność panelu", 20, colors.text, 700),
    text(986, 384, "80%", 20, colors.text, 700, "end"),
    rect(222, 436, 764, 10, colors.elevated, 5),
    rect(222, 436, 610, 10, colors.accent, 5),
    circle(832, 441, 14, colors.text, colors.accent, 4),
    rect(204, 498, 800, 90, colors.card, 18, colors.border, 1),
    text(222, 528, "ŁĄCZNOŚĆ", 13, colors.muted, 700),
    text(222, 563, "Wi-Fi: online     MQTT: online     CYD: online", 15, colors.success, 600),
  ].join("");
}

function confirmOverlay() {
  return [
    `<rect x="0" y="0" width="1024" height="600" fill="#02060C" fill-opacity="0.82"/>`,
    rect(232, 157, 560, 286, colors.surface, 24, colors.border, 1),
    circle(282, 211, 27, colors.warningDark, colors.warning, 1),
    text(282, 220, "!", 26, colors.warning, 700, "middle"),
    text(322, 207, "Wyłączyć filtr?", 24, colors.text, 700),
    multiline(258, 278, [
      "Filtr zostanie wyłączony na 15 minut.",
      "Zabezpieczenia CYD pozostaną aktywne.",
    ], 16, colors.muted, 28),
    rect(258, 365, 238, 54, colors.elevated, 12),
    text(377, 399, "Anuluj", 16, colors.text, 700, "middle"),
    rect(528, 365, 238, 54, colors.accent, 12),
    text(647, 399, "Potwierdź", 16, colors.text, 700, "middle"),
  ].join("");
}

function recoveryBody() {
  return [
    rect(248, 128, 712, 360, colors.card, 24, colors.warning, 1),
    circle(604, 206, 42, colors.warningDark, colors.warning, 1),
    text(604, 221, "!", 38, colors.warning, 700, "middle"),
    text(604, 286, "Sterownik CYD jest offline", 28, colors.text, 700, "middle"),
    text(604, 324, "Ostatni stan pozostaje widoczny, ale sterowanie jest zablokowane.", 15, colors.muted, 400, "middle"),
    rect(324, 360, 560, 52, colors.elevated, 12),
    text(604, 392, "1. Sprawdź zasilanie stałej bramki ESP32-C6", 15, colors.text, 600, "middle"),
    rect(324, 424, 268, 48, colors.accent, 12),
    text(458, 454, "↻  Ponów połączenie", 15, colors.text, 700, "middle"),
    rect(608, 424, 276, 48, colors.elevated, 12),
    text(746, 454, "Otwórz diagnostykę", 15, colors.text, 700, "middle"),
  ].join("");
}

function bootFrame() {
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="600" viewBox="0 0 1024 600">
  <title>HMI Screen Boot Connecting</title>
  ${rect(0, 0, 1024, 600, colors.canvas, 0)}
  ${rect(460, 120, 104, 104, colors.accent, 34)}
  ${text(512, 189, "A", 42, colors.text, 700, "middle")}
  ${text(512, 300, "AquaCYD", 38, colors.text, 700, "middle")}
  ${text(512, 350, "MQTT online, oczekiwanie na telemetrię CYD…", 19, colors.muted, 400, "middle")}
  ${rect(302, 393, 420, 8, colors.elevated, 4)}
  ${rect(302, 393, 319, 8, colors.accent, 4)}
  ${text(512, 452, "ESP32-P4 • LVGL 9 • bezpieczna automatyka lokalna", 14, colors.info, 600, "middle")}
</svg>
`;
}

function foundationsFrame() {
  const swatches = Object.entries(colors)
    .slice(0, 12)
    .map(([name, value], index) => {
      const column = index % 4;
      const row = Math.floor(index / 4);
      const x = 40 + column * 242;
      const y = 110 + row * 76;
      return [
        rect(x, y, 56, 56, value, 12, colors.border, 1),
        text(x + 70, y + 24, name, 14, colors.text, 700),
        text(x + 70, y + 46, value, 12, colors.muted, 400),
      ].join("");
    })
    .join("");
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="600" viewBox="0 0 1024 600">
  <title>AquaCYD HMI Foundations</title>
  ${rect(0, 0, 1024, 600, colors.canvas, 0)}
  ${text(40, 52, "AquaCYD HMI • Foundations", 30, colors.text, 700)}
  ${text(40, 82, "1024×600 • siatka 4 px • minimalny obszar dotyku 48×48 px", 15, colors.muted, 400)}
  ${swatches}
  ${text(40, 380, "Typografia", 21, colors.text, 700)}
  ${text(40, 424, "Nagłówek ekranu 28 / Bold", 28, colors.text, 700)}
  ${text(40, 460, "Tytuł sekcji 20 / Bold", 20, colors.text, 700)}
  ${text(40, 492, "Tekst interfejsu 16 / Regular", 16, colors.muted, 400)}
  ${rect(560, 378, 420, 166, colors.card, 18, colors.border, 1)}
  ${text(584, 414, "Komponenty bazowe", 20, colors.text, 700)}
  ${chip(584, 434, 166, "SYSTEM ONLINE", colors.success, colors.successDark)}
  ${rect(770, 434, 184, 48, colors.accent, 12)}
  ${text(862, 464, "Przycisk główny", 14, colors.text, 700, "middle")}
  ${rect(584, 496, 370, 10, colors.elevated, 5)}
  ${rect(584, 496, 248, 10, colors.accent, 5)}
  ${text(584, 534, "Motion: 120 / 220 / 360 ms", 14, colors.info, 600)}
</svg>
`;
}

const files = new Map([
  ["00-foundations-components.svg", foundationsFrame()],
  ["01-boot-connecting.svg", bootFrame()],
  ["dashboard-online.svg", frame("Centrum akwarium", 0, dashboardBody(false))],
  [
    "dashboard-critical.svg",
    frame("Centrum akwarium", 0, dashboardBody(true), {
      alarmState: "ALARM KRYTYCZNY",
      alarm: true,
    }),
  ],
  ["controls-ready.svg", frame("Sterowanie czasowe", 1, controlsBody())],
  [
    "controls-confirm-filter-off.svg",
    frame("Sterowanie czasowe", 1, controlsBody(), { overlay: confirmOverlay() }),
  ],
  ["sensors-online.svg", frame("Czujniki i jakość wody", 2, sensorsBody())],
  [
    "alarms-critical.svg",
    frame("Alarmy i bezpieczeństwo", 3, alarmsBody(), {
      alarmState: "ALARM KRYTYCZNY",
      alarm: true,
    }),
  ],
  ["automation-overview.svg", frame("Automatyka CYD", 4, automationBody())],
  [
    "automation-schedule-editor.svg",
    frame("Automatyka CYD", 4, automationBody(), {
      overlay: scheduleEditorOverlay(),
    }),
  ],
  [
    "automation-temperature-editor.svg",
    frame("Automatyka CYD", 4, automationBody(), {
      overlay: temperatureEditorOverlay(),
    }),
  ],
  ["system-online.svg", frame("System i diagnostyka", 5, systemBody())],
  [
    "recovery-controller-offline.svg",
    frame("Tryb odzyskiwania", 5, recoveryBody(), {
      linkState: "CYD OFFLINE",
      alarmState: "BEZ ALARMÓW",
      syncText: "18 s temu",
    }),
  ],
]);

await mkdir(outputDirectory, { recursive: true });
for (const [name, content] of files) {
  await writeFile(resolve(outputDirectory, name), content, "utf8");
}

console.log(`Wygenerowano ${files.size} ramek HMI w ${outputDirectory}`);
