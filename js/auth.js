// ══════════════════════════════════════
// MineGuard — Auth System
// ══════════════════════════════════════

const CREDENTIALS = {
  supervisor: { id: 'SUP001', password: 'admin123', name: 'Amit Verma', role: 'supervisor' },
  employee: { id: 'EMP001', password: 'miner123', name: 'Rajesh Kumar', role: 'employee' }
};

function login(role, id, password) {
  const cred = CREDENTIALS[role];
  if (!cred) return { success: false, message: 'Invalid role selected' };
  if (id.toUpperCase() !== cred.id) return { success: false, message: 'Invalid ID. Check the hint below.' };
  if (password !== cred.password) return { success: false, message: 'Incorrect password. Check the hint below.' };
  
  const session = { id: cred.id, name: cred.name, role: cred.role, loginTime: Date.now() };
  sessionStorage.setItem('mineguard_session', JSON.stringify(session));
  return { success: true, session };
}

function getSession() {
  const raw = sessionStorage.getItem('mineguard_session');
  if (!raw) return null;
  try { return JSON.parse(raw); } catch { return null; }
}

function logout() {
  sessionStorage.removeItem('mineguard_session');
  window.location.href = 'index.html';
}

function requireAuth(requiredRole) {
  const session = getSession();
  if (!session) { window.location.href = 'index.html'; return null; }
  if (requiredRole && session.role !== requiredRole) { window.location.href = 'index.html'; return null; }
  return session;
}

function formatTime(date) {
  return new Date(date).toLocaleTimeString('en-IN', { hour:'2-digit', minute:'2-digit', second:'2-digit' });
}

function formatDate(date) {
  return new Date(date).toLocaleDateString('en-IN', { day:'2-digit', month:'short', year:'numeric' });
}

function updateClock(elementId) {
  const el = document.getElementById(elementId);
  if (el) {
    const now = new Date();
    el.textContent = now.toLocaleTimeString('en-IN', { hour:'2-digit', minute:'2-digit', second:'2-digit' });
  }
}
