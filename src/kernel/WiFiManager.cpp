#include "WiFiManager.h"
#include "StorageManager.h"
#include "Logger.h"

extern StorageManager storage;

const char* CONFIG_AP_SSID = "Inventory-Box-Setup";
const char* CONFIG_AP_PASS = "12345678";
const IPAddress CONFIG_AP_IP(192, 168, 4, 1);

WiFiManager::WiFiManager()
    : configPortal(nullptr), apMode(false), 
      connectionStart(0), connectionTimeout(15000) {}

void WiFiManager::begin() {
    configPortal = new WebServer(CONFIG_AP_IP, 80);
    
    // Check for stored credentials
    if (!hasStoredCredentials()) {
        Serial.println("[WiFi] No credentials stored, starting config portal...");
        startConfigPortal();
        return;
    }
    
    // Try to connect
    String ssid = storage.getString("wifi_ssid", "");
    String pass = storage.getString("wifi_pass", "");
    
    Serial.print("[WiFi] Connecting to: ");
    Serial.println(ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    connectionStart = millis();
    
    // Wait for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 50) {
        delay(200);
        attempts++;
        
        // Check timeout
        if (millis() - connectionStart > connectionTimeout) {
            Serial.println("[WiFi] Connection timeout, starting config portal...");
            startConfigPortal();
            return;
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] Connected! IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("[WiFi] Signal: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        apMode = false;
    } else {
        Serial.println("[WiFi] Connection failed, starting config portal...");
        startConfigPortal();
    }
}

void WiFiManager::update() {
    if (apMode && configPortal) {
        configPortal->handleClient();
    }
    
    // Check if connected WiFi dropped
    if (!apMode && WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection lost, restarting...");
        begin();
    }
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isAPMode() {
    return apMode;
}

String WiFiManager::getIP() {
    if (apMode) {
        return CONFIG_AP_IP.toString();
    }
    return WiFi.localIP().toString();
}

String WiFiManager::getSSID() {
    if (apMode) {
        return String(CONFIG_AP_SSID);
    }
    return WiFi.SSID();
}

int WiFiManager::getRSSI() {
    if (apMode) return 0;
    return WiFi.RSSI();
}

void WiFiManager::setCredentials(const char* ssid, const char* pass) {
    storage.putString("wifi_ssid", ssid);
    storage.putString("wifi_pass", pass);
}

bool WiFiManager::hasStoredCredentials() {
    String ssid = storage.getString("wifi_ssid", "");
    return ssid.length() > 0;
}

void WiFiManager::startConfigPortal() {
    apMode = true;
    
    // Stop existing WiFi
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    
    // Start AP
    WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASS);
    
    Serial.print("[WiFi] Config portal started: ");
    Serial.println(CONFIG_AP_SSID);
    Serial.print("[WiFi] AP IP: ");
    Serial.println(CONFIG_AP_IP);
    Serial.print("[WiFi] Password: ");
    Serial.println(CONFIG_AP_PASS);
    
    // Setup web routes
    configPortal->on("/", [this]() { handleRoot(); });
    configPortal->on("/save", [this]() { handleSave(); });
    configPortal->on("/scan", [this]() { handleScan(); });
    configPortal->onNotFound([this]() { handleRoot(); });
    
    configPortal->begin();
}

void WiFiManager::stopConfigPortal() {
    if (configPortal) {
        configPortal->stop();
        delete configPortal;
        configPortal = nullptr;
    }
    WiFi.softAPdisconnect(true);
    apMode = false;
}

void WiFiManager::handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Inventory Box - WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: "Segoe UI", Arial, sans-serif;
            background: #e8eef4;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: #fff;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            width: 400px;
            max-width: 90%;
        }
        h1 { color: #1a3c6e; margin-bottom: 5px; font-size: 20px; }
        .subtitle { color: #888; margin-bottom: 20px; font-size: 12px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; color: #555; font-size: 12px; }
        select, input {
            width: 100%;
            padding: 10px;
            border: 1px solid #c8d6e5;
            border-radius: 4px;
            font-size: 14px;
        }
        select:focus, input:focus { outline: none; border-color: #4a90d9; }
        .btn {
            width: 100%;
            padding: 12px;
            background: #1a3c6e;
            color: #fff;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 14px;
        }
        .btn:hover { background: #2c5282; }
        .btn:disabled { background: #ccc; cursor: not-allowed; }
        .networks {
            max-height: 200px;
            overflow-y: auto;
            border: 1px solid #c8d6e5;
            border-radius: 4px;
            margin-bottom: 10px;
        }
        .network-item {
            padding: 10px;
            cursor: pointer;
            border-bottom: 1px solid #e8eef4;
        }
        .network-item:hover { background: #f0f3f7; }
        .network-item.selected { background: #dbeafe; }
        .network-name { font-weight: 600; }
        .network-rssi { font-size: 11px; color: #888; }
        .loading { text-align: center; padding: 20px; color: #888; }
        .message {
            padding: 10px;
            border-radius: 4px;
            margin-bottom: 15px;
            font-size: 12px;
        }
        .message.success { background: #d4edda; color: #155724; }
        .message.error { background: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Inventory Box Setup</h1>
        <p class="subtitle">Connect to your WiFi network</p>
        
        <div id="message"></div>
        
        <div class="form-group">
            <label>Select Network</label>
            <div class="networks" id="networks">
                <div class="loading">Scanning for networks...</div>
            </div>
        </div>
        
        <div class="form-group">
            <label>Password</label>
            <input type="password" id="password" placeholder="WiFi Password">
        </div>
        
        <button class="btn" id="saveBtn" onclick="saveConfig()">Connect</button>
    </div>

    <script>
        let selectedSSID = '';
        
        async function scanNetworks() {
            try {
                const res = await fetch('/scan');
                const data = await res.json();
                const container = document.getElementById('networks');
                
                if (data.networks.length === 0) {
                    container.innerHTML = '<div class="loading">No networks found, click to rescan</div>';
                    return;
                }
                
                container.innerHTML = data.networks.map(n => 
                    `<div class="network-item" onclick="selectNetwork('${n.ssid}')">
                        <div class="network-name">${n.ssid}</div>
                        <div class="network-rssi">Signal: ${n.rssi} dBm</div>
                    </div>`
                ).join('');
            } catch (err) {
                document.getElementById('networks').innerHTML = 
                    '<div class="loading">Scan failed, try again</div>';
            }
        }
        
        function selectNetwork(ssid) {
            selectedSSID = ssid;
            document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));
            event.target.closest('.network-item').classList.add('selected');
        }
        
        async function saveConfig() {
            const password = document.getElementById('password').value;
            
            if (!selectedSSID) {
                showMessage('Please select a network', 'error');
                return;
            }
            
            document.getElementById('saveBtn').disabled = true;
            document.getElementById('saveBtn').textContent = 'Connecting...';
            
            try {
                const res = await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid: selectedSSID, password: password })
                });
                
                const data = await res.json();
                
                if (data.success) {
                    showMessage('Connected! Device will restart...', 'success');
                    document.getElementById('saveBtn').textContent = 'Success!';
                } else {
                    showMessage(data.error || 'Connection failed', 'error');
                    document.getElementById('saveBtn').disabled = false;
                    document.getElementById('saveBtn').textContent = 'Connect';
                }
            } catch (err) {
                showMessage('Error saving configuration', 'error');
                document.getElementById('saveBtn').disabled = false;
                document.getElementById('saveBtn').textContent = 'Connect';
            }
        }
        
        function showMessage(text, type) {
            document.getElementById('message').innerHTML = 
                `<div class="message ${type}">${text}</div>`;
        }
        
        scanNetworks();
        setInterval(scanNetworks, 5000);
    </script>
</body>
</html>
    )rawliteral";
    
    configPortal->send(200, "text/html", html);
}

void WiFiManager::handleSave() {
    if (configPortal->method() == HTTP_POST) {
        String body = configPortal->arg("plain");
        
        // Parse JSON
        String ssid, password;
        
        int ssidStart = body.indexOf("\"ssid\":\"") + 8;
        if (ssidStart > 7) {
            int ssidEnd = body.indexOf("\"", ssidStart);
            ssid = body.substring(ssidStart, ssidEnd);
        }
        
        int passStart = body.indexOf("\"password\":\"") + 12;
        if (passStart > 11) {
            int passEnd = body.indexOf("\"", passStart);
            password = body.substring(passStart, passEnd);
        }
        
        if (ssid.length() == 0) {
            String error = "{\"success\":false,\"error\":\"Invalid SSID\"}";
            configPortal->send(400, "application/json", error);
            return;
        }
        
        // Save credentials
        setCredentials(ssid.c_str(), password.c_str());
        
        Serial.print("[WiFi] Credentials saved for: ");
        Serial.println(ssid);
        
        // Try to connect
        configPortal->send(200, "application/json", "{\"success\":true}");
        
        // Restart to connect
        delay(1000);
        ESP.restart();
    }
}

void WiFiManager::handleScan() {
    int n = WiFi.scanComplete();
    
    // If scan not done, start one
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        configPortal->send(200, "application/json", "{\"networks\":[]}");
        return;
    }
    
    if (n == WIFI_SCAN_RUNNING) {
        configPortal->send(200, "application/json", "{\"networks\":[]}");
        return;
    }
    
    // Parse results
    String json = "{\"networks\":[";
    bool first = true;
    
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        
        if (!first) json += ",";
        first = false;
        
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    
    json += "]}";
    
    // Clear scan results for next time
    WiFi.scanDelete();
    
    configPortal->send(200, "application/json", json);
}