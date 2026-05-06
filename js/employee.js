let empSession = null;
let empDevice = null;
let sparklines = {};
let alertList = [];
let updateInterval = null;

function initEmployeeDashboard() {
  empSession = requireAuth('employee');
  if (!empSession) return;

  document.getElementById('empName').textContent = empSession.name;
  document.getElementById('empId').textContent = empSession.id;
  document.getElementById('empAvatar').textContent = initials(empSession.name);

  sparklines = {
    hr: [],
    spo2: [],
    co: [],
    o2: [],
    airtemp: []
  };

  setWaitingState();
  pollEmployeeData();
  updateInterval = setInterval(pollEmployeeData, 2000);
  setInterval(() => updateClock('clockTime'), 1000);
  updateClock('clockTime');
}

async function pollEmployeeData() {
  try {
    const res = await fetch('/devices', { cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const payload = await res.json();
    const miners = payload.miners || [];
    const device = miners.find(m => m.id === empSession.id);

    if (!device) {
      setWaitingState(`Waiting for ESP device ${empSession.id}`);
      return;
    }

    if (!device.online) {
      setWaitingState(`ESP device ${empSession.id} is offline`);
      return;
    }

    empDevice = device;
    updateConnectionStatus(device.online, device.last_seen);
    updateSensorReadings(device);
  } catch (err) {
    setWaitingState('Backend offline');
  }
}

function updateSensorReadings(device) {
  const d = device.data || {};

  document.getElementById('empName').textContent = device.name || empSession.name;
  document.getElementById('empId').textContent = device.id || empSession.id;
  document.getElementById('empAvatar').textContent = initials(device.name || empSession.name);

  pushHistory('hr', d.hr);
  pushHistory('spo2', d.spo2);
  pushHistory('co', d.co);
  pushHistory('o2', d.o2);
  pushHistory('airtemp', d.airtemp);

  updateGauge('hrGauge', d.hr, 40, 140, hrStatus(d.hr));
  setText('hrValue', fmt(d.hr, 0));
  updateBadge('hrBadge', hrStatus(d.hr));

  updateGauge('spo2Gauge', d.spo2, 80, 100, spo2Status(d.spo2));
  setText('spo2Value', fmt(d.spo2, 0));
  updateBadge('spo2Badge', spo2Status(d.spo2));

  drawSparkline('hrSparkline', sparklines.hr, '#22c55e');
  drawSparkline('spo2Sparkline', sparklines.spo2, '#3b82f6');

  updateGasCard('co', d.co, 60, highStatus(d.co, 35, 50));
  updateGasCard('ch4', d.ch4, 5, highStatus(d.ch4, 1, 5), 2);
  updateGasCard('h2s', d.h2s, 15, highStatus(d.h2s, 5, 10));
  updateGasCard('o2', d.o2, 23.5, lowStatus(d.o2, 20, 19.5), 1);
  updateGasCard('smoke', d.smoke, 150, highStatus(d.smoke, 50, 150));

  updateSimpleCard('temp', d.airtemp, tempStatus(d.airtemp), 1);
  updateSimpleCard('depth', d.depth, 'info', 1);
  updateSimpleCard('pressure', d.pressure, 'safe', 1);
  setText('locationZone', device.zone || '--');

  const fallEl = document.getElementById('fallStatus');
  const fallCard = document.getElementById('fallCard');
  if (d.fall) {
    fallEl.textContent = 'FALL DETECTED';
    fallEl.className = 'value-big value-danger';
    fallCard.className = 'card status-danger';
  } else {
    const ax_ = d.ax ?? 0, ay_ = d.ay ?? 0, az_ = d.az ?? 0;
    let tilt = 'Upright';
    if      (az_ >  0.9)  tilt = 'Flat (face up)';
    else if (az_ < -0.9)  tilt = 'Upside Down';
    else if (ax_ >  0.9)  tilt = 'Tilted Right';
    else if (ax_ < -0.9)  tilt = 'Tilted Left';
    else                  tilt = 'Angled';
    fallEl.textContent = tilt;
    fallEl.className = 'value-big value-safe';
    fallCard.className = 'card status-safe';
  }

  const ax = fmt(d.ax, 2);
  const ay = fmt(d.ay, 2);
  const az = fmt(d.az, 2);
  setText('accelData', `X:${ax} Y:${ay} Z:${az}`);

  alertList = (device.alerts || []).slice().reverse().map(a => ({
    severity: a.l === 'crit' ? 'danger' : a.l === 'warn' ? 'warning' : 'safe',
    msg: a.m,
    time: a.ts
  }));
  renderAlerts();
}

function setWaitingState(message = 'Waiting for ESP sensor data') {
  updateConnectionStatus(false);
  ['hrValue', 'spo2Value', 'coValue', 'ch4Value', 'h2sValue', 'o2Value', 'smokeValue',
   'tempValue', 'depthValue', 'pressureValue'].forEach(id => setText(id, '--'));
  setText('fallStatus', 'Waiting');
  setText('accelData', 'X:-- Y:-- Z:--');
  setText('locationZone', '--');
  ['hrBadge', 'spo2Badge', 'coBadge', 'ch4Badge', 'h2sBadge', 'o2Badge', 'smokeBadge']
    .forEach(id => updateBadge(id, 'warning', 'Waiting'));
  alertList = [{ severity: 'warning', msg: message, time: new Date().toISOString() }];
  renderAlerts();
}

function updateConnectionStatus(online, lastSeen) {
  const status = document.getElementById('deviceStatusText');
  const dot = document.getElementById('deviceStatusDot');
  if (!status || !dot) return;
  status.textContent = online ? 'Real ESP Connected' : 'Waiting for ESP';
  dot.style.background = online ? 'var(--safe)' : 'var(--warning)';
  dot.style.animation = online ? 'pulse-dot 2s ease-in-out infinite' : 'none';
  if (lastSeen) status.title = `Last seen ${lastSeen}`;
}

function pushHistory(key, value) {
  if (typeof value !== 'number' || Number.isNaN(value)) return;
  sparklines[key].push(value);
  if (sparklines[key].length > 20) sparklines[key].shift();
}

function updateGauge(id, value, min, max, status) {
  const el = document.getElementById(id);
  if (!el) return;
  const circle = el.querySelector('.gauge-fill');
  if (!circle) return;
  const r = 42;
  const circumference = 2 * Math.PI * r;
  const numeric = typeof value === 'number' ? value : min;
  const pct = Math.min(1, Math.max(0, (numeric - min) / (max - min)));
  circle.style.strokeDasharray = circumference;
  circle.style.strokeDashoffset = circumference * (1 - pct);
  circle.style.stroke = statusColor(status);
}

function updateGasCard(sensor, value, maxVal, status, decimals = 0) {
  const valEl = document.getElementById(`${sensor}Value`);
  const barEl = document.getElementById(`${sensor}Bar`);
  const badgeEl = document.getElementById(`${sensor}Badge`);
  if (!valEl) return;

  valEl.textContent = fmt(value, decimals);
  valEl.className = `value-big value-${status}`;

  if (barEl) {
    const numeric = typeof value === 'number' ? value : 0;
    const pct = Math.min(100, Math.max(0, (numeric / maxVal) * 100));
    barEl.style.width = `${pct}%`;
    barEl.className = `progress-fill ${status}`;
  }

  updateBadgeElement(badgeEl, status);
}

function updateSimpleCard(id, value, status, decimals = 0) {
  const el = document.getElementById(`${id}Value`);
  if (!el) return;
  el.textContent = fmt(value, decimals);
  el.className = `value-big value-${status}`;
}

function updateBadge(id, status, label) {
  updateBadgeElement(document.getElementById(id), status, label);
}

function updateBadgeElement(el, status, label) {
  if (!el) return;
  el.className = `status-badge ${status}`;
  el.innerHTML = `<span class="badge-dot"></span>${label || statusLabel(status)}`;
}

function drawSparkline(id, data, color) {
  const svg = document.getElementById(id);
  if (!svg || data.length < 2) {
    if (svg) svg.innerHTML = '';
    return;
  }
  const w = svg.clientWidth || 200;
  const h = svg.clientHeight || 40;
  const min = Math.min(...data);
  const max = Math.max(...data);
  const range = max - min || 1;
  const points = data.map((v, i) => {
    const x = (i / (data.length - 1)) * w;
    const y = h - ((v - min) / range) * (h - 4) - 2;
    return `${x},${y}`;
  }).join(' ');
  svg.innerHTML = `<polyline points="${points}" stroke="${color}" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`;
}

function renderAlerts() {
  const container = document.getElementById('alertList');
  const countEl = document.getElementById('alertCount');
  if (!container) return;
  if (countEl) countEl.textContent = alertList.length;

  if (!alertList.length) {
    container.innerHTML = `
      <div class="alert-item">
        <span class="alert-severity">OK</span>
        <div class="alert-content">
          <div class="alert-msg">No recent alerts from ESP.</div>
          <div class="alert-meta">LIVE</div>
        </div>
        <span class="alert-time">${formatTime(new Date())}</span>
      </div>`;
    return;
  }

  container.innerHTML = alertList.map(a => `
    <div class="alert-item">
      <span class="alert-severity">${a.severity === 'danger' ? 'CRIT' : a.severity === 'warning' ? 'WARN' : 'OK'}</span>
      <div class="alert-content">
        <div class="alert-msg">${escapeHtml(a.msg)}</div>
        <div class="alert-meta">${a.severity.toUpperCase()}</div>
      </div>
      <span class="alert-time">${a.time ? String(a.time).slice(11, 19) : formatTime(new Date())}</span>
    </div>
  `).join('');
}

async function triggerSOS() {
  const modal = document.getElementById('sosModal');
  modal.classList.add('visible');
  if (!empDevice) {
    alertList.unshift({ severity: 'warning', msg: 'Cannot send SOS until ESP device is online.', time: new Date().toISOString() });
    renderAlerts();
    return;
  }
  await fetch('/sos', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: empDevice.id })
  });
  await pollEmployeeData();
}

async function cancelSOS() {
  document.getElementById('sosModal').classList.remove('visible');
  if (!empDevice) return;
  await fetch('/clear_sos', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: empDevice.id })
  });
  await pollEmployeeData();
}

function fmt(value, decimals = 0) {
  if (typeof value !== 'number' || Number.isNaN(value)) return '--';
  return Number(value).toFixed(decimals);
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function hrStatus(v) {
  if (typeof v !== 'number') return 'warning';
  if (v >= 120 || v <= 50) return 'danger';
  if (v >= 100 || v <= 60) return 'warning';
  return 'safe';
}

function spo2Status(v) {
  return lowStatus(v, 95, 92);
}

function lowStatus(v, warn, danger) {
  if (typeof v !== 'number') return 'warning';
  if (v < danger) return 'danger';
  if (v < warn) return 'warning';
  return 'safe';
}

function highStatus(v, warn, danger) {
  if (typeof v !== 'number') return 'warning';
  if (v > danger) return 'danger';
  if (v > warn) return 'warning';
  return 'safe';
}

function tempStatus(v) {
  if (typeof v !== 'number') return 'warning';
  if (v >= 40) return 'danger';
  if (v >= 35) return 'warning';
  return 'safe';
}

function statusLabel(status) {
  if (status === 'danger') return 'Critical';
  if (status === 'warning') return 'Warning';
  return 'Normal';
}

function statusColor(status) {
  if (status === 'danger') return '#ef4444';
  if (status === 'warning') return '#fbbf24';
  return '#22c55e';
}

function initials(name) {
  return String(name || 'MG').split(' ').map(part => part[0]).join('').slice(0, 2).toUpperCase();
}

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

document.addEventListener('DOMContentLoaded', initEmployeeDashboard);
