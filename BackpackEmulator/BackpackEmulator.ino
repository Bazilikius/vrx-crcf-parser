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

void loadConfig() {
    prefs.begin("backpack", false);
    if (!prefs.isKey("ch_v")) {
        memset(config.uid, 0, 6);
        config.ch_v = 12;
        config.ch_b = 11;
        config.l_grid = 0;
        config.ch_mask = 0b11110001; // 1, 4, 5, 6, 7, 8 (2, 3 disabled)
        config.crsf_min = 991;
        config.crsf_max = 2012;
        memcpy(config.vtx_table, default_vtx, sizeof(default_vtx));
    } else {
        prefs.getBytes("uid", config.uid, 6);
        config.ch_v = prefs.getInt("ch_v");
        config.ch_b = prefs.getInt("ch_b");
        config.l_grid = prefs.getInt("l_grid");
        config.ch_mask = prefs.getUChar("ch_mask");
        config.crsf_min = prefs.getInt("c_min", 991);
        config.crsf_max = prefs.getInt("c_max", 2012);
        if (prefs.getBytes("vtx", config.vtx_table, sizeof(config.vtx_table)) != sizeof(config.vtx_table)) {
            memcpy(config.vtx_table, default_vtx, sizeof(default_vtx));
        }
    }
}

void saveConfig() {
    prefs.putBytes("uid", config.uid, 6);
    prefs.putInt("ch_v", config.ch_v);
    prefs.putInt("ch_b", config.ch_b);
    prefs.putInt("l_grid", config.l_grid);
    prefs.putUChar("ch_mask", config.ch_mask);
    prefs.putInt("c_min", config.crsf_min);
    prefs.putInt("c_max", config.crsf_max);
    prefs.putBytes("vtx", config.vtx_table, sizeof(config.vtx_table));
}

String getVtxHtml() {
    String html = "<h3>VTX Frequencies (MHz)</h3><div style='overflow-x:auto;'><table style='font-size:14px;'><tr><th>Band</th>";
    for(int c=1; c<=8; c++) html += "<th>CH" + String(c) + "</th>";
    html += "</tr>";
    const char* band_names[] = {"A", "B", "E", "F", "R", "L", "X (ELRS)"};
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

const char* HTML_HEADER =
"<!DOCTYPE html><html><head><title>Backpack Config</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;margin:15px;line-height:1.5;}div{margin-bottom:12px;}label{display:block;font-weight:bold;}input[type=text],input[type=number],select{padding:6px;width:100%;box-sizing:border-box;} .btn{padding:10px 15px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;} .btn-sec{background:#6c757d;} table{border-collapse:collapse;width:100%;} td,th{border:1px solid #ccc;padding:4px; text-align:center;}</style></head>"
"<body><h1>Backpack Bridge</h1><form action='/save' method='POST'>";

void handleRoot() {
    String html = HTML_HEADER;
    char uid_buf[13];
    snprintf(uid_buf, 13, "%02X%02X%02X%02X%02X%02X", config.uid[0], config.uid[1], config.uid[2], config.uid[3], config.uid[4], config.uid[5]);

    html += "<div><label>Binding UID (HEX):</label><input type='text' name='uid' value='" + String(uid_buf) + "' pattern='[0-9A-Fa-f]{12}'></div>";
    html += "<div style='display:flex;gap:10px;'>";
    html += "<div style='flex:1;'><label>Video CH (S2):</label><input type='number' name='ch_v' value='" + String(config.ch_v) + "'></div>";
    html += "<div style='flex:1;'><label>Band CH (S3):</label><input type='number' name='ch_b' value='" + String(config.ch_b) + "'></div>";
    html += "</div>";

    html += "<div style='display:flex;gap:10px;'>";
    html += "<div style='flex:1;'><label>CRSF Min (us):</label><input type='number' name='c_min' value='" + String(config.crsf_min) + "'></div>";
    html += "<div style='flex:1;'><label>CRSF Max (us):</label><input type='number' name='c_max' value='" + String(config.crsf_max) + "'></div>";
    html += "</div>";

    html += "<div><label>L-Band Mode:</label><select name='l_grid'><option value='0' " + String(config.l_grid==0?"selected":"") + ">Grid 1 (Std)</option><option value='1' " + String(config.l_grid==1?"selected":"") + ">Grid 2 (ELRS -> Band X)</option></select></div>";

    html += "<div><label>Active Channels:</label>";
    for(int i=0; i<8; i++) {
        String checked = (config.ch_mask & (1 << i)) ? "checked" : "";
        html += "<input type='checkbox' name='m_" + String(i) + "' " + checked + "> " + String(i+1) + " &nbsp;";
    }
    html += "</div>";

    html += getVtxHtml();

    html += "<br><input type='submit' class='btn' value='Save & Restart'></form>";
    html += "<hr><h3>Binding & Maintenance</h3>";
    html += "<div style='display:flex;gap:10px;'>";
    html += "<a href='/bind' class='btn btn-sec'>Send Bind Packet</a>";
    html += "<a href='/reset_vtx' class='btn btn-sec' onclick='return confirm(\"Reset VTX table to defaults?\")'>Reset Table</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
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
    if (server.hasArg("c_min")) config.crsf_min = server.arg("c_min").toInt();
    if (server.hasArg("c_max")) config.crsf_max = server.arg("c_max").toInt();
    if (server.hasArg("l_grid")) config.l_grid = server.arg("l_grid").toInt();

    uint8_t new_mask = 0;
    for(int i=0; i<8; i++) { if (server.hasArg("m_" + String(i))) new_mask |= (1 << i); }
    config.ch_mask = new_mask;

    for(int b=0; b<7; b++) {
        for(int c=0; c<8; c++) {
            String name = "f_" + String(b) + "_" + String(c);
            if (server.hasArg(name)) config.vtx_table[b][c] = server.arg(name).toInt();
        }
    }

    saveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
    delay(500);
    ESP.restart();
}

void handleBind() {
    backpack.sendBindPacket(config.uid);
    server.send(200, "text/plain", "Bind packet sent!");
}

void handleResetVtx() {
    memcpy(config.vtx_table, default_vtx, sizeof(default_vtx));
    saveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

int last_band = -1;
int last_channel = -1;

void setup() {
    Serial.begin(115200);
    loadConfig();
    WiFi.mode(WIFI_AP_STA);
    uint8_t mac[6]; memcpy(mac, config.uid, 6); mac[0] &= 0xFE;
    esp_wifi_set_mac(WIFI_IF_STA, mac);
    WiFi.softAP("Backpack-Emul");
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/bind", handleBind);
    server.on("/reset_vtx", handleResetVtx);
    server.begin();
    backpack.begin(config.uid);
    crsf.begin(Serial2);
    Serial.println("Ready.");
}

void loop() {
    server.handleClient();
    crsf.handle();
    if (crsf.updated) {
        int v_idx = config.ch_v - 1;
        int b_idx = config.ch_b - 1;

        int enabled_chs[8]; int enabled_count = 0;
        for(int i=0; i<8; i++) { if (config.ch_mask & (1 << i)) enabled_chs[enabled_count++] = i; }

        int current_ch = -1;
        if (v_idx >= 0 && v_idx < 16 && enabled_count > 0) {
            int val = crsf.channels[v_idx];
            // Linear mapping using the specific 991-2012 range (default)
            // formula: (val - min) * enabled_count / (max - min + 1)
            int pos = (val - config.crsf_min) * enabled_count / (config.crsf_max - config.crsf_min + 1);
            if (pos < 0) pos = 0; if (pos >= enabled_count) pos = enabled_count - 1;
            current_ch = enabled_chs[pos];
        }

        int current_band = -1;
        if (b_idx >= 0 && b_idx < 16) {
            int val = crsf.channels[b_idx];
            int pos = (val - config.crsf_min) * 6 / (config.crsf_max - config.crsf_min + 1);
            if (pos < 0) pos = 0; if (pos > 5) pos = 5;
            current_band = pos;
        }

        if (current_ch != -1 && current_band != -1) {
            if (current_ch != last_channel || current_band != last_band) {
                last_channel = current_ch; last_band = current_band;
                uint8_t msp_idx; int log_band = current_band;
                if (current_band == 5 && config.l_grid == 1) { msp_idx = 6 * 8 + current_ch; log_band = 6; }
                else { msp_idx = current_band * 8 + current_ch; }
                backpack.sendVtxConfig(msp_idx);
                Serial.printf("Switch: B%d C%d (%d MHz) [CRSF: %d]\n",
                              current_band, current_ch+1, config.vtx_table[log_band][current_ch], crsf.channels[v_idx]);
            }
        }
        crsf.updated = false;
    }
}
