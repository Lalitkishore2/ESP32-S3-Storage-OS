#ifndef STORAGE_HUB_APP_H
#define STORAGE_HUB_APP_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef STA_SSID
// config.h already included
#elif defined(WIFI_SSID)
// alternate config
#else
#include "config.h"
#endif

/**
 * ============================================================================
 * ESP32-S3 STORAGE HUB & APP FRAMEWORK (GENERIC COMMUNITY EDITION)
 * ============================================================================
 * Non-intrusive integration framework for ESP32-S3 PlatformIO & Arduino C++ projects.
 * Runs Port 80 Storage Hub management task on Core 0 while Core 1 executes native user setup() and loop().
 */
namespace StorageHubApp {
    static WebServer hubServer(80);
    static WebServer appServer(8080);
    static String s_project_name = "ESP32-S3 Application";
    static String s_custom_app_html = "";

    struct LogEntry {
        unsigned long ts;
        char text[128];
    };
    static LogEntry s_logs[50];
    static int s_log_head = 0;
    static int s_log_count = 0;

    inline void addLog(const char* msg) {
        s_logs[s_log_head].ts = millis();
        strncpy(s_logs[s_log_head].text, msg, sizeof(s_logs[s_log_head].text) - 1);
        s_logs[s_log_head].text[sizeof(s_logs[s_log_head].text) - 1] = '\0';
        s_log_head = (s_log_head + 1) % 50;
        if (s_log_count < 50) s_log_count++;
    }

    inline void log(const String& msg) {
        addLog(msg.c_str());
        Serial.println(msg);
    }

    inline void logf(const char* fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        addLog(buf);
        Serial.println(buf);
    }

    // ============================================================
    // RATE-LIMITED & PSRAM-CACHED USB STORAGE ENGINE FOR USER APPS
    // ============================================================
    namespace Storage {
        static unsigned long s_last_io_ms = 0;
        static int s_token_bucket = 10; // Max 10 ops/sec
        static unsigned long s_last_token_refill_ms = 0;

        inline bool checkRateLimit() {
            unsigned long now = millis();
            if (now - s_last_token_refill_ms >= 1000) {
                s_token_bucket = 10;
                s_last_token_refill_ms = now;
            }
            if (s_token_bucket <= 0 || (now - s_last_io_ms < 100)) {
                log("[STORAGE-THROTTLE] Rate limit reached (max 10 ops/sec, 100ms cooldown)");
                return false;
            }
            s_token_bucket--;
            s_last_io_ms = now;
            return true;
        }

        inline bool readFile(const char* path, String& output) {
            if (!checkRateLimit()) return false;
            char safePath[256];
            if (path[0] == '/') snprintf(safePath, sizeof(safePath), "/usb%s", path);
            else snprintf(safePath, sizeof(safePath), "/usb/data/%s", path);

            FILE* f = fopen(safePath, "rb");
            if (!f) return false;
            output = "";
            char buf[128];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
                buf[n] = '\0';
                output += buf;
            }
            fclose(f);
            logf("[STORAGE-APP] Read %u bytes from %s", output.length(), safePath);
            return true;
        }

        inline bool writeFile(const char* path, const String& data) {
            if (!checkRateLimit()) return false;
            char safePath[256];
            if (path[0] == '/') snprintf(safePath, sizeof(safePath), "/usb%s", path);
            else snprintf(safePath, sizeof(safePath), "/usb/data/%s", path);

            FILE* f = fopen(safePath, "wb");
            if (!f) return false;
            fwrite(data.c_str(), 1, data.length(), f);
            fclose(f);
            logf("[STORAGE-APP] Wrote %u bytes to %s", data.length(), safePath);
            return true;
        }

        inline bool appendFile(const char* path, const String& data) {
            if (!checkRateLimit()) return false;
            char safePath[256];
            if (path[0] == '/') snprintf(safePath, sizeof(safePath), "/usb%s", path);
            else snprintf(safePath, sizeof(safePath), "/usb/data/%s", path);

            FILE* f = fopen(safePath, "ab");
            if (!f) return false;
            fwrite(data.c_str(), 1, data.length(), f);
            fclose(f);
            logf("[STORAGE-APP] Appended %u bytes to %s", data.length(), safePath);
            return true;
        }

        inline bool exists(const char* path) {
            char safePath[256];
            if (path[0] == '/') snprintf(safePath, sizeof(safePath), "/usb%s", path);
            else snprintf(safePath, sizeof(safePath), "/usb/data/%s", path);

            FILE* f = fopen(safePath, "rb");
            if (f) { fclose(f); return true; }
            return false;
        }
    }

    // Fast reboot to ota_0 Core OS
    inline void restore_hub() {
        Serial.println("[STORAGE-HUB] Fast switching boot partition to ota_0...");
        const esp_partition_t* ota0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        if (ota0) {
            esp_ota_set_boot_partition(ota0);
        }
        delay(50);
        ESP.restart();
    }

    // Custom Web Extension API
    inline void setAppHtml(const char* html) {
        s_custom_app_html = String(html);
    }

    inline void addEndpoint(const char* uri, WebServer::THandlerFunction fn, HTTPMethod method = HTTP_GET) {
        appServer.on(uri, method, fn);
    }

    // ============================================================
    // GENERIC DEFAULT PROJECT DASHBOARD HTML (PORT 8080)
    // Works for any custom project out of the box!
    // ============================================================
    static const char GENERIC_PROJECT_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Application Dashboard</title>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Fira+Code:wght@400;500&display=swap" rel="stylesheet">
  <style>
    :root { --bg: #12100e; --card: #1c1917; --border: #2e2924; --accent: #ea580c; --success: #10b981; --text: #f5f5f4; --muted: #a8a29e; }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text); font-family: 'Inter', sans-serif; padding: 32px 20px; min-height: 100vh; }
    .container { max-width: 1000px; margin: 0 auto; }
    .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 28px; border-bottom: 1px solid var(--border); padding-bottom: 20px; }
    .badge { background: rgba(234,88,12,0.15); color: var(--accent); border: 1px solid rgba(234,88,12,0.3); padding: 6px 14px; border-radius: 20px; font-size: 12px; font-weight: 600; }
    .title { font-size: 28px; font-weight: 700; color: #fff; margin-top: 6px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 18px; margin-bottom: 28px; }
    .card { background: var(--card); border: 1px solid var(--border); border-radius: 14px; padding: 22px; }
    .card-label { font-size: 12px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.8px; margin-bottom: 8px; font-weight: 600; }
    .card-value { font-size: 28px; font-weight: 700; color: #fff; font-family: 'Fira Code', monospace; }
    .btn { background: var(--accent); color: #fff; border: none; padding: 10px 20px; border-radius: 8px; font-weight: 600; cursor: pointer; text-decoration: none; display: inline-block; transition: 0.2s; }
    .btn:hover { opacity: 0.9; transform: translateY(-1px); }
    .btn-outline { background: transparent; border: 1px solid var(--border); color: var(--text); }
    .btn-outline:hover { background: rgba(255,255,255,0.05); }
    .status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; background: var(--success); box-shadow: 0 0 10px var(--success); }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div>
        <span class="badge"><span class="status-dot"></span> Core 1 Active Execution</span>
        <h1 class="title" id="app-title">ESP32-S3 Application</h1>
      </div>
      <a href="http://192.168.0.8:80/" class="btn btn-outline">Back to Storage Hub (Port 80)</a>
    </div>

    <div class="grid">
      <div class="card"><div class="card-label">Free Internal RAM</div><div class="card-value" id="heap">-- KB</div></div>
      <div class="card"><div class="card-label">PSRAM Free Cache</div><div class="card-value" id="psram">-- MB</div></div>
      <div class="card"><div class="card-label">Active Core Allocation</div><div class="card-value" style="color:var(--success)">Core 0 & 1</div></div>
      <div class="card"><div class="card-label">System Uptime</div><div class="card-value" id="uptime">0s</div></div>
    </div>

    <div class="card">
      <h3 style="margin-bottom: 12px; font-size: 18px;">Application Status & Architecture</h3>
      <p style="color: var(--muted); font-size: 14px; line-height: 1.6;">
        This application was integrated using the generic <code>StorageHubApp</code> C++ framework. Core 1 runs native hardware operations uninhibited while Core 0 handles network telemetry on Port 80 and web app endpoints on Port 8080.
      </p>
    </div>
  </div>

  <script>
    setInterval(() => {
      fetch('/api/app/status').then(r=>r.json()).then(d=>{
        document.getElementById('heap').innerText = Math.round(d.heap/1024) + ' KB';
        document.getElementById('psram').innerText = (d.psram/(1024*1024)).toFixed(1) + ' MB';
        document.getElementById('uptime').innerText = d.uptime + 's';
        if(d.project) document.getElementById('app-title').innerText = d.project;
      }).catch(()=>{});
    }, 1500);
  </script>
</body>
</html>)rawliteral";

    // ============================================================
    // MASTER FULL STORAGE HUB DASHBOARD HTML (PORT 80 HUB)
    // Clean, Generic Setup UI for Any Community ESP32-S3 Project
    // ============================================================
    static const char FULL_DASHBOARD[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Storage & Memory Hub</title>
  <link href="https://fonts.googleapis.com/css2?family=Instrument+Serif:ital@0;1&family=Inter:wght@300;400;500;600;700&family=Fira+Code:wght@400;500&display=swap" rel="stylesheet">
  <style>
    :root { --bg: #12100e; --sidebar-bg: #181512; --card-bg: #1c1917; --card-border: #2e2924; --accent-coral: #ea580c; --accent-coral-glow: rgba(234, 88, 12, 0.25); --success: #10b981; --danger: #ef4444; --text-main: #f5f5f4; --text-muted: #a8a29e; --text-subtle: #78716c; --radius-lg: 14px; --radius-md: 8px; }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text-main); font-family: 'Inter', sans-serif; min-height: 100vh; display: flex; overflow-x: hidden; overflow-y: auto; }
    .sidebar { width: 64px; background: var(--sidebar-bg); border-right: 1px solid var(--card-border); display: flex; flex-direction: column; align-items: center; padding: 16px 0; min-height: 100vh; position: fixed; left: 0; top: 0; z-index: 50; }
    .sidebar-brand { width: 40px; height: 40px; border-radius: 50%; background: #fff; display: flex; align-items: center; justify-content: center; margin-bottom: 24px; }
    .sidebar-brand svg { width: 22px; height: 22px; stroke: #12100e; stroke-width: 2.2; fill: none; }
    .sidebar-nav { display: flex; flex-direction: column; gap: 8px; width: 100%; align-items: center; }
    .nav-icon { width: 42px; height: 42px; border-radius: 10px; display: flex; align-items: center; justify-content: center; color: var(--text-muted); cursor: pointer; transition: all 0.2s; border: 1px solid transparent; }
    .nav-icon svg { width: 20px; height: 20px; stroke: currentColor; stroke-width: 1.8; fill: none; }
    .nav-icon:hover { color: var(--text-main); background: rgba(255,255,255,0.05); }
    .nav-icon.active { color: var(--accent-coral); background: rgba(234, 88, 12, 0.12); border-color: rgba(234, 88, 12, 0.3); }
    .main-wrapper { margin-left: 64px; flex: 1; display: flex; flex-direction: column; min-width: 0; }
    .header-bar { height: 60px; border-bottom: 1px solid var(--card-border); display: flex; justify-content: space-between; align-items: center; padding: 0 28px; background: var(--bg); position: sticky; top: 0; z-index: 40; }
    .header-path { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: var(--text-subtle); font-weight: 600; display: flex; align-items: center; gap: 8px; }
    .header-path strong { color: var(--text-main); font-weight: 600; }
    .header-controls { display: flex; align-items: center; gap: 14px; }
    .active-project-pill { background: rgba(16, 185, 129, 0.12); border: 1px solid rgba(16, 185, 129, 0.3); padding: 5px 12px; border-radius: 20px; font-size: 12px; color: var(--success); font-weight: 600; display: flex; align-items: center; gap: 6px; }
    .btn-coral { background: var(--accent-coral); color: #fff; border: none; padding: 8px 16px; font-size: 13px; font-weight: 600; border-radius: var(--radius-md); cursor: pointer; display: inline-flex; align-items: center; gap: 6px; text-decoration:none; transition: all 0.2s; box-shadow: 0 4px 14px var(--accent-coral-glow); }
    .btn-coral:hover { opacity: 0.92; transform: translateY(-1px); }
    .btn-danger { background: rgba(239, 68, 68, 0.12); color: var(--danger); border: 1px solid rgba(239, 68, 68, 0.3); padding: 5px 10px; font-size: 11px; border-radius: 6px; cursor: pointer; }
    .btn-danger:hover { background: var(--danger); color: #fff; }
    .content-container { padding: 32px 36px; max-width: 1380px; margin: 0 auto; width: 100%; }
    .view-panel { display: none; }
    .view-panel.active { display: block; animation: fadeIn 0.2s ease-in-out; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }
    .page-title { font-family: 'Instrument Serif', serif; font-size: 44px; font-weight: 400; color: #fff; line-height: 1.1; margin-bottom: 6px; }
    .page-subtitle { font-size: 14px; color: var(--text-muted); margin-bottom: 28px; }
    .section-card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: var(--radius-lg); padding: 24px; margin-bottom: 28px; }
    .terminal-window { background: #090807; border: 1px solid var(--card-border); border-radius: var(--radius-md); padding: 18px; font-family: 'Fira Code', monospace; font-size: 12px; color: #10b981; height: 380px; overflow-y: auto; line-height: 1.6; box-shadow: inset 0 2px 8px rgba(0,0,0,0.8); }
    .log-line { display: flex; gap: 12px; margin-bottom: 3px; }
    .log-ts { color: var(--text-subtle); flex-shrink: 0; font-size: 11px; }
    .log-txt { color: #f5f5f4; word-break: break-all; }
    .doc-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; margin-top: 20px; }
    .doc-card { background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px; }
    .doc-card h3 { color: var(--accent-coral); font-size: 16px; margin-bottom: 10px; display:flex; align-items:center; gap:8px; }
    .doc-card p, .doc-card ul { color: var(--text-muted); font-size: 13px; line-height: 1.6; }
    .doc-card ul { list-style: disc inside; margin-top: 8px; }
  </style>
</head>
<body>
  <div class="sidebar">
    <div class="sidebar-brand"><svg viewBox="0 0 24 24"><polygon points="12 2 2 7 12 12 22 7 12 2"></polygon><polyline points="2 17 12 22 22 17"></polyline><polyline points="2 12 12 17 22 12"></polyline></svg></div>
    <div class="sidebar-nav">
      <div class="nav-icon active" id="nav-apps" onclick="switchView('apps')" title="Code Store & Terminal"><svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="17 8 12 3 7 8"></polyline><line x1="12" y1="3" x2="12" y2="15"></line></svg></div>
      <div class="nav-icon" id="nav-docs" onclick="switchView('docs')" title="System Architecture & Specs"><svg viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path><polyline points="14 2 14 8 20 8"></polyline><line x1="16" y1="13" x2="8" y2="13"></line><line x1="16" y1="17" x2="8" y2="17"></line><polyline points="10 9 9 9 8 9"></polyline></svg></div>
    </div>
  </div>

  <div class="main-wrapper">
    <div class="header-bar">
      <div class="header-path">MAIN / <strong id="header-title">USB CODE STORE & PROJECT LAUNCHER</strong></div>
      <div class="header-controls">
        <div class="active-project-pill" id="pill-project">Running: User App</div>
        <a class="btn-coral" href="http://192.168.0.8:8080/" target="_blank">Open Project Web App (8080)</a>
        <button class="btn-danger" onclick="stopApp()">Stop & Restore Hub</button>
      </div>
    </div>

    <div class="content-container">
      <!-- VIEW 1: APPS & TERMINAL -->
      <div class="view-panel active" id="view-apps">
        <h1 class="page-title">USB Code Store & Project Launcher</h1>
        <p class="page-subtitle">Active project execution status, live serial terminal, and controller telemetry.</p>

        <div class="section-card">
          <div style="display:flex; justify-content:space-between; align-items:center;">
            <div style="font-size:18px; font-weight:600; color:#fff;">Active Project Execution</div>
            <div>
              <a class="btn-coral" href="http://192.168.0.8:8080/" target="_blank">Open Project Web App (Port 8080)</a>
              <button class="btn-danger" onclick="stopApp()">Stop Active Project & Restore Hub</button>
            </div>
          </div>
        </div>

        <div class="section-card">
          <h3 style="font-size:16px; color:#fff; margin-bottom:14px;">PlatformIO Serial Terminal & Real-Time Project Console</h3>
          <div class="terminal-window" id="term"></div>
        </div>
      </div>

      <!-- VIEW 2: GENERIC SYSTEM ARCHITECTURE SPECS -->
      <div class="view-panel" id="view-docs">
        <h1 class="page-title">System Architecture & Technical Guide</h1>
        <p class="page-subtitle">Generic technical overview, hardware pinouts, and dual-core FreeRTOS execution flow.</p>

        <div class="doc-grid">
          <div class="doc-card">
            <h3>Dual-Core Execution Architecture</h3>
            <p><strong>Core 0:</strong> Background Storage Hub Web Server (Port 80), HTTP API Telemetry, and BOOT button recovery listener.<br>
               <strong>Core 1:</strong> 240 MHz uninhibited execution of native project setup() and loop().</p>
          </div>
          <div class="doc-card">
            <h3>Hardware Pinouts & Wiring</h3>
            <ul>
              <li><strong>USB D- Pin:</strong> GPIO 19</li>
              <li><strong>USB D+ Pin:</strong> GPIO 20</li>
              <li><strong>BOOT Recovery Button:</strong> GPIO 0</li>
              <li><strong>Hardware UART:</strong> 115200 Baud</li>
            </ul>
          </div>
          <div class="doc-card">
            <h3>Multi-Port Web Architecture</h3>
            <p><strong>Port 80:</strong> Central Storage Hub OS Dashboard & Console Stream.<br>
               <strong>Port 8080:</strong> Independent project application web interface.</p>
          </div>
          <div class="doc-card">
            <h3>Storage & PSRAM Protection</h3>
            <p>Integrated <code>StorageHubApp::Storage</code> rate limiter restricts active project file I/O to max 10 ops/sec with a 100ms cooldown, protecting real-time hardware execution.</p>
          </div>
        </div>
      </div>
    </div>
  </div>

  <script>
    function switchView(v) {
      document.querySelectorAll('.view-panel').forEach(p=>p.classList.remove('active'));
      document.querySelectorAll('.nav-icon').forEach(i=>i.classList.remove('active'));
      if(v==='apps') { document.getElementById('view-apps').classList.add('active'); document.getElementById('nav-apps').classList.add('active'); document.getElementById('header-title').innerText='USB CODE STORE & PROJECT LAUNCHER'; }
      else if(v==='docs') { document.getElementById('view-docs').classList.add('active'); document.getElementById('nav-docs').classList.add('active'); document.getElementById('header-title').innerText='SYSTEM ARCHITECTURE & TECHNICAL GUIDE'; }
    }
    function stopApp() { fetch('/api/apps/stop', {method:'POST'}).then(()=>location.href='http://192.168.0.8:80/'); }
    setInterval(() => {
      fetch('/api/console').then(r=>r.json()).then(d=>{
        let t = document.getElementById('term');
        t.innerHTML = d.logs.map(l=>'<div class="log-line"><span class="log-ts">[+'+(l.ts/1000).toFixed(2)+'s]</span><span class="log-txt">'+l.text+'</span></div>').join('');
        t.scrollTop = t.scrollHeight;
      }).catch(()=>{});
    }, 1500);
  </script>
</body>
</html>)rawliteral";

    // ============================================================
    // INIT & SERVER SETUP
    // ============================================================
    inline void init(const char* projectName) {
        s_project_name = String(projectName);
        Serial.begin(115200);

        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_STA);
#if defined(STA_SSID) && defined(STA_PASS)
            WiFi.begin(STA_SSID, STA_PASS);
#elif defined(WIFI_SSID) && defined(WIFI_PASSWORD)
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
        }

        logf("[%s] Booting application on ota_1...", projectName);

        // === PORT 80: Hub Management, Documentation & Console ===
        hubServer.on("/", HTTP_GET, []() { hubServer.send(200, "text/html", FULL_DASHBOARD); });
        hubServer.on("/api/stats", HTTP_GET, []() {
            char json[256];
            snprintf(json, sizeof(json),
                "{\"heap\":%u,\"psram\":%u,\"ota\":\"ota_1\",\"project\":\"%s\",\"uptime\":%lu}",
                ESP.getFreeHeap(), ESP.getFreePsram(), s_project_name.c_str(), millis() / 1000);
            hubServer.send(200, "application/json", json);
        });
        hubServer.on("/api/apps/active", HTTP_GET, []() {
            hubServer.send(200, "application/json", "{\"status\":\"running\",\"active_project\":\"" + s_project_name + "\"}");
        });
        hubServer.on("/api/console", HTTP_GET, []() {
            String json = "{\"logs\":[";
            for (int i = 0; i < s_log_count; i++) {
                int idx = (s_log_head - s_log_count + i + 50) % 50;
                json += "{\"ts\":" + String(s_logs[idx].ts) + ",\"text\":\"" + String(s_logs[idx].text) + "\"}";
                if (i < s_log_count - 1) json += ",";
            }
            json += "]}";
            hubServer.send(200, "application/json", json);
        });
        hubServer.on("/api/apps/stop", HTTP_POST, []() {
            hubServer.send(200, "application/json", "{\"status\":\"stopping\"}");
            delay(50);
            restore_hub();
        });

        // === PORT 8080: Project Dedicated Web App & Interactive APIs ===
        appServer.on("/", HTTP_GET, []() {
            if (s_custom_app_html.length() > 0) {
                appServer.send(200, "text/html", s_custom_app_html);
            } else {
                appServer.send(200, "text/html", GENERIC_PROJECT_HTML);
            }
        });

        // Generic Telemetry Status API
        appServer.on("/api/app/status", HTTP_GET, []() {
            char buf[256];
            snprintf(buf, sizeof(buf), "{\"heap\":%u,\"psram\":%u,\"uptime\":%lu,\"project\":\"%s\"}",
                ESP.getFreeHeap(), ESP.getFreePsram(), millis() / 1000, s_project_name.c_str());
            appServer.send(200, "application/json", buf);
        });

        hubServer.begin();
        appServer.begin();
        logf("[%s] Web Services active (Port 80 Hub, Port 8080 App)", projectName);

        // Core 0 background handler task
        xTaskCreatePinnedToCore(
            [](void* param) {
                unsigned long lastHB = 0;
                while (true) {
                    hubServer.handleClient();
                    appServer.handleClient();

                    if (millis() - lastHB > 3000) {
                        lastHB = millis();
                        logf("[%s] Core 0 Active | Free Heap: %u bytes | Up: %lu s",
                            s_project_name.c_str(), ESP.getFreeHeap(), millis() / 1000);
                    }

                    if (digitalRead(0) == LOW) {
                        logf("[%s] BOOT button pressed. Restoring Core OS...", s_project_name.c_str());
                        restore_hub();
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            },
            "HubCore0", 10240, NULL, 1, NULL, 0
        );
    }

    inline void loop() {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

#endif // STORAGE_HUB_APP_H
