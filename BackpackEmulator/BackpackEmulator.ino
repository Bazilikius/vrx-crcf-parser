#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "crsf.h"
#include "elrs_backpack.h"

struct Config {
    uint8_t uid[6];
    int ch_v;
    int ch_b;
    int l_grid;
};

Config config;
Preferences prefs;
WebServer server(80);
CRSFParser crsf;
ELRSBackpack backpack;

void loadConfig() {
    prefs.begin("backpack", false);
    if (!prefs.isKey("ch_v")) {
        uint8_t default_uid[6] = {0,0,0,0,0,0};
        prefs.putBytes("uid", default_uid, 6);
        prefs.putInt("ch_v", 12);
        prefs.putInt("ch_b", 11);
        prefs.putInt("l_grid", 0);
    }
    prefs.getBytes("uid", config.uid, 6);
    config.ch_v = prefs.getInt("ch_v");
    config.ch_b = prefs.getInt("ch_b");
    config.l_grid = prefs.getInt("l_grid");
}

void saveConfig() {
    prefs.putBytes("uid", config.uid, 6);
    prefs.putInt("ch_v", config.ch_v);
    prefs.putInt("ch_b", config.ch_b);
    prefs.putInt("l_grid", config.l_grid);
}

const char* HTML_PAGE =
"<!DOCTYPE html><html><head><title>Backpack Config</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;margin:20px;}div{margin-bottom:15px;}label{display:block;}input,select{width:100%;padding:8px;} .btn{padding:10px 20px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;} .btn-secondary{background:#6c757d;}</style></head>"
"<body><h1>Backpack Emulator</h1>"
"<form action='/save' method='POST'>"
"<div><label>Binding UID (HEX 12 chars):</label><input type='text' name='uid' value='%02X%02X%02X%02X%02X%02X' pattern='[0-9A-Fa-f]{12}'></div>"
"<div><label>Video CH (S2/CH12):</label><input type='number' name='ch_v' value='%d'></div>"
"<div><label>Band CH (S3/CH11):</label><input type='number' name='ch_b' value='%d'></div>"
"<div><label>L-Band Grid:</label><select name='l_grid'><option value='0' %s>Grid 1 (Standard)</option><option value='1' %s>Grid 2 (ELRS)</option></select></div>"
"<input type='submit' class='btn' value='Save & Restart'></form>"
"<hr><h2>Binding</h2><p>Put your VRX into binding mode, then click below:</p>"
"<a href='/bind' class='btn btn-secondary'>Send Bind Packet</a>"
"</body></html>";

void handleRoot() {
    char buf[2048];
    snprintf(buf, sizeof(buf), HTML_PAGE,
             config.uid[0], config.uid[1], config.uid[2], config.uid[3], config.uid[4], config.uid[5],
             config.ch_v, config.ch_b,
             config.l_grid == 0 ? "selected" : "",
             config.l_grid == 1 ? "selected" : "");
    server.send(200, "text/html", buf);
}

void handleSave() {
    if (server.hasArg("uid")) {
        String s = server.arg("uid");
        if (s.length() == 12) {
            for(int i=0; i<6; i++) {
                String b = s.substring(i*2, i*2+2);
                config.uid[i] = strtol(b.c_str(), NULL, 16);
            }
        }
    }
    if (server.hasArg("ch_v")) config.ch_v = server.arg("ch_v").toInt();
    if (server.hasArg("ch_b")) config.ch_b = server.arg("ch_b").toInt();
    if (server.hasArg("l_grid")) config.l_grid = server.arg("l_grid").toInt();
    saveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
    delay(500);
    ESP.restart();
}

void handleBind() {
    backpack.sendBindPacket(config.uid);
    server.send(200, "text/plain", "Bind packet sent! Check your VRX.");
}

int last_band = -1;
int last_channel = -1;

void setup() {
    Serial.begin(115200);
    loadConfig();

    WiFi.mode(WIFI_AP_STA);

    uint8_t mac[6];
    memcpy(mac, config.uid, 6);
    mac[0] &= 0xFE;
    esp_wifi_set_mac(WIFI_IF_STA, mac);

    WiFi.softAP("Backpack-Emul");

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/bind", handleBind);
    server.begin();

    backpack.begin(config.uid);
    crsf.begin(Serial2);

    Serial.println("Backpack Emulator Started");
}

void loop() {
    server.handleClient();
    crsf.handle();

    if (crsf.updated) {
        int v_idx = config.ch_v - 1;
        int b_idx = config.ch_b - 1;

        int current_ch = -1;
        if (v_idx >= 0 && v_idx < 16) {
            current_ch = (crsf.channels[v_idx] - 172) * 8 / (1811 - 172 + 1);
            if (current_ch < 0) current_ch = 0;
            if (current_ch > 7) current_ch = 7;
        }

        int current_band = -1;
        if (b_idx >= 0 && b_idx < 16) {
            current_band = (crsf.channels[b_idx] - 172) * 6 / (1811 - 172 + 1);
            if (current_band < 0) current_band = 0;
            if (current_band > 5) current_band = 5;
        }

        if (current_ch != -1 && current_band != -1) {
            if (current_ch != last_channel || current_band != last_band) {
                last_channel = current_ch;
                last_band = current_band;

                uint8_t msp_idx;
                if (current_band == 5 && config.l_grid == 1) {
                    msp_idx = 6 * 8 + current_ch;
                } else {
                    msp_idx = current_band * 8 + current_ch;
                }
                backpack.sendVtxConfig(msp_idx);
                Serial.printf("Switch: Band %d, Channel %d (MSP Index %d)\n", current_band, current_ch+1, msp_idx);
            }
        }
        crsf.updated = false;
    }
}
