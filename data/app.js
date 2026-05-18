/**
 * ESP32 Inventory Box - Main Application
 * Shared utilities and API client
 */

const API_BASE = '/api';
let statusInterval = null;
let diagnosticsInterval = null;
let currentUser = null;
let systemStatus = 'OK';

// ============ API CLIENT ============

const API = {
    async get(endpoint) {
        const res = await fetch(API_BASE + endpoint);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    },
    
    async post(endpoint, data) {
        const res = await fetch(API_BASE + endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    },
    
    async put(endpoint, data) {
        const res = await fetch(API_BASE + endpoint, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    },
    
    async delete(endpoint) {
        const res = await fetch(API_BASE + endpoint, { method: 'DELETE' });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    }
};

// ============ INITIALIZATION ============

document.addEventListener('DOMContentLoaded', () => {
    // Start polling
    fetchStatus();
    loadDiagnostics();
    
    statusInterval = setInterval(fetchStatus, 2000);
    diagnosticsInterval = setInterval(loadDiagnostics, 5000);
    
    // Setup modal close on overlay click
    document.getElementById('modal-overlay').addEventListener('click', (e) => {
        if (e.target.id === 'modal-overlay') hideModal();
    });
});

// ============ GLOBAL HELPERS ============

function formatUptime(ms) {
    const sec = Math.floor(ms / 1000);
    const min = Math.floor(sec / 60);
    const hr = Math.floor(min / 60);
    const days = Math.floor(hr / 24);
    
    if (days > 0) return `${days}d ${hr % 24}h`;
    if (hr > 0) return `${hr}h ${min % 60}m`;
    if (min > 0) return `${min}m ${sec % 60}s`;
    return `${sec}s`;
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function formatDate(timestamp) {
    if (!timestamp) return '--';
    const date = new Date(timestamp * 1000);
    return date.toLocaleString('en-US', {
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

// ============ TOAST NOTIFICATIONS ============

function showToast(message, type = 'info', duration = 3000) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    container.appendChild(toast);
    
    setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
    }, duration);
}

// ============ MODAL HELPERS ============

function showModal(title, bodyHtml, footerHtml) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-body').innerHTML = bodyHtml;
    document.getElementById('modal-footer').innerHTML = footerHtml;
    document.getElementById('modal-overlay').classList.add('show');
}

function hideModal() {
    document.getElementById('modal-overlay').classList.remove('show');
}

function showConfirm(title, message, onConfirm) {
    showModal(title, `<p>${message}</p>`, `
        <button class="btn" onclick="hideModal()">Cancel</button>
        <button class="btn btn-primary" onclick="hideModal(); (${onConfirm})()">Confirm</button>
    `);
}

// ============ STATUS & DIAGNOSTICS ============

async function fetchStatus() {
    try {
        const data = await API.get('/status');
        
        document.getElementById('connection-dot').classList.add('connected');
        document.getElementById('connection-text').textContent = 'Online';
        
        // Update global status display if on dashboard
        const weightEl = document.getElementById('global-weight');
        if (weightEl) weightEl.textContent = data.weight.toFixed(1) + ' g';
        
        const baselineEl = document.getElementById('global-baseline');
        if (baselineEl) baselineEl.textContent = data.baseline.toFixed(1);
        
        const deltaEl = document.getElementById('global-delta');
        if (deltaEl) deltaEl.textContent = (data.delta >= 0 ? '+' : '') + data.delta.toFixed(1);
        
        const contentsEl = document.getElementById('global-contents');
        if (contentsEl) contentsEl.textContent = data.contents || 0;
        
        const stateEl = document.getElementById('global-state');
        if (stateEl) stateEl.textContent = data.state || '--';
        
        const heapEl = document.getElementById('global-heap');
        if (heapEl) heapEl.textContent = formatBytes(data.freeHeap);
        
        const uptimeEl = document.getElementById('global-uptime');
        if (uptimeEl) uptimeEl.textContent = formatUptime(data.uptime);
        
        const rssiEl = document.getElementById('global-rssi');
        if (rssiEl) rssiEl.textContent = data.wifiRssi + ' dBm';
        
        const ipEl = document.getElementById('global-ip');
        if (ipEl) ipEl.textContent = data.ipAddress || '--';
        
        if (data.currentUser && data.currentUser > 0) {
            currentUser = data.currentUser;
            document.getElementById('user-display').textContent = data.currentUserName || `User #${currentUser}`;
        }
        
        if (data.hasErrors || data.systemStatus !== 'OK') {
            updateStatusDot('warning');
        }
        
    } catch (err) {
        console.error('Status fetch failed:', err);
        document.getElementById('connection-dot').classList.remove('connected');
        document.getElementById('connection-text').textContent = 'Offline';
        updateStatusDot('error');
    }
}

async function loadDiagnostics() {
    try {
        const data = await API.get('/diagnostics');
        
        // Update overall status
        systemStatus = data.overallStatus || 'UNKNOWN';
        
        const statusBadge = document.getElementById('diag-status-badge');
        if (statusBadge) {
            statusBadge.textContent = systemStatus;
            statusBadge.className = `badge badge-${systemStatus === 'OK' ? 'success' : systemStatus === 'WARNING' ? 'warning' : 'danger'}`;
        }
        
        // Update error banner
        const errorBanner = document.getElementById('error-banner');
        const navDot = document.getElementById('nav-status-dot');
        
        if (data.errorCount > 0 || data.warningCount > 0) {
            errorBanner.classList.add('show');
            if (data.errorCount > 0) {
                errorBanner.classList.add('warning');
                navDot.classList.add('error');
                navDot.classList.remove('warning');
            } else {
                navDot.classList.add('warning');
                navDot.classList.remove('error');
            }
            document.getElementById('error-title').textContent = data.errorCount > 0 ? 'System Error' : 'System Warning';
            document.getElementById('error-details').textContent = `${data.errorCount} error(s), ${data.warningCount} warning(s)`;
        } else {
            errorBanner.classList.remove('show', 'warning');
            navDot.classList.remove('warning', 'error');
        }
        
        // Update diagnostics page if loaded
        const diagList = document.getElementById('diagnostics-list');
        if (diagList && data.components) {
            diagList.innerHTML = data.components.map(c => `
                <tr>
                    <td class="diag-name">${c.name}</td>
                    <td>
                        <span class="badge badge-${c.status === 'OK' ? 'success' : c.status === 'WARNING' ? 'warning' : 'danger'}">${c.status}</span>
                        ${c.lastError ? `<div class="diag-error">${c.lastError}</div>` : ''}
                    </td>
                    <td style="color: var(--text-muted); text-align: right;">
                        ${c.errorCount > 0 ? c.errorCount + ' error(s)' : 'No errors'}
                    </td>
                </tr>
            `).join('');
        }
        
        const diagUptime = document.getElementById('diag-uptime');
        if (diagUptime) diagUptime.textContent = formatUptime(data.uptime);
        
        const diagErrors = document.getElementById('diag-total-errors');
        if (diagErrors) diagErrors.textContent = data.totalErrors || 0;
        
        const diagLastError = document.getElementById('diag-last-error');
        if (diagLastError) diagLastError.textContent = data.lastError || 'None';
        
    } catch (err) {
        console.error('Diagnostics fetch failed:', err);
    }
}

function updateStatusDot(status) {
    const dot = document.getElementById('nav-status-dot');
    if (dot) {
        dot.classList.remove('warning', 'error');
        if (status !== 'ok') dot.classList.add(status);
    }
}

// ============ RESTART ============

function restartSystem() {
    showConfirm('Restart System', 'Are you sure you want to restart?', 'performRestart');
}

function performRestart() {
    showToast('Restarting...', 'info');
    
    fetch(API_BASE + '/restart', { method: 'POST' })
        .then(() => {
            document.body.innerHTML = `
                <div style="display:flex;align-items:center;justify-content:center;min-height:100vh;background:#1a3c6e;color:#fff;font-family:Arial,sans-serif;text-align:center;flex-direction:column;">
                    <h1 style="font-size:2em;margin-bottom:20px;">Restarting...</h1>
                    <p>System is restarting. Page will refresh when back online.</p>
                </div>
            `;
            waitForReconnect();
        })
        .catch(() => {
            showToast('Restart failed', 'error');
        });
}

function waitForReconnect() {
    let attempts = 0;
    const max = 30;
    
    function tryConnect() {
        fetch(API_BASE + '/status')
            .then(() => window.location.reload())
            .catch(() => {
                attempts++;
                if (attempts < max) setTimeout(tryConnect, 1000);
            });
    }
    
    setTimeout(tryConnect, 3000);
}

function refreshAll() {
    fetchStatus();
    loadDiagnostics();
    showToast('Refreshed', 'success');
}

// ============ GLOBAL EXPORTS ============

window.API = API;
window.showToast = showToast;
window.showModal = showModal;
window.hideModal = hideModal;
window.showConfirm = showConfirm;
window.formatUptime = formatUptime;
window.formatBytes = formatBytes;
window.formatDate = formatDate;
window.restartSystem = restartSystem;
window.refreshAll = refreshAll;