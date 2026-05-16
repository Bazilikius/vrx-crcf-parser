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
    uint16_t vtx_table[7][8];
    uint8_t ch_mask;
    int crsf_min;
    int crsf_max;
    long baud;
};

Config config;
Preferences prefs;
WebServer server(80);
CRSFParser crsf;
ELRSBackpack backpack;

const uint16_t default_vtx[7][8] = {
    {5865,5845,5825,5805,5785,5765,5745,5725}, // A
    {5733,5752,5771,5790,5809,5828,5847,5866}, // B
    {5705,5685,5665,5645,5885,5905,5925,5945}, // E
    {5740,5760,5780,5800,5820,5840,5860,5880}, // F
    {5658,5695,5732,5769,5806,5843,5880,5917}, // R
    {5362,5399,5436,5473,5510,5547,5584,5621}, // L (Std)
    {5333,5373,5413,5453,5493,5533,5573,5613}  // X (ELRS L)
};

const int s3_points[] = {988, 1193, 1398, 1602, 1807, 2012}; // 6 bands
const int s2_points[] = {988, 1116, 1244, 1372, 1500, 1671, 1842, 2012}; // 8 channels (CH5=1500)

void loadConfig() {
    prefs.begin("backpack_v4", false);
    if (!prefs.isKey("ch_v")) {
        memset(config.uid, 0, 6);
        config.ch_v = 12; config.ch_b = 11; config.l_grid = 0; config.ch_mask = 0xFF;
        config.crsf_min = 991; config.crsf_max = 2012; config.baud = 416700;
        memcpy(config.vtx_table, default_vtx, sizeof(default_vtx));
        saveConfig();
    } else {
        prefs.getBytes("uid", config.uid, 6);
        config.ch_v = prefs.getInt("ch_v"); config.ch_b = prefs.getInt("ch_b");
        config.l_grid = prefs.getInt("l_grid"); config.ch_mask = prefs.getUChar("ch_mask");
        config.crsf_min = prefs.getInt("c_min", 991); config.crsf_max = prefs.getInt("c_max", 2012);
        config.baud = prefs.getLong("baud", 416700);
        prefs.getBytes("vtx", config.vtx_table, sizeof(config.vtx_table));
    }
}

void saveConfig() {
    prefs.putBytes("uid", config.uid, 6);
    prefs.putInt("ch_v", config.ch_v); prefs.putInt("ch_b", config.ch_b);
    prefs.putInt("l_grid", config.l_grid); prefs.putUChar("ch_mask", config.ch_mask);
    prefs.putInt("c_min", config.crsf_min); prefs.putInt("c_max", config.crsf_max);
    prefs.putLong("baud", config.baud);
    prefs.putBytes("vtx", config.vtx_table, sizeof(config.vtx_table));
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><title>Config</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;margin:15px;line-height:1.5;}label{display:block;font-weight:bold;}input,select{padding:6px;width:100%;box-sizing:border-box;margin-bottom:10px;} .btn{padding:10px 15px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;} .btn-sec{background:#6c757d;} table{border-collapse:collapse;width:100%;} td,th{border:1px solid #ccc;padding:4px; text-align:center;}</style></head><body><h1>Backpack Bridge</h1><form action='/save' method='POST'>";
    char uid_buf[13]; snprintf(uid_buf, 13, "%02X%02X%02X%02X%02X%02X", config.uid[0], config.uid[1], config.uid[2], config.uid[3], config.uid[4], config.uid[5]);
    html += "<div><label>Binding UID:</label><input type='text' name='uid' value='" + String(uid_buf) + "'></div>";
    html += "<div><label>CRSF Baud Rate:</label><input type='number' name='baud' value='" + String(config.baud) + "'></div>";
    html += "<div style='display:flex;gap:10px;'><div><label>Video CH:</label><input type='number' name='ch_v' value='" + String(config.ch_v) + "'></div><div><label>Band CH:</label><input type='number' name='ch_b' value='" + String(config.ch_b) + "'></div></div>";
    html += "<div><label>CRSF Range (us):</label><div style='display:flex;gap:10px;'><input type='number' name='c_min' value='" + String(config.crsf_min) + "'><input type='number' name='c_max' value='" + String(config.crsf_max) + "'></div></div>";
    html += "<div><label>L-Band Mode:</label><select name='l_grid'><option value='0' " + String(config.l_grid==0?"selected":"") + ">Grid 1 (Std)</option><option value='1' " + String(config.l_grid==1?"selected":"") + ">Grid 2 (X)</option></select></div>";
    html += "<br><input type='submit' class='btn' value='Save & Restart'></form><hr><a href='/bind' class='btn btn-sec'>Bind VRX</a></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("uid")) { String s = server.arg("uid"); if (s.length() == 12) for(int i=0; i<6; i++) config.uid[i] = strtol(s.substring(i*2, i*2+2).c_str(), NULL, 16); }
    if (server.hasArg("baud")) config.baud = server.arg("baud").toInt();
    if (server.hasArg("ch_v")) config.ch_v = server.arg("ch_v").toInt();
    if (server.hasArg("ch_b")) config.ch_b = server.arg("ch_b").toInt();
    if (server.hasArg("c_min")) config.crsf_min = server.arg("c_min").toInt();
    if (server.hasArg("c_max")) config.crsf_max = server.arg("c_max").toInt();
    if (server.hasArg("l_grid")) config.l_grid = server.arg("l_grid").toInt();
    saveConfig(); server.sendHeader("Location", "/"); server.send(303); delay(500); ESP.restart();
}

void handleBind() { backpack.sendBindPacket(config.uid); server.send(200, "text/plain", "Bind sent!"); }

int get_nearest_index(int value, const int points[], int count) {
    int nearest = 0; int min_dist = abs(value - points[0]);
    for(int i=1; i<count; i++) { int dist = abs(value - points[i]); if (dist < min_dist) { min_dist = dist; nearest = i; } }
    return nearest;
}

int last_band = -1; int last_channel = -1;
uint32_t last_debug = 0;

void setup() {
    Serial.begin(115200); loadConfig(); WiFi.mode(WIFI_AP_STA);
    uint8_t mac[6]; memcpy(mac, config.uid, 6); mac[0] &= 0xFE; esp_wifi_set_mac(WIFI_IF_STA, mac);
    WiFi.softAP("Backpack-Emul");
    server.on("/", handleRoot); server.on("/save", HTTP_POST, handleSave); server.on("/bind", handleBind); server.begin();
    backpack.begin(config.uid);
    crsf.begin(Serial2, config.baud);
    Serial.println("Ready.");
}

void loop() {
    server.handleClient(); crsf.handle();
    int v_idx = config.ch_v - 1; int b_idx = config.ch_b - 1;
    if (millis() - last_debug > 2000) {
        Serial.printf("DEBUG: CH%d:%dus CH%d:%dus\n", config.ch_v, crsf.channels[v_idx], config.ch_b, crsf.channels[b_idx]);
        last_debug = millis();
    }
    if (crsf.updated) {
        int pos_v = get_nearest_index(crsf.channels[v_idx], s2_points, 8);
        int pos_b = get_nearest_index(crsf.channels[b_idx], s3_points, 6);
        if (pos_v != -1 && pos_b != -1) {
            if (pos_v != last_channel || pos_b != last_band) {
                last_channel = pos_v; last_band = pos_b;
                uint8_t msp_idx; int log_band = pos_b;
                if (pos_b == 5 && config.l_grid == 1) { msp_idx = 6 * 8 + pos_v; log_band = 6; }
                else msp_idx = pos_b * 8 + pos_v;
                backpack.sendVtxConfig(msp_idx);
                Serial.printf("Switch -> B%d C%d (%d MHz)\n", pos_b, pos_v+1, config.vtx_table[log_band][pos_v]);
            }
        }
        crsf.updated = false;
    }
}
