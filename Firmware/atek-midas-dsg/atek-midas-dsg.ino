#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
//#include <ArduinoOTA.h>
#include "display.h"
#include "main.h"
#include "Lmx2820.h"
#include "MCP23S17.h"
#include "ADC78H90.h"
#include "RemoteControl.h"

const char *apPassword = "12345678";

String apSSID = "ATEK_DSG_22.6GHz";
String deviceSerial = "UNKNOWN";

String currentFrequency; 
String currentAmplitude;  
String currentFreqUnit; 
bool FilterStatus; 
bool RFStatus;
bool rfOutputEnabled = false; 

// --- EXTERN VARIABLES FOR SWEEP ---
extern String enteredFreqValue;
extern String enteredUnitValue;
extern String enteredAmpValue;
extern String StartValueForSweepMenu;
extern String StartUnitForSweepMenu;
extern String StopValueForSweepMenu;
extern String StopUnitForSweepMenu;
extern String StepValueForSweepMenu;
extern String StepUnitForSweepMenu;
extern String DwellValueForSweepMenu;
extern String AmpValueSweepForSweepMenu;
extern String StepTypeValueForSweepMenu;
extern String CountValueForSweepMenu;
extern bool isSweepRunning;
extern MenuState currentMenu;
extern double currentHz; 
extern uint32_t sweepCycleCount;

// Global variables to store real-time telemetry data
float last_usb_voltage = 0.0;
float last_dsg_current = 0.0;
bool last_pll_lock = false;
float temp = 0.0; 

// Firmware build timestamp, auto-generated at compile time.
// __DATE__ format: "Aug 13 2026", __TIME__ format: "14:32:51"
// Combined result matches: "Aug 13 2026 14:32:51"
const char* FW_BUILD_TIMESTAMP = __DATE__ " " __TIME__;

char* FloatToChar(float avg) {
    static char buffer[20];
    snprintf(buffer, sizeof(buffer), "%.2f", avg); 
    return buffer;
}

char* DoubleToChar(double num) {
    static char buffer[30];
    snprintf(buffer, sizeof(buffer), "%.4f", num); 
    return buffer;
}

WebServer server(80);
Preferences preferences;

// --------------------------------------------------------------
// Persistent calibration table storage
// The calibration points are loaded from and saved to NVS memory.
// --------------------------------------------------------------
// ==============================================================
//  Calibration Data Structure and NVS Functions
// ==============================================================
struct CalibData {
    uint16_t freq_MHz;
    int8_t att6_on;    
    int8_t att3_on;
    int8_t att_n3_on;
    int8_t att6_off;
    int8_t att3_off;
    int8_t att_n3_off;
};

#define MAX_CALIB_POINTS 600
CalibData calibTable[MAX_CALIB_POINTS];
uint16_t calibCount = 0;

void InitCalibrationData() {
    preferences.begin("calib", false);

    // DSG serial number
    deviceSerial = preferences.getString("serial", "UNKNOWN");

    // Create device-specific Wi-Fi SSID
    if (deviceSerial != "UNKNOWN" && deviceSerial.length() > 0) {
      apSSID = "ATEK_DSG_22.6GHz_" + deviceSerial;
      } else {
        apSSID = "ATEK_DSG_22.6GHz";
        }

    calibCount = preferences.getUShort("count", 0);
    if (calibCount > 0 && calibCount <= MAX_CALIB_POINTS) {
        size_t dataLen = calibCount * sizeof(CalibData);
        size_t readLen = preferences.getBytes("data", calibTable, dataLen);
        if (readLen != dataLen) {
            calibCount = 0; // Read error
        } else {
            Serial.print("[NVS] Loaded calibration points: ");
            Serial.println(calibCount);
        }
    }
    preferences.end();
}

void SaveCalibrationToNVS() {
    preferences.begin("calib", false);

    preferences.putString("serial", deviceSerial);
    preferences.putUShort("count", calibCount);
    preferences.putBytes("data", calibTable, calibCount * sizeof(CalibData));
    
    preferences.end();
    Serial.println("[NVS] Kalibrasyon ve seri numarasi NVS'e kaydedildi.");
}

void ClearCalibrationRAM() {
    calibCount = 0;
    memset(calibTable, 0, sizeof(calibTable));
}

bool AddCalibrationPoint(uint16_t f, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6) {
    if (calibCount >= MAX_CALIB_POINTS) return false;
    calibTable[calibCount].freq_MHz = f;
    calibTable[calibCount].att6_on = a1;
    calibTable[calibCount].att3_on = a2;
    calibTable[calibCount].att_n3_on = a3;
    calibTable[calibCount].att6_off = a4;
    calibTable[calibCount].att3_off = a5;
    calibTable[calibCount].att_n3_off = a6;
    calibCount++;
    return true;
}
// ==============================================================

// --------------------------------------------------------------
// Persistent RF settings
// These functions read and write the last selected CW/RF parameters.
// --------------------------------------------------------------
void readRFSettings() { 
  preferences.begin("RFSettings", false);
  currentFrequency = preferences.getString("frequency", "1000");
  currentAmplitude = preferences.getString("amplitude", "10");   
  currentFreqUnit = preferences.getString("freqUnit", "MHz");
  FilterStatus = preferences.getBool("FilterStat", false);      
  RFStatus  = preferences.getBool("RFStat", false);
  
  Serial.println("=== RF Settings ===");
  Serial.print("Frequency: "); Serial.println(currentFrequency);
  Serial.print("Amplitude: "); Serial.println(currentAmplitude);
  Serial.print("Frequency Unit: "); Serial.println(currentFreqUnit);
  Serial.print("Filter Status: "); Serial.println(FilterStatus ? "ON" : "OFF");
  Serial.println("====================");
  preferences.end();
}

void saveRFSettings() { 
  preferences.begin("RFSettings", false);
  preferences.putString("frequency", currentFrequency);
  preferences.putString("amplitude", currentAmplitude);
  preferences.putString("freqUnit", currentFreqUnit);
  preferences.putBool("FilterStat", FilterStatus );
  preferences.putBool("RFStat", false ); 
  preferences.end();
}

void toggleRFOutput() {
  rfOutputEnabled = !rfOutputEnabled; 
  SetRfOnOff(rfOutputEnabled); 
}

void handleToggleRFOutput() {
  toggleRFOutput();
  server.send(200, "text/plain", rfOutputEnabled ? "1" : "0"); 
}

// --------------------------------------------------------------
// Wi-Fi configuration storage and connection handling
// --------------------------------------------------------------
String wifiSSID;
String wifiPassword;

void readWiFiCredentials() {
  preferences.begin("WifiSettings", false);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();
}
 
char statusMessage[50];  

bool connectToWiFi(int tryCount) {
  if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
    snprintf(statusMessage, sizeof(statusMessage), "Connecting to %s", wifiSSID);
    ConnectionStatus(statusMessage,true);
    for (int i = 1; i <= tryCount; i++) {
      ConnectionStatus(" Try ",false);
      String str = String(i);  
      ConnectionStatus(str.c_str(), false);
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str()); 
      int connectionTryCount = 0;
      while (WiFi.status() != WL_CONNECTED && connectionTryCount < 10) { 
        delay(1500);
        ConnectionStatus(".",false);
        connectionTryCount++;
      }
      Serial.println();

      if (WiFi.status() == WL_CONNECTED) {
        ConnectionStatus("Connected to Wi-Fi!",true);
        return true;
      } else {
        ConnectionStatus("Wi-Fi Connection Failed!",true);
        if (i < tryCount) {
          delay(5000);
        }
      }
    }
  } else {
    ConnectionStatus("No WiFi credentials stored.",true);
  }
  return false;
}

void handleSave() {
  wifiSSID = server.arg("ssid");
  wifiPassword = server.arg("password");
  if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
    preferences.begin("WifiSettings", false);
    preferences.putString("ssid", wifiSSID);
    preferences.putString("password", wifiPassword);
    preferences.end();

    server.sendHeader("Location", "/");
    server.send(302, "text/html; charset=utf-8", "Credentials saved. Redirecting..."); 
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "text/plain; charset=utf-8", "SSID and password cannot be empty!");
  }
}

enum WiFiState {
  WIFI_INIT,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_HOTSPOT,
  WIFI_FAILED
};

WiFiState wifiState = WIFI_INIT;
unsigned long wifiStartTime = 0;
int wifiRetryCount = 0;

// --------------------------------------------------------------
// Main hardware initialization
// Initializes serial control, display, touch input, fan, ADC, IO expander, and PLL.
// --------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  // Load the calibration table from non-volatile storage (NVS) during startup
  InitCalibrationData();
  Serial.println("Starting...");

  RC_Begin();
  RC_SetWrite([](const char* s){ Serial.print(s); });  

  SetupDisplay();
  initTouch();
  Fan_Init();

  readRFSettings(); 

  drawMainMenu();
  SetRfOnOff(false);

  SetFreqUnitOnMainMenu(currentFreqUnit);
  SetFreqOnMainMenu(currentFrequency);
  SetAmpOnMainMenu(currentAmplitude);

  pinMode(IO1_CS, OUTPUT);
  digitalWrite(IO1_CS, HIGH); 

  pinMode(ADC_CS, OUTPUT);
  digitalWrite(ADC_CS, HIGH); 

  pinMode(PLL_CS, OUTPUT);
  digitalWrite(PLL_CS, HIGH); 
  
  InitADC();
  Serial.print("\r\n");
  

  temp = Read_Temp(); 
  last_usb_voltage = Read_5V_Voltage();
  SetTemp(String(temp, 1).c_str());
  SetUSBVoltge(String(last_usb_voltage, 1).c_str()); 

  IO_EXP1_Init();

  ConnectionStatus("Wait...", true);  delay(1000);

  InitPLL();
  
  SetFilter(FilterStatus);

  // Convert the saved CW frequency to MHz
double startupFreqMHz = currentFrequency.toDouble();

if (currentFreqUnit == "KHz")
{
    startupFreqMHz /= 1000.0;
}
else if (currentFreqUnit == "MHz")
{
    // Already in MHz
}
else if (currentFreqUnit == "GHz")
{
    startupFreqMHz *= 1000.0;
}

  // Apply the saved frequency to the PLL
Lmx2820SetFreqinMHz(startupFreqMHz, 10000000, FilterStatus);
}

// --------------------------------------------------------------
// Legacy Wi-Fi task entry point
// Currently returns immediately, so the active Wi-Fi flow is handled elsewhere.
// --------------------------------------------------------------
void WiFiTask(void *parameter) {
  return;
  bool isHotSpot = false;
  readRFSettings();
  readWiFiCredentials();
  SetWifiStatus(WIFI_STATUS_OFF);
  if (connectToWiFi(3)) { 
    ConnectionStatus("IP address: ", true);
    String ipStr = WiFi.localIP().toString();
    ConnectionStatus(ipStr.c_str(), false);
  } else {
    SetWifiStatus(WIFI_STATUS_HOTSPOT);
    ConnectionStatus("Creating Hotspot.", true);
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(apSSID.c_str(), apPassword);
    String dots = "";
    int retries = 0;
    while (WiFi.softAPIP().toString() != "192.168.4.1" && retries++ < 50) {
        delay(500);
        dots += "*";
        ConnectionStatus(dots.c_str(), true);
    }
    ConnectionStatus("Hotspot IP:", true);
    String ipStr = WiFi.softAPIP().toString();
    ConnectionStatus(ipStr.c_str(), false);
    isHotSpot = true;
  }

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/applyCW", handleApplyCW); 
  server.on("/applySweep", handleApplySweep); 
  server.on("/toggleRFOutput", handleToggleRFOutput);
  server.on("/toggleSweep", handleToggleSweep);
  server.on("/telemetry", handleTelemetry); 
  server.on("/toggleSweep", handleToggleSweep);
  server.on("/telemetry", handleTelemetry); 
  server.on("/getSettings", handleGetSettings); // <--- New route added

  server.begin();
  ConnectionStatus("Web Server Ready.", true);
  if (isHotSpot)
  {
    SetWifiStatus(WIFI_STATUS_HOTSPOT);
  }
  else
  {
    SetWifiStatus(WIFI_STATUS_ON);
  }

  vTaskDelete(NULL);
}

void InitServer()
{
  server.on("/", handleRoot); 
  server.on("/save", handleSave);
  server.on("/applyCW", handleApplyCW);
  server.on("/applySweep", handleApplySweep); 
  server.on("/toggleRFOutput", handleToggleRFOutput);
  server.on("/toggleSweep", handleToggleSweep);
  server.on("/telemetry", handleTelemetry); 

  server.begin();
}

// --------------------------------------------------------------
// Web interface generator
// Builds the browser-based control panel for CW, sweep, telemetry, and Wi-Fi settings.
// --------------------------------------------------------------
String getHTML() {
  // --- Added section: expose live variables to the HTML interface ---
  extern String FreqValueForMainMenu;
  extern String FreqUnitForMainMenu;
  extern String AmpValueForMainMenu;

  String btnRfColor = rfOutputEnabled ? "#a6e3a1" : "#f38ba8";
  String btnRfText  = rfOutputEnabled ? "RF OUTPUT: ON" : "RF OUTPUT: OFF";
  
  String btnSweepColor = isSweepRunning ? "#f38ba8" : "#a6e3a1";
  
  // Fix: character encoding issues were resolved.
  String btnSweepText  = isSweepRunning ? "&#9209; STOP SWEEP" : "&#9654; START SWEEP";
  
  String html = R"=====( 
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>DSG 22.6 GHz - Web Control Center</title>
  <style>
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      text-align: center;
      background-color: #1e1e2e;
      color: #cdd6f4;
      margin: 0;
      padding: 10px;
    }
    .container {
      max-width: 600px;
      margin: 0 auto;
      padding: 20px;
      background-color: #313244;
      border-radius: 10px;
      box-shadow: 0 0 15px rgba(0, 0, 0, 0.5);
      border: 1px solid #45475a;
    }
    h1, h2 { color: #89b4fa; margin-top:0; }
    hr { border: 0; height: 1px; background: #45475a; margin: 25px 0; }
    
    label {
      display: block;
      text-align: left;
      font-weight: bold;
      margin-bottom: 5px;
      color: #cdd6f4;
      font-size: 14px;
    }
    
    input[type=number], input[type=text], input[type=password], select {
      background-color: #11111b;
      color: white;
      border: 1px solid #45475a;
      padding: 10px;
      border-radius: 5px;
      box-sizing: border-box;
      font-size: 15px;
    }
    
    button {
      background-color: #45475a;
      color: #cdd6f4;
      border: 1px solid #45475a;
      border-radius: 5px;
      cursor: pointer;
      font-weight: bold;
      transition: all 0.2s;
    }
    button:hover { border-color: #89b4fa; }
    
    .spinbox-container {
      display: flex; 
      justify-content: space-between; 
      align-items: center; 
      gap: 5px;
      margin-bottom: 10px;
    }
    
    .preset-btn {
      flex: 1;
      padding: 5px;
      font-size: 12px;
      font-weight: normal;
    }

    .btn-step {
      width: 45px;
      padding: 10px;
      font-size: 18px;
    }

    .wide-button {
      width: 100%;
      padding: 15px;
      font-size: 16px;
      margin-top: 10px;
      color: #11111b;
    }
    .panel {
      background-color: #1e1e2e;
      padding: 15px;
      border-radius: 8px;
      margin-bottom: 20px;
      border: 1px solid #45475a;
    }
    .live-item {
      background-color: #11111b;
      padding: 10px;
      border-radius: 4px;
      font-size: 16px;
      font-weight: bold;
      color: #f9e2af;
      margin-bottom: 10px;
    }
  </style>
</head>
<body>
  <div class="container">
    
    <div class="panel">
        <h2>&#128225; CW (Continuous Wave)</h2>
        <label>CW Frequency:</label>
        <div class="spinbox-container">
            <button id="btn_minus" class="btn-step">-</button>
            <input type="number" step="0.001" id="frequency" value=")=====" + FreqValueForMainMenu + R"=====(" style="width:45%;">
            <select id="freqUnit" style="width:30%;">
              <option value="KHz" )=====" + String(FreqUnitForMainMenu == "KHz" ? "selected" : "") + R"=====(>KHz</option>
              <option value="MHz" )=====" + String(FreqUnitForMainMenu == "MHz" ? "selected" : "") + R"=====(>MHz</option>
              <option value="GHz" )=====" + String(FreqUnitForMainMenu == "GHz" ? "selected" : "") + R"=====(>GHz</option>
            </select>
            <button id="btn_plus" class="btn-step">+</button>
        </div>

        <label>Increment Step:</label>
        <div class="spinbox-container" style="margin-bottom: 20px;">
            <input type="number" step="0.001" id="step_val" value="100.0" style="width:65%;">
            <select id="stepUnit" style="width:30%;">
              <option value="KHz">KHz</option>
              <option value="MHz" selected>MHz</option>
              <option value="GHz">GHz</option>
            </select>
        </div>

        <label>Target Power (dBm):</label>
        <input type="number" id="amplitude" min="-20" max="20" step="0.1" value=")=====" + AmpValueForMainMenu + R"=====(" style="width:100%; margin-bottom:15px;">

        <label>Filter:</label>
        <select id="filterSelect" style="width:100%; margin-bottom:15px;">
            <option value="0" )=====" + String(!FilterStatus ? "selected" : "") + R"=====(>Filter: OFF (0.15-22.6 GHz)</option>
            <option value="1" )=====" + String(FilterStatus ? "selected" : "") + R"=====(>Filter: ON (2-18 GHz)</option>
        </select>

        <button class="wide-button" id="applyCW" style="background-color:#89b4fa;">APPLY CW SETTINGS</button>
        <button class="wide-button" id="btn_rf" style="background-color:)=====" + btnRfColor + R"=====(">)=====" + btnRfText + R"=====(</button>
    </div>

    <div class="panel">
        <h2>&#128200; Sweep Settings</h2>
        
        <label>Start Frequency:</label>
        <div class="spinbox-container">
            <input type="number" step="0.001" id="sw_start" value=")=====" + StartValueForSweepMenu + R"=====(" style="width:65%;">
            <select id="sw_start_unit" style="width:30%;">
                <option value="KHz" )=====" + String(StartUnitForSweepMenu == "KHz" ? "selected" : "") + R"=====(>KHz</option>
                <option value="MHz" )=====" + String(StartUnitForSweepMenu == "MHz" ? "selected" : "") + R"=====(>MHz</option>
                <option value="GHz" )=====" + String(StartUnitForSweepMenu == "GHz" ? "selected" : "") + R"=====(>GHz</option>
            </select>
        </div>
        <div class="spinbox-container" style="margin-bottom: 15px;">
           <button class="preset-btn" onclick="setPreset('sw_start', 150, 'MHz')">150 MHz</button>
           <button class="preset-btn" onclick="setPreset('sw_start', 1, 'GHz')">1 GHz</button>
           <button class="preset-btn" onclick="setPreset('sw_start', 5, 'GHz')">5 GHz</button>
        </div>

        <label>Stop Frequency:</label>
        <div class="spinbox-container">
            <input type="number" step="0.001" id="sw_stop" value=")=====" + StopValueForSweepMenu + R"=====(" style="width:65%;">
            <select id="sw_stop_unit" style="width:30%;">
                <option value="KHz" )=====" + String(StopUnitForSweepMenu == "KHz" ? "selected" : "") + R"=====(>KHz</option>
                <option value="MHz" )=====" + String(StopUnitForSweepMenu == "MHz" ? "selected" : "") + R"=====(>MHz</option>
                <option value="GHz" )=====" + String(StopUnitForSweepMenu == "GHz" ? "selected" : "") + R"=====(>GHz</option>
            </select>
        </div>
        <div class="spinbox-container" style="margin-bottom: 15px;">
           <button class="preset-btn" onclick="setPreset('sw_stop', 5, 'GHz')">5 GHz</button>
           <button class="preset-btn" onclick="setPreset('sw_stop', 10, 'GHz')">10 GHz</button>
           <button class="preset-btn" onclick="setPreset('sw_stop', 22.6, 'GHz')">22.6 GHz</button>
        </div>

        <label>Step:</label>
        <div class="spinbox-container" style="margin-bottom: 15px;">
            <input type="number" step="0.001" id="sw_step" value=")=====" + StepValueForSweepMenu + R"=====(" style="width:65%;">
            <select id="sw_step_unit" style="width:30%;">
                <option value="KHz" )=====" + String(StepUnitForSweepMenu == "KHz" ? "selected" : "") + R"=====(>KHz</option>
                <option value="MHz" )=====" + String(StepUnitForSweepMenu == "MHz" ? "selected" : "") + R"=====(>MHz</option>
                <option value="GHz" )=====" + String(StepUnitForSweepMenu == "GHz" ? "selected" : "") + R"=====(>GHz</option>
            </select>
        </div>

        <div style="display:flex; gap:10px; margin-bottom: 15px;">
            <div style="flex:1;">
                <label>Dwell (ms):</label>
                <input type="number" id="sw_dwell" value=")=====" + DwellValueForSweepMenu + R"=====(" style="width:100%;">
            </div>
            <div style="flex:1;">
                <label>Target Power (dBm):</label>
                <input type="number" id="sw_att" min="-20" max="20" step="0.1" value=")=====" + AmpValueSweepForSweepMenu + R"=====(" style="width:100%;">
            </div>
        </div>

        <label>Type:</label>
        <select id="sw_type" style="width:100%; margin-bottom:15px;">
            <option value="Lin" )=====" + String(StepTypeValueForSweepMenu == "Lin" ? "selected" : "") + R"=====(>Linear (LIN)</option>
            <option value="Log" )=====" + String(StepTypeValueForSweepMenu == "Log" ? "selected" : "") + R"=====(>Logarithmic (LOG)</option>
        </select>

        <label>Sweep Count (0 = &#8734;):</label>
        <input type="number"
               id="sw_count"
               min="0"
               step="1"
               value=")=====" + CountValueForSweepMenu + R"=====("
               style="width:100%; margin-bottom:15px;">

        <button class="wide-button" id="applySweep" style="background-color:#89b4fa;">LOAD SWEEP SETTINGS</button>
        <button class="wide-button" id="btn_sweep_toggle" style="background-color:)=====" + btnSweepColor + R"=====(">)=====" + btnSweepText + R"=====(</button>
    </div>

    <div class="panel">
      <h2>&#128202; Device Screen (Live)</h2>
      <div id="live_curr" class="live-item">Current: --.- A</div>
      <div id="live_volt" class="live-item">Voltage: --.- V</div>
      <div id="live_power" class="live-item">Power: --.- W</div>
      <div id="live_temp" class="live-item">Temperature: --.- C</div>
      <div id="live_pll" class="live-item">LD Result: UNKNOWN</div>
    </div>

    <div class="panel">
      <h2>Wi-Fi Credentials</h2>
      <label>SSID:</label>
      <input type="text" id="ssid" name="ssid" style="width:100%; margin-bottom:10px;">
      <label>Password:</label>
      <input type="password" id="password" name="password" style="width:100%; margin-bottom:10px;">
      <button class="wide-button" id="save-credentials" style="background-color:#a6e3a1;">Save Credentials</button>
    </div>

    <script>
      window.onload = function() {
          // Add a cache-busting timestamp to force fresh settings retrieval:
          fetch('/getSettings?t=' + new Date().getTime())
          .then(response => response.json())
          .then(data => {
              document.getElementById('frequency').value = data.cw_freq;
              document.getElementById('freqUnit').value = data.cw_unit;
              document.getElementById('amplitude').value = data.cw_amp;
              document.getElementById('filterSelect').value = data.filt;
              
              document.getElementById('sw_start').value = data.sw_start;
              document.getElementById('sw_start_unit').value = data.sw_start_u;
              document.getElementById('sw_stop').value = data.sw_stop;
              document.getElementById('sw_stop_unit').value = data.sw_stop_u;
              document.getElementById('sw_step').value = data.sw_step;
              document.getElementById('sw_step_unit').value = data.sw_step_u;
              document.getElementById('sw_dwell').value = data.sw_dwell;
              document.getElementById('sw_att').value = data.sw_amp;
              document.getElementById('sw_type').value = data.sw_type;
              document.getElementById('sw_count').value = data.sw_count;
              
              const btnRf = document.getElementById('btn_rf');
              if (data.rf_out == 1) {
                  btnRf.style.backgroundColor = "#a6e3a1";
                  btnRf.innerText = "RF OUTPUT: ON";
              } else {
                  btnRf.style.backgroundColor = "#f38ba8";
                  btnRf.innerText = "RF OUTPUT: OFF";
              }
              
              const btnSweep = document.getElementById('btn_sweep_toggle');
              if (data.sw_run == 1) {
                  btnSweep.style.backgroundColor = "#f38ba8";
                  btnSweep.innerHTML = "&#9209; STOP SWEEP";
                  btnRf.style.backgroundColor = "#f9e2af";
                  btnRf.innerText = "RF OUTPUT: SWP";
              } else {
                  btnSweep.style.backgroundColor = "#a6e3a1";
                  btnSweep.innerHTML = "&#9654; START SWEEP";
              }
          })
          .catch(err => console.log("Sync failed:", err));
      };
      
      function getHz(val, unit) {
          if(unit === 'KHz') return val * 1e3;
          if(unit === 'MHz') return val * 1e6;
          if(unit === 'GHz') return val * 1e9;
          return val;
      }

      function validateFrequency(value, unit) {
        const hz = getHz(parseFloat(value), unit);
        
        const minHz = 150e6;   // 150 MHz
        const maxHz = 22.6e9;  // 22.6 GHz

        if (isNaN(hz)) {
          alert("Please enter a valid frequency.");
          return false;
        }
        
        if (hz < minHz) {
          alert("Frequency is below 150 MHz. Please enter a value between 150 MHz and 22.6 GHz.");
          return false;
        }
        
        if (hz > maxHz) {
          alert("Frequency is above 22.6 GHz. Please enter a value between 150 MHz and 22.6 GHz.");
          return false;
        }

        return true;
      }

      function fromHz(hz, unit) {
          if(unit === 'KHz') return hz / 1e3;
          if(unit === 'MHz') return hz / 1e6;
          if(unit === 'GHz') return hz / 1e9;
          return hz;
      }
      function setPreset(target, val, unit) {
          document.getElementById(target).value = val;
          document.getElementById(target + '_unit').value = unit;
      }

      // --- CW LOGIC ---
      document.getElementById('btn_plus').addEventListener('click', () => {
          let fVal = parseFloat(document.getElementById('frequency').value) || 0;
          let fUnit = document.getElementById('freqUnit').value;
          let sVal = parseFloat(document.getElementById('step_val').value) || 0;
          let sUnit = document.getElementById('stepUnit').value;
          
          let fHz = getHz(fVal, fUnit);
          let sHz = getHz(sVal, sUnit);
          document.getElementById('frequency').value = parseFloat(fromHz(fHz + sHz, fUnit).toFixed(3));
      });

      document.getElementById('btn_minus').addEventListener('click', () => {
          let fVal = parseFloat(document.getElementById('frequency').value) || 0;
          let fUnit = document.getElementById('freqUnit').value;
          let sVal = parseFloat(document.getElementById('step_val').value) || 0;
          let sUnit = document.getElementById('stepUnit').value;
          
          let fHz = getHz(fVal, fUnit);
          let sHz = getHz(sVal, sUnit);
          let newHz = fHz - sHz;
          if(newHz < 0) newHz = 0;
          document.getElementById('frequency').value = parseFloat(fromHz(newHz, fUnit).toFixed(3));
      });

      document.getElementById('applyCW').addEventListener('click', () => {
          const btn = document.getElementById('applyCW');
          const originalColor = btn.style.backgroundColor;
          
          const freq = document.getElementById('frequency').value;
          const unit = document.getElementById('freqUnit').value;
          const att = document.getElementById('amplitude').value;
          const filt = document.getElementById('filterSelect').value;

          if (!validateFrequency(freq, unit)) {
            return;
          }

          fetch('/applyCW', {
              method: 'POST',
              headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
              body: `freq=${freq}&unit=${unit}&att=${att}&filt=${filt}`
          })
          .then(response => response.text())
          .then(data => {
              btn.style.backgroundColor = "#a6e3a1";
              
              document.getElementById('btn_sweep_toggle').style.backgroundColor = "#a6e3a1";
              // Fix: button text markup was cleaned up.
              document.getElementById('btn_sweep_toggle').innerHTML = "&#9654; START SWEEP";

              setTimeout(() => { btn.style.backgroundColor = originalColor; }, 1000);
          });
      });

      document.getElementById('btn_rf').addEventListener('click', () => {
          fetch('/toggleRFOutput', { method: 'POST' })
          .then(response => response.text())
          .then(state => {
              const btn = document.getElementById('btn_rf');
              if(state === "1") {
                  btn.style.backgroundColor = "#a6e3a1";
                  btn.innerText = "RF OUTPUT: ON";
              } else {
                  btn.style.backgroundColor = "#f38ba8";
                  btn.innerText = "RF OUTPUT: OFF";
              }
          });
      });

      // --- SWEEP LOGIC ---
      document.getElementById('applySweep').addEventListener('click', () => {
          const btn = document.getElementById('applySweep');
          const originalColor = btn.style.backgroundColor;

          const start = document.getElementById('sw_start').value;
          const start_u = document.getElementById('sw_start_unit').value;
          const stop = document.getElementById('sw_stop').value;
          const stop_u = document.getElementById('sw_stop_unit').value;
          if (!validateFrequency(start, start_u)) {
            return;
          }

          if (!validateFrequency(stop, stop_u)) {
            return;
          }

          const step = document.getElementById('sw_step').value;
          const step_u = document.getElementById('sw_step_unit').value;
          const dwell = document.getElementById('sw_dwell').value;
          const att = document.getElementById('sw_att').value;
          const type = document.getElementById('sw_type').value;
          const count = document.getElementById('sw_count').value;

          fetch('/applySweep', {
              method: 'POST',
              headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
              body: `start=${start}&start_u=${start_u}&stop=${stop}&stop_u=${stop_u}&step=${step}&step_u=${step_u}&dwell=${dwell}&att=${att}&type=${type}&count=${count}`
          })
          .then(response => response.text())
          .then(data => {
              btn.style.backgroundColor = "#a6e3a1";
              setTimeout(() => { btn.style.backgroundColor = originalColor; }, 1000);
          });
      });

      document.getElementById('btn_sweep_toggle').addEventListener('click', () => {
          fetch('/toggleSweep', { method: 'POST' })
          .then(response => response.text())
          .then(state => {
              const btn = document.getElementById('btn_sweep_toggle');
              if(state === "1") {
                  btn.style.backgroundColor = "#f38ba8";
                  // Fix: button text markup was cleaned up.
                  btn.innerHTML = "&#9209; STOP SWEEP";
                  document.getElementById('btn_rf').style.backgroundColor = "#f9e2af";
                  document.getElementById('btn_rf').innerText = "RF OUTPUT: SWP";
              } else {
                  btn.style.backgroundColor = "#a6e3a1";
                  // Fix: button text markup was cleaned up.
                  btn.innerHTML = "&#9654; START SWEEP";
                  document.getElementById('btn_rf').style.backgroundColor = "#f38ba8";
                  document.getElementById('btn_rf').innerText = "RF OUTPUT: OFF";
              }
          });
      });

      // TELEMETRY POLLING
      setInterval(() => {
          fetch('/telemetry')
          .then(response => response.json())
          .then(data => {
              document.getElementById('live_curr').innerText = `Current: ${data.curr} A`;
              document.getElementById('live_volt').innerText = `Voltage: ${data.volt} V`;
              document.getElementById('live_power').innerText = `Power: ${data.power} W`;
              document.getElementById('live_temp').innerText = `Temperature: ${data.temp} C`;
              
              const btnSweep = document.getElementById('btn_sweep_toggle');
              const btnRf = document.getElementById('btn_rf');

              if (data.sw_run === 1) {
                btnSweep.style.backgroundColor = "#f38ba8";
                btnSweep.innerHTML = "&#9209; STOP SWEEP";

                btnRf.style.backgroundColor = "#f9e2af";
                btnRf.innerText = "RF OUTPUT: SWP";
              }
              else {
                // Only reset RF indication if the web page was previously in Sweep mode
                if (btnSweep.innerText.includes("STOP SWEEP")) {
                  btnRf.style.backgroundColor = "#f38ba8";
                  btnRf.innerText = "RF OUTPUT: OFF";
                }

                btnSweep.style.backgroundColor = "#a6e3a1";
                btnSweep.innerHTML = "&#9654; START SWEEP";
              }

              const pll = document.getElementById('live_pll');
              if (data.lock === 1) {
                  pll.innerText = "LD Result: LOCKED";
                  pll.style.color = "#a6e3a1"; // Green
              } else {
                  pll.innerText = "LD Result: UNLOCKED";
                  pll.style.color = "#f38ba8"; // Red
              }
          })
          .catch(err => console.log(err));
      }, 1500);

      document.getElementById('save-credentials').addEventListener('click', async () => {
        const button = document.getElementById('save-credentials');
        const ssid = document.getElementById('ssid').value;
        const password = document.getElementById('password').value;
        await fetch('/save', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: `ssid=${ssid}&password=${password}`
        });
        button.innerText = "Saved! Restarting...";
      });
    </script>
  </div>
</body>
</html>
)=====";
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", getHTML()); 
}

// --------------------------------------------------------------
// Web API: live telemetry
// Returns voltage, current, power, temperature, and PLL lock status as JSON.
// --------------------------------------------------------------
void handleTelemetry() {
    String json = "{";
    json += "\"volt\":\"" + String(last_usb_voltage, 2) + "\",";
    json += "\"curr\":\"" + String(last_dsg_current, 2) + "\",";
    json += "\"power\":\"" + String(last_usb_voltage * last_dsg_current, 2) + "\",";
    json += "\"temp\":\"" + String(temp, 1) + "\",";
    json += "\"lock\":" + String(last_pll_lock ? 1 : 0) + ",";
    json += "\"sw_run\":" + String(isSweepRunning ? 1 : 0);
    json += "}";
    server.send(200, "application/json", json);
}

void handleGetSettings() {
    extern String FreqValueForMainMenu;
    extern String FreqUnitForMainMenu;
    extern String AmpValueForMainMenu;
    extern String StartValueForSweepMenu;
    extern String StartUnitForSweepMenu;
    extern String StopValueForSweepMenu;
    extern String StopUnitForSweepMenu;
    extern String StepValueForSweepMenu;
    extern String StepUnitForSweepMenu;
    extern String DwellValueForSweepMenu;
    extern String AmpValueSweepForSweepMenu;
    extern String StepTypeValueForSweepMenu;
    extern String CountValueForSweepMenu;
    extern bool rfOutputEnabled;
    extern bool isSweepRunning;

    String json = "{";
    json += "\"cw_freq\":\"" + FreqValueForMainMenu + "\",";
    json += "\"cw_unit\":\"" + FreqUnitForMainMenu + "\",";
    json += "\"cw_amp\":\"" + AmpValueForMainMenu + "\",";
    json += "\"filt\":" + String(FilterStatus ? 1 : 0) + ",";
    json += "\"sw_start\":\"" + StartValueForSweepMenu + "\",";
    json += "\"sw_start_u\":\"" + StartUnitForSweepMenu + "\",";
    json += "\"sw_stop\":\"" + StopValueForSweepMenu + "\",";
    json += "\"sw_stop_u\":\"" + StopUnitForSweepMenu + "\",";
    json += "\"sw_step\":\"" + StepValueForSweepMenu + "\",";
    json += "\"sw_step_u\":\"" + StepUnitForSweepMenu + "\",";
    json += "\"sw_dwell\":\"" + DwellValueForSweepMenu + "\",";
    json += "\"sw_amp\":\"" + AmpValueSweepForSweepMenu + "\",";
    json += "\"sw_type\":\"" + StepTypeValueForSweepMenu + "\",";
    json += "\"sw_count\":\"" + CountValueForSweepMenu + "\",";
    json += "\"rf_out\":" + String(rfOutputEnabled ? 1 : 0) + ",";
    json += "\"sw_run\":" + String(isSweepRunning ? 1 : 0);
    json += "}";
    server.send(200, "application/json", json);
}


// --------------------------------------------------------------
// Fan control with hysteresis
// Fan turns on at 45 C and remains on until temperature falls to 40 C.
// --------------------------------------------------------------
static uint8_t fanState = 0;   

void Fan_Init(void)
{
    gpio_reset_pin((gpio_num_t)FAN_PIN);
    gpio_set_direction((gpio_num_t)FAN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)FAN_PIN, 0);
}

void Fan_Control(float temp_val)
{
    if (!fanState && temp_val >= FAN_ON_TEMP)
    {
        gpio_set_level((gpio_num_t)FAN_PIN, 1);
        fanState = 1;
    }
    else if (fanState && temp_val <= FAN_OFF_TEMP)
    {
        gpio_set_level((gpio_num_t)FAN_PIN, 0);
        fanState = 0;
    }
}

// --------------------------------------------------------------
// Web API: apply CW settings
// Maps browser inputs to the existing SCPI command handler.
// --------------------------------------------------------------
void handleApplyCW() {
    // Stop sweep mode while preserving the current RF output state (rfOutputEnabled).
    isSweepRunning = false; 

    String freq = server.arg("freq");
    String unit = server.arg("unit");
    String att = server.arg("att");
    String filt = server.arg("filt");

    // 1. Switch the display to CW mode. The existing RF state is preserved while redrawing the screen.
    RC_HandleLine((char*)"DISP:MENU CW");

    // 2. Convert web input values into SCPI-style commands and pass them to the system.
    String cmdFreq = "FREQ " + freq + unit;
    RC_HandleLine((char*)cmdFreq.c_str());

    String cmdFilt = String("FILT ") + (filt == "1" ? "ON" : "OFF");
    RC_HandleLine((char*)cmdFilt.c_str());

    // Trigger the automatic power-level handling through POW:LEV.
    String cmdPow = "POW:LEV " + att;
    RC_HandleLine((char*)cmdPow.c_str());

    // Removed the previously unnecessary OUTP OFF and rfOutputEnabled = false behavior from this path.

    // Store the latest values so the device can retain them after restart.
    currentFrequency = freq;
    currentFreqUnit = unit;
    currentAmplitude = att;

    server.send(200, "text/plain", "CW Settings Applied");
}

// --------------------------------------------------------------
// Web API: apply sweep settings
// Maps browser sweep inputs to the existing SCPI command handler.
// --------------------------------------------------------------
void handleApplySweep() {
    // 1. Switch the display to Sweep mode.
    RC_HandleLine((char*)"DISP:MENU SWP");

    String start = server.arg("start");
    String start_u = server.arg("start_u");
    String stop = server.arg("stop");
    String stop_u = server.arg("stop_u");
    String step = server.arg("step");
    String step_u = server.arg("step_u");
    String dwell = server.arg("dwell");
    String att = server.arg("att");
    String type = server.arg("type");
    String count = server.arg("count");

    // 2. Convert all sweep parameters into SCPI-style commands and pass them to the system.
    String cmdStart = "SWEEP:STAR " + start + start_u;
    RC_HandleLine((char*)cmdStart.c_str());

    String cmdStop = "SWEEP:STOP " + stop + stop_u;
    RC_HandleLine((char*)cmdStop.c_str());

    String cmdStep = "SWEEP:STEP " + step + step_u;
    RC_HandleLine((char*)cmdStep.c_str());

    String cmdDwell = "SWEEP:DWEL " + dwell;
    RC_HandleLine((char*)cmdDwell.c_str());

    // Target power setting used for sweep mode.
    String cmdPow = "SWEEP:POW " + att;
    RC_HandleLine((char*)cmdPow.c_str());

    String cmdType = "SWEEP:TYPE " + type;
    RC_HandleLine((char*)cmdType.c_str());

    String cmdCount = "SWEEP:COUN " + count;
    RC_HandleLine((char*)cmdCount.c_str());

    server.send(200, "text/plain", "Sweep Settings Applied");
}


// --------------------------------------------------------------
// Web API: start/stop sweep mode
// Updates RF output state and refreshes the sweep screen.
// --------------------------------------------------------------
void handleToggleSweep() {
    isSweepRunning = !isSweepRunning;
    currentMenu = SWEEP_MENU;
    
    if (isSweepRunning) {
        currentHz = 0; 
        sweepCycleCount = 0;
        SetRfOnOff(true);
        rfOutputEnabled = true;
    } else {
        SetRfOnOff(false);
        rfOutputEnabled = false;
    }
    
    drawSweepMenu(); 

    server.send(200, "text/plain", isSweepRunning ? "1" : "0");
}

// --------------------------------------------------------------
// Non-blocking Wi-Fi state machine
// Attempts stored Wi-Fi credentials first, then falls back to hotspot mode.
// --------------------------------------------------------------
void manageWiFiConnection() {
  static unsigned long lastCheckTime = 0;
  switch (wifiState) {
    case WIFI_INIT:
      readWiFiCredentials();
      SetWifiStatus(WIFI_STATUS_OFF);
      if (wifiSSID.length() > 0 && wifiPassword.length() > 0) {
        wifiState = WIFI_CONNECTING;
        wifiStartTime = millis();
        wifiRetryCount = 0;
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
        ConnectionStatus("Conn to WiFi...", true);
      } else {
        wifiState = WIFI_HOTSPOT;
      }
      break;
    case WIFI_CONNECTING:
      if (millis() - wifiStartTime > 2000) { 
        wifiStartTime = millis();
        if (WiFi.status() == WL_CONNECTED) {
          wifiState = WIFI_CONNECTED;
          ConnectionStatus("Connected :)", true);
          String ipStr = WiFi.localIP().toString();
          ConnectionStatus(ipStr.c_str(), true);
          SetWifiStatus(WIFI_STATUS_ON);
          InitServer();
        } else {
          wifiRetryCount++;
          ConnectionStatus("Retrying WiFi...", true);
          if (wifiRetryCount >= 10) { 
            wifiState = WIFI_HOTSPOT;
          }
        }
      }
      break;
    case WIFI_CONNECTED:
      break;
    case WIFI_HOTSPOT:
    {
      ConnectionStatus("Hotspot...", true);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apSSID.c_str(), apPassword);
      
      int retries = 0;
      String dots = "";
      while (WiFi.softAPIP().toString() == "0.0.0.0" && retries++ < 30) {
          dots += ".";
          ConnectionStatus(dots.c_str(), true);
          delay(500);
      }

      ConnectionStatus("Hotspot IP:", true);
      ConnectionStatus(WiFi.softAPIP().toString().c_str(), true);
      InitServer();
      SetWifiStatus(WIFI_STATUS_HOTSPOT);

      wifiState = WIFI_FAILED;
      break;
    }
    case WIFI_FAILED:
      break;
  }
}

unsigned long lastUpdateTime1, lastUpdateTime2 = 0;

// --------------------------------------------------------------
// Main runtime loop
// Handles touch input, fan control, serial command processing, sweep execution, web client handling, and screen telemetry updates.
// --------------------------------------------------------------
void loop() {
  unsigned long currentTime = millis();
  
  handleTouch();
  if (currentTime - lastUpdateTime1 >= 1000)
  {
    lastUpdateTime1 = currentTime;
    temp = Read_Temp();
    Fan_Control(temp);
  }

  while (Serial.available()) 
  {
    RC_ProcessByte((uint8_t)Serial.read());
  }

  if (isSweepRunning && currentMenu == SWEEP_MENU)
  {
     RunSweep();
     server.handleClient(); 
     return; 
  }
  
  server.handleClient();
  manageWiFiConnection();
  
  if (currentTime - lastUpdateTime2 >= 500)
  {
    lastUpdateTime2 = currentTime;
    if (currentMenu == MAIN_MENU)
    {
      last_usb_voltage = Read_5V_Voltage();
      last_dsg_current = Read_5V_Current();
      last_pll_lock = isPLL_Locked();

      SetLock(last_pll_lock);
      SetTemp(String(temp, 1).c_str());
      SetUSBVoltge(String(last_usb_voltage, 1).c_str()); 
    }
    else if (currentMenu == INFO_MENU)
    {
      drawInfoScreen();
    }
  } 
}