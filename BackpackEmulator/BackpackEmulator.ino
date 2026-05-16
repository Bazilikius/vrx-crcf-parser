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
const int s2_points[] = {988, 1116, 1244, 1372, 1500, 1671, 1842, 2012}; // 8 channels

void loadConfig() {
    prefs.begin("backpack_v3", false);
    if (!prefs.isKey("ch_v")) {
        memset(config.uid, 0, 6);
        config.ch_v = 12; config.ch_b = 11; config.l_grid = 0; config.ch_mask = 0xFF;
        memcpy(config.vtx_table, default_vtx, sizeof(default_vtx));
        saveConfig();
    } else {
        prefs.getBytes("uid", config.uid, 6);
        config.ch_v = prefs.getInt("ch_v");
        config.ch_b = prefs.getInt("ch_b");
        config.l_grid = prefs.getInt("l_grid");
        config.ch_mask = prefs.getUChar("ch_mask");
        prefs.getBytes("vtx", config.vtx_table, sizeof(config.vtx_table));
    }
}

void saveConfig() {
    prefs.putBytes("uid", config.uid, 6);
    prefs.putInt("ch_v", config.ch_v); prefs.putInt("ch_b", config.ch_b);
    prefs.putInt("l_grid", config.l_grid); prefs.putUChar("ch_mask", config.ch_mask);
    prefs.putBytes("vtx", config.vtx_table, sizeof(config.vtx_table));
}

String getVtxHtml() {
    String html = "<h3>VTX Table</h3><div style='overflow-x:auto;'><table><tr><th>Band</th>";
    for(int c=1; c<=8; c++) html += "<th>CH" + String(c) + "</th>";
    html += "</tr>";
    const char* band_names[] = {"A", "B", "E", "F", "R", "L", "X"};
    for(int b=0; b<7; b++) {
        html += "<tr><td><b>" + String(band_names[b]) + "</b></td>";
        for(int c=0; c<8; c++) {
            String name = "f_" + String(b) + "_" + String(c);
            html += "<td><input type='number' name='" + name + "' value='" + String(config.vtx_table[b][c]) + "' style='width:55px;'></td>";
        }
        html += "</tr>";
    }
    html += "</table></div>";
    return html;
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><title>Config</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;margin:15px;}input[type=text],input[type=number],select{padding:6px;width:100%;box-sizing:border-box;} .btn{padding:10px 15px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;} .btn-sec{background:#6c757d;} table{border-collapse:collapse;width:100%;} td,th{border:1px solid #ccc;padding:4px; text-align:center;}</style></head><body><h1>Backpack Bridge</h1><form action='/save' method='POST'>";
    char uid_buf[13]; snprintf(uid_buf, 13, "%02X%02X%02X%02X%02X%02X", config.uid[0], config.uid[1], config.uid[2], config.uid[3], config.uid[4], config.uid[5]);
    html += "<div><label>UID:</label><input type='text' name='uid' value='" + String(uid_buf) + "'></div>";
    html += "<div style='display:flex;gap:10px;'><div><label>Video CH:</label><input type='number' name='ch_v' value='" + String(config.ch_v) + "'></div><div><label>Band CH:</label><input type='number' name='ch_b' value='" + String(config.ch_b) + "'></div></div>";
    html += "<div><label>L-Band Mode:</label><select name='l_grid'><option value='0' " + String(config.l_grid==0?"selected":"") + ">Grid 1</option><option value='1' " + String(config.l_grid==1?"selected":"") + ">Grid 2 (X)</option></select></div>";
    html += "<div><label>Active Channels:</label>";
    for(int i=0; i<8; i++) { String checked = (config.ch_mask & (1 << i)) ? "checked" : ""; html += "<input type='checkbox' name='m_" + String(i) + "' " + checked + "> " + String(i+1) + " &nbsp;"; }
    html += "</div>" + getVtxHtml() + "<br><input type='submit' class='btn' value='Save'></form><hr><a href='/bind' class='btn btn-sec'>Bind VRX</a></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("uid")) { String s = server.arg("uid"); if (s.length() == 12) for(int i=0; i<6; i++) config.uid[i] = strtol(s.substring(i*2, i*2+2).c_str(), NULL, 16); }
    if (server.hasArg("ch_v")) config.ch_v = server.arg("ch_v").toInt();
    if (server.hasArg("ch_b")) config.ch_b = server.arg("ch_b").toInt();
    if (server.hasArg("l_grid")) config.l_grid = server.arg("l_grid").toInt();
    uint8_t new_mask = 0; for(int i=0; i<8; i++) if (server.hasArg("m_" + String(i))) new_mask |= (1 << i);
    config.ch_mask = new_mask;
    for(int b=0; b<7; b++) for(int c=0; c<8; c++) { String name = "f_" + String(b) + "_" + String(c); if (server.hasArg(name)) config.vtx_table[b][c] = server.arg(name).toInt(); }
    saveConfig(); server.sendHeader("Location", "/"); server.send(303); delay(500); ESP.restart();
}

void handleBind() { backpack.sendBindPacket(config.uid); server.send(200, "text/plain", "Bind sent!"); }

int get_nearest_index(int value, const int points[], int count) {
    int nearest = 0; int min_dist = abs(value - points[0]);
    for(int i=1; i<count; i++) { int dist = abs(value - points[i]); if (dist < min_dist) { min_dist = dist; nearest = i; } }
    return nearest; // Always return closest
}

int last_band = -1; int last_channel = -1;
uint32_t last_debug = 0;

void setup() {
    Serial.begin(115200); loadConfig(); WiFi.mode(WIFI_AP_STA);
    uint8_t mac[6]; memcpy(mac, config.uid, 6); mac[0] &= 0xFE; esp_wifi_set_mac(WIFI_IF_STA, mac);
    WiFi.softAP("Backpack-Emul");
    server.on("/", handleRoot); server.on("/save", HTTP_POST, handleSave); server.on("/bind", handleBind); server.begin();
    backpack.begin(config.uid); crsf.begin(Serial2); Serial.println("Ready.");
}

void loop() {
    server.handleClient(); crsf.handle();

    int v_idx = config.ch_v - 1; int b_idx = config.ch_b - 1;
    if (millis() - last_debug > 1000) {
        Serial.printf("DEBUG: S2(Ch%d):%d S3(Ch%d):%d\n", config.ch_v, crsf.channels[v_idx], config.ch_b, crsf.channels[b_idx]);
        last_debug = millis();
    }

    if (crsf.updated) {
        int enabled_chs[8]; int enabled_count = 0;
        for(int i=0; i<8; i++) if (config.ch_mask & (1 << i)) enabled_chs[enabled_count++] = i;

        int current_ch = -1;
        if (v_idx >= 0 && v_idx < 16 && enabled_count > 0) {
            int pos = get_nearest_index(crsf.channels[v_idx], s2_points, 8);
            current_ch = enabled_chs[map(pos, 0, 7, 0, enabled_count - 1)];
        }

        int current_band = -1;
        if (b_idx >= 0 && b_idx < 16) {
            current_band = get_nearest_index(crsf.channels[b_idx], s3_points, 6);
        }

        if (current_ch != -1 && current_band != -1) {
            if (current_ch != last_channel || current_band != last_band) {
                last_channel = current_ch; last_band = current_band;
                uint8_t msp_idx; int log_band = current_band;
                if (current_band == 5 && config.l_grid == 1) { msp_idx = 6 * 8 + current_ch; log_band = 6; }
                else msp_idx = current_band * 8 + current_ch;
                backpack.sendVtxConfig(msp_idx);
                Serial.printf("Switching -> B%d C%d (%d MHz)\n", current_band, current_ch+1, config.vtx_table[log_band][current_ch]);
            }
        }
        crsf.updated = false;
    }
}
