// ══════════════════════════════════════
// MineGuard — Demo Data Generator
// ══════════════════════════════════════

const MINERS = [
  { id:'EMP001', name:'Rajesh Kumar', zone:'Zone A - Tunnel 3', status:'safe' },
  { id:'EMP002', name:'Vikram Singh', zone:'Zone B - Shaft 2', status:'safe' },
  { id:'EMP003', name:'Anil Sharma', zone:'Zone A - Tunnel 1', status:'warning', warningType:'High CO' },
  { id:'EMP004', name:'Suresh Yadav', zone:'Zone C - Tunnel 5', status:'safe' },
  { id:'EMP005', name:'Deepak Patel', zone:'Zone B - Shaft 1', status:'danger', warningType:'Fall Detected' },
  { id:'EMP006', name:'Ramesh Gupta', zone:'Zone A - Tunnel 2', status:'safe' },
  { id:'EMP007', name:'Manoj Tiwari', zone:'Zone C - Tunnel 4', status:'warning', warningType:'Low O₂' },
  { id:'EMP008', name:'Sanjay Mishra', zone:'Zone B - Shaft 3', status:'safe' },
  { id:'EMP009', name:'Arvind Das', zone:'Zone A - Tunnel 1', status:'safe' },
  { id:'EMP010', name:'Prakash Roy', zone:'Zone C - Tunnel 5', status:'safe' },
  { id:'EMP011', name:'Naveen Joshi', zone:'Zone B - Shaft 2', status:'warning', warningType:'High Temp' },
  { id:'EMP012', name:'Karan Mehra', zone:'Zone A - Tunnel 3', status:'safe' }
];

const SAFE_RANGES = {
  heartRate: { min:60, max:100, unit:'bpm', label:'Heart Rate' },
  spo2: { min:95, max:100, unit:'%', label:'SpO₂' },
  co: { min:0, max:25, unit:'ppm', label:'CO Level', warnAt:15, dangerAt:25 },
  ch4: { min:0, max:1.0, unit:'%', label:'CH₄ (Methane)', warnAt:0.5, dangerAt:1.0 },
  h2s: { min:0, max:10, unit:'ppm', label:'H₂S', warnAt:5, dangerAt:10 },
  o2: { min:19.5, max:23.5, unit:'%', label:'O₂ Level', warnBelow:19.5, dangerBelow:18.0 },
  smoke: { min:0, max:100, unit:'', label:'Smoke/LPG' },
  temperature: { min:20, max:40, unit:'°C', label:'Temperature', warnAt:35, dangerAt:42 },
  humidity: { min:30, max:80, unit:'%', label:'Humidity' },
  depth: { min:-200, max:-50, unit:'m', label:'Depth' },
  pressure: { min:950, max:1050, unit:'hPa', label:'Pressure' }
};

function rand(min, max) { return Math.random() * (max - min) + min; }
function randInt(min, max) { return Math.floor(rand(min, max + 1)); }

function generateSensorData(miner) {
  const isSafe = miner.status === 'safe';
  const isWarning = miner.status === 'warning';
  const isDanger = miner.status === 'danger';

  let heartRate, spo2, co, ch4, h2s, o2, smoke, temperature, humidity, depth;

  // Heart Rate
  if (isDanger) heartRate = randInt(105, 130);
  else heartRate = randInt(68, 92);

  // SpO2
  if (isDanger) spo2 = randInt(88, 93);
  else if (isWarning) spo2 = randInt(92, 96);
  else spo2 = randInt(95, 99);

  // CO
  if (isWarning && miner.warningType === 'High CO') co = rand(18, 24).toFixed(1);
  else if (isDanger) co = rand(26, 40).toFixed(1);
  else co = rand(1, 8).toFixed(1);

  // CH4
  if (isWarning) ch4 = rand(0.3, 0.7).toFixed(2);
  else ch4 = rand(0.05, 0.3).toFixed(2);

  // H2S
  h2s = rand(0, isSafe ? 3 : 7).toFixed(1);

  // O2
  if (isWarning && miner.warningType === 'Low O₂') o2 = rand(18.5, 19.4).toFixed(1);
  else if (isDanger) o2 = rand(16.0, 18.0).toFixed(1);
  else o2 = rand(19.5, 20.9).toFixed(1);

  // Smoke
  smoke = isDanger ? randInt(60, 85) : randInt(5, 25);

  // Temperature
  if (isWarning && miner.warningType === 'High Temp') temperature = rand(36, 40).toFixed(1);
  else temperature = rand(26, 34).toFixed(1);

  // Humidity
  humidity = randInt(50, 78);

  // Depth
  depth = randInt(-180, -100);

  // Fall detection
  const fallDetected = isDanger && miner.warningType === 'Fall Detected';

  // Motion data (accelerometer)
  const accelX = rand(-0.5, 0.5).toFixed(2);
  const accelY = rand(-0.5, 0.5).toFixed(2);
  const accelZ = fallDetected ? rand(2.5, 4.0).toFixed(2) : rand(0.8, 1.2).toFixed(2);

  return {
    heartRate, spo2, co: parseFloat(co), ch4: parseFloat(ch4),
    h2s: parseFloat(h2s), o2: parseFloat(o2), smoke, temperature: parseFloat(temperature),
    humidity, depth, fallDetected, accelX, accelY, accelZ,
    pressure: randInt(980, 1020), location: miner.zone,
    lastUpdated: new Date().toLocaleTimeString()
  };
}

function getSensorStatus(sensor, value) {
  const range = SAFE_RANGES[sensor];
  if (!range) return 'safe';
  if (sensor === 'o2') {
    if (value < (range.dangerBelow || 18)) return 'danger';
    if (value < (range.warnBelow || 19.5)) return 'warning';
    return 'safe';
  }
  if (range.dangerAt !== undefined && value >= range.dangerAt) return 'danger';
  if (range.warnAt !== undefined && value >= range.warnAt) return 'warning';
  return 'safe';
}

function getStatusLabel(status) {
  return status === 'safe' ? 'Normal' : status === 'warning' ? 'Warning' : 'Critical';
}

function generateAlerts(miners, sensorDataMap) {
  const alerts = [];
  const now = new Date();
  miners.forEach(m => {
    const d = sensorDataMap[m.id];
    if (!d) return;
    if (d.fallDetected) {
      alerts.push({ severity:'danger', icon:'🔴', msg:`Fall detected — ${m.name} (${m.id})`, miner:m.name, time:new Date(now - randInt(0,120)*1000) });
    }
    if (d.co >= 15) {
      alerts.push({ severity: d.co >= 25 ? 'danger' : 'warning', icon: d.co >= 25 ? '🔴' : '🟡', msg:`CO level high: ${d.co} ppm — ${m.name}`, miner:m.name, time:new Date(now - randInt(0,300)*1000) });
    }
    if (d.o2 < 19.5) {
      alerts.push({ severity: d.o2 < 18 ? 'danger' : 'warning', icon: d.o2 < 18 ? '🔴' : '🟡', msg:`O₂ level low: ${d.o2}% — ${m.name}`, miner:m.name, time:new Date(now - randInt(0,300)*1000) });
    }
    if (d.temperature >= 35) {
      alerts.push({ severity:'warning', icon:'🟡', msg:`High temperature: ${d.temperature}°C — ${m.name}`, miner:m.name, time:new Date(now - randInt(0,300)*1000) });
    }
    if (d.ch4 >= 0.5) {
      alerts.push({ severity:'warning', icon:'🟡', msg:`CH₄ elevated: ${d.ch4}% — ${m.name}`, miner:m.name, time:new Date(now - randInt(60,600)*1000) });
    }
  });
  alerts.sort((a,b) => b.time - a.time);
  return alerts;
}

// Sparkline history
function createSparklineHistory(baseValue, variance, count) {
  const history = [];
  let val = baseValue;
  for (let i = 0; i < count; i++) {
    val += rand(-variance, variance);
    val = Math.max(baseValue - variance * 3, Math.min(baseValue + variance * 3, val));
    history.push(val);
  }
  return history;
}

function getInitials(name) {
  return name.split(' ').map(n => n[0]).join('').toUpperCase();
}
