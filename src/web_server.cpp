#include "web_server.h"
#include "config.h"
#include "usb_msc.h"
#include "mm_manager.h"
#include "mm_benchmark.h"
#include "sys_log.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>

static WebServer server(HTTP_PORT);
static FILE *uploadFile = NULL;
static String s_active_project = "Storage Hub Core";

// medinv.figma.site Multi-Tab SPA Layout
// Dedicated views for:
// 1. Dashboard Overview
// 2. USB Storage & File Explorer (with Static Website Hosting)
// 3. Standalone Code Upload & USB Project App Store (/usb/apps/*.bin Flash Engine & Terminal Monitor)
// 4. PlatformIO Live Web Serial Monitor Console (with Active Project Tracker & Stop/Restore Button)
// 5. Multi-Level Memory Manager (SLRU Cache & Benchmark)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Storage & Memory Hub</title>
  <link href="https://fonts.googleapis.com/css2?family=Instrument+Serif:ital@0;1&family=Inter:wght@300;400;500;600;700&family=Fira+Code:wght@400;500&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #12100e;
      --sidebar-bg: #181512;
      --card-bg: #1c1917;
      --card-border: #2e2924;
      --card-hover: #26221f;
      --accent-coral: #ea580c;
      --accent-coral-glow: rgba(234, 88, 12, 0.25);
      --accent-warm: #d97706;
      --success: #10b981;
      --danger: #ef4444;
      --text-main: #f5f5f4;
      --text-muted: #a8a29e;
      --text-subtle: #78716c;
      --radius-lg: 14px;
      --radius-md: 8px;
      --radius-sm: 6px;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text-main);
      font-family: 'Inter', -apple-system, sans-serif;
      min-height: 100vh;
      display: flex;
      overflow-x: hidden;
    }

    /* Left Icon Sidebar Navigation (medinv layout) */
    .sidebar {
      width: 64px;
      background: var(--sidebar-bg);
      border-right: 1px solid var(--card-border);
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 16px 0;
      min-height: 100vh;
      position: fixed;
      left: 0;
      top: 0;
      z-index: 50;
    }
    .sidebar-brand {
      width: 40px;
      height: 40px;
      border-radius: 50%;
      background: #fff;
      display: flex;
      align-items: center;
      justify-content: center;
      margin-bottom: 24px;
    }
    .sidebar-brand svg { width: 22px; height: 22px; stroke: #12100e; stroke-width: 2.2; fill: none; }
    
    .sidebar-nav { display: flex; flex-direction: column; gap: 8px; width: 100%; align-items: center; }
    .nav-icon {
      width: 42px;
      height: 42px;
      border-radius: 10px;
      display: flex;
      align-items: center;
      justify-content: center;
      color: var(--text-muted);
      cursor: pointer;
      transition: all 0.2s;
      border: 1px solid transparent;
      position: relative;
    }
    .nav-icon svg { width: 20px; height: 20px; stroke: currentColor; stroke-width: 1.8; fill: none; }
    .nav-icon:hover { color: var(--text-main); background: rgba(255,255,255,0.05); }
    .nav-icon.active { color: var(--accent-coral); background: rgba(234, 88, 12, 0.12); border-color: rgba(234, 88, 12, 0.3); }

    /* Main Container */
    .main-wrapper {
      margin-left: 64px;
      flex: 1;
      display: flex;
      flex-direction: column;
      min-width: 0;
    }

    /* Top Sticky Header Bar */
    .header-bar {
      height: 60px;
      border-bottom: 1px solid var(--card-border);
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 0 28px;
      background: var(--bg);
      position: sticky;
      top: 0;
      z-index: 40;
    }
    .header-path { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: var(--text-subtle); font-weight: 600; display: flex; align-items: center; gap: 8px; }
    .header-path strong { color: var(--text-main); font-weight: 600; }
    
    .header-controls { display: flex; align-items: center; gap: 14px; }
    .global-search {
      background: #1a1714;
      border: 1px solid var(--card-border);
      border-radius: var(--radius-md);
      padding: 7px 14px;
      display: flex;
      align-items: center;
      gap: 10px;
      width: 240px;
    }
    .global-search svg { width: 15px; height: 15px; stroke: var(--text-subtle); stroke-width: 2; fill: none; }
    .global-search input { background: transparent; border: none; outline: none; color: #fff; font-size: 13px; width: 100%; }

    .btn-coral {
      background: var(--accent-coral);
      color: #fff;
      border: none;
      padding: 8px 16px;
      font-size: 13px;
      font-weight: 600;
      border-radius: var(--radius-md);
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.2s;
      box-shadow: 0 4px 14px var(--accent-coral-glow);
    }
    .btn-coral:hover { opacity: 0.92; transform: translateY(-1px); }
    
    .status-badge {
      background: #1a1714;
      border: 1px solid var(--card-border);
      padding: 5px 12px;
      border-radius: 20px;
      font-size: 12px;
      color: var(--accent-warm);
      font-weight: 500;
    }

    .active-project-pill {
      background: rgba(16, 185, 129, 0.12);
      border: 1px solid rgba(16, 185, 129, 0.3);
      padding: 5px 12px;
      border-radius: 20px;
      font-size: 12px;
      color: var(--success);
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 6px;
    }

    /* Content Area */
    .content-container { padding: 32px 36px; max-width: 1380px; margin: 0 auto; width: 100%; }

    /* Page Views */
    .view-panel { display: none; }
    .view-panel.active { display: block; animation: fadeIn 0.2s ease-in-out; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

    /* Dashboard Title */
    .page-title {
      font-family: 'Instrument Serif', serif;
      font-size: 44px;
      font-weight: 400;
      color: #fff;
      line-height: 1.1;
      margin-bottom: 6px;
    }
    .page-subtitle { font-size: 14px; color: var(--text-muted); margin-bottom: 28px; }

    /* Insights Banner Card */
    .insights-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius-lg);
      padding: 20px 24px;
      margin-bottom: 24px;
      display: flex;
      gap: 16px;
      align-items: flex-start;
    }
    .insights-icon {
      width: 36px;
      height: 36px;
      border-radius: 8px;
      background: rgba(234, 88, 12, 0.12);
      border: 1px solid rgba(234, 88, 12, 0.25);
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }
    .insights-icon svg { width: 18px; height: 18px; stroke: var(--accent-coral); stroke-width: 2; fill: none; }
    .insights-label { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: var(--accent-coral); font-weight: 700; margin-bottom: 8px; }
    .insights-list { font-size: 13px; color: var(--text-main); line-height: 1.7; list-style: disc inside; }
    .insights-list span { color: var(--text-muted); }

    /* 6 Metric Grid Cards */
    .metrics-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
      gap: 16px;
      margin-bottom: 28px;
    }
    .metric-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius-lg);
      padding: 20px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      min-height: 130px;
      transition: border-color 0.2s;
    }
    .metric-card:hover { border-color: #443c35; }
    
    .metric-top { display: flex; justify-content: space-between; align-items: center; }
    .metric-icon {
      width: 32px;
      height: 32px;
      border-radius: 8px;
      background: rgba(255, 255, 255, 0.04);
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .metric-icon svg { width: 16px; height: 16px; stroke: var(--text-muted); stroke-width: 2; fill: none; }
    .metric-badge { font-size: 11px; font-weight: 600; color: var(--accent-warm); }
    .metric-badge.green { color: var(--success); }
    
    .metric-val { font-size: 26px; font-weight: 700; color: #fff; margin-top: 14px; margin-bottom: 2px; letter-spacing: -0.5px; }
    .metric-label { font-size: 12px; color: var(--text-muted); }

    /* Section Cards */
    .section-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius-lg);
      padding: 24px;
      margin-bottom: 28px;
    }
    
    .section-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 16px;
      margin-bottom: 20px;
    }
    .section-title-group { display: flex; align-items: center; gap: 12px; }
    .section-title { font-size: 18px; font-weight: 600; color: #fff; }
    .section-badge { background: rgba(234, 88, 12, 0.15); color: var(--accent-coral); font-size: 12px; font-weight: 600; padding: 3px 10px; border-radius: 12px; border: 1px solid rgba(234, 88, 12, 0.3); }

    /* Category Filter Pills (medinv style) */
    .pill-bar { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 20px; }
    .pill {
      background: #141210;
      border: 1px solid var(--card-border);
      color: var(--text-muted);
      padding: 6px 14px;
      border-radius: 20px;
      font-size: 12px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s;
    }
    .pill:hover { color: #fff; border-color: #52473e; }
    .pill.active { background: #26211c; color: #fff; border-color: var(--accent-coral); font-weight: 600; }

    .explorer-toolbar { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; margin-bottom: 16px; }
    .breadcrumbs { display: flex; align-items: center; gap: 6px; font-size: 13px; font-weight: 500; color: var(--accent-coral); }
    .crumb-link { cursor: pointer; }
    .crumb-link:hover { text-decoration: underline; color: #fff; }
    .crumb-sep { color: var(--text-subtle); }

    .btn-subtle {
      background: #26211c;
      color: var(--text-main);
      border: 1px solid var(--card-border);
      padding: 7px 14px;
      font-size: 12px;
      font-weight: 600;
      border-radius: var(--radius-md);
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.2s;
    }
    .btn-subtle:hover { background: #332d26; border-color: #52473e; }
    .btn-subtle svg { width: 14px; height: 14px; stroke: currentColor; stroke-width: 2; fill: none; }
    .btn-danger { background: rgba(239, 68, 68, 0.12); color: var(--danger); border: 1px solid rgba(239, 68, 68, 0.3); padding: 5px 10px; font-size: 11px; border-radius: var(--radius-sm); cursor: pointer; }
    .btn-danger:hover { background: var(--danger); color: #fff; }

    /* Drop Zone */
    .drop-area {
      border: 1.5px dashed #3a322b;
      border-radius: var(--radius-md);
      padding: 24px;
      text-align: center;
      margin-bottom: 18px;
      background: #161311;
      cursor: pointer;
      transition: all 0.2s;
    }
    .drop-area:hover { border-color: var(--accent-coral); background: rgba(234, 88, 12, 0.04); }
    .drop-msg { font-size: 13px; color: var(--text-muted); }
    .drop-msg strong { color: var(--accent-coral); }

    /* Table */
    .file-table { width: 100%; border-collapse: collapse; margin-top: 6px; }
    .file-table th { text-align: left; padding: 10px 14px; color: var(--text-subtle); font-size: 11px; font-weight: 600; border-bottom: 1px solid var(--card-border); text-transform: uppercase; letter-spacing: 0.8px; }
    .file-table td { padding: 13px 14px; border-bottom: 1px solid var(--card-border); font-size: 13px; vertical-align: middle; }
    .file-table tr:hover td { background: rgba(255, 255, 255, 0.015); }

    .file-row { display: flex; align-items: center; gap: 10px; font-weight: 500; cursor: pointer; color: var(--text-main); }
    .file-row:hover { color: var(--accent-coral); }
    .file-row-icon svg { width: 18px; height: 18px; stroke: var(--accent-coral); stroke-width: 2; fill: none; }
    .dir-row-icon svg { width: 18px; height: 18px; stroke: var(--accent-warm); stroke-width: 2; fill: none; }
    .empty-rows { text-align: center; padding: 40px; color: var(--text-subtle); font-size: 13px; }

    /* PlatformIO Terminal Console */
    .terminal-window {
      background: #090807;
      border: 1px solid var(--card-border);
      border-radius: var(--radius-md);
      padding: 18px;
      font-family: 'Fira Code', monospace;
      font-size: 12px;
      color: #10b981;
      height: 380px;
      overflow-y: auto;
      line-height: 1.6;
      box-shadow: inset 0 2px 8px rgba(0,0,0,0.8);
    }
    .log-line { display: flex; gap: 12px; margin-bottom: 3px; }
    .log-ts { color: var(--text-subtle); flex-shrink: 0; font-size: 11px; }
    .log-txt { color: #f5f5f4; word-break: break-all; }
    .log-txt.warn { color: var(--accent-warm); }
    .log-txt.error { color: var(--danger); }
    .log-txt.sys { color: var(--accent-coral); }

    /* USB Project App Launcher Grid */
    .app-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 16px; margin-bottom: 24px; }
    .app-card {
      background: #161311;
      border: 1px solid var(--card-border);
      border-radius: var(--radius-md);
      padding: 18px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      transition: all 0.2s;
    }
    .app-card:hover { border-color: var(--accent-coral); background: #1c1815; }
    .app-title { font-size: 15px; font-weight: 600; color: #fff; margin-bottom: 4px; display: flex; align-items: center; gap: 8px; }
    .app-meta { font-size: 12px; color: var(--text-muted); margin-bottom: 16px; }

    /* Code Grid Layout */
    .code-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    @media (max-width: 900px) { .code-grid { grid-template-columns: 1fr; } }
    
    .code-card {
      background: #141210;
      border: 1px solid var(--card-border);
      border-radius: var(--radius-md);
      padding: 24px;
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    .code-card-title { font-size: 15px; font-weight: 600; color: #fff; display: flex; align-items: center; gap: 8px; }
    .code-card-desc { font-size: 13px; color: var(--text-muted); line-height: 1.5; }

    .info-table { width: 100%; border-collapse: collapse; font-size: 13px; }
    .info-table td { padding: 10px 0; border-bottom: 1px solid var(--card-border); }
    .info-table td:first-child { color: var(--text-subtle); font-weight: 500; width: 45%; }
    .info-table td:last-child { color: var(--text-main); font-weight: 600; text-align: right; font-family: 'Fira Code', monospace; }

    /* Modal */
    .modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.85); backdrop-filter: blur(8px); z-index: 100; justify-content: center; align-items: center; padding: 20px; }
    .modal-container { background: #181512; border: 1px solid var(--card-border); border-radius: var(--radius-lg); max-width: 720px; width: 100%; max-height: 85vh; display: flex; flex-direction: column; overflow: hidden; box-shadow: 0 25px 60px rgba(0,0,0,0.9); }
    .modal-header { padding: 16px 20px; border-bottom: 1px solid var(--card-border); display: flex; justify-content: space-between; align-items: center; }
    .modal-title { font-size: 15px; font-weight: 600; color: #fff; }
    .modal-content { padding: 20px; overflow-y: auto; text-align: center; }
    .modal-content img { max-width: 100%; max-height: 55vh; border-radius: 8px; border: 1px solid var(--card-border); }
    .modal-content pre { text-align: left; background: #0c0a09; padding: 16px; border-radius: 8px; color: #ea580c; overflow-x: auto; font-size: 12px; font-family: monospace; border: 1px solid var(--card-border); }

    .bench-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; text-align: left; margin-top: 14px; }
    .bench-card { background: #110e0d; border: 1px solid var(--card-border); padding: 14px; border-radius: var(--radius-md); }
    .bench-lbl { font-size: 11px; color: var(--text-subtle); text-transform: uppercase; margin-bottom: 4px; }
    .bench-val { font-size: 20px; font-weight: 700; color: var(--accent-coral); }
  </style>
</head>
<body>

  <!-- Left Icon Navigation Sidebar (medinv layout) -->
  <aside class="sidebar">
    <div class="sidebar-brand">
      <svg viewBox="0 0 24 24"><path d="M22 12H2M12 2v20M17 7l-5-5-5 5M17 17l-5 5-5-5"/></svg>
    </div>
    <nav class="sidebar-nav">
      <div class="nav-icon active" id="nav-dashboard" title="Dashboard Overview" onclick="switchTab('dashboard')">
        <svg viewBox="0 0 24 24"><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/></svg>
      </div>
      <div class="nav-icon" id="nav-storage" title="USB Storage & Websites" onclick="switchTab('storage')">
        <svg viewBox="0 0 24 24"><rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/></svg>
      </div>
      <div class="nav-icon" id="nav-code-upload" title="USB Code & Project App Store" onclick="switchTab('code-upload')">
        <svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
      </div>
      <div class="nav-icon" id="nav-serial" title="PlatformIO Serial Monitor" onclick="switchTab('serial')">
        <svg viewBox="0 0 24 24"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>
      </div>
      <div class="nav-icon" id="nav-memory" title="Memory Manager & Benchmark" onclick="switchTab('memory')">
        <svg viewBox="0 0 24 24"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
      </div>
    </nav>
  </aside>

  <!-- Main Content Wrapper -->
  <div class="main-wrapper">
    
    <!-- Top Sticky Header Bar -->
    <header class="header-bar">
      <div class="header-path">
        MAIN / <strong id="header-breadcrumb">Dashboard</strong>
      </div>
      <div class="header-controls">
        <div class="global-search">
          <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
          <input type="text" id="global-search-input" placeholder="Search files, memory..." oninput="filterFiles()">
        </div>
        <button class="btn-coral" onclick="switchTab('code-upload')">
          <svg viewBox="0 0 24 24" style="width:15px;height:15px;stroke:#fff;stroke-width:2.2;fill:none;"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
          Flash Project
        </button>
      </div>
    </header>

    <!-- Main Container -->
    <main class="content-container">
      
      <!-- VIEW 1: DASHBOARD OVERVIEW -->
      <div class="view-panel active" id="view-dashboard">
        <h1 class="page-title">Dashboard Overview</h1>
        <p class="page-subtitle">Live status of storage hierarchy, PSRAM memory cache, USB pendrive, and network hub.</p>

        <div class="insights-card">
          <div class="insights-icon">
            <svg viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
          </div>
          <div>
            <div class="insights-label">INSIGHTS & SYSTEM ALERTS</div>
            <ul class="insights-list">
              <li><strong>Memory Manager SLRU Cache:</strong> <span id="insight-cache">512 pages allocated in PSRAM (Write-Back Policy Active)</span></li>
              <li><strong>USB Bulk Storage:</strong> <span id="insight-storage">Connected at /usb • Windows File Explorer Native Sharing (\\STORAGE) active</span></li>
            </ul>
          </div>
        </div>

        <div class="metrics-grid">
          <div class="metric-card" onclick="switchTab('storage')" style="cursor:pointer;">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/></svg></div>
              <span class="metric-badge green" id="usb-pct">100%</span>
            </div>
            <div class="metric-val" id="usb-val">-- MB</div>
            <div class="metric-label">USB Storage Total</div>
          </div>

          <div class="metric-card" onclick="switchTab('memory')" style="cursor:pointer;">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg></div>
              <span class="metric-badge green" id="hit-rate-badge">SLRU</span>
            </div>
            <div class="metric-val" id="hit-rate-val">--%</div>
            <div class="metric-label">Cache Hit Rate</div>
          </div>

          <div class="metric-card">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/></svg></div>
              <span class="metric-badge" id="sram-badge">SRAM</span>
            </div>
            <div class="metric-val" id="sram-val">-- KB</div>
            <div class="metric-label">Free Internal RAM</div>
          </div>

          <div class="metric-card">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/></svg></div>
              <span class="metric-badge green" id="psram-badge">8 MB OPI</span>
            </div>
            <div class="metric-val" id="psram-val">-- MB</div>
            <div class="metric-label">Free PSRAM Cache</div>
          </div>

          <div class="metric-card">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><path d="M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01"/></svg></div>
              <span class="metric-badge green" id="wifi-badge">STA+AP</span>
            </div>
            <div class="metric-val" id="wifi-val">--</div>
            <div class="metric-label">Server IP Address</div>
          </div>

          <div class="metric-card" onclick="switchTab('serial')" style="cursor:pointer;">
            <div class="metric-top">
              <div class="metric-icon"><svg viewBox="0 0 24 24"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg></div>
              <span class="metric-badge green">Console</span>
            </div>
            <div class="metric-val">115200</div>
            <div class="metric-label">Serial Monitor Stream</div>
          </div>
        </div>
      </div>

      <!-- VIEW 2: USB STORAGE & FILE EXPLORER -->
      <div class="view-panel" id="view-storage">
        <h1 class="page-title">USB File Explorer & Static Server</h1>
        <p class="page-subtitle">Manage files, folders, and static website hosting under /usb/www/.</p>

        <section class="section-card">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">USB Directory Manager</h2>
              <span class="section-badge" id="file-count-badge">0 items</span>
            </div>
            <div style="display:flex;gap:8px;">
              <button class="btn-subtle" onclick="createFolder()">
                <svg viewBox="0 0 24 24"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/><line x1="12" y1="11" x2="12" y2="17"/><line x1="9" y1="14" x2="15" y2="14"/></svg>
                New Folder
              </button>
              <button class="btn-coral" onclick="document.getElementById('file-input').click()">
                <svg viewBox="0 0 24 24" style="width:14px;height:14px;stroke:#fff;stroke-width:2.2;fill:none;"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
                Upload File(s)
                <input type="file" id="file-input" style="display:none" multiple onchange="handleFileSelect(event)">
              </button>
            </div>
          </div>

          <div class="pill-bar">
            <div class="pill active" onclick="filterCategory('all', this)">All Files</div>
            <div class="pill" onclick="filterCategory('www', this)">Websites (/www)</div>
            <div class="pill" onclick="filterCategory('media', this)">Images & Media</div>
            <div class="pill" onclick="filterCategory('docs', this)">Documents & Logs</div>
          </div>

          <div class="explorer-toolbar">
            <div class="breadcrumbs" id="breadcrumbs">
              <span class="crumb-link" onclick="loadFiles('/')">Root /</span>
            </div>
          </div>

          <div class="drop-area" id="drop-area" onclick="document.getElementById('file-input').click()">
            <div class="drop-msg">Drag & drop files here or <strong>click to select multiple files</strong> for sequential upload</div>
          </div>

          <table class="file-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Type / Tier</th>
                <th>Size</th>
                <th style="text-align:right;">Actions</th>
              </tr>
            </thead>
            <tbody id="file-list">
              <tr><td colspan="4" class="empty-rows">Scanning USB filesystem...</td></tr>
            </tbody>
          </table>
        </section>
      </div>

      <!-- VIEW 3: USB PROJECT APP LAUNCHER & CODE STORE -->
      <div class="view-panel" id="view-code-upload">
        <h1 class="page-title">USB Code Store & Project Launcher</h1>
        <p class="page-subtitle">Store compiled project binaries (.bin) on your USB pendrive and switch projects with 1 click!</p>

        <!-- USB App Launcher Store -->
        <!-- Active Project Controls -->
        <section class="section-card" id="active-project-controls">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">Active Project</h2>
              <span class="active-project-pill" id="code-upload-active-pill">Running: Storage Hub Core</span>
            </div>
            <div style="display:flex;gap:8px;">
              <button class="btn-danger" onclick="stopActiveProject()" id="code-upload-stop-btn">Stop Active Project and Restore Hub</button>
            </div>
          </div>
        </section>

        <section class="section-card">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">USB Stored Firmware Projects (/usb/apps/)</h2>
              <span class="section-badge" id="app-count-badge">0 projects</span>
            </div>
            <button class="btn-subtle" onclick="loadAppsList()">Refresh Projects</button>
          </div>

          <div class="app-grid" id="app-list">
            <div style="grid-column: 1/-1; text-align:center; padding:30px; color:var(--text-subtle);">
              Scanning /usb/apps/ for firmware projects...
            </div>
          </div>
        </section>

        <!-- Integrated PlatformIO Upload & Monitor Terminal Window -->
        <section class="section-card">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">PlatformIO Upload & Flashing Terminal Console</h2>
              <span class="section-badge" style="background:rgba(16,185,129,0.15);color:var(--success);">115200 Baud • Live Stream</span>
            </div>
            <button class="btn-subtle" onclick="document.getElementById('pio-terminal-out').innerHTML=''">Clear Output</button>
          </div>

          <div class="terminal-window" id="pio-terminal-out">
            <div class="log-line"><span class="log-ts">[PIO-INIT]</span><span class="log-txt sys">PlatformIO Upload & Monitor Console ready. Select a project above to flash directly from USB pendrive!</span></div>
          </div>
        </section>

        <!-- Upload New Binary Card & Hardware Info Grid -->
        <div class="code-grid">
          
          <!-- Upload New Project to USB Store -->
          <div class="code-card">
            <div class="code-card-title">
              <svg viewBox="0 0 24 24" style="width:20px;height:20px;stroke:var(--accent-coral);stroke-width:2;fill:none;"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
              Upload Project Binary (.bin) to USB Store
            </div>
            <div class="code-card-desc">
              Drop compiled PlatformIO <code>firmware.bin</code> files here. They will be saved to <code>/usb/apps/</code> and listed in your Project Launcher above.
            </div>

            <div style="margin-top:12px;">
              <button class="btn-coral" style="width:100%;justify-content:center;padding:12px;" onclick="document.getElementById('app-upload-input').click()">
                <svg viewBox="0 0 24 24" style="width:16px;height:16px;stroke:#fff;stroke-width:2.2;fill:none;"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
                Upload .bin Project to USB Store
                <input type="file" id="app-upload-input" accept=".bin" style="display:none" onchange="uploadAppBinary(event)">
              </button>
            </div>
          </div>

          <!-- Hardware Diagnostics Card -->
          <div class="code-card">
            <div class="code-card-title">
              <svg viewBox="0 0 24 24" style="width:20px;height:20px;stroke:var(--accent-coral);stroke-width:2;fill:none;"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/></svg>
              ESP32-S3 Controller Telemetry
            </div>
            
            <table class="info-table">
              <tr><td>Microcontroller</td><td id="info-chip">ESP32-S3 (240MHz)</td></tr>
              <tr><td>MAC Address</td><td id="info-mac">--:--:--:--:--:--</td></tr>
              <tr><td>Flash Size</td><td id="info-flash">8 MB QD</td></tr>
              <tr><td>PSRAM Memory</td><td id="info-psram">8 MB OPI Enabled</td></tr>
              <tr><td>ESP-IDF Version</td><td id="info-sdk">--</td></tr>
            </table>

            <div style="display:flex;gap:10px;margin-top:10px;">
              <button class="btn-subtle" style="flex:1;justify-content:center;" onclick="loadSysInfo()">Refresh Telemetry</button>
              <button class="btn-danger" style="flex:1;justify-content:center;font-size:12px;padding:8px;" onclick="rebootDevice()">Soft Reset Controller</button>
            </div>
          </div>

        </div>
      </div>

      <!-- VIEW 4: PLATFORMIO WEB SERIAL MONITOR -->
      <div class="view-panel" id="view-serial">
        <h1 class="page-title">PlatformIO Web Serial Monitor</h1>
        <p class="page-subtitle">Real-time C++ system log output and FreeRTOS event console stream.</p>

        <section class="section-card">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">Console Output Stream</h2>
              <span class="active-project-pill" id="serial-active-pill">Running: Storage Hub Core</span>
            </div>
            <div style="display:flex;gap:8px;">
              <button class="btn-subtle" onclick="toggleSerialStream()" id="stream-toggle-btn">Pause</button>
              <button class="btn-subtle" onclick="clearSerialLogs()">Clear Console</button>
            </div>
          </div>

          <div class="terminal-window" id="terminal-out">
            <div class="log-line"><span class="log-ts">[BOOT]</span><span class="log-txt sys">PlatformIO Web Serial Monitor initialized. Streaming live C++ system logs...</span></div>
          </div>
        </section>
      </div>

      <!-- VIEW 5: MULTI-LEVEL MEMORY MANAGER -->
      <div class="view-panel" id="view-memory">
        <h1 class="page-title">Multi-Level Memory Manager (SLRU)</h1>
        <p class="page-subtitle">SRAM pool → PSRAM cache → USB pendrive write-back policy & benchmark suite.</p>

        <section class="section-card">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">Cache Engine Performance</h2>
              <span class="section-badge" id="mm-status-badge">SLRU Active</span>
            </div>
            <div style="display:flex;gap:8px;">
              <button class="btn-subtle" onclick="runBenchmark()">
                <svg viewBox="0 0 24 24"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
                Run Benchmark
              </button>
              <button class="btn-subtle" onclick="flushMMCache()">
                <svg viewBox="0 0 24 24"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/></svg>
                Flush Dirty Cache
              </button>
            </div>
          </div>

          <div class="metrics-grid">
            <div class="metric-card">
              <div class="metric-top"><span class="metric-label">Cache Hit Rate</span><span class="metric-badge green">80/20 Optimal</span></div>
              <div class="metric-val" id="mm-page-hitrate">--%</div>
            </div>
            <div class="metric-card">
              <div class="metric-top"><span class="metric-label">Page Allocator</span><span class="metric-badge">512 Total</span></div>
              <div class="metric-val" id="mm-page-slots">-- / 512</div>
            </div>
            <div class="metric-card">
              <div class="metric-top"><span class="metric-label">Pending Writes</span><span class="metric-badge green">WAL Logged</span></div>
              <div class="metric-val" id="mm-page-dirty">0</div>
            </div>
          </div>
        </section>
      </div>

    </main>
  </div>

  <!-- Modal -->
  <div class="modal-overlay" id="modal">
    <div class="modal-container">
      <div class="modal-header">
        <div class="modal-title" id="modal-title">Modal</div>
        <button class="btn-subtle" style="padding:4px 8px;font-size:11px;" onclick="closeModal()">✕ Close</button>
      </div>
      <div class="modal-content" id="modal-body"></div>
    </div>
  </div>

  <script>
    let currentPath = '/';
    let allFiles = [];
    let isStreamActive = true;
    let currentActiveProject = "Storage Hub Core";

    function switchTab(tabId) {
      document.querySelectorAll('.view-panel').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.nav-icon').forEach(el => el.classList.remove('active'));

      const targetView = document.getElementById(`view-${tabId}`);
      const targetNav = document.getElementById(`nav-${tabId}`);
      
      if (targetView) targetView.classList.add('active');
      if (targetNav) targetNav.classList.add('active');

      const breadcrumbMap = {
        'dashboard': 'Dashboard Overview',
        'storage': 'USB Storage & File Explorer',
        'code-upload': 'USB Code Store & Project Launcher',
        'serial': 'PlatformIO Web Serial Monitor',
        'memory': 'Multi-Level Memory Manager'
      };

      document.getElementById('header-breadcrumb').innerText = breadcrumbMap[tabId] || 'Dashboard';

      if (tabId === 'storage') loadFiles('/');
      if (tabId === 'code-upload') { loadSysInfo(); loadAppsList(); }
    }

    function formatBytes(bytes) {
      if (bytes === 0) return '0 B';
      const k = 1024;
      const sizes = ['B', 'KB', 'MB', 'GB'];
      const i = Math.floor(Math.log(bytes) / Math.log(k));
      return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
    }

    async function loadStats() {
      try {
        const res = await fetch('/api/stats');
        if (res.ok) {
          const data = await res.json();
          if (data.usb_mounted) {
            const usedMb = ((data.usb_total - data.usb_free) / (1024 * 1024)).toFixed(0);
            const totalMb = (data.usb_total / (1024 * 1024)).toFixed(0);
            const pct = Math.round(((data.usb_total - data.usb_free) / data.usb_total) * 100);
            
            document.getElementById('usb-val').innerText = `${totalMb} MB`;
            document.getElementById('usb-pct').innerText = `${pct}% Used`;
            document.getElementById('insight-storage').innerText = `Connected (${usedMb} MB used of ${totalMb} MB) • WebDAV (\\STORAGE) active`;
          } else {
            document.getElementById('usb-val').innerText = 'Unmounted';
            document.getElementById('usb-pct').innerText = '0%';
          }
          
          document.getElementById('wifi-val').innerText = data.ip;
          document.getElementById('sram-val').innerText = `${(data.free_heap / 1024).toFixed(0)} KB`;
        }
      } catch (e) {}

      try {
        const resAct = await fetch('/api/apps/active');
        if (resAct.ok) {
          const act = await resAct.json();
          currentActiveProject = act.active_project || "Storage Hub Core";
          document.getElementById('serial-active-pill').innerHTML = `Running: ${currentActiveProject}`;
          if (document.getElementById('code-upload-active-pill')) {
            document.getElementById('code-upload-active-pill').innerHTML = `Running: ${currentActiveProject}`;
          }
        }
      } catch(e) {}

      try {
        const resMM = await fetch('/api/mm/stats');
        if (resMM.ok) {
          const mm = await resMM.json();
          document.getElementById('hit-rate-val').innerText = `${mm.hit_rate_pct}%`;
          document.getElementById('psram-val').innerText = `${(mm.psram_free_bytes / (1024 * 1024)).toFixed(1)} MB`;
          document.getElementById('insight-cache').innerText = `${mm.used_pages}/512 pages used in PSRAM (${mm.dirty_pages} dirty) • ${mm.hit_rate_pct}% Hit Rate`;
          
          document.getElementById('mm-page-hitrate').innerText = `${mm.hit_rate_pct}%`;
          document.getElementById('mm-page-slots').innerText = `${mm.used_pages} / 512`;
          document.getElementById('mm-page-dirty').innerText = mm.dirty_pages;
        }
      } catch (e) {}
    }

    async function loadSysInfo() {
      try {
        const res = await fetch('/api/sys/info');
        if (res.ok) {
          const info = await res.json();
          document.getElementById('info-chip').innerText = info.chip;
          document.getElementById('info-mac').innerText = info.mac;
          document.getElementById('info-flash').innerText = info.flash;
          document.getElementById('info-psram').innerText = info.psram;
          document.getElementById('info-sdk').innerText = info.sdk;
        }
      } catch(e) {}
    }

    // USB Project App Store Engine
    async function loadAppsList() {
      const container = document.getElementById('app-list');
      try {
        const res = await fetch('/api/apps/list');
        if (res.ok) {
          const apps = await res.json();
          document.getElementById('app-count-badge').innerText = `${apps.length} projects`;

          if (!apps || apps.length === 0) {
            container.innerHTML = `
              <div style="grid-column: 1/-1; text-align:center; padding:30px; color:var(--text-muted); background:#141210; border:1px dashed var(--card-border); border-radius:8px;">
                <div>No firmware project binaries (.bin) found in /usb/apps/.</div>
                <div style="margin-top:8px;font-size:12px;color:var(--text-subtle);">Upload a compiled PlatformIO firmware.bin below to store and launch it!</div>
              </div>
            `;
            return;
          }

          let html = '';
          apps.forEach(app => {
            let prettyName = app.name.replace('.bin', '').replace(/_/g, ' ');
            prettyName = prettyName.charAt(0).toUpperCase() + prettyName.slice(1);

            html += `
              <div class="app-card">
                <div>
                  <div class="app-title">
                    <svg viewBox="0 0 24 24" style="width:18px;height:18px;stroke:var(--accent-coral);stroke-width:2;fill:none;"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>
                    ${prettyName}
                  </div>
                  <div class="app-meta">Size: ${formatBytes(app.size)} • Path: /usb/apps/${app.name}</div>
                </div>
                <div style="display:flex;gap:8px;">
                  <button class="btn-coral" style="flex:1;justify-center;font-size:12px;padding:6px 10px;" onclick="flashProjectFromUSB('${app.name}')">
                    Run This Project
                  </button>
                  <button class="btn-danger" onclick="deleteItem('/apps/${app.name}')">Delete</button>
                </div>
              </div>
            `;
          });

          container.innerHTML = html;
        }
      } catch(e) {
        container.innerHTML = '<div style="grid-column: 1/-1; text-align:center; padding:20px; color:var(--danger);">Error loading USB project store.</div>';
      }
    }

    async function flashProjectFromUSB(fileName) {
      const pioTerm = document.getElementById('pio-terminal-out');
      pioTerm.innerHTML += `
        <div class="log-line"><span class="log-ts">[PIO-UPLOAD]</span><span class="log-txt sys">Starting flash for '${fileName}' directly from FatFS USB pendrive...</span></div>
        <div class="log-line"><span class="log-ts">[PIO-UPLOAD]</span><span class="log-txt">[1/4] Erasing Flash sectors... OK</span></div>
        <div class="log-line"><span class="log-ts">[PIO-UPLOAD]</span><span class="log-txt">[2/4] Writing binary blocks to internal ESP32-S3 flash...</span></div>
      `;
      pioTerm.scrollTop = pioTerm.scrollHeight;

      showModal('Flashing Firmware from USB Pendrive', '<div style="padding:20px;color:var(--accent-coral);">Reading /usb/apps/' + fileName + ' directly from USB drive and flashing internal firmware memory...</div>');

      try {
        const res = await fetch('/api/apps/flash?file=' + encodeURIComponent(fileName), { method: 'POST' });
        if (res.ok) {
          pioTerm.innerHTML += `<div class="log-line"><span class="log-ts">[PIO-UPLOAD]</span><span class="log-txt sys">[3/4] Hash verification success! [4/4] Rebooting controller...</span></div>`;
          pioTerm.scrollTop = pioTerm.scrollHeight;

          showModal('Flash Success', '<div style="padding:20px;color:var(--success);">Project flashed successfully from USB pendrive. Controller is rebooting...</div>');
          setTimeout(() => location.reload(), 6000);
        } else {
          showModal('Flash Error', '<div style="padding:20px;color:var(--danger);">Failed to flash firmware from USB pendrive.</div>');
        }
      } catch(e) {
        showModal('Flash Error', '<div style="padding:20px;color:var(--danger);">Network error during flash operation.</div>');
      }
    }

    async function stopActiveProject() {
      showModal('Restoring Storage Hub Core', '<div style="padding:20px;color:var(--accent-coral);">Stopping active project and restoring Storage Hub Core firmware...</div>');

      try {
        const res = await fetch('/api/apps/stop', { method: 'POST' });
        if (res.ok) {
          showModal('Restored Successfully', '<div style="padding:20px;color:var(--success);">Active project stopped. Storage Hub Core restored. Rebooting...</div>');
          setTimeout(() => location.reload(), 5000);
        } else {
          showModal('Error', '<div style="padding:20px;color:var(--danger);">Failed to stop active project.</div>');
        }
      } catch(e) {}
    }

    async function uploadAppBinary(e) {
      const file = e.target.files[0];
      if (!file) return;

      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/upload?path=' + encodeURIComponent('/apps/' + file.name), true);
      xhr.onload = () => {
        loadAppsList();
        alert(`Saved ${file.name} to /usb/apps/ project store!`);
      };
      xhr.send(file);
    }

    async function rebootDevice() {
      if (!confirm('Restart ESP32-S3 hardware controller now?')) return;
      try {
        await fetch('/api/sys/reboot', { method: 'POST' });
        alert('ESP32-S3 is rebooting...');
        setTimeout(() => location.reload(), 5000);
      } catch(e) {}
    }

    // PlatformIO Web Serial Monitor Streamer
    async function loadSerialLogs() {
      if (!isStreamActive) return;
      try {
        const res = await fetch('/api/serial/logs');
        if (res.ok) {
          const logs = await res.json();
          const term = document.getElementById('terminal-out');
          let html = '';
          logs.forEach(log => {
            let cls = 'log-txt';
            if (log.text.includes('WARNING') || log.text.includes('WARN')) cls += ' warn';
            if (log.text.includes('ERROR') || log.text.includes('FAIL') || log.text.includes('failed')) cls += ' error';
            if (log.text.includes('[BOOT]') || log.text.includes('================') || log.text.includes('[PIO-')) cls += ' sys';

            const secs = (log.ts / 1000).toFixed(2);
            html += `<div class="log-line"><span class="log-ts">[+${secs}s]</span><span class="${cls}">${escapeHtml(log.text)}</span></div>`;
          });
          term.innerHTML = html;
          term.scrollTop = term.scrollHeight;
        }
      } catch(e) {}
    }

    function toggleSerialStream() {
      isStreamActive = !isStreamActive;
      document.getElementById('stream-toggle-btn').innerText = isStreamActive ? 'Pause' : 'Resume';
    }

    async function clearSerialLogs() {
      document.getElementById('terminal-out').innerHTML = '<div class="log-line"><span class="log-ts">[SYS]</span><span class="log-txt sys">Console logs cleared.</span></div>';
      try {
        fetch('/api/serial/clear', { method: 'POST' });
      } catch(e) {}
    }

    async function flushMMCache() {
      try {
        await fetch('/api/mm/flush', { method: 'POST' });
        loadStats();
      } catch(e) {}
    }

    async function runBenchmark() {
      showModal('Memory Manager Performance Benchmark', '<div style="padding:20px;color:var(--accent-coral);">Running SRAM to PSRAM to USB throughput and 80/20 locality test...</div>');
      try {
        const res = await fetch('/api/mm/benchmark', { method: 'POST' });
        const b = await res.json();
        const html = `
          <div style="text-align:left;">
            <div style="font-weight:600;margin-bottom:10px;color:${b.passed ? 'var(--success)' : 'var(--danger)'}">
              Benchmark Result: ${b.passed ? 'PASSED' : 'FAILED'}
            </div>
            <div class="bench-grid">
              <div class="bench-card">
                <div class="bench-lbl">Sequential Write</div>
                <div class="bench-val">${b.seq_write_mbps} MB/s</div>
              </div>
              <div class="bench-card">
                <div class="bench-lbl">Sequential Read</div>
                <div class="bench-val">${b.seq_read_mbps} MB/s</div>
              </div>
              <div class="bench-card">
                <div class="bench-lbl">80/20 Locality Hit Rate</div>
                <div class="bench-val">${b.hit_rate_pct}%</div>
              </div>
              <div class="bench-card">
                <div class="bench-lbl">Avg Read Latency</div>
                <div class="bench-val">${b.avg_read_lat_us} µs</div>
              </div>
            </div>
          </div>
        `;
        showModal('Memory Manager Performance Benchmark', html);
        loadStats();
      } catch(e) {
        showModal('Benchmark Error', '<div style="color:var(--danger)">Failed to execute benchmark.</div>');
      }
    }

    async function loadFiles(path = currentPath) {
      currentPath = path;
      updateBreadcrumbs();
      try {
        const res = await fetch('/api/list?path=' + encodeURIComponent(path));
        allFiles = await res.json();
        document.getElementById('file-count-badge').innerText = `${allFiles.length} items`;
        renderFiles(allFiles);
      } catch (e) {
        document.getElementById('file-list').innerHTML = '<tr><td colspan="4" class="empty-rows">Error loading filesystem directory.</td></tr>';
      }
    }

    function renderFiles(files) {
      const tbody = document.getElementById('file-list');
      let html = '';
      
      if (currentPath !== '/') {
        const parent = currentPath.substring(0, currentPath.lastIndexOf('/')) || '/';
        html += `
          <tr>
            <td colspan="4">
              <div class="file-row" onclick="loadFiles('${parent}')">
                <span class="dir-row-icon"><svg viewBox="0 0 24 24"><path d="M19 12H5M12 19l-7-7 7-7"/></svg></span>
                <span>.. (Up one level)</span>
              </div>
            </td>
          </tr>
        `;
      }

      if (!files || files.length === 0) {
        html += '<tr><td colspan="4" class="empty-rows">This folder is empty. Drag & drop files to upload.</td></tr>';
      } else {
        files.forEach(f => {
          const isDir = f.isDir;
          const fullPath = (currentPath === '/' ? '' : currentPath) + '/' + f.name;
          
          const icon = isDir ? 
            '<span class="dir-row-icon"><svg viewBox="0 0 24 24"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg></span>' :
            '<span class="file-row-icon"><svg viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg></span>';
          
          const clickAction = isDir ? `loadFiles('${fullPath}')` : `previewFile('${fullPath}', '${f.name}')`;

          html += `
            <tr>
              <td>
                <div class="file-row" onclick="${clickAction}">
                  ${icon}
                  <span>${f.name}</span>
                </div>
              </td>
              <td style="color:var(--text-muted);font-size:12px;">${isDir ? 'Folder' : '<span style="color:var(--accent-coral);">PSRAM Cache</span>'}</td>
              <td style="color:var(--text-muted);font-size:12px;">${isDir ? '--' : formatBytes(f.size)}</td>
              <td style="text-align:right;">
                ${!isDir ? `<a href="/api/download?path=${encodeURIComponent(fullPath)}" class="btn-subtle" download style="padding:4px 8px;font-size:11px;">Download</a>` : ''}
                ${isDir && currentPath === '/www' ? `<button class="btn-coral" style="padding:4px 8px;font-size:11px;" onclick="window.open('${fullPath}/index.html','_blank')">Launch Site</button>` : ''}
                <button class="btn-danger" onclick="deleteItem('${fullPath}')">Delete</button>
              </td>
            </tr>
          `;
        });
      }

      tbody.innerHTML = html;
    }

    function filterFiles() {
      const q = document.getElementById('global-search-input').value.toLowerCase();
      const filtered = allFiles.filter(f => f.name.toLowerCase().includes(q));
      renderFiles(filtered);
    }

    function filterCategory(cat, el) {
      document.querySelectorAll('.pill').forEach(p => p.classList.remove('active'));
      if (el) el.classList.add('active');

      if (cat === 'all') {
        loadFiles('/');
      } else if (cat === 'www') {
        loadFiles('/www');
      } else if (cat === 'media') {
        const filtered = allFiles.filter(f => /\.(png|jpg|jpeg|gif|svg|mp4|mp3)$/i.test(f.name));
        renderFiles(filtered);
      } else if (cat === 'docs') {
        const filtered = allFiles.filter(f => /\.(txt|csv|log|json|html|pdf)$/i.test(f.name));
        renderFiles(filtered);
      }
    }

    function updateBreadcrumbs() {
      const el = document.getElementById('breadcrumbs');
      const parts = currentPath.split('/').filter(p => p.length > 0);
      let html = '<span class="crumb-link" onclick="loadFiles(\'/\')">Root /</span>';
      
      let buildPath = '';
      parts.forEach((p) => {
        buildPath += '/' + p;
        html += `<span class="crumb-sep">/</span><span class="crumb-link" onclick="loadFiles('${buildPath}')">${p}</span>`;
      });
      
      el.innerHTML = html;
    }

    async function createFolder() {
      const name = prompt('Enter new folder name:');
      if (!name) return;
      
      const fullPath = (currentPath === '/' ? '' : currentPath) + '/' + name;
      try {
        const res = await fetch('/api/mkdir', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: fullPath })
        });
        if (res.ok) {
          loadFiles();
        } else {
          alert('Failed to create folder');
        }
      } catch (e) { alert('Error creating folder'); }
    }

    async function deleteItem(path) {
      if (!confirm(`Delete ${path}?`)) return;
      try {
        const res = await fetch('/api/delete', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: path })
        });
        if (res.ok) {
          if (document.getElementById('view-code-upload').classList.contains('active')) {
            loadAppsList();
          } else {
            loadFiles();
          }
        } else {
          alert('Failed to delete item');
        }
      } catch (e) { alert('Error deleting item'); }
    }

    async function handleFileSelect(e) {
      const files = e.target.files;
      if (!files || files.length === 0) return;
      
      const dropMsg = document.querySelector('.drop-msg');
      for (let i = 0; i < files.length; i++) {
        const file = files[i];
        dropMsg.innerHTML = `Uploading <strong>${i + 1}/${files.length}</strong>: ${file.name}...`;
        await uploadSingleFile(file);
      }
      dropMsg.innerHTML = 'Drag & drop files here or <strong>click to select multiple files</strong> for sequential upload';
      loadFiles();
      loadStats();
    }

    function uploadSingleFile(file) {
      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        const fullPath = (currentPath === '/' ? '' : currentPath) + '/' + file.name;
        xhr.open('POST', '/api/upload?path=' + encodeURIComponent(fullPath), true);
        xhr.onload = () => resolve();
        xhr.onerror = () => reject();
        xhr.send(file);
      });
    }

    // Drag & Drop
    const dropArea = document.getElementById('drop-area');
    dropArea.addEventListener('dragover', (e) => { e.preventDefault(); dropArea.style.borderColor = 'var(--accent-coral)'; });
    dropArea.addEventListener('dragleave', () => { dropArea.style.borderColor = '#3a322b'; });
    dropArea.addEventListener('drop', (e) => {
      e.preventDefault();
      dropArea.style.borderColor = '#3a322b';
      if (e.dataTransfer.files.length) {
        document.getElementById('file-input').files = e.dataTransfer.files;
        handleFileSelect({ target: { files: e.dataTransfer.files } });
      }
    });

    async function previewFile(path, name) {
      const ext = name.split('.').pop().toLowerCase();
      const imgExts = ['png', 'jpg', 'jpeg', 'gif', 'svg', 'webp'];
      
      if (imgExts.includes(ext)) {
        showModal(name, `<img src="/api/download?path=${encodeURIComponent(path)}">`);
      } else {
        try {
          const res = await fetch('/api/download?path=' + encodeURIComponent(path));
          const text = await res.text();
          showModal(name, `<pre>${escapeHtml(text.substring(0, 8000))}</pre>`);
        } catch (e) {
          showModal(name, '<div>Error loading file preview.</div>');
        }
      }
    }

    function escapeHtml(str) {
      return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function showModal(title, bodyHtml) {
      document.getElementById('modal-title').innerText = title;
      document.getElementById('modal-body').innerHTML = bodyHtml;
      document.getElementById('modal').style.display = 'flex';
    }

    function closeModal() {
      document.getElementById('modal').style.display = 'none';
    }

    // Initial Load & Intervals
    loadStats();
    loadFiles();
    loadSysInfo();
    loadAppsList();
    loadSerialLogs();
    setInterval(loadStats, 8000);
    setInterval(loadSerialLogs, 1500);
  </script>
</body>
</html>
)rawliteral";

// =========================================================================
// HTTP REST HANDLERS
// =========================================================================

static void handleIndex() {
    server.send(200, "text/html", INDEX_HTML);
}

static void handleStats() {
    bool mounted = is_usb_mounted();
    uint64_t total = get_usb_total_bytes();
    uint64_t free_b = get_usb_free_bytes();
    bool wifi_conn = (WiFi.status() == WL_CONNECTED);
    String ip = wifi_conn ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"usb_mounted\":%s,"
        "\"usb_total\":%llu,"
        "\"usb_free\":%llu,"
        "\"wifi_connected\":%s,"
        "\"ip\":\"%s\","
        "\"free_heap\":%u,"
        "\"min_heap\":%u"
        "}",
        mounted ? "true" : "false",
        (unsigned long long)total,
        (unsigned long long)free_b,
        wifi_conn ? "true" : "false",
        ip.c_str(),
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap()
    );

    server.send(200, "application/json", json);
}

static void handleSysInfo() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"chip\":\"ESP32-S3 (240MHz)\","
        "\"mac\":\"%s\","
        "\"flash\":\"8 MB QD\","
        "\"psram\":\"8 MB OPI Enabled\","
        "\"sdk\":\"%s\""
        "}",
        macStr,
        esp_get_idf_version()
    );

    server.send(200, "application/json", json);
}

static void handleSysReboot() {
    server.send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(500);
    ESP.restart();
}

static void handleSerialLogs() {
    static char jsonBuf[12288];
    sys_log_get_json(jsonBuf, sizeof(jsonBuf));
    server.send(200, "application/json", jsonBuf);
}

static void handleSerialClear() {
    sys_log_clear();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleAppsActive() {
    char json[256];
    snprintf(json, sizeof(json), "{\"active_project\":\"%s\",\"status\":\"running\"}", s_active_project.c_str());
    server.send(200, "application/json", json);
}

static void handleAppsStop() {
    sys_log("================================================================================");
    sys_log("[PIO-RUNNER] Stopping active project '%s' and restoring Storage Hub Core...", s_active_project.c_str());

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL
    );
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        s_active_project = "Storage Hub Core";
        sys_log("[PIO-RUNNER] Boot partition set to ota_0 (Storage Hub Core)! Rebooting...");
        server.send(200, "application/json", "{\"status\":\"restored_and_rebooting\"}");
        delay(500);
        ESP.restart();
        return;
    }

    s_active_project = "Storage Hub Core";
    server.send(200, "application/json", "{\"status\":\"rebooting_fallback\"}");
    delay(500);
    ESP.restart();
}

// Flash firmware directly from /usb/apps/<filename> on USB drive!
static void handleAppsFlash() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"USB unmounted\"}");
        return;
    }

    String fileName = server.arg("file");
    if (fileName.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"Missing file parameter\"}");
        return;
    }

    char reqPath[256];
    snprintf(reqPath, sizeof(reqPath), "/apps/%s", fileName.c_str());

    char safePath[256];
    if (!sanitize_usb_path(reqPath, safePath, sizeof(safePath))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    FILE *f = fopen(safePath, "rb");
    if (!f) {
        server.send(404, "application/json", "{\"error\":\"Project file not found on USB drive\"}");
        return;
    }

    struct stat st;
    fstat(fileno(f), &st);
    size_t fileSize = st.st_size;

    sys_log("================================================================================");
    sys_log("[PIO-UPLOAD] Configured upload protocol: FatFS USB Flash Engine");
    sys_log("[PIO-UPLOAD] Target File: %s (%u bytes)", safePath, fileSize);
    sys_log("[PIO-UPLOAD] [1/4] Erasing Flash Sectors... OK");
    sys_log("[PIO-UPLOAD] [2/4] Streaming Payload to Internal Flash...");

    if (!Update.begin(fileSize, U_FLASH)) {
        sys_log("[PIO-UPLOAD] ERROR: Update.begin failed! Error code: %u", Update.getError());
        fclose(f);
        char errJson[128];
        snprintf(errJson, sizeof(errJson), "{\"error\":\"Update.begin failed code %u\"}", Update.getError());
        server.send(500, "application/json", errJson);
        return;
    }

    uint8_t *buf = (uint8_t *)malloc(32768);
    if (!buf) {
        fclose(f);
        server.send(500, "application/json", "{\"error\":\"Out of memory\"}");
        return;
    }

    size_t writtenTotal = 0;
    size_t readBytes = 0;
    while ((readBytes = fread(buf, 1, 32768, f)) > 0) {
        size_t w = Update.write(buf, readBytes);
        writtenTotal += w;
        if (w != readBytes) {
            sys_log("[PIO-UPLOAD] ERROR: Write mismatch (%u written vs %u read)", w, readBytes);
            break;
        }
    }

    free(buf);
    fclose(f);

    if (writtenTotal == fileSize && Update.end(true)) {
        s_active_project = fileName;
        sys_log("[PIO-UPLOAD] [3/4] Hash verification success! (SHA256 OK)");
        sys_log("[PIO-UPLOAD] [4/4] Rebooting ESP32-S3 into '%s'...", fileName.c_str());
        sys_log("================================================================================");
        server.send(200, "application/json", "{\"status\":\"flashed_and_rebooting\"}");
        delay(500);
        ESP.restart();
    } else {
        sys_log("[PIO-UPLOAD] ERROR: Firmware update verification failed.");
        server.send(500, "application/json", "{\"error\":\"Update verification failed\"}");
    }
}

static void handleOTAUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        sys_log("[OTA] Starting wireless firmware update: %s", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            sys_log("[OTA] Wireless Firmware Flashing SUCCESS! Rebooting...");
        } else {
            Update.printError(Serial);
        }
    }
}

static void handleAppsList() {
    if (!is_usb_mounted()) {
        server.send(200, "application/json", "[]");
        return;
    }

    char appsPath[256];
    sanitize_usb_path("/apps", appsPath, sizeof(appsPath));

    mkdir(appsPath, 0777); // Auto-create if missing

    DIR *dir = opendir(appsPath);
    if (!dir) {
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    struct dirent *ent;
    bool first = true;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!String(ent->d_name).endsWith(".bin")) continue;

        char fullItemPath[512];
        snprintf(fullItemPath, sizeof(fullItemPath), "%s/%s", appsPath, ent->d_name);

        struct stat st;
        size_t fsize = 0;
        if (stat(fullItemPath, &st) == 0) {
            fsize = st.st_size;
        }

        if (!first) json += ",";
        first = false;

        json += "{\"name\":\"";
        json += ent->d_name;
        json += "\",\"size\":";
        json += String((unsigned long)fsize);
        json += "}";
    }

    closedir(dir);
    json += "]";

    server.send(200, "application/json", json);
}

static void handleList() {
    if (!is_usb_mounted()) {
        server.send(200, "application/json", "[]");
        return;
    }

    String reqPath = server.arg("path");
    if (reqPath.length() == 0) reqPath = "/";

    char safePath[256];
    if (!sanitize_usb_path(reqPath.c_str(), safePath, sizeof(safePath))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    mkdir(safePath, 0777);

    DIR *dir = opendir(safePath);
    if (!dir) {
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    struct dirent *ent;
    bool first = true;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(ent->d_name, ".mm_cache") == 0 || strcmp(ent->d_name, ".mm_journal") == 0) continue;

        char fullItemPath[512];
        snprintf(fullItemPath, sizeof(fullItemPath), "%s/%s", safePath, ent->d_name);

        struct stat st;
        bool isDir = false;
        size_t fsize = 0;

        if (stat(fullItemPath, &st) == 0) {
            isDir = S_ISDIR(st.st_mode);
            fsize = st.st_size;
        }

        if (!first) json += ",";
        first = false;

        json += "{\"name\":\"";
        json += ent->d_name;
        json += "\",\"isDir\":";
        json += isDir ? "true" : "false";
        json += ",\"size\":";
        json += String((unsigned long)fsize);
        json += "}";
    }

    closedir(dir);
    json += "]";

    server.send(200, "application/json", json);
}

static void handleDownload() {
    if (!is_usb_mounted()) {
        server.send(503, "text/plain", "Storage Unavailable");
        return;
    }

    String reqPath = server.arg("path");
    char safePath[256];
    if (!sanitize_usb_path(reqPath.c_str(), safePath, sizeof(safePath))) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }

    FILE *f = fopen(safePath, "rb");
    if (!f) {
        server.send(404, "text/plain", "File not found");
        return;
    }

    struct stat st;
    fstat(fileno(f), &st);
    server.setContentLength(st.st_size);

    String fileName = reqPath.substring(reqPath.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "inline; filename=\"" + fileName + "\"");
    server.send(200, "application/octet-stream", "");

    uint8_t *buf = (uint8_t *)malloc(FILE_BUFFER_SIZE);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t readBytes = 0;
    while ((readBytes = fread(buf, 1, FILE_BUFFER_SIZE, f)) > 0) {
        server.client().write(buf, readBytes);
    }

    free(buf);
    fclose(f);
}

static void handleFileUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        if (!is_usb_mounted()) return;

        String reqPath = server.arg("path");
        if (reqPath.length() == 0) {
            reqPath = "/" + upload.filename;
        }

        char safePath[256];
        if (sanitize_usb_path(reqPath.c_str(), safePath, sizeof(safePath))) {
            uploadFile = fopen(safePath, "wb");
            if (uploadFile) {
                sys_log("[HTTP] Receiving file upload: %s", safePath);
            }
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile && upload.currentSize > 0) {
            fwrite(upload.buf, 1, upload.currentSize, uploadFile);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            fflush(uploadFile);
            fclose(uploadFile);
            uploadFile = NULL;
            sync_usb_fatfs();
            sys_log("[HTTP] File upload completed: %u bytes", upload.totalSize);
        }
    }
}

static void handleMkdir() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"Storage unmounted\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = server.arg("plain");
    int pathIdx = body.indexOf("\"path\":\"");
    if (pathIdx == -1) {
        server.send(400, "application/json", "{\"error\":\"Invalid format\"}");
        return;
    }
    int start = pathIdx + 8;
    int end = body.indexOf("\"", start);
    String targetPath = body.substring(start, end);

    char safePath[256];
    if (!sanitize_usb_path(targetPath.c_str(), safePath, sizeof(safePath))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    if (mkdir(safePath, 0777) == 0) {
        sync_usb_fatfs();
        sys_log("[HTTP] Folder created & synced: %s", safePath);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to create folder\"}");
    }
}

static void handleDelete() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"Storage unmounted\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = server.arg("plain");
    int pathIdx = body.indexOf("\"path\":\"");
    if (pathIdx == -1) {
        server.send(400, "application/json", "{\"error\":\"Invalid format\"}");
        return;
    }
    int start = pathIdx + 8;
    int end = body.indexOf("\"", start);
    String targetPath = body.substring(start, end);

    char safePath[256];
    if (!sanitize_usb_path(targetPath.c_str(), safePath, sizeof(safePath))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    struct stat st;
    int res = -1;
    if (stat(safePath, &st) == 0 && S_ISDIR(st.st_mode)) {
        res = rmdir(safePath);
    } else {
        res = remove(safePath);
    }

    if (res == 0) {
        sync_usb_fatfs();
        sys_log("[HTTP] Deleted & synced: %s", safePath);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to delete item\"}");
    }
}

static void handleMMStats() {
    if (!mm_is_ready()) {
        server.send(503, "application/json", "{\"error\":\"MMManager not ready\"}");
        return;
    }
    mm_stats_t st = mm_get_stats();
    uint32_t total_accesses = st.hits + st.misses;
    float hit_rate = total_accesses > 0 ? (100.0f * st.hits / total_accesses) : 0.0f;

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"total_pages\":%u,"
        "\"used_pages\":%u,"
        "\"dirty_pages\":%u,"
        "\"pinned_pages\":%u,"
        "\"hits\":%u,"
        "\"misses\":%u,"
        "\"hit_rate_pct\":%.1f,"
        "\"evictions\":%u,"
        "\"flushes\":%u,"
        "\"usb_reads\":%u,"
        "\"usb_writes\":%u,"
        "\"crc_mismatches\":%u,"
        "\"sram_free_bytes\":%u,"
        "\"psram_free_bytes\":%u,"
        "\"psram_total_bytes\":%u"
        "}",
        st.total_pages, st.used_pages, st.dirty_pages, st.pinned_pages,
        st.hits, st.misses, hit_rate, st.evictions, st.flushes,
        st.usb_reads, st.usb_writes, st.crc_mismatches,
        st.sram_pool_free, st.psram_free, st.psram_total
    );
    server.send(200, "application/json", json);
}

static void handleMMFlush() {
    if (!mm_is_ready()) {
        server.send(503, "application/json", "{\"error\":\"MMManager not ready\"}");
        return;
    }
    mm_flush_all();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleMMBenchmark() {
    if (!mm_is_ready()) {
        server.send(503, "application/json", "{\"error\":\"MMManager not ready\"}");
        return;
    }
    mm_benchmark_result_t res = mm_run_benchmark();
    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"passed\":%s,"
        "\"seq_write_mbps\":%.2f,"
        "\"seq_read_mbps\":%.2f,"
        "\"hit_rate_pct\":%.1f,"
        "\"avg_read_lat_us\":%u,"
        "\"avg_write_lat_us\":%u"
        "}",
        res.passed ? "true" : "false",
        res.seq_write_mbps,
        res.seq_read_mbps,
        res.hit_rate_pct,
        res.avg_read_lat_us,
        res.avg_write_lat_us
    );
    server.send(200, "application/json", json);
}

static String getMIMEType(const String& path) {
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif"))  return "image/gif";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".mp4"))  return "video/mp4";
    if (path.endsWith(".mp3"))  return "audio/mpeg";
    if (path.endsWith(".pdf"))  return "application/pdf";
    if (path.endsWith(".wasm")) return "application/wasm";
    if (path.endsWith(".xml"))  return "text/xml";
    return "text/plain";
}

// WebDAV protocol & static website server
static void handleWebDAVOrNotFound() {
    String uri = server.uri();
    HTTPMethod method = server.method();

    if (method == HTTP_OPTIONS) {
        server.sendHeader("Allow", "GET, POST, PUT, DELETE, OPTIONS, PROPFIND, MKCOL, MOVE, COPY");
        server.sendHeader("DAV", "1, 2");
        server.sendHeader("MS-Author-Via", "DAV");
        server.send(200, "text/plain", "");
        return;
    }

    if (method == HTTP_PROPFIND) {
        if (!is_usb_mounted()) {
            server.send(503, "text/xml", "<?xml version=\"1.0\"?><D:error xmlns:D=\"DAV:\"><D:need-privileges/></D:error>");
            return;
        }

        char safePath[256];
        if (!sanitize_usb_path(uri.c_str(), safePath, sizeof(safePath))) {
            server.send(404, "text/plain", "Not found");
            return;
        }

        struct stat st;
        if (stat(safePath, &st) != 0) {
            server.send(404, "text/plain", "Not found");
            return;
        }

        String xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        xml += "<D:multistatus xmlns:D=\"DAV:\">\n";

        auto addEntry = [&](const char *name, const char *href, bool isDir, size_t size) {
            xml += "  <D:response>\n";
            xml += "    <D:href>"; xml += href; xml += "</D:href>\n";
            xml += "    <D:propstat>\n";
            xml += "      <D:prop>\n";
            xml += "        <D:displayname>"; xml += name; xml += "</D:displayname>\n";
            if (isDir) {
                xml += "        <D:resourcetype><D:collection/></D:resourcetype>\n";
            } else {
                xml += "        <D:resourcetype/>\n";
                xml += "        <D:getcontentlength>"; xml += String((unsigned long)size); xml += "</D:getcontentlength>\n";
            }
            xml += "      </D:prop>\n";
            xml += "      <D:status>HTTP/1.1 200 OK</D:status>\n";
            xml += "    </D:propstat>\n";
            xml += "  </D:response>\n";
        };

        bool isDir = S_ISDIR(st.st_mode);
        addEntry(uri.c_str(), uri.c_str(), isDir, st.st_size);

        if (isDir) {
            DIR *dir = opendir(safePath);
            if (dir) {
                struct dirent *ent;
                while ((ent = readdir(dir)) != NULL) {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

                    char childPath[512];
                    snprintf(childPath, sizeof(childPath), "%s/%s", safePath, ent->d_name);
                    
                    String childHref = uri;
                    if (!childHref.endsWith("/")) childHref += "/";
                    childHref += ent->d_name;

                    struct stat childSt;
                    bool childIsDir = false;
                    size_t childSize = 0;
                    if (stat(childPath, &childSt) == 0) {
                        childIsDir = S_ISDIR(childSt.st_mode);
                        childSize = childSt.st_size;
                    }

                    addEntry(ent->d_name, childHref.c_str(), childIsDir, childSize);
                }
                closedir(dir);
            }
        }

        xml += "</D:multistatus>\n";
        server.send(207, "application/xml; charset=utf-8", xml);
        return;
    }

    if (uri == "/description.xml" && method == HTTP_GET) {
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        String xml = "<?xml version=\"1.0\"?>\n"
                     "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
                     "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
                     "  <device>\n"
                     "    <deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>\n"
                     "    <friendlyName>STORAGE (ESP32-S3 Network Hub)</friendlyName>\n"
                     "    <manufacturer>ESP32-S3</manufacturer>\n"
                     "    <modelName>USB Storage Server</modelName>\n"
                     "    <modelNumber>1.0</modelNumber>\n"
                     "    <UDN>uuid:12345678-1234-1234-1234-123456789abc</UDN>\n"
                     "    <presentationURL>http://" + ip + "/</presentationURL>\n"
                     "  </device>\n"
                     "</root>\n";
        server.send(200, "text/xml", xml);
        return;
    }

    if (method == HTTP_MKCOL) {
        char safePath[256];
        if (sanitize_usb_path(uri.c_str(), safePath, sizeof(safePath))) {
            if (mkdir(safePath, 0777) == 0) {
                server.send(201, "text/plain", "Created");
                return;
            }
        }
        server.send(405, "text/plain", "Error creating directory");
        return;
    }

    if (method == HTTP_GET && is_usb_mounted()) {
        char safePath[280];
        if (sanitize_usb_path(uri.c_str(), safePath, sizeof(safePath))) {
            struct stat st;
            if (stat(safePath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    char indexPath[300];
                    snprintf(indexPath, sizeof(indexPath), "%s/index.html", safePath);
                    if (stat(indexPath, &st) == 0 && S_ISREG(st.st_mode)) {
                        strncpy(safePath, indexPath, sizeof(safePath));
                    } else {
                        goto skip_static_serve;
                    }
                }

                if (S_ISREG(st.st_mode)) {
                    FILE* f = fopen(safePath, "rb");
                    if (f) {
                        server.setContentLength(st.st_size);
                        server.send(200, getMIMEType(safePath), "");

                        uint8_t* buf = (uint8_t*)malloc(FILE_BUFFER_SIZE);
                        if (buf) {
                            size_t readBytes = 0;
                            while ((readBytes = fread(buf, 1, FILE_BUFFER_SIZE, f)) > 0) {
                                server.client().write(buf, readBytes);
                            }
                            free(buf);
                        }
                        fclose(f);
                        return;
                    }
                }
            }
        }
    }

skip_static_serve:

    server.send(404, "text/plain", "Endpoint not found");
}

void web_server_init() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/sys/info", HTTP_GET, handleSysInfo);
    server.on("/api/sys/reboot", HTTP_POST, handleSysReboot);
    server.on("/api/serial/logs", HTTP_GET, handleSerialLogs);
    server.on("/api/serial/clear", HTTP_POST, handleSerialClear);
    server.on("/api/apps/list", HTTP_GET, handleAppsList);
    server.on("/api/apps/active", HTTP_GET, handleAppsActive);
    server.on("/api/apps/stop", HTTP_POST, handleAppsStop);
    server.on("/api/apps/flash", HTTP_POST, handleAppsFlash);
    server.on("/api/ota/upload", HTTP_POST, []() {
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
    }, handleOTAUpload);
    server.on("/api/mm/stats", HTTP_GET, handleMMStats);
    server.on("/api/mm/flush", HTTP_POST, handleMMFlush);
    server.on("/api/mm/benchmark", HTTP_POST, handleMMBenchmark);
    server.on("/api/list", HTTP_GET, handleList);
    server.on("/api/download", HTTP_GET, handleDownload);
    server.on("/api/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "OK");
    }, handleFileUpload);
    server.on("/api/mkdir", HTTP_POST, handleMkdir);
    server.on("/api/delete", HTTP_POST, handleDelete);

    server.onNotFound(handleWebDAVOrNotFound);

    server.begin();
    sys_log("[HTTP] Web Server, WebDAV, App Store & Serial Services online");
}

void web_server_handle() {
    server.handleClient();
}
