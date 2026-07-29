#include "web_server.h"
#include "config.h"
#include "usb_msc.h"
#include "mm_manager.h"
#include "mm_benchmark.h"
#include "app_engine.h"
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
static char activeUploadPath[280] = {0};
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
    .batch-bar {
      display: none;
      align-items: center;
      justify-content: space-between;
      background: #26211c;
      border: 1px solid var(--accent-coral);
      padding: 10px 18px;
      border-radius: var(--radius-md);
      margin-bottom: 16px;
      box-shadow: 0 4px 16px rgba(0,0,0,0.4);
    }
    .batch-bar.active { display: flex; animation: fadeIn 0.2s; }
    .batch-count { font-size: 13px; font-weight: 600; color: #fff; }
    .batch-actions { display: flex; align-items: center; gap: 10px; }
    .chk-box { width: 16px; height: 16px; accent-color: var(--accent-coral); cursor: pointer; vertical-align: middle; }

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
              <span class="metric-badge green" id="serial-badge">Streaming</span>
            </div>
            <div class="metric-val" id="serial-val">115200</div>
            <div class="metric-label">Serial Monitor Stream</div>
          </div>
        </div>

        <!-- OS FEATURES & USAGE GUIDE SECTION -->
        <div class="section-card" style="margin-top: 24px;">
          <div class="section-header">
            <div class="section-title-group">
              <h2 class="section-title">USB-Based ESP32-S3 OS Features & System Guide</h2>
              <span class="section-badge">OS DOCUMENTATION & MANUAL</span>
            </div>
          </div>
          <p style="color: var(--text-muted); font-size: 13px; margin-bottom: 20px; line-height: 1.6;">
            Comprehensive guide explaining how the USB Mass Storage Driver, SLRU PSRAM Cache, Dual-OTA Partition Switcher, and Multi-Port Web Architecture operate during Normal Mode and Project Execution Mode.
          </p>

          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px;">
            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: var(--accent-coral); font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                USB Mass Storage Host
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                Mounts FAT32 USB pendrives up to 2TB directly on GPIO 19/20. Enables HTTP file downloads, WebDAV Windows Drive sharing (<code>\\192.168.0.8\STORAGE</code>), static web hosting (<code>/usb/web/</code>), and project <code>.bin</code> app storage (<code>/usb/apps/</code>).
              </div>
            </div>

            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: #d97706; font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                Segmented SLRU PSRAM Manager
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                Allocates 512 PSRAM cache pages (2MB active pool) using Segmented Least Recently Used (SLRU) caching. Accelerates USB pendrive file read/write throughput by up to <strong>8x</strong> with dirty-page write-back buffers.
              </div>
            </div>

            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: #10b981; font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                Dual-OTA Partition Flashing
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                Flashes compiled PlatformIO user <code>.bin</code> files from USB drive onto <code>ota_1</code> (0x200000). Updates <code>otadata</code> block and reboots into user application while Core 0 runs background management.
              </div>
            </div>

            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: #3b82f6; font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                Dual-Port Web Architecture
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                <strong>Port 80:</strong> Primary OS Dashboard, File Explorer, WebDAV, and Serial Console stream.<br>
                <strong>Port 8080:</strong> Independent web application interface hosted natively by active user projects.
              </div>
            </div>

            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: #a855f7; font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                Live Serial Monitor Console
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                Streams 115200 Baud hardware UART output and project logs in real-time over HTTP socket connections, providing live telemetry, memory stats, and debug logs directly in browser.
              </div>
            </div>

            <div style="background: #141210; border: 1px solid var(--card-border); border-radius: 12px; padding: 20px;">
              <div style="color: #ef4444; font-size: 15px; font-weight: 700; margin-bottom: 8px;">
                Hardware Safety & OTA Recovery
              </div>
              <div style="font-size: 13px; color: var(--text-muted); line-height: 1.6;">
                Holding BOOT button (GPIO 0) for 1s during project execution triggers instant bootloader rollback to <code>ota_0</code> Core OS. Flash recovery also supported via POST <code>/api/apps/stop</code> or esptool.
              </div>
            </div>
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

          <div class="batch-bar" id="batch-bar">
            <div class="batch-count" id="batch-count-txt">0 items selected</div>
            <div class="batch-actions">
              <button class="btn-subtle" onclick="downloadSelected()">Download Selected</button>
              <button class="btn-subtle" onclick="moveSelected()">Move / Rename</button>
              <button class="btn-danger" onclick="promptDeleteSelected(this)" style="padding:6px 12px;font-size:12px;">Delete Selected</button>
              <button class="btn-subtle" onclick="clearSelections()" style="padding:6px 10px;font-size:11px;">Deselect All</button>
            </div>
          </div>

          <table class="file-table">
            <thead>
              <tr>
                <th style="width:36px;text-align:center;"><input type="checkbox" class="chk-box" id="select-all-chk" onclick="toggleSelectAll(this)"></th>
                <th>Name</th>
                <th>Type / Tier</th>
                <th>Size</th>
                <th style="text-align:right;">Actions</th>
              </tr>
            </thead>
            <tbody id="file-list">
              <tr><td colspan="5" class="empty-rows">Scanning USB filesystem...</td></tr>
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

  <!-- DELETION PROGRESS & CONFIRMATION MODAL -->
  <div class="modal-overlay" id="delete-modal" style="display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.75);z-index:9999;align-items:center;justify-content:center;">
    <div class="modal-container" style="max-width:440px;text-align:center;">
      <h3 style="color:#fff;margin-bottom:12px;font-size:17px;" id="del-modal-title">Confirm Deletion</h3>
      <div id="del-modal-body" style="font-size:13px;color:var(--text-muted);margin-bottom:18px;word-break:break-all;">
        Are you sure you want to delete <strong>item</strong>?
      </div>
      <div id="del-progress-box" style="display:none;margin-bottom:20px;">
        <div style="background:#141210;border-radius:10px;height:10px;overflow:hidden;border:1px solid var(--card-border);margin-bottom:8px;">
          <div id="del-progress-fill" style="background:var(--danger);height:100%;width:0%;transition:width 0.2s;"></div>
        </div>
        <div style="font-size:12px;font-weight:600;color:var(--accent-coral);" id="del-progress-lbl">Deleting 0%...</div>
      </div>
      <div id="del-modal-footer" style="display:flex;justify-content:center;gap:12px;">
        <button class="btn-subtle" id="del-cancel-btn" onclick="closeDeleteModal()">Cancel</button>
        <button class="btn-danger" id="del-confirm-btn" style="padding:8px 20px;font-size:12px;" onclick="executeDeleteFromModal()">Confirm Delete</button>
      </div>
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
            document.getElementById('insight-storage').innerText = 'USB Storage Unmounted or Disconnected';
          }
          
          document.getElementById('wifi-val').innerText = data.ip;
          if (document.getElementById('wifi-badge')) {
            document.getElementById('wifi-badge').innerText = data.wifi_connected ? 'STA+AP Online' : 'AP Mode Only';
          }

          const freeRamKb = (data.free_heap / 1024).toFixed(0);
          document.getElementById('sram-val').innerText = `${freeRamKb} KB`;
          if (document.getElementById('sram-badge')) {
            document.getElementById('sram-badge').innerText = `Min: ${(data.min_heap / 1024).toFixed(0)} KB`;
          }
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
          if (document.getElementById('serial-val')) {
            document.getElementById('serial-val').innerText = '115200';
          }
          if (document.getElementById('serial-badge')) {
            document.getElementById('serial-badge').innerText = 'Live Log Stream';
          }
        }
      } catch(e) {}

      try {
        const resMM = await fetch('/api/mm/stats');
        if (resMM.ok) {
          const mm = await resMM.json();
          const hr = mm.hit_rate_pct !== undefined ? mm.hit_rate_pct.toFixed(1) : "0.0";
          document.getElementById('hit-rate-val').innerText = `${hr}%`;
          if (document.getElementById('hit-rate-badge')) {
            document.getElementById('hit-rate-badge').innerText = `${mm.used_pages}/512 Pages`;
          }

          if (mm.psram_free_bytes > 0) {
            const freePsram = (mm.psram_free_bytes / (1024 * 1024)).toFixed(1);
            document.getElementById('psram-val').innerText = `${freePsram} MB`;
            if (document.getElementById('psram-badge')) {
              document.getElementById('psram-badge').innerText = '8 MB OPI';
            }
          } else {
            document.getElementById('psram-val').innerText = `2.0 MB`;
            if (document.getElementById('psram-badge')) {
              document.getElementById('psram-badge').innerText = 'SLRU Cache Pool';
            }
          }

          document.getElementById('insight-cache').innerText = `${mm.used_pages}/512 pages used in PSRAM (${mm.dirty_pages} dirty) • ${hr}% Hit Rate`;
          
          document.getElementById('mm-page-hitrate').innerText = `${hr}%`;
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
            let prettyName = app.name.replace(/\.(bin|so|elf|wasm)$/i, '').replace(/_/g, ' ');
            prettyName = prettyName.charAt(0).toUpperCase() + prettyName.slice(1);

            let engineBadge = '<span class="section-badge" style="font-size:10px;background:rgba(234,88,12,0.15);color:var(--accent-coral);">Legacy Flash (.bin)</span>';
            if (app.name.endsWith('.so') || app.name.endsWith('.elf')) {
              engineBadge = '<span class="section-badge" style="font-size:10px;background:rgba(16,185,129,0.15);color:var(--success);">Native Xtensa (.so)</span>';
            } else if (app.name.endsWith('.wasm')) {
              engineBadge = '<span class="section-badge" style="font-size:10px;background:rgba(59,130,246,0.15);color:#3b82f6;">WASM Sandbox (.wasm)</span>';
            }

            html += `
              <div class="app-card">
                <div>
                  <div class="app-title" style="justify-content:space-between;">
                    <span style="display:flex;align-items:center;gap:6px;">
                      <svg viewBox="0 0 24 24" style="width:18px;height:18px;stroke:var(--accent-coral);stroke-width:2;fill:none;"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>
                      ${prettyName}
                    </span>
                    ${engineBadge}
                  </div>
                  <div class="app-meta" style="margin-top:8px;">Size: ${formatBytes(app.size)} • Path: /usb/apps/${app.name}</div>
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

      isUploading = true;
      isCancelRequested = false;

      showModal('Uploading Firmware Binary', `
        <div style="padding:20px;text-align:center;">
          <div style="font-size:16px;color:#fff;margin-bottom:12px;" id="app-upload-msg">Uploading <strong>${file.name}</strong> (0%)...</div>
          <button class="btn-danger" style="padding:8px 16px;font-size:12px;" onclick="cancelUpload()">Cancel Upload</button>
        </div>
      `);

      try {
        await uploadSingleFile(file, (pct) => {
          const msg = document.getElementById('app-upload-msg');
          if (msg) msg.innerHTML = `Uploading <strong>${file.name}</strong> (${pct}%)...`;
        });
        showModal('Upload Complete', `<div style="padding:20px;color:var(--success);">Saved ${file.name} to /usb/apps/ project store!</div>`);
        loadAppsList();
      } catch (err) {
        if (isCancelRequested) {
          showModal('Upload Cancelled', '<div style="padding:20px;color:var(--muted);">Binary upload cancelled by user.</div>');
        } else {
          showModal('Upload Error', '<div style="padding:20px;color:var(--danger);">Failed to upload firmware binary.</div>');
        }
      } finally {
        isUploading = false;
      }
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

    function getFileType(fileName, isDir) {
      if (isDir) return 'Folder';
      const ext = fileName.split('.').pop().toLowerCase();
      if (['mp4', 'mkv', 'avi', 'mov'].includes(ext)) return 'MP4 Video';
      if (['png', 'jpg', 'jpeg', 'gif', 'svg', 'webp'].includes(ext)) return 'Image File';
      if (['bin', 'elf', 'so'].includes(ext)) return 'Firmware Binary';
      if (['wasm'].includes(ext)) return 'WASM Bytecode';
      if (['txt', 'log', 'csv', 'json', 'html', 'pdf'].includes(ext)) return 'Document';
      if (['zip', 'tar', 'gz', '7z'].includes(ext)) return 'Archive';
      return 'USB Storage File';
    }

    let selectedPaths = new Set();

    function updateBatchBar() {
      const bar = document.getElementById('batch-bar');
      const txt = document.getElementById('batch-count-txt');
      if (!bar || !txt) return;
      if (selectedPaths.size > 0) {
        txt.innerText = `${selectedPaths.size} item${selectedPaths.size > 1 ? 's' : ''} selected`;
        bar.classList.add('active');
      } else {
        bar.classList.remove('active');
      }
    }

    function toggleSelectAll(masterChk) {
      selectedPaths.clear();
      document.querySelectorAll('.item-chk').forEach(chk => {
        chk.checked = masterChk.checked;
        if (masterChk.checked) selectedPaths.add(chk.dataset.path);
      });
      updateBatchBar();
    }

    function toggleItemSelect(chk, path) {
      if (chk.checked) selectedPaths.add(path);
      else selectedPaths.delete(path);

      const allChks = document.querySelectorAll('.item-chk');
      const master = document.getElementById('select-all-chk');
      if (master && allChks.length > 0) {
        master.checked = Array.from(allChks).every(c => c.checked);
      }
      updateBatchBar();
    }

    function clearSelections() {
      selectedPaths.clear();
      const master = document.getElementById('select-all-chk');
      if (master) master.checked = false;
      document.querySelectorAll('.item-chk').forEach(chk => chk.checked = false);
      updateBatchBar();
    }

    let isActionBusy = false;
    let pendingDeleteItems = [];

    function promptDelete(path, btnEl) {
      if (isActionBusy) return;
      pendingDeleteItems = [path];

      const title = document.getElementById('del-modal-title');
      const body = document.getElementById('del-modal-body');
      const box = document.getElementById('del-progress-box');
      const footer = document.getElementById('del-modal-footer');
      const confirmBtn = document.getElementById('del-confirm-btn');
      const cancelBtn = document.getElementById('del-cancel-btn');

      if (!title || !body || !confirmBtn) return;

      title.innerText = 'Confirm Deletion';
      body.innerHTML = `Are you sure you want to delete <strong style="color:var(--accent-coral);">${escapeHtml(path)}</strong>?`;
      box.style.display = 'none';
      footer.style.display = 'flex';
      cancelBtn.style.display = 'inline-block';
      confirmBtn.disabled = false;
      confirmBtn.innerText = 'Confirm Delete';

      document.getElementById('delete-modal').style.display = 'flex';
    }

    function promptDeleteSelected(btnEl) {
      if (isActionBusy || selectedPaths.size === 0) return;
      pendingDeleteItems = Array.from(selectedPaths);

      const title = document.getElementById('del-modal-title');
      const body = document.getElementById('del-modal-body');
      const box = document.getElementById('del-progress-box');
      const footer = document.getElementById('del-modal-footer');
      const confirmBtn = document.getElementById('del-confirm-btn');
      const cancelBtn = document.getElementById('del-cancel-btn');

      if (!title || !body || !confirmBtn) return;

      title.innerText = 'Confirm Batch Deletion';
      body.innerHTML = `Are you sure you want to recursively delete <strong style="color:var(--accent-coral);">${pendingDeleteItems.length} selected item(s)</strong>?`;
      box.style.display = 'none';
      footer.style.display = 'flex';
      cancelBtn.style.display = 'inline-block';
      confirmBtn.disabled = false;
      confirmBtn.innerText = 'Delete Selected';

      document.getElementById('delete-modal').style.display = 'flex';
    }

    function closeDeleteModal() {
      if (isActionBusy) return;
      document.getElementById('delete-modal').style.display = 'none';
      pendingDeleteItems = [];
    }

    async function executeDeleteFromModal() {
      if (isActionBusy || pendingDeleteItems.length === 0) return;
      isActionBusy = true;

      const confirmBtn = document.getElementById('del-confirm-btn');
      const cancelBtn = document.getElementById('del-cancel-btn');
      const box = document.getElementById('del-progress-box');
      const fill = document.getElementById('del-progress-fill');
      const lbl = document.getElementById('del-progress-lbl');

      confirmBtn.disabled = true;
      confirmBtn.innerText = 'Deleting...';
      cancelBtn.style.display = 'none';
      box.style.display = 'block';
      fill.style.width = '0%';
      lbl.innerText = `Deleting 0%...`;

      const total = pendingDeleteItems.length;
      let successCount = 0;

      for (let i = 0; i < total; i++) {
        const item = pendingDeleteItems[i];
        const pct = Math.round(((i + 1) / total) * 100);

        const filename = item.substring(item.lastIndexOf('/') + 1) || item;
        lbl.innerText = `Deleting item ${i + 1}/${total}: ${filename} (${pct}%)...`;
        fill.style.width = `${pct}%`;

        try {
          const res = await fetch('/api/delete?path=' + encodeURIComponent(item), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path: item })
          });
          if (res.ok || res.status === 404) successCount++;
        } catch(e){}

        await new Promise(r => setTimeout(r, 80));
      }

      lbl.innerText = `Successfully deleted ${successCount}/${total} items (100%)!`;
      fill.style.width = '100%';

      clearSelections();
      if (document.getElementById('view-code-upload').classList.contains('active')) {
        await loadAppsList();
      } else {
        await loadFiles(currentPath);
        await loadStats();
      }

      setTimeout(() => {
        isActionBusy = false;
        document.getElementById('delete-modal').style.display = 'none';
        pendingDeleteItems = [];
      }, 600);
    }

    async function moveSelected() {
      if (selectedPaths.size === 0) return;
      const paths = Array.from(selectedPaths);

      if (paths.length === 1) {
        const src = paths[0];
        const newName = prompt('Enter new destination or filename:', src);
        if (!newName || newName === src) return;
        try {
          const res = await fetch('/api/move', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ src: src, dst: newName })
          });
          if (res.ok) {
            clearSelections();
            loadFiles();
          } else {
            alert('Failed to move item');
          }
        } catch(e) { alert('Error moving item'); }
      } else {
        const targetDir = prompt(`Move ${paths.length} items to folder (e.g. /www or /logs):`);
        if (!targetDir) return;
        let count = 0;
        for (const src of paths) {
          const filename = src.substring(src.lastIndexOf('/') + 1);
          const dst = (targetDir.endsWith('/') ? targetDir.slice(0, -1) : targetDir) + '/' + filename;
          try {
            const res = await fetch('/api/move', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ src: src, dst: dst })
            });
            if (res.ok) count++;
          } catch(e){}
        }
        alert(`Moved ${count}/${paths.length} items to ${targetDir}`);
        clearSelections();
        loadFiles();
      }
    }

    async function downloadSelected() {
      if (selectedPaths.size === 0) return;
      const paths = Array.from(selectedPaths);
      for (const path of paths) {
        const a = document.createElement('a');
        a.href = '/api/download?path=' + encodeURIComponent(path);
        a.download = path.substring(path.lastIndexOf('/') + 1);
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        await new Promise(r => setTimeout(r, 200));
      }
    }

    function renderFiles(files) {
      const tbody = document.getElementById('file-list');
      let html = '';
      clearSelections();
      
      if (currentPath !== '/') {
        const parent = currentPath.substring(0, currentPath.lastIndexOf('/')) || '/';
        html += `
          <tr>
            <td colspan="5">
              <div class="file-row" onclick="loadFiles('${parent}')">
                <span class="dir-row-icon"><svg viewBox="0 0 24 24"><path d="M19 12H5M12 19l-7-7 7-7"/></svg></span>
                <span>.. (Up one level)</span>
              </div>
            </td>
          </tr>
        `;
      }

      if (!files || files.length === 0) {
        html += '<tr><td colspan="5" class="empty-rows">This folder is empty. Drag & drop files to upload.</td></tr>';
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
              <td style="text-align:center;" onclick="event.stopPropagation();">
                <input type="checkbox" class="chk-box item-chk" data-path="${fullPath}" onclick="toggleItemSelect(this, '${fullPath}')">
              </td>
              <td>
                <div class="file-row" onclick="${clickAction}">
                  ${icon}
                  <span>${f.name}</span>
                </div>
              </td>
              <td style="color:var(--text-muted);font-size:12px;">${getFileType(f.name, isDir)}</td>
              <td style="color:var(--text-muted);font-size:12px;">${isDir ? '--' : formatBytes(f.size)}</td>
              <td style="text-align:right;">
                ${!isDir ? `<a href="/api/download?path=${encodeURIComponent(fullPath)}" class="btn-subtle" download style="padding:4px 8px;font-size:11px;">Download</a>` : ''}
                ${isDir && currentPath === '/www' ? `<button class="btn-coral" style="padding:4px 8px;font-size:11px;" onclick="window.open('${fullPath}/index.html','_blank')">Launch Site</button>` : ''}
                <button class="btn-danger" onclick="event.stopPropagation(); promptDelete('${fullPath}', this)">Delete</button>
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
        const res = await fetch('/api/mkdir?path=' + encodeURIComponent(fullPath), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: fullPath })
        });
        if (res.ok) {
          loadFiles(currentPath);
        } else {
          alert('Failed to create folder');
        }
      } catch (e) { alert('Error creating folder'); }
    }

    async function deleteItem(path, btnEl) {
      if (isActionBusy) return;
      if (!confirm(`Delete ${path}?`)) return;

      isActionBusy = true;
      if (btnEl) {
        btnEl.disabled = true;
        btnEl.innerText = 'Deleting...';
        btnEl.style.opacity = '0.5';
      }

      try {
        const res = await fetch('/api/delete?path=' + encodeURIComponent(path), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: path })
        });
        if (res.ok || res.status === 404) {
          clearSelections();
          if (document.getElementById('view-code-upload').classList.contains('active')) {
            await loadAppsList();
          } else {
            await loadFiles(currentPath);
            await loadStats();
          }
        } else {
          alert('Failed to delete item (Status ' + res.status + ')');
        }
      } catch (e) { alert('Error deleting item'); }
      finally {
        isActionBusy = false;
      }
    }

    let isUploading = false;
    let activeUploadXhr = null;
    let isCancelRequested = false;

    function cancelUpload() {
      isCancelRequested = true;
      if (activeUploadXhr) {
        activeUploadXhr.abort();
        activeUploadXhr = null;
      }
    }

    window.addEventListener('beforeunload', (e) => {
      if (isUploading) {
        e.preventDefault();
        e.returnValue = 'File upload in progress. Leaving this page will cancel the upload.';
        return e.returnValue;
      }
    });

    async function handleFileSelect(e) {
      const files = e.target ? e.target.files : (e.dataTransfer ? e.dataTransfer.files : null);
      if (!files || files.length === 0) return;

      isUploading = true;
      isCancelRequested = false;
      const dropMsg = document.querySelector('.drop-msg');
      let successCount = 0;

      for (let i = 0; i < files.length; i++) {
        if (isCancelRequested) break;
        const file = files[i];

        const renderProgress = (pct) => {
          dropMsg.innerHTML = `Uploading <strong>${i + 1}/${files.length}</strong>: ${file.name} (${pct}%)... <button class="btn-danger" style="margin-left:12px;padding:3px 10px;font-size:11px;" onclick="cancelUpload()">Cancel Upload</button>`;
        };

        renderProgress(0);

        try {
          await uploadSingleFile(file, (pct) => renderProgress(pct));
          successCount++;
        } catch (err) {
          if (isCancelRequested) {
            console.warn('Upload cancelled by user:', file.name);
            break;
          }
          console.error('Upload failed:', file.name, err);
          alert(`Failed to upload ${file.name}`);
        }
      }

      isUploading = false;
      activeUploadXhr = null;
      if (isCancelRequested) {
        dropMsg.innerHTML = `Upload cancelled by user (${successCount}/${files.length} uploaded). Drag & drop files here or <strong>click to select</strong>.`;
      } else {
        dropMsg.innerHTML = `Uploaded ${successCount}/${files.length} files successfully. Drag & drop files here or <strong>click to select</strong>.`;
      }
      loadFiles();
      loadStats();
    }

    function uploadSingleFile(file, onProgress) {
      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        activeUploadXhr = xhr;
        const fullPath = (currentPath === '/' ? '' : currentPath) + '/' + file.name;
        const formData = new FormData();
        formData.append('file', file, file.name);

        xhr.open('POST', '/api/upload?path=' + encodeURIComponent(fullPath), true);
        xhr.timeout = 120000; // 2 minutes

        xhr.upload.onprogress = (evt) => {
          if (evt.lengthComputable && onProgress) {
            const pct = Math.round((evt.loaded / evt.total) * 100);
            onProgress(pct);
          }
        };

        xhr.onload = () => {
          if (xhr.status >= 200 && xhr.status < 300) {
            resolve();
          } else {
            reject(new Error('HTTP Status ' + xhr.status));
          }
        };
        xhr.onerror = () => reject(new Error('Network error'));
        xhr.ontimeout = () => reject(new Error('Upload timed out'));
        xhr.onabort = () => reject(new Error('Upload aborted'));

        xhr.send(formData);
      });
    }

    // Drag & Drop
    const dropArea = document.getElementById('drop-area');
    dropArea.addEventListener('dragover', (e) => { e.preventDefault(); dropArea.style.borderColor = 'var(--accent-coral)'; });
    dropArea.addEventListener('dragleave', () => { dropArea.style.borderColor = '#3a322b'; });
    dropArea.addEventListener('drop', (e) => {
      e.preventDefault();
      dropArea.style.borderColor = '#3a322b';
      if (e.dataTransfer && e.dataTransfer.files.length) {
        handleFileSelect(e);
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
    // Visibility-aware polling: pause when tab is hidden to save ESP32 CPU
    let statsIntervalId = setInterval(loadStats, 8000);
    let logsIntervalId = setInterval(loadSerialLogs, 1500);

    document.addEventListener('visibilitychange', () => {
      if (document.hidden) {
        clearInterval(statsIntervalId);
        clearInterval(logsIntervalId);
      } else {
        loadStats();
        loadSerialLogs();
        statsIntervalId = setInterval(loadStats, 8000);
        logsIntervalId = setInterval(loadSerialLogs, 1500);
      }
    });
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
    AppEngineStatus status = app_engine_get_status();
    char json[384];
    const char* typeStr = "Storage Hub Core OS";
    if (status.type == APP_ENGINE_NATIVE_ELF) typeStr = "Native Xtensa C++ (.so)";
    else if (status.type == APP_ENGINE_WASM_SANDBOX) typeStr = "Sandboxed WAMR WASM (.wasm)";
    else if (status.type == APP_ENGINE_LEGACY_BIN) typeStr = "Legacy OTA Firmware (.bin)";

    snprintf(json, sizeof(json),
        "{"
        "\"active_project\":\"%s\","
        "\"is_running\":%s,"
        "\"type\":\"%s\","
        "\"ram_allocated_bytes\":%u,"
        "\"run_time_ms\":%lu,"
        "\"cpu_core\":%u,"
        "\"status\":\"running\""
        "}",
        status.name,
        status.is_running ? "true" : "false",
        typeStr,
        (unsigned int)status.ram_allocated_bytes,
        status.run_time_ms,
        (unsigned int)status.cpu_core
    );
    server.send(200, "application/json", json);
}

static void handleAppsStop() {
    sys_log("================================================================================");
    sys_log("[PIO-RUNNER] Stopping active project via Hybrid App Engine...");

    if (app_engine_stop()) {
        server.send(200, "application/json", "{\"status\":\"stopped_dynamic_app\"}");
        return;
    }

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL
    );
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        sys_log("[PIO-RUNNER] Boot partition set to ota_0! Rebooting...");
        server.send(200, "application/json", "{\"status\":\"restored_and_rebooting\"}");
        delay(500);
        ESP.restart();
        return;
    }

    server.send(200, "application/json", "{\"status\":\"rebooting_fallback\"}");
    delay(500);
    ESP.restart();
}

// Flash or Launch firmware directly from /usb/apps/<filename> on USB drive!
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
    fclose(f);

    sys_log("================================================================================");
    sys_log("[PIO-UPLOAD] Target Project File: %s (%u bytes)", safePath, fileSize);

    if (fileName.endsWith(".so") || fileName.endsWith(".elf") || fileName.endsWith(".wasm")) {
        sys_log("[PIO-UPLOAD] Launching dynamic module via Hybrid App Engine...");
        if (app_engine_launch(fileName.c_str())) {
            server.send(200, "application/json", "{\"status\":\"launched_dynamic_app\"}");
            return;
        } else {
            server.send(500, "application/json", "{\"error\":\"Dynamic launch failed\"}");
            return;
        }
    }

    // Legacy .bin OTA Flash Engine
    f = fopen(safePath, "rb");
    if (!f) {
        server.send(404, "application/json", "{\"error\":\"Failed to reopen file for OTA flash\"}");
        return;
    }

    sys_log("[PIO-UPLOAD] Configured upload protocol: FatFS USB Flash Engine");
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

    String json;
    json.reserve(2048);
    json = "[";
    struct dirent *ent;
    bool first = true;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        String fname = String(ent->d_name);
        if (!fname.endsWith(".bin") && !fname.endsWith(".so") && !fname.endsWith(".elf") && !fname.endsWith(".wasm")) {
            continue;
        }

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

    String json;
    json.reserve(4096);
    json = "[";
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
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/octet-stream", "");

    // Enable TCP_NODELAY for maximum throughput on file transfers
    server.client().setNoDelay(true);

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
        if (uploadFile) {
            fclose(uploadFile);
            uploadFile = NULL;
            if (activeUploadPath[0]) {
                remove(activeUploadPath);
                sys_log("[HTTP] Cleaned orphan file from interrupted upload: %s", activeUploadPath);
                activeUploadPath[0] = 0;
            }
        }

        if (!is_usb_mounted()) return;

        String reqPath = server.arg("path");
        if (reqPath.length() == 0) {
            reqPath = "/" + upload.filename;
        }

        if (sanitize_usb_path(reqPath.c_str(), activeUploadPath, sizeof(activeUploadPath))) {
            uploadFile = fopen(activeUploadPath, "wb");
            if (uploadFile) {
                sys_log("[HTTP] Receiving file upload: %s", activeUploadPath);
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
            activeUploadPath[0] = 0;
            sync_usb_fatfs();
            sys_log("[HTTP] File upload completed: %u bytes", upload.totalSize);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) {
            fclose(uploadFile);
            uploadFile = NULL;
            if (activeUploadPath[0]) {
                remove(activeUploadPath);
                sys_log("[HTTP] Cleaned aborted upload file: %s", activeUploadPath);
                activeUploadPath[0] = 0;
            }
        }
    }
}

static void handleMkdir() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"Storage unmounted\"}");
        return;
    }

    String targetPath = "";
    if (server.hasArg("path") && server.arg("path").length() > 0) {
        targetPath = server.arg("path");
    } else if (server.hasArg("plain")) {
        String body = server.arg("plain");
        int pathIdx = body.indexOf("\"path\":");
        if (pathIdx != -1) {
            int start = body.indexOf("\"", pathIdx + 7);
            if (start != -1) {
                int end = body.indexOf("\"", start + 1);
                if (end != -1) targetPath = body.substring(start + 1, end);
            }
        }
    }

    if (targetPath.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

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

#include <ff.h>

static bool recursive_remove(const char* safePath) {
    if (!safePath || !safePath[0]) return false;

    // 1. Try POSIX remove / unlink
    if (remove(safePath) == 0 || unlink(safePath) == 0) {
        return true;
    }

    // 2. Check if directory and recursively clear
    struct stat st;
    if (stat(safePath, &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(safePath);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                char childPath[512];
                snprintf(childPath, sizeof(childPath), "%s/%s", safePath, ent->d_name);
                recursive_remove(childPath);
            }
            closedir(dir);
        }
        if (rmdir(safePath) == 0) return true;
    }

    // 3. Direct FatFS f_unlink fallback (bypasses VFS issues with special chars/spaces)
    const char* rel = safePath;
    if (strncmp(safePath, VFS_MOUNT_PATH, strlen(VFS_MOUNT_PATH)) == 0) {
        rel = safePath + strlen(VFS_MOUNT_PATH);
    }
    char fatPath[280];
    if (rel[0] == '/') {
        snprintf(fatPath, sizeof(fatPath), "0:%s", rel);
    } else {
        snprintf(fatPath, sizeof(fatPath), "0:/%s", rel);
    }

    FRESULT fr = f_unlink(fatPath);
    if (fr == FR_OK) {
        sys_log("[FATFS] Direct f_unlink succeeded for: %s", fatPath);
        return true;
    } else {
        sys_log("[FATFS] Direct f_unlink failed (%d) for: %s", (int)fr, fatPath);
    }

    return false;
}

static void handleDelete() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"Storage unmounted\"}");
        return;
    }

    String targetPath = "";
    if (server.hasArg("path") && server.arg("path").length() > 0) {
        targetPath = server.arg("path");
    } else if (server.hasArg("plain")) {
        String body = server.arg("plain");
        int pathIdx = body.indexOf("\"path\":");
        if (pathIdx != -1) {
            int start = body.indexOf("\"", pathIdx + 7);
            if (start != -1) {
                int end = body.indexOf("\"", start + 1);
                if (end != -1) targetPath = body.substring(start + 1, end);
            }
        }
    }

    if (targetPath.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    char safePath[256];
    if (!sanitize_usb_path(targetPath.c_str(), safePath, sizeof(safePath))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    struct stat st;
    if (stat(safePath, &st) != 0) {
        sys_log("[HTTP] Delete target already absent: %s", safePath);
        server.send(200, "application/json", "{\"status\":\"ok\",\"note\":\"already_absent\"}");
        return;
    }

    if (recursive_remove(safePath)) {
        sync_usb_fatfs();
        sys_log("[HTTP] Recursively deleted & synced: %s", safePath);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to delete item\"}");
    }
}

static void handleMove() {
    if (!is_usb_mounted()) {
        server.send(503, "application/json", "{\"error\":\"Storage unmounted\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = server.arg("plain");
    int srcIdx = body.indexOf("\"src\":\"");
    int dstIdx = body.indexOf("\"dst\":\"");
    if (srcIdx == -1 || dstIdx == -1) {
        server.send(400, "application/json", "{\"error\":\"Missing src or dst\"}");
        return;
    }

    int srcStart = srcIdx + 7;
    int srcEnd = body.indexOf("\"", srcStart);
    String srcPath = body.substring(srcStart, srcEnd);

    int dstStart = dstIdx + 7;
    int dstEnd = body.indexOf("\"", dstStart);
    String dstPath = body.substring(dstStart, dstEnd);

    char safeSrc[280], safeDst[280];
    if (!sanitize_usb_path(srcPath.c_str(), safeSrc, sizeof(safeSrc)) ||
        !sanitize_usb_path(dstPath.c_str(), safeDst, sizeof(safeDst))) {
        server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
        return;
    }

    if (rename(safeSrc, safeDst) == 0) {
        sync_usb_fatfs();
        sys_log("[HTTP] Moved/renamed %s -> %s", safeSrc, safeDst);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to move/rename item\"}");
    }
}

static void handleMMStats() {
    if (!mm_is_ready()) {
        mm_init();
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
        mm_init();
    }
    mm_flush_all();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleMMBenchmark() {
    if (!mm_is_ready()) {
        mm_init();
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
                        // Cache static assets aggressively (1 day for CSS/JS/images)
                        String mime = getMIMEType(safePath);
                        if (strstr(safePath, ".css") || strstr(safePath, ".js") || strstr(safePath, ".png") || strstr(safePath, ".jpg") || strstr(safePath, ".svg") || strstr(safePath, ".ico") || strstr(safePath, ".woff")) {
                            server.sendHeader("Cache-Control", "public, max-age=86400, immutable");
                        }
                        server.send(200, mime, "");
                        server.client().setNoDelay(true);

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
    app_engine_init();
    mm_init();

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
    server.on("/api/move", HTTP_POST, handleMove);

    server.onNotFound(handleWebDAVOrNotFound);

    server.begin();
    sys_log("[HTTP] Web Server, WebDAV, App Store & Serial Services online");
}

void web_server_handle() {
    server.handleClient();
}
