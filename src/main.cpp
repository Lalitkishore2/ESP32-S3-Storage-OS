#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include "config.h"
#include "usb_msc.h"
#include "web_server.h"
#include "mm_manager.h"
#include "sys_log.h"

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize circular logger for Web Serial Monitor
    sys_log_init();

    sys_log("==================================================");
    sys_log("  ESP32-S3 USB Storage & Network Hub Booting...   ");
    sys_log("==================================================");

    // 1. Initialize Dual Wi-Fi Modes (AP + STA)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Start SoftAP Hotspot
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
    IPAddress apIP = WiFi.softAPIP();

    sys_log("[HOTSPOT] Network SSID : '%s' | Pass : '%s'", AP_SSID, AP_PASS);
    sys_log("[HOTSPOT] Access Point IP: http://%s", apIP.toString().c_str());

    // 2. Configure Fixed Static IP and Connect to Home Wi-Fi Router
#if USE_STATIC_IP
    IPAddress staticIP(STATIC_IP);
    IPAddress staticGW(STATIC_GATEWAY);
    IPAddress staticSN(STATIC_SUBNET);
    IPAddress staticDNS(STATIC_DNS);
    if (WiFi.config(staticIP, staticGW, staticSN, staticDNS)) {
        sys_log("[WIFI] Static IP configured: http://%s", staticIP.toString().c_str());
    } else {
        sys_log("[WIFI] Static IP configuration failed, using DHCP.");
    }
#endif

    sys_log("[WIFI] Connecting to Home Wi-Fi '%s'...", STA_SSID);
    WiFi.begin(STA_SSID, STA_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        sys_log("[WIFI] Connected! Local IP: http://%s", WiFi.localIP().toString().c_str());
    } else {
        sys_log("[WIFI] Home Wi-Fi pending. Running in AP+STA mode (AP IP: http://%s)", apIP.toString().c_str());
    }

    // 3. Register NetBIOS Name Service for Windows (\\STORAGE and http://STORAGE/)
    NBNS.begin(NETBIOS_NAME);
    sys_log("[NETBIOS] Broadcast name: \\\\%s (Access via http://%s/)", NETBIOS_NAME, NETBIOS_NAME);

    // 4. Register mDNS Hostname & Network Services (http://storage.local)
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.setInstanceName("ESP32-S3 Storage OS Hub");
        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("webdav", "tcp", HTTP_PORT);
        MDNS.addService("workstation", "tcp", HTTP_PORT);
        MDNS.addServiceTxt("http", "tcp", "path", "/");
        sys_log("[MDNS] Domain: http://%s.local (or http://%s/)", MDNS_HOSTNAME, MDNS_HOSTNAME);
    }

    // 5. Initialize Web Server & WebDAV Handlers
    web_server_init();

    // 6. Initialize USB Host MSC Driver
    if (!usb_msc_init()) {
        sys_log("[USB] WARNING: USB Host init failed. Storage unavailable.");
    }

    // 7. Initialize Multi-Level Memory Manager (SRAM → PSRAM → USB)
    delay(2000);  // Allow USB device enumeration to complete
    if (!mm_init()) {
        sys_log("[MM] WARNING: Memory Manager init failed. Basic mode only.");
    }

    // 8. Configure GPIO 0 BOOT button for emergency recovery
    pinMode(0, INPUT_PULLUP);

    sys_log("==================================================");
    sys_log("  System Ready! Storage Server Online (Dual Core Active).");
    sys_log("==================================================");

    // Pin Web Server & Network Client processing to Core 0 FreeRTOS Task
    xTaskCreatePinnedToCore(
        [](void* param) {
            while (true) {
                web_server_handle();
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        },
        "WebCore0Task", 8192, NULL, 1, NULL, 0
    );
}

static unsigned long s_last_mm_stats = 0;
static unsigned long s_last_heartbeat = 0;
static unsigned long s_boot_press_start = 0;

void loop() {
    // Core 1 Execution Engine: Memory Manager, USB MSC Host, and System Telemetry
    unsigned long now = millis();

    // 1. Continuous 2-second Web Serial Monitor Heartbeat
    if (now - s_last_heartbeat >= 2000) {
        s_last_heartbeat = now;
        uint32_t freeHeap = ESP.getFreeHeap() / 1024;
        float freePsram = (float)ESP.getFreePsram() / (1024 * 1024);
        bool usbMounted = is_usb_mounted();
        sys_log("[SYS-MONITOR] Heap: %u KB | PSRAM Free: %.1f MB | USB: %s | Core 0/1 Dual Active",
                freeHeap, freePsram, usbMounted ? "CONNECTED" : "UNMOUNTED");
    }

    // 2. Physical BOOT Button (GPIO 0) Recovery Handler
    if (digitalRead(0) == LOW) {
        if (s_boot_press_start == 0) {
            s_boot_press_start = now;
        } else if (now - s_boot_press_start >= 1000) {
            sys_log("==================================================");
            sys_log("[RECOVERY] BOOT button held for 1s! Restoring Storage Hub Core...");
            sys_log("==================================================");
            delay(500);
            ESP.restart();
        }
    } else {
        s_boot_press_start = 0;
    }

    // 3. Periodic Memory Manager stats dump
    if (mm_is_ready() && (now - s_last_mm_stats >= MM_STATS_INTERVAL_MS)) {
        s_last_mm_stats = now;
        mm_print_stats();
    }

    delay(10);
}