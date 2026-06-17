const tabs = document.querySelectorAll(".tab");
const settingsSubtabs = document.querySelectorAll(".settings-subtab");
const settingsSubpanels = document.querySelectorAll(".settings-subpanel");
const panels = {
  dashboard: document.querySelector("#dashboardPanel"),
  settings: document.querySelector("#settingsPanel"),
  radar: document.querySelector("#radarPanel"),
  fm225: document.querySelector("#fm225Panel"),
  rfid: document.querySelector("#rfidPanel")
};

const connectionStatus = document.querySelector("#connectionStatus");
const settingsForm = document.querySelector("#settingsForm");
const settingsMessage = document.querySelector("#settingsMessage");
const connectionLabel = document.querySelector("#connectionLabel");
const restartDeviceButton = document.querySelector("#restartDeviceButton");
const loadLogsButton = document.querySelector("#loadLogsButton");
const mp3Buttons = document.querySelectorAll(".mp3-action");
const fm225Buttons = document.querySelectorAll(".fm225-action");
const rfidButtons = document.querySelectorAll(".rfid-action");
const webStorageButtons = document.querySelectorAll(".web-storage-action");
const automationModeButtons = document.querySelectorAll(".automation-mode-option");
const manualRelayCards = document.querySelectorAll(".manual-relay-card");
const lockCards = document.querySelectorAll(".lock-card");
const lockPulseTimers = {};

function setText(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.textContent = value;
  }
}

function setValue(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.value = value;
  }
}

function setChecked(id, value) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.checked = Boolean(value);
  }
}

function setHidden(id, hidden) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.classList.toggle("hidden", hidden);
  }
}

function setCardState(id, enabled) {
  const element = document.querySelector(`#${id}`);
  if (element) {
    element.dataset.state = enabled ? "on" : "off";
    element.classList.toggle("active", enabled);
  }
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  if (days > 0) {
    return `${days}d ${hours}h`;
  }
  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }
  return `${minutes}m`;
}

function formatNumber(value, decimals, suffix) {
  return Number.isFinite(value) ? `${value.toFixed(decimals)} ${suffix}` : "--";
}

function clampNumber(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function formatPower(value) {
  if (!Number.isFinite(value)) {
    return "-- W";
  }
  const abs = Math.abs(value);
  if (abs >= 1000) {
    return `${(value / 1000).toFixed(1)} kW`;
  }
  return `${Math.round(value).toLocaleString()} W`;
}

function formatEnergy(value) {
  return Number.isFinite(value) ? `${value.toFixed(1)} kWh` : "-- kWh";
}

function flowDuration(value) {
  const watts = Math.abs(value);
  if (!Number.isFinite(watts) || watts < 20) {
    return "0s";
  }
  const seconds = clampNumber(4.6 - Math.log10(watts + 10), 0.75, 3.4);
  return `${seconds.toFixed(2)}s`;
}

function setFlowDot(id, active, value, reverse = false) {
  const dot = document.querySelector(`#${id}`);
  if (!dot) {
    return;
  }

  dot.classList.toggle("active", active);
  if (!active) {
    return;
  }

  const animation = dot.querySelector("animateMotion");
  if (!animation) {
    return;
  }

  const duration = flowDuration(value);
  const direction = reverse ? "1;0" : "0;1";
  if (dot.dataset.duration !== duration || dot.dataset.direction !== direction) {
    animation.setAttribute("dur", duration);
    animation.setAttribute("keyPoints", direction);
    animation.setAttribute("keyTimes", "0;1");
    animation.setAttribute("calcMode", "linear");
    dot.dataset.duration = duration;
    dot.dataset.direction = direction;
    if (typeof animation.beginElement === "function") {
      animation.beginElement();
    }
  }
}

function formatI2cAddress(address) {
  return address ? `0x${address.toString(16).toUpperCase().padStart(2, "0")}` : "--";
}

function formatHex24(value) {
  return value ? `0x${value.toString(16).toUpperCase().padStart(6, "0")}` : "--";
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return "--";
  }

  const units = ["B", "KB", "MB", "GB"];
  let value = bytes;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  const decimals = unitIndex === 0 || value >= 10 ? 0 : 1;
  return `${value.toFixed(decimals)} ${units[unitIndex]}`;
}

function formatState(active, activeLabel, idleLabel) {
  return active ? activeLabel : idleLabel;
}

function updateConnection(status, label) {
  connectionStatus.classList.remove("online", "offline");
  connectionStatus.classList.add(status);
  connectionLabel.textContent = label;
}

function updateMp3Status(status) {
  const file = status.mp3File || status.file || 0;
  const folder = status.mp3Folder || status.folder || 1;
  const totalFiles = status.mp3TotalFiles || status.totalFiles || 255;
  const volume = status.mp3Volume ?? status.volume;
  const initialized = Boolean(status.mp3Initialized ?? status.initialized);
  const playing = Boolean(status.mp3Playing ?? status.playing);

  setText("mp3PlayerState", initialized ? formatState(playing, "Playing", "Idle") : "Offline");
  setText("mp3CurrentTrack", `Folder ${folder}, track ${file || "--"} / ${totalFiles}`);
  if (Number.isFinite(volume)) {
    setValue("mp3LiveVolumeInput", volume);
    setText("mp3LiveVolumeValue", volume);
  }
}

function formatAge(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
    return "--";
  }
  if (milliseconds < 1000) {
    return `${milliseconds} ms`;
  }
  return `${Math.round(milliseconds / 1000)} s`;
}

function formatDistance(value) {
  return Number.isFinite(value) && value > 0 ? `${value} cm` : "-- cm";
}

function updateRadarStatus(status) {
  const online = Boolean(status.radarOnline);
  const present = Boolean(status.radarPresent);
  const moving = Boolean(status.radarMovingTarget);
  const stationary = Boolean(status.radarStationaryTarget);
  const state = status.radarState || "unknown";

  setText("radarState", online ? state.replaceAll("_", " ") : "Waiting");
  setText("radarStatus", online ? "LD2410B data received" : "UART on RX GPIO40, TX GPIO41");
  setText("radarPresence", online ? formatState(present, "Present", "Clear") : "--");
  setText("radarDashboardState", online ? formatState(present, "Motion", "Clear") : "--");
  setText(
    "radarTargetSummary",
    online ? `Detection distance ${formatDistance(status.radarDetectionDistanceCm)}` : "No radar data yet"
  );
  setText(
    "radarDashboardStatus",
    online
      ? `${state.replaceAll("_", " ")}, ${formatDistance(status.radarDetectionDistanceCm)}`
      : "LD2410B waiting for data"
  );
  setText("radarMovingDistance", formatDistance(status.radarMovingDistanceCm));
  setText("radarMovingEnergy", `Energy ${Number.isFinite(status.radarMovingEnergy) ? status.radarMovingEnergy : "--"}`);
  setText("radarStationaryDistance", formatDistance(status.radarStationaryDistanceCm));
  setText(
    "radarStationaryEnergy",
    `Energy ${Number.isFinite(status.radarStationaryEnergy) ? status.radarStationaryEnergy : "--"}`
  );
  setText("radarDetectionDistance", formatDistance(status.radarDetectionDistanceCm));
  setText("radarMovingState", formatState(moving, "Detected", "Clear"));
  setText("radarStationaryState", formatState(stationary, "Detected", "Clear"));
  setText("radarEngineeringMode", formatState(status.radarEngineeringMode, "Enabled", "Off"));
  setText("radarLastUpdate", online ? formatAge(status.radarLastUpdateAgeMs) : "--");
}

function updateTdsStatus(status) {
  const enabled = Boolean(status.tdsMonitorEnabled);
  const online = Boolean(status.tdsMonitorOnline);
  const card = document.querySelector("#tdsMonitorCard");
  const tdsGauge = document.querySelector("#tdsGauge");
  const waterGauge = document.querySelector("#tdsWaterGauge");
  setHidden("tdsMonitorCard", !enabled);
  if (!enabled) {
    return;
  }

  const ppm = status.tdsPpm;
  const waterLevel = status.tdsWaterLevelPercent;
  const ppmValue = Number.isFinite(ppm) ? clampNumber(ppm, 0, 1000) : 0;
  const waterValue = Number.isFinite(waterLevel) ? clampNumber(waterLevel, 0, 100) : 0;

  setText("tdsMonitorStatus", online ? "Live" : "Offline");
  setText("tdsPpm", Number.isFinite(ppm) ? `${Math.round(ppm)}` : "--");
  setText("tdsWaterLevel", Number.isFinite(waterLevel) ? `${Math.round(waterValue)}%` : "--%");
  setCardState("tdsMonitorCard", online);

  if (card) {
    card.style.setProperty("--tds-gauge", `${(ppmValue / 1000) * 360}deg`);
    card.style.setProperty("--tds-level", `${waterValue}%`);
  }
  if (tdsGauge) {
    if (Number.isFinite(ppm)) {
      tdsGauge.setAttribute("aria-valuenow", `${Math.round(ppmValue)}`);
      tdsGauge.setAttribute("aria-valuetext", `${Math.round(ppm)} ppm`);
    } else {
      tdsGauge.removeAttribute("aria-valuenow");
      tdsGauge.setAttribute("aria-valuetext", "TDS unavailable");
    }
  }
  if (waterGauge) {
    if (Number.isFinite(waterLevel)) {
      waterGauge.setAttribute("aria-valuenow", `${Math.round(waterValue)}`);
      waterGauge.setAttribute("aria-valuetext", `${Math.round(waterValue)} percent`);
    } else {
      waterGauge.removeAttribute("aria-valuenow");
      waterGauge.setAttribute("aria-valuetext", "Water level unavailable");
    }
  }
}

function updateInverterCard(prefix, status, label) {
  const enabled = status[`${prefix}Enabled`] !== false;
  const online = Boolean(status[`${prefix}Online`]);
  const pv = status[`${prefix}PvPowerW`];
  const grid = status[`${prefix}GridPowerW`];
  const gridVoltage = status[`${prefix}GridVoltageV`];
  const home = status[`${prefix}HomePowerW`];
  const battery = status[`${prefix}BatteryPowerW`];
  const batterySoc = status[`${prefix}BatterySoc`];
  const eventText = status[`${prefix}LastEvent`] || "Waiting for inverter data";

  setText(`${prefix}InverterStatus`, enabled ? (online ? "Live" : eventText) : "Disabled");
  setText(`${prefix}PvPower`, formatPower(pv));
  setText(`${prefix}GridVoltage`, Number.isFinite(gridVoltage) ? `${Math.round(gridVoltage)} V` : "-- V");
  setText(`${prefix}GridPower`, formatPower(grid));
  setText(`${prefix}HomePower`, formatPower(home));
  setText(`${prefix}HomeImport`, formatPower(home));
  setText(`${prefix}BatterySoc`, Number.isFinite(batterySoc) ? `${Math.round(batterySoc)} %` : "-- %");
  setText(`${prefix}BatteryPower`, formatPower(battery));
  setCardState(`${prefix}InverterCard`, enabled && online);

  const active = enabled && online;
  const pvActive = active && Number.isFinite(pv) && pv > 20;
  const gridImport = active && Number.isFinite(grid) && grid > 20;
  const gridExport = active && Number.isFinite(grid) && grid < -20;
  const batteryCharging = active && Number.isFinite(battery) && battery < -20;
  const batteryDischarging = active && Number.isFinite(battery) && battery > 20;

  setFlowDot(`${prefix}PvInverterDot`, pvActive, pv);
  setFlowDot(`${prefix}GridInverterDot`, gridImport || gridExport, Math.abs(grid), gridExport);
  setFlowDot(`${prefix}InverterHomeDot`, active && Number.isFinite(home) && home > 20, home);
  setFlowDot(`${prefix}BatteryInverterDot`, batteryCharging || batteryDischarging, Math.abs(battery), batteryCharging);
}

function updateInverterStatus(status) {
  updateInverterCard("nitrox", status, "Nitrox");
  updateInverterCard("solax", status, "Solax");
  updateGrowattCard(status);
}

function updateGrowattCard(status) {
  const enabled = status.growattEnabled !== false;
  const online = Boolean(status.growattOnline);
  const eventText = status.growattLastEvent || "Waiting for Growatt data";

  setText("growattInverterStatus", enabled ? (online ? "Live" : eventText) : "Disabled");
  setText("growattPvPower", formatPower(status.growattPvPowerW));
  setText("growattGridPower", formatPower(status.growattGridPowerW));
  setText("growattGridVoltage", online ? "On Grid" : "--");
  setText("growattTodayEnergy", formatEnergy(status.growattTodayYieldKwh));
  setText("growattTotalEnergy", formatEnergy(status.growattTotalEnergyKwh));
  setCardState("growattInverterCard", enabled && online);
}

function renderFm225UserTable(users) {
  const body = document.querySelector("#fm225UserTableBody");
  if (!body) {
    return;
  }

  body.textContent = "";
  if (!users.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.textContent = "No users loaded";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  users.forEach((user, index) => {
    const row = document.createElement("tr");
    const numberCell = document.createElement("td");
    const idCell = document.createElement("td");
    const nameCell = document.createElement("td");
    const actionCell = document.createElement("td");
    const getButton = document.createElement("button");
    const deleteButton = document.createElement("button");
    const actions = document.createElement("div");

    numberCell.textContent = `${index + 1}`;
    idCell.textContent = `${user.id}`;
    nameCell.textContent = user.name || "N/A";
    actions.className = "table-action-row";

    getButton.className = "icon-action";
    getButton.type = "button";
    getButton.textContent = "Get";
    getButton.addEventListener("click", () => {
      runFm225Action("get-user", user.id);
    });

    deleteButton.className = "icon-action danger-action";
    deleteButton.type = "button";
    deleteButton.textContent = "Delete";
    deleteButton.addEventListener("click", () => {
      runFm225Action("delete-user", user.id);
    });

    actions.append(getButton, deleteButton);
    actionCell.appendChild(actions);
    row.append(numberCell, idCell, nameCell, actionCell);
    body.appendChild(row);
  });
}

function updateFm225Status(status) {
  const responding = Boolean(status.fm225Responding);
  const statusText = status.fm225Status || "UART ready";
  const eventText = status.fm225LastEvent || "Waiting for FM225 response";
  const radarText = status.fm225RadarPresenceEnabled ? status.fm225RadarStatus || "Radar presence waiting" : "";
  const users = Array.isArray(status.fm225Users) ? status.fm225Users : [];
  const recognizedName = status.fm225LastRecognizedName || "";
  const recognizedId = status.fm225LastRecognizedUserId || 0;
  const verifySummary = status.fm225VerifySummary || "Verification not started";

  setText("fm225DashboardState", responding ? statusText : "UART");
  setText("fm225DashboardStatus", radarText || (responding ? eventText : "FM225 waiting for response"));
  setText("fm225State", responding ? statusText : "UART Ready");
  setText(
    "fm225Status",
    `RX GPIO38, TX GPIO37${status.fm225Version ? `, ${status.fm225Version}` : ""}${
      status.fm225SerialNumber ? `, SN ${status.fm225SerialNumber}` : ""
    }`
  );
  setText("fm225Recognized", recognizedId ? `ID ${recognizedId}` : "--");
  setText("fm225VerifyStatus", radarText || verifySummary);
  setText("fm225FaceState", status.fm225FaceUpdated ? status.fm225FaceState : "--");
  setText("fm225FaceVerifyResult", radarText || verifySummary);
  setText("fm225FacePose", `Yaw ${status.fm225FaceYaw || 0}, pitch ${status.fm225FacePitch || 0}, roll ${status.fm225FaceRoll || 0}`);
  setText("fm225UserCount", `${status.fm225UserCount || 0}`);
  renderFm225UserTable(users);
  setText("fm225EnrollState", Number.isFinite(status.fm225LastEnrollResult) && status.fm225LastEnrollResult === 0 ? "Complete" : "Ready");
  setText("fm225LastEvent", radarText || eventText);
}

function renderTagTable(tags) {
  const body = document.querySelector("#rfidTagTableBody");
  if (!body) {
    return;
  }

  body.textContent = "";
  if (!tags.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.textContent = "No authorized tags";
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  tags.forEach((tag, index) => {
    const row = document.createElement("tr");
    const numberCell = document.createElement("td");
    const tagCell = document.createElement("td");
    const addedCell = document.createElement("td");
    const actionCell = document.createElement("td");
    const deleteButton = document.createElement("button");

    numberCell.textContent = `${index + 1}`;
    tagCell.textContent = tag;
    addedCell.textContent = "N/A";
    deleteButton.className = "icon-action danger-action rfid-delete-tag";
    deleteButton.type = "button";
    deleteButton.textContent = "Delete";
    deleteButton.dataset.tag = tag;
    deleteButton.addEventListener("click", () => {
      runRfidAction("delete", tag);
    });

    actionCell.appendChild(deleteButton);
    row.append(numberCell, tagCell, addedCell, actionCell);
    body.appendChild(row);
  });
}

function updateRfidAddModeUI(active, pendingTags, remainingMs) {
  setHidden("rfidAddTagButton", active);
  setHidden("rfidSaveTagsButton", !active);
  setHidden("rfidCancelAddButton", !active);
  setHidden("rfidPendingPanel", !active);

  if (active) {
    const seconds = Math.ceil((remainingMs || 0) / 1000);
    setText("rfidAddModeMessage", `Scan RFID tags to add${seconds > 0 ? ` (${seconds}s)` : ""}`);
    setText("rfidPendingTagList", pendingTags.length ? pendingTags.join(", ") : "No pending tags");
  }
}

function updateRfidStatus(status) {
  const initialized = Boolean(status.rfidInitialized);
  const tagPresent = Boolean(status.rfidTagPresent);
  const authorized = Boolean(status.rfidLastAuthorized);
  const tags = Array.isArray(status.rfidTags) ? status.rfidTags : [];
  const pendingTags = Array.isArray(status.rfidPendingTags) ? status.rfidPendingTags : [];
  const addModeActive = Boolean(status.rfidAddModeActive);
  const lastTag = status.rfidLastTag || "";
  const state = !initialized ? "Offline" : addModeActive ? "Add Mode" : tagPresent ? formatState(authorized, "Allowed", "Denied") : "Ready";
  const event = status.rfidLastEvent || "No tag scanned";

  setText("rfidDashboardState", state);
  setText("rfidDashboardStatus", addModeActive ? "Scan RFID tags to add" : lastTag ? `${lastTag} - ${event}` : "RDM6300 on GPIO17");
  setText("rfidState", state);
  setText("rfidStatus", addModeActive ? "Add mode active: scan tags" : initialized ? "RX GPIO17, 9600 baud" : "RFID reader offline");
  setText("rfidLastTag", lastTag || "--");
  setText("rfidLastEvent", event);
  setText("rfidTagCount", `${status.rfidTagCount || 0}`);
  setText("rfidReadCount", `Reads ${status.rfidTotalReads || 0}`);
  setText("rfidActionState", addModeActive ? "Add mode active: scan tags" : event || "Ready");
  updateRfidAddModeUI(addModeActive, pendingTags, status.rfidAddModeRemainingMs);
  renderTagTable(tags);
}

function modeLabel(mode) {
  return mode === "automatic" ? "Automatic" : "Manual";
}

function relayStateLabel(enabled) {
  return enabled ? "ON" : "OFF";
}

function doorStatusLabel(closed) {
  return closed ? "Door closed" : "Door opened";
}

function formatDurationMs(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
    return "0s";
  }
  if (milliseconds < 1000) {
    return `${milliseconds}ms`;
  }
  return `${Math.round(milliseconds / 1000)}s`;
}

function setRelayModeControl(device, mode) {
  const normalizedMode = mode || "manual";
  const card = document.querySelector(`.manual-relay-card[data-device="${device}"]`);
  if (card) {
    card.dataset.mode = normalizedMode;
    card.classList.toggle("manual-mode", normalizedMode === "manual");
    card.classList.toggle("automatic-mode", normalizedMode === "automatic");
  }

  document.querySelectorAll(`.automation-mode-option[data-device="${device}"]`).forEach((button) => {
    const selected = button.dataset.mode === normalizedMode;
    button.classList.toggle("active", selected);
    button.setAttribute("aria-pressed", selected ? "true" : "false");
  });
}

function updateRelayCardUI(device, cardId, enabled, mode) {
  setCardState(cardId, enabled);
  setRelayModeControl(device, mode);
}

function setLockCardPulsing(cardId, pulsing, remainingMs = 0) {
  const card = document.querySelector(`#${cardId}`);
  if (!card) {
    return;
  }

  if (lockPulseTimers[cardId]) {
    clearTimeout(lockPulseTimers[cardId]);
    lockPulseTimers[cardId] = null;
  }

  card.classList.toggle("pulsing", pulsing);
  card.classList.toggle("busy", pulsing);

  if (pulsing && remainingMs > 0) {
    lockPulseTimers[cardId] = setTimeout(() => {
      card.classList.remove("pulsing", "busy");
      refreshStatus();
    }, remainingMs);
  }
}

function updateLockCardUI(cardId, stateId, statusId, active, remainingMs, pulseMs) {
  const card = document.querySelector(`#${cardId}`);
  if (card) {
    card.dataset.pulseMs = Number.isFinite(pulseMs) && pulseMs > 0 ? `${pulseMs}` : card.dataset.pulseMs || "1000";
  }

  setText(stateId, active ? "UNLOCKING" : "OFF");
  setText(statusId, active ? `Unlock pulse ${formatDurationMs(remainingMs)} remaining` : "Momentary unlock ready");
  setCardState(cardId, active);
  setLockCardPulsing(cardId, active, remainingMs);
}

function setLockCardPendingText(cardId) {
  if (cardId === "doorLockCard") {
    setText("doorLockState", "UNLOCKING");
    setText("doorLockStatus", "Unlock pulse active...");
  } else if (cardId === "garageLockCard") {
    setText("garageLockState", "UNLOCKING");
    setText("garageLockStatus", "Unlock pulse active...");
  }
}

function updateAutomationStatus(status) {
  updateLockCardUI(
    "doorLockCard",
    "doorLockState",
    "doorLockStatus",
    Boolean(status.doorLockOn || status.doorLockRemainingMs > 0),
    status.doorLockRemainingMs,
    status.doorLockPulseMs
  );
  setText("doorReedStatus", doorStatusLabel(Boolean(status.doorReedClosed)));

  updateLockCardUI(
    "garageLockCard",
    "garageLockState",
    "garageLockStatus",
    Boolean(status.garageLockOn || status.garageLockRemainingMs > 0),
    status.garageLockRemainingMs,
    status.garageLockPulseMs
  );
  setText("garageReedStatus", doorStatusLabel(Boolean(status.garageReedClosed)));

  setText("outdoorLightState", relayStateLabel(status.outdoorLightOn));
  setText(
    "outdoorLightStatus",
    `${modeLabel(status.outdoorLightMode)}, lux ${Number.isFinite(status.lux) ? status.lux.toFixed(1) : "--"}`
  );
  updateRelayCardUI("outdoorLight", "outdoorLightCard", status.outdoorLightOn, status.outdoorLightMode);

  setText("exhaustFanState", relayStateLabel(status.exhaustFanOn));
  setText(
    "exhaustFanStatus",
    `${modeLabel(status.exhaustFanMode)}, temp ${Number.isFinite(status.temperature) ? status.temperature.toFixed(1) : "--"} C`
  );
  updateRelayCardUI("exhaustFan", "exhaustFanCard", status.exhaustFanOn, status.exhaustFanMode);

  setText("motionLight1State", relayStateLabel(status.motionLight1On));
  setText(
    "motionLight1Status",
    `${modeLabel(status.motionLight1Mode)}, ${status.motion1Active ? "motion" : "quiet"}, hold ${formatDurationMs(
      status.motionLight1RemainingMs
    )}`
  );
  updateRelayCardUI("motionLight1", "motionLight1Card", status.motionLight1On, status.motionLight1Mode);

  setText("motionLight2State", relayStateLabel(status.motionLight2On));
  setText(
    "motionLight2Status",
    `${modeLabel(status.motionLight2Mode)}, ${status.motion2Active ? "motion" : "quiet"}, hold ${formatDurationMs(
      status.motionLight2RemainingMs
    )}`
  );
  updateRelayCardUI("motionLight2", "motionLight2Card", status.motionLight2On, status.motionLight2Mode);
}

async function refreshStatus() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Status API failed: ${response.status}`);
    }

    const status = await response.json();
    const climateCard = document.querySelector("#climateSensorCard");
    const outdoorClimateCard = document.querySelector("#outdoorClimateCard");
    const climateGauge = document.querySelector("#climateGauge");
    const outdoorTempGauge = document.querySelector("#outdoorTempGauge");
    const lightGauge = document.querySelector("#lightGauge");
    const climateOnline = Boolean(status.sht3xOnline);
    const outdoorClimateOnline = Boolean(status.ds18b20Online || (status.bh1750Online && status.lightValid));
    const humidityValue = Number.isFinite(status.humidity) ? clampNumber(status.humidity, 0, 100) : 0;
    const lightValue = Number.isFinite(status.lux) ? clampNumber(status.lux, 0, 1000) : 0;
    const outdoorTempValue = Number.isFinite(status.ds18b20Temperature) ? clampNumber(status.ds18b20Temperature, -10, 60) : -10;

    setText("climateStatus", climateOnline ? "Live" : "Offline");
    setText("outdoorClimateStatus", outdoorClimateOnline ? "Live" : "Offline");
    setText("temperature", formatNumber(status.temperature, 1, "C"));
    setText("humidity", Number.isFinite(status.humidity) ? `${Math.round(humidityValue)}% RH` : "--% RH");
    setText("lux", formatNumber(status.lux, 1, "lx"));
    const smokeDetected = Boolean(status.mq135AlarmActive ?? status.mq135DigitalActive);
    const smokeThreshold = Number.isFinite(status.mq135AlarmThresholdRaw) ? status.mq135AlarmThresholdRaw : "--";
    setText("mq135Status", smokeDetected ? "Smoke detected" : "Clear");
    setText("mq135State", formatState(smokeDetected, "Alert", "Clear"));
    setText("mq135Reading", `Raw ${status.mq135AnalogRaw} / ${smokeThreshold} (${formatNumber(status.mq135AnalogVoltage, 2, "V")})`);
    setCardState("mq135Card", smokeDetected);
    setText("ds18b20Temperature", formatNumber(status.ds18b20Temperature, 1, "C"));
    setCardState("climateSensorCard", climateOnline);
    setCardState("outdoorClimateCard", outdoorClimateOnline);
    if (climateCard) {
      climateCard.style.setProperty("--climate-humidity", `${(humidityValue / 100) * 360}deg`);
    }
    if (outdoorClimateCard) {
      outdoorClimateCard.style.setProperty("--climate-light", `${(lightValue / 1000) * 100}%`);
      outdoorClimateCard.style.setProperty("--outdoor-temp", `${((outdoorTempValue + 10) / 70) * 360}deg`);
    }
    if (climateGauge) {
      if (Number.isFinite(status.humidity)) {
        climateGauge.setAttribute("aria-valuenow", `${Math.round(humidityValue)}`);
        climateGauge.setAttribute("aria-valuetext", `${Math.round(humidityValue)} percent relative humidity`);
      } else {
        climateGauge.removeAttribute("aria-valuenow");
        climateGauge.setAttribute("aria-valuetext", "Humidity unavailable");
      }
    }
    if (outdoorTempGauge) {
      if (Number.isFinite(status.ds18b20Temperature)) {
        outdoorTempGauge.setAttribute("aria-valuenow", `${status.ds18b20Temperature.toFixed(1)}`);
        outdoorTempGauge.setAttribute("aria-valuetext", `${status.ds18b20Temperature.toFixed(1)} degrees Celsius`);
      } else {
        outdoorTempGauge.removeAttribute("aria-valuenow");
        outdoorTempGauge.setAttribute("aria-valuetext", "Outdoor temperature unavailable");
      }
    }
    if (lightGauge) {
      if (Number.isFinite(status.lux)) {
        lightGauge.setAttribute("aria-valuenow", `${Math.round(lightValue)}`);
        lightGauge.setAttribute("aria-valuetext", `${status.lux.toFixed(1)} lux`);
      } else {
        lightGauge.removeAttribute("aria-valuenow");
        lightGauge.setAttribute("aria-valuetext", "Outdoor light unavailable");
      }
    }
    setText("storageSettingsState", status.storageOnline ? "Online" : "Offline");
    setText("storageSettingsAvailable", formatBytes(status.storageAvailableBytes));
    setText("storageSettingsTotal", formatBytes(status.storageTotalBytes));
    setText("storageSettingsJedec", formatHex24(status.storageJedecId));
    setText("webStorageSettingsState", status.webStorageReady ? `${status.webStorageFileCount || 0} files` : "Not seeded");
    setText("webStorageSettingsEvent", status.webStorageLastEvent || "--");
    updateMp3Status(status);
    updateRadarStatus(status);
    updateTdsStatus(status);
    updateInverterStatus(status);
    updateFm225Status(status);
    updateRfidStatus(status);
    updateAutomationStatus(status);
    setText("deviceName", status.deviceName);
    setText("ipAddress", status.ip);
    setText("wifiRssi", `${status.rssi} dBm`);
    setText("freeHeap", `${Math.round(status.freeHeap / 1024)} KB`);
    setText("uptime", formatUptime(status.uptimeSeconds));
    updateConnection(status.wifiConnected ? "online" : "offline", status.wifiConnected ? "Online" : "WiFi offline");
  } catch (error) {
    updateConnection("offline", "API offline");
  }
}

async function postAutomation(endpoint, payload = new URLSearchParams()) {
  automationModeButtons.forEach((button) => {
    button.disabled = true;
  });
  manualRelayCards.forEach((card) => {
    card.classList.add("busy");
  });

  try {
    const response = await fetch(`/api/automation/${endpoint}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: payload,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`Automation action failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    updateConnection("offline", "Automation failed");
  } finally {
    automationModeButtons.forEach((button) => {
      button.disabled = false;
    });
    manualRelayCards.forEach((card) => {
      card.classList.remove("busy");
    });
  }
}

async function postLockPulse(card) {
  if (card.classList.contains("busy")) {
    return;
  }

  const pulseMs = Number.parseInt(card.dataset.pulseMs || "1000", 10);
  setLockCardPulsing(card.id, true, Number.isFinite(pulseMs) ? pulseMs : 1000);
  setLockCardPendingText(card.id);

  try {
    const response = await fetch(`/api/automation/${card.dataset.lockEndpoint}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`Lock pulse failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    setLockCardPulsing(card.id, false);
    updateConnection("offline", "Lock pulse failed");
  }
}

function automationPayload(device) {
  const payload = new URLSearchParams();
  payload.set("device", device);
  return payload;
}

async function runRfidAction(action, tag = "") {
  if (action === "clear" && !window.confirm("Clear all RFID tags?")) {
    return;
  }

  const payload = new URLSearchParams();
  if (tag) {
    payload.set("tag", tag);
  }

  const endpoints = {
    "start-add": "add-mode/start",
    "cancel-add": "add-mode/cancel",
    "save-pending": "add-mode/save"
  };
  const endpoint = endpoints[action] || action;

  rfidButtons.forEach((button) => {
    button.disabled = true;
  });

  try {
    const response = await fetch(`/api/rfid/${endpoint}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: payload,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`RFID action failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    setText("rfidActionState", "Command failed");
  } finally {
    rfidButtons.forEach((button) => {
      button.disabled = false;
    });
  }
}

async function runWebStorageAction(action) {
  if (action !== "seed") {
    return;
  }

  webStorageButtons.forEach((button) => {
    button.disabled = true;
  });
  setText("webStorageSettingsEvent", "Seeding...");

  try {
    const response = await fetch("/api/web-storage/seed", {
      method: "POST",
      cache: "no-store"
    });

    const result = await response.json();
    if (!response.ok || !result.ok) {
      throw new Error(result.lastEvent || `Seed failed: ${response.status}`);
    }

    setText("webStorageSettingsState", `${result.fileCount || 0} files`);
    setText("webStorageSettingsEvent", result.lastEvent || "Seeded");
    await refreshStatus();
  } catch (error) {
    setText("webStorageSettingsEvent", "Seed failed");
  } finally {
    webStorageButtons.forEach((button) => {
      button.disabled = false;
    });
  }
}

function addFm225CommonEnrollPayload(payload) {
  payload.set("name", document.querySelector("#fm225EnrollName").value || "User");
  payload.set("direction", document.querySelector("#fm225EnrollDirection").value);
  payload.set("timeoutSec", document.querySelector("#fm225EnrollTimeout").value);
  payload.set("admin", document.querySelector("#fm225EnrollAdmin").checked ? "true" : "false");
}

function fm225PayloadFor(action, userId = null) {
  const payload = new URLSearchParams();

  if (action === "verify") {
    payload.set("timeoutSec", document.querySelector("#fm225VerifyTimeout").value);
  } else if (action === "get-user" || action === "delete-user") {
    payload.set("userId", userId === null ? "0" : `${userId}`);
  } else if (action.startsWith("enroll-")) {
    addFm225CommonEnrollPayload(payload);
    if (action === "enroll-integrated") {
      payload.set("enrollType", "0");
      payload.set("duplicateMode", "1");
    }
  } else if (action === "demo-on" || action === "demo-off") {
    payload.set("enabled", action === "demo-on" ? "true" : "false");
  }

  return payload;
}

function fm225EndpointFor(action) {
  const endpoints = {
    "demo-on": "demo",
    "demo-off": "demo"
  };
  return endpoints[action] || action;
}

async function runFm225Action(action, userId = null) {
  fm225Buttons.forEach((button) => {
    button.disabled = true;
  });
  if (action === "verify") {
    setText("fm225Recognized", "--");
    setText("fm225VerifyStatus", "Verifying face...");
    setText("fm225FaceVerifyResult", "Verifying face...");
    setText("fm225LastEvent", "Verifying face...");
  }

  try {
    const response = await fetch(`/api/fm225/${fm225EndpointFor(action)}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: fm225PayloadFor(action, userId),
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`FM225 action failed: ${response.status}`);
    }

    await refreshStatus();
  } catch (error) {
    setText("fm225LastEvent", "Command failed");
    setText("fm225DashboardStatus", "FM225 command failed");
  } finally {
    fm225Buttons.forEach((button) => {
      button.disabled = false;
    });
  }
}

async function runMp3Action(action) {
  mp3Buttons.forEach((button) => {
    button.disabled = true;
  });

  try {
    const body = new URLSearchParams();
    if (action === "file") {
      const trackInput = document.querySelector("#mp3TrackInput");
      body.set("track", trackInput ? trackInput.value : "1");
    }
    const response = await fetch(`/api/mp3/${action}`, {
      method: "POST",
      headers: action === "file" ? { "Content-Type": "application/x-www-form-urlencoded" } : undefined,
      body: action === "file" ? body : undefined,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`MP3 action failed: ${response.status}`);
    }

    updateMp3Status(await response.json());
    await refreshStatus();
  } catch (error) {
    setText("mp3PlayerState", "Command failed");
  } finally {
    mp3Buttons.forEach((button) => {
      button.disabled = false;
    });
  }
}

async function setMp3LiveVolume(volume) {
  setText("mp3LiveVolumeValue", volume);
  setValue("mp3VolumeInput", volume);

  const payload = new URLSearchParams();
  payload.set("volume", volume);

  try {
    const response = await fetch("/api/mp3/volume", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: payload,
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`MP3 volume failed: ${response.status}`);
    }

    updateMp3Status(await response.json());
  } catch (error) {
    setText("mp3PlayerState", "Volume failed");
  }
}

function applySettings(settings) {
  setValue("doorLockPulseMsInput", settings.doorLockPulseMs);
  setValue("garageLockPulseMsInput", settings.garageLockPulseMs);
  setValue("outdoorLightOnBelowLuxInput", settings.outdoorLightOnBelowLux);
  setValue("outdoorLightOffAboveLuxInput", settings.outdoorLightOffAboveLux);
  setValue("exhaustFanOnAboveTemperatureInput", settings.exhaustFanOnAboveTemperature);
  setValue("exhaustFanOffBelowTemperatureInput", settings.exhaustFanOffBelowTemperature);
  setValue("motionLight1DurationMsInput", settings.motionLight1DurationMs);
  setValue("motionLight2DurationMsInput", settings.motionLight2DurationMs);
  setChecked("rfidDoorUnlockEnabledInput", settings.rfidDoorUnlockEnabled);
  setValue("mp3VolumeInput", settings.mp3Volume ?? 25);
  setValue("mp3LiveVolumeInput", settings.mp3Volume ?? 25);
  setText("mp3LiveVolumeValue", settings.mp3Volume ?? 25);
  setChecked("mp3StartupSoundEnabledInput", settings.mp3StartupSoundEnabled);
  setValue("mp3StartupTrackInput", settings.mp3StartupTrack || 1);
  setChecked("mp3SmokeAlarmEnabledInput", settings.mp3SmokeAlarmEnabled);
  setValue("mp3SmokeAlarmTrackInput", settings.mp3SmokeAlarmTrack || 2);
  setValue("mp3SmokeAlarmThresholdRawInput", settings.mp3SmokeAlarmThresholdRaw ?? 2000);
  setChecked("tdsMonitorEnabledInput", settings.tdsMonitorEnabled);
  setValue("tdsMonitorAddressInput", settings.tdsMonitorAddress || "http://tds.local/api/tds");
  setChecked("solaxEnabledInput", settings.solaxEnabled);
  setValue("solaxAddressInput", settings.solaxAddress || "http://192.168.100.23/");
  setValue("solaxPasswordInput", settings.solaxPassword || "");
  setValue("solaxIntervalSecondsInput", Math.round((settings.solaxIntervalMs || 10000) / 1000));
  setChecked("nitroxEnabledInput", settings.nitroxEnabled);
  setValue("nitroxHostInput", settings.nitroxHost || "192.168.100.121");
  setValue("nitroxPortInput", settings.nitroxPort || 8899);
  setValue("nitroxLoggerSerialInput", settings.nitroxLoggerSerial || 1732083940);
  setValue("nitroxSlaveIdInput", settings.nitroxSlaveId || 1);
  setValue("nitroxIntervalSecondsInput", Math.round((settings.nitroxIntervalMs || 10000) / 1000));
  setChecked("growattEnabledInput", settings.growattEnabled);
  setValue("growattBaseUrlInput", settings.growattBaseUrl || "https://openapi.growatt.com/v1/");
  setValue("growattTokenInput", settings.growattToken || "");
  setValue("growattPlantIdInput", settings.growattPlantId || 0);
  setValue("growattIntervalSecondsInput", Math.round((settings.growattIntervalMs || 300000) / 1000));
  setValue("wifiSsidInput", settings.wifiSsid || "");
  setValue("wifiPasswordInput", "");
  setValue("mdnsHostnameInput", settings.mdnsHostname || "home-automation");
  setChecked("otaEnabledInput", settings.otaEnabled);
  setChecked("loginAuthEnabledInput", settings.loginAuthEnabled);
  setValue("loginUsernameInput", settings.loginUsername || "admin");
  setValue("loginPasswordInput", "");
  setChecked("logRfidEnabledInput", settings.logRfidEnabled);
  setChecked("logFm225EnabledInput", settings.logFm225Enabled);
  setChecked("logDoorReedEnabledInput", settings.logDoorReedEnabled);
  setChecked("logGarageReedEnabledInput", settings.logGarageReedEnabled);
  setChecked("logDoorUnlockEnabledInput", settings.logDoorUnlockEnabled);
  setChecked("logGarageUnlockEnabledInput", settings.logGarageUnlockEnabled);
  setChecked("fm225RadarPresenceEnabledInput", settings.fm225RadarPresenceEnabled);
  setValue("fm225RadarMinDistanceCmInput", settings.fm225RadarMinDistanceCm);
  setValue("fm225RadarMinEnergyInput", settings.fm225RadarMinEnergy);
  setValue("outdoorLightModeInput", settings.outdoorLightMode || "manual");
  setValue("exhaustFanModeInput", settings.exhaustFanMode || "manual");
  setValue("motionLight1ModeInput", settings.motionLight1Mode || "manual");
  setValue("motionLight2ModeInput", settings.motionLight2Mode || "manual");
}

async function loadSettings() {
  const response = await fetch("/api/settings", { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Settings API failed: ${response.status}`);
  }
  applySettings(await response.json());
}

function formToPayload(form) {
  const payload = new URLSearchParams();
  payload.set("doorLockPulseMs", form.doorLockPulseMs.value);
  payload.set("garageLockPulseMs", form.garageLockPulseMs.value);
  payload.set("outdoorLightOnBelowLux", form.outdoorLightOnBelowLux.value);
  payload.set("outdoorLightOffAboveLux", form.outdoorLightOffAboveLux.value);
  payload.set("exhaustFanOnAboveTemperature", form.exhaustFanOnAboveTemperature.value);
  payload.set("exhaustFanOffBelowTemperature", form.exhaustFanOffBelowTemperature.value);
  payload.set("motionLight1DurationMs", form.motionLight1DurationMs.value);
  payload.set("motionLight2DurationMs", form.motionLight2DurationMs.value);
  payload.set("rfidDoorUnlockEnabled", form.rfidDoorUnlockEnabled.checked ? "true" : "false");
  payload.set("mp3Volume", form.mp3Volume.value);
  payload.set("mp3StartupSoundEnabled", form.mp3StartupSoundEnabled.checked ? "true" : "false");
  payload.set("mp3StartupTrack", form.mp3StartupTrack.value);
  payload.set("mp3SmokeAlarmEnabled", form.mp3SmokeAlarmEnabled.checked ? "true" : "false");
  payload.set("mp3SmokeAlarmTrack", form.mp3SmokeAlarmTrack.value);
  payload.set("mp3SmokeAlarmThresholdRaw", form.mp3SmokeAlarmThresholdRaw.value);
  payload.set("tdsMonitorEnabled", form.tdsMonitorEnabled.checked ? "true" : "false");
  payload.set("tdsMonitorAddress", form.tdsMonitorAddress.value);
  payload.set("solaxEnabled", form.solaxEnabled.checked ? "true" : "false");
  payload.set("solaxAddress", form.solaxAddress.value);
  payload.set("solaxPassword", form.solaxPassword.value);
  payload.set("solaxIntervalSeconds", form.solaxIntervalSeconds.value);
  payload.set("nitroxEnabled", form.nitroxEnabled.checked ? "true" : "false");
  payload.set("nitroxHost", form.nitroxHost.value);
  payload.set("nitroxPort", form.nitroxPort.value);
  payload.set("nitroxLoggerSerial", form.nitroxLoggerSerial.value);
  payload.set("nitroxSlaveId", form.nitroxSlaveId.value);
  payload.set("nitroxIntervalSeconds", form.nitroxIntervalSeconds.value);
  payload.set("growattEnabled", form.growattEnabled.checked ? "true" : "false");
  payload.set("growattBaseUrl", form.growattBaseUrl.value);
  payload.set("growattToken", form.growattToken.value);
  payload.set("growattPlantId", form.growattPlantId.value);
  payload.set("growattIntervalSeconds", form.growattIntervalSeconds.value);
  payload.set("wifiSsid", form.wifiSsid.value);
  payload.set("wifiPassword", form.wifiPassword.value);
  payload.set("mdnsHostname", form.mdnsHostname.value);
  payload.set("otaEnabled", form.otaEnabled.checked ? "true" : "false");
  payload.set("loginAuthEnabled", form.loginAuthEnabled.checked ? "true" : "false");
  payload.set("loginUsername", form.loginUsername.value);
  payload.set("loginPassword", form.loginPassword.value);
  payload.set("logRfidEnabled", form.logRfidEnabled.checked ? "true" : "false");
  payload.set("logFm225Enabled", form.logFm225Enabled.checked ? "true" : "false");
  payload.set("logDoorReedEnabled", form.logDoorReedEnabled.checked ? "true" : "false");
  payload.set("logGarageReedEnabled", form.logGarageReedEnabled.checked ? "true" : "false");
  payload.set("logDoorUnlockEnabled", form.logDoorUnlockEnabled.checked ? "true" : "false");
  payload.set("logGarageUnlockEnabled", form.logGarageUnlockEnabled.checked ? "true" : "false");
  payload.set("fm225RadarPresenceEnabled", form.fm225RadarPresenceEnabled.checked ? "true" : "false");
  payload.set("fm225RadarMinDistanceCm", form.fm225RadarMinDistanceCm.value);
  payload.set("fm225RadarMinEnergy", form.fm225RadarMinEnergy.value);
  payload.set("outdoorLightMode", form.outdoorLightMode.value);
  payload.set("exhaustFanMode", form.exhaustFanMode.value);
  payload.set("motionLight1Mode", form.motionLight1Mode.value);
  payload.set("motionLight2Mode", form.motionLight2Mode.value);
  return payload;
}

function renderLogs(logs) {
  const list = document.querySelector("#logsList");
  if (!list) {
    return;
  }

  list.textContent = "";
  if (!Array.isArray(logs) || logs.length === 0) {
    list.textContent = "No logs saved.";
    return;
  }

  logs.slice().reverse().forEach((log) => {
    const row = document.createElement("div");
    row.className = "log-entry";

    const time = document.createElement("span");
    time.textContent = formatUptime(Number(log.uptimeSeconds) || 0);

    const category = document.createElement("strong");
    category.textContent = String(log.category || "event").replaceAll("_", " ");

    const message = document.createElement("div");
    message.textContent = log.message || "";

    row.append(time, category, message);
    list.append(row);
  });
}

async function loadEventLogs() {
  const list = document.querySelector("#logsList");
  if (list) {
    list.textContent = "Loading logs...";
  }

  const response = await fetch("/api/logs", { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Logs API failed: ${response.status}`);
  }

  const payload = await response.json();
  renderLogs(payload.logs);
}

tabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    const selectedTab = tab.dataset.tab;

    tabs.forEach((item) => item.classList.toggle("active", item === tab));
    Object.entries(panels).forEach(([name, panel]) => {
      panel.classList.toggle("active", name === selectedTab);
    });
  });
});

settingsSubtabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    const selectedTab = tab.dataset.settingsTab;

    settingsSubtabs.forEach((item) => item.classList.toggle("active", item === tab));
    settingsSubpanels.forEach((panel) => {
      panel.classList.toggle("active", panel.dataset.settingsPanel === selectedTab);
    });
  });
});

if (restartDeviceButton) {
  restartDeviceButton.addEventListener("click", async () => {
    if (!window.confirm("Restart controller now?")) {
      return;
    }

    restartDeviceButton.disabled = true;
    settingsMessage.textContent = "Restarting...";

    try {
      await fetch("/api/restart", {
        method: "POST",
        cache: "no-store"
      });
    } catch (error) {
      // The controller may drop the connection before the response completes.
    }
  });
}

if (loadLogsButton) {
  loadLogsButton.addEventListener("click", async () => {
    loadLogsButton.disabled = true;
    try {
      await loadEventLogs();
    } catch (error) {
      const list = document.querySelector("#logsList");
      if (list) {
        list.textContent = "Failed to load logs.";
      }
    } finally {
      loadLogsButton.disabled = false;
    }
  });
}

settingsForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  settingsMessage.textContent = "Saving...";

  try {
    const response = await fetch("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: formToPayload(settingsForm)
    });

    if (!response.ok) {
      throw new Error(`Save failed: ${response.status}`);
    }

    applySettings(await response.json());
    settingsMessage.textContent = "Saved";
    await refreshStatus();
  } catch (error) {
    settingsMessage.textContent = "Save failed";
  }
});

mp3Buttons.forEach((button) => {
  button.addEventListener("click", () => {
    runMp3Action(button.dataset.action);
  });
});

const mp3LiveVolumeInput = document.querySelector("#mp3LiveVolumeInput");
if (mp3LiveVolumeInput) {
  mp3LiveVolumeInput.addEventListener("input", () => {
    setMp3LiveVolume(mp3LiveVolumeInput.value);
  });
}

fm225Buttons.forEach((button) => {
  button.addEventListener("click", () => {
    runFm225Action(button.dataset.action);
  });
});

rfidButtons.forEach((button) => {
  button.addEventListener("click", () => {
    runRfidAction(button.dataset.action);
  });
});

webStorageButtons.forEach((button) => {
  button.addEventListener("click", () => {
    runWebStorageAction(button.dataset.action);
  });
});

lockCards.forEach((card) => {
  card.addEventListener("click", () => {
    postLockPulse(card);
  });
});

automationModeButtons.forEach((button) => {
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    const payload = automationPayload(button.dataset.device);
    payload.set("mode", button.dataset.mode);
    postAutomation("mode", payload);
  });
});

manualRelayCards.forEach((card) => {
  card.addEventListener("click", (event) => {
    if (event.target.closest(".mode-toggle") || card.classList.contains("busy") || card.dataset.mode !== "manual") {
      return;
    }
    postAutomation("toggle", automationPayload(card.dataset.device));
  });
});

refreshStatus();
loadSettings().catch(() => {
  settingsMessage.textContent = "Load failed";
});
setInterval(refreshStatus, 2000);
