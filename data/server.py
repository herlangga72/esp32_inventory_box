#!/usr/bin/env python3
"""Local test server for ESP32 Inventory Box web UI.
Usage: python3 server.py [port]
Serves data/ directory, returns stub JSON for API endpoints.
"""
import http.server
import json
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
DATA_DIR = os.path.dirname(os.path.abspath(__file__))

# Stub API responses
STUBS = {
    "/api/status": {
        "connected": True, "apMode": False,
        "ipAddress": "192.168.1.100", "wifiRssi": -45, "wifiSSID": "LocalTest",
        "systemStatus": "OK", "hasErrors": False,
        "state": "IDLE", "contents": 3, "currentUser": 1,
        "weight": 450.5, "baseline": 200.0, "delta": 250.5,
        "uptime": 123456, "freeHeap": 180000
    },
    "/api/tools": {
        "tools": [
            {"id":1,"name":"Hammer","weight":300.0,"tolerance":5.0,"active":True},
            {"id":2,"name":"Screwdriver Set","weight":150.0,"tolerance":3.0,"active":True},
            {"id":3,"name":"Wrench 10mm","weight":80.0,"tolerance":2.0,"active":True},
        ]
    },
    "/api/users": {
        "users": [
            {"id":1,"name":"Operator","active":True},
            {"id":2,"name":"Technician","active":True},
        ]
    },
    "/api/logs": {
        "logs": [
            {"ts":1700000001,"level":3,"tag":"INIT","msg":"Storage: OK"},
            {"ts":1700000002,"level":3,"tag":"INIT","msg":"WiFi: OK - MyWiFi (-45 dBm)"},
            {"ts":1700000010,"level":3,"tag":"STATE","msg":"TOOL_PLACED uid=1 w=300.0 d=300.0"},
            {"ts":1700000020,"level":2,"tag":"WIFI","msg":"Signal: -45 dBm"},
            {"ts":1700000030,"level":3,"tag":"STATE","msg":"TOOL_REMOVED uid=1 w=0.0 d=-300.0"},
        ],
        "total": 5, "dropped": 0, "fileSize": 2048
    },
    "/api/diagnostics": {
        "overallStatus": "OK", "uptime": 123456, "totalErrors": 0,
        "lastError": "", "okCount": 6, "warningCount": 0, "errorCount": 0,
        "components": [
            {"name":"Storage","status":"OK","lastError":"","errorCount":0},
            {"name":"HX711","status":"OK","lastError":"","errorCount":0},
            {"name":"MPU6050","status":"OK","lastError":"","errorCount":0},
            {"name":"Display","status":"OK","lastError":"","errorCount":0},
            {"name":"WiFi","status":"OK","lastError":"","errorCount":0},
            {"name":"WebServer","status":"OK","lastError":"","errorCount":0},
        ]
    },
    "/api/config": {
        "deviceName": "ESP32-Inventory-Box",
        "wifiSSID": "LocalTest", "wifiRssi": -45,
        "freeHeap": 180000, "uptime": 123456, "logLevel": 3
    },
    "/api/wifi": {
        "connected": True, "apMode": False,
        "ip": "192.168.1.100", "ssid": "LocalTest", "rssi": -45
    },
}

POST_STUBS = {
    "/api/calibrate": {"success": True, "baseline": 200.0},
    "/api/restart": {"success": True, "message": "Restarting..."},
    "/api/users/login": {"success": True, "userId": 1, "name": "Operator"},
    "/api/users/logout": {"success": True},
    "/api/tools": {"success": True, "id": 99},
    "/api/users": {"success": True, "id": 99},
    "/api/config": {"success": True},
    "/api/wifi": {"success": True, "message": "Credentials saved. Rebooting..."},
    "/api/logs/clear": {"success": True},
}


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DATA_DIR, **kwargs)

    def _send_json(self, data, code=200):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        # Strip query string for matching
        path = self.path.split("?")[0]

        if path in STUBS:
            self._send_json(STUBS[path])
        elif path == "/api/logs/download":
            csv = "1700000001,3,INIT,\"Storage: OK\"\n1700000002,3,INIT,\"WiFi: OK\"\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/csv")
            self.send_header("Content-Length", len(csv))
            self.end_headers()
            self.wfile.write(csv.encode())
        else:
            super().do_GET()

    def do_POST(self):
        path = self.path.split("?")[0]
        if path in POST_STUBS:
            self._send_json(POST_STUBS[path])
        else:
            self._send_json({"error": True, "code": 404, "message": "Not found"}, 404)

    def do_PUT(self):
        path = self.path.split("?")[0]
        self._send_json({"success": True})

    def do_DELETE(self):
        path = self.path.split("?")[0]
        self._send_json({"success": True})


if __name__ == "__main__":
    os.chdir(DATA_DIR)
    addr = ("0.0.0.0", PORT)
    server = http.server.HTTPServer(addr, Handler)
    print(f"Serving {DATA_DIR} at http://localhost:{PORT}")
    print("API stubs: /api/status, /api/tools, /api/users, /api/logs, /api/diagnostics, /api/config, /api/wifi")
    print("Press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()
