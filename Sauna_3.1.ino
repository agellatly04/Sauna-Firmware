#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266httpUpdate.h>

// Configuration
const char* ssid = "Wi-Fi 6"; //"Netgear32"
const char* password = "gellatly"; //"melodicdiamond611"
const uint16_t IR_SEND_PIN = 5;
const int ONE_WIRE_BUS = 0;  // DS18B20 data pin (can use any digital pin)
const float TEMP_THRESHOLD = 115.0;  // Temperature threshold for power reduction
const unsigned long CHECK_INTERVAL = 30000;  // Check temperature every 20 seconds
const unsigned long UPDATE_INTERVAL = 3600000; // Check for updates every hour

// Setup OneWire and DallasTemperature instances
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// IR signals (replace with actual values)
const uint32_t IR_SIGNALS[] = {
  0xFF22DD,  // ON/OFF
  0xFF906F,  // POWER_UP
  0xFFB04F,  // POWER_DOWN
  0xFFE01F,  // TIME_UP
  0xFF6897   // TIME_DOWN
};

// Global variables
IRsend irsend(IR_SEND_PIN);
ESP8266WebServer server(80);
bool saunaOn = false;
int powerLevel = 7;
int timeSetting = 45;
unsigned long saunaStartTime = 0;
float currentTemp = 0.0;
unsigned long lastTempCheck = 0;  // For tracking temperature check interval
unsigned long lastUpdateCheck = 0; // For tracking OTA update interval

void checkForUpdates() {
  WiFiClientSecure client;
  client.setInsecure();  // Disable SSL verification for simplicity
  t_httpUpdate_return result = ESPhttpUpdate.update(client, "https://raw.githubusercontent.com/agellatly04/Sauna-Firmware/refs/heads/main/Sauna_3.1.ino");
  
  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("Update failed! Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No new updates available.");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("Update successful! Rebooting...");
      ESP.restart();
      break;
  }
}

// Function to read temperature from DS18B20
float readTemperature() {
  sensors.requestTemperatures(); // Send command to get temperatures
  float tempC = sensors.getTempCByIndex(0); // Get temperature in Celsius
  
  // Convert to Fahrenheit
  float tempF = (tempC * 9.0 / 5.0) + 32.0;
  
  // Check if reading is valid
  if (tempF == -196.6) { // DS18B20 error value after conversion to F
    return currentTemp; // Return last valid reading
  }
  
  return tempF;
}

const char webPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Sauna Control</title>
  <style>
    body { 
      font-family: Arial; 
      text-align: center; 
      margin: 0; 
      padding: 80px 20px 20px 20px;
      font-size: 36px;
      min-height: 100vh;
      box-sizing: border-box;
    }
    .title {
      font-size: 72px;
      font-weight: bold;
      text-decoration: underline;
      margin-bottom: 40px;
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      background: white;
      padding: 20px;
      z-index: 100;
    }
    .content {
      padding-top: 40px;
    }
    .btn { 
      padding: 40px 80px; 
      font-size: 48px; 
      margin: 20px; 
      border-radius: 15px; 
      border: 3px solid gray;
      color: white;
      cursor: pointer;
    }
    .power { 
      background: green;
      font-size: 60px;
      padding: 50px 100px;
    }
    .control { 
      background: blue; 
    }
    #status { 
      font-size: 96px; 
      font-weight: bold; 
      margin: 30px;
    }
    .display { 
      font-size: 48px; 
      margin: 30px; 
      padding: 20px; 
      border-radius: 10px; 
    }
    #power-display {
      background-color: black;
      color: red;
      font-weight: bold;
      padding: 30px;
      display: inline-block;
      min-width: 400px;
      border-radius: 15px;
      font-size: 60px;
    }
    #timer-display {
      background-color: yellow;
      font-weight: bold;
      padding: 30px;
      display: inline-block;
      min-width: 200px;
      border-radius: 15px;
    }
    #temp-display {
      background-color: orange;
      color: white;
      font-weight: bold;
      padding: 30px;
      display: inline-block;
      min-width: 300px;
      border-radius: 15px;
      font-size: 54px;
    }
    #elapsed-display {
      font-weight: bold;
      font-size: 54px;
    }
    .controls {
      display: flex;
      justify-content: center;
      gap: 30px;
    }
    .control-column {
      display: flex;
      flex-direction: column;
      align-items: center;
    }
  </style>
</head>
<body>
  <div class="title">Sauna Controller</div>
  <div class="content">
    <div id="status">OFF</div>
    <div class="display">
      <span id="temp-display">Temperature: <span id="temp">--</span>°F</span>
    </div>
    <div class="display">
      <span id="power-display">Power Level: <span id="power">7</span></span>
    </div>
    <div class="display">
      <span id="timer-display">Timer: <span id="time">45</span> min</span>
    </div>
    <div class="display" id="elapsed-display">Elapsed: <span id="elapsed">0:00</span></div>

    <button class="btn power" onclick="send('toggle')">ON/OFF</button><br>

    <div class="controls">
      <div class="control-column">
        <button class="btn control" onclick="send('power_up')">Power +</button><br>
        <button class="btn control" onclick="send('power_down')">Power -</button><br>
      </div>
      <div class="control-column">
        <button class="btn control" onclick="send('time_up')">Timer +</button><br>
        <button class="btn control" onclick="send('time_down')">Timer -</button><br>
      </div>
    </div>
  </div>

  <script>
    function send(cmd) {
      fetch('/cmd?action=' + cmd).then(function(r) { 
        return r.json(); 
      }).then(function(data) {
        document.getElementById('status').textContent = data.on ? 'ON' : 'OFF';
        document.getElementById('power').textContent = data.power;
        document.getElementById('time').textContent = data.time;
        document.getElementById('elapsed').textContent = data.elapsed;
        document.getElementById('temp').textContent = data.temp.toFixed(1);
        
        // Check elapsed time
        const [minutes, seconds] = data.elapsed.split(':').map(Number);
        if (minutes >= 60) {
          // Reset webpage display without sending IR signals
          window.location.reload(true);
        }
      });
    }
    setInterval(function() {
      send('status');
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

// Forward declaration
void handleCommand();

void setup() {
  Serial.begin(115200);  // Initialize serial communication
  Serial.println("\nStarting up...");
  
  irsend.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Start up the DS18B20 library (only call sensors.begin() once)
  sensors.begin();
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("You can access the sauna control at: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Or using mDNS at: http://sauna.local");

  // Add mDNS support
  if (MDNS.begin("sauna")) {
    Serial.println("mDNS responder started");
  }

  server.on("/", []() { server.send_P(200, "text/html", webPage); });
  server.on("/cmd", handleCommand);
  server.begin();

  // Check for OTA updates at startup
  checkForUpdates();
}

void handleCommand() {
  String cmd = server.arg("action");
  
  // Calculate elapsed time
  unsigned long seconds = 0;
  if (saunaOn) {
    seconds = (millis() - saunaStartTime) / 1000;
    
    // If 60 minutes have elapsed, reset the webpage state
    if (seconds >= 3600) { // 3600 seconds = 60 minutes
      saunaOn = false;
      powerLevel = 7;
      timeSetting = 45;
    }
  }
  
  // Process commands only if we haven't hit the timeout
  if (seconds < 3600) {
    if (cmd == "toggle") {
      saunaOn = !saunaOn;
      if (saunaOn) {
        saunaStartTime = millis();
        powerLevel = 7;
        timeSetting = 45;
      }
      irsend.sendNEC(IR_SIGNALS[0], 32);
    }
    else if (saunaOn) {
      if (cmd == "power_up" && powerLevel < 7) {
        powerLevel++;
        irsend.sendNEC(IR_SIGNALS[1], 32);
      }
      else if (cmd == "power_down" && powerLevel > 0) {
        powerLevel--;
        irsend.sendNEC(IR_SIGNALS[2], 32);
      }
      else if (cmd == "time_up") {
        timeSetting += 5;
        irsend.sendNEC(IR_SIGNALS[3], 32);
      }
      else if (cmd == "time_down" && timeSetting > 5) {
        timeSetting -= 5;
        irsend.sendNEC(IR_SIGNALS[4], 32);
      }
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);

  // Read temperature
  currentTemp = readTemperature();

  String elapsed = "0:00";
  if (saunaOn) {
    elapsed = String(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + String(seconds % 60);
  }

  String jsonResponse = "{\"on\":" + String(saunaOn ? "true" : "false") + 
                       ",\"power\":" + String(powerLevel) + 
                       ",\"time\":" + String(timeSetting) + 
                       ",\"elapsed\":\"" + elapsed + "\"" +
                       ",\"temp\":" + String(currentTemp, 1) + "}";

  server.send(200, "application/json", jsonResponse);
}

void loop() {
  MDNS.update();
  server.handleClient();
  
  // Check temperature every 20 seconds
  if (saunaOn && millis() - lastTempCheck >= CHECK_INTERVAL) {
    currentTemp = readTemperature();
    if (currentTemp >= TEMP_THRESHOLD && powerLevel > 0) {
      powerLevel--;
      irsend.sendNEC(IR_SIGNALS[2], 32);
    }
    lastTempCheck = millis();
  }
  
  // Check for updates every hour
  if (millis() - lastUpdateCheck >= UPDATE_INTERVAL) {
    checkForUpdates();
    lastUpdateCheck = millis();
  }
}
