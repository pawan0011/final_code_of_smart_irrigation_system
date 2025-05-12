#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <ThingSpeak.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <WiFi.h>
#include <WiFiMulti.h>

WiFiMulti wifiMulti;

// =========================
// WiFi Credentials
// =========================
//const char* ssid = "OPPO A12";
//const char* password = "00000000";

// === Flags ===
bool awaitingSSID = false;
bool awaitingPASS = false;
String newSSID = "";
String newPASS = "";

// =========================
// Telegram Credentials
// =========================
#define BOT_TOKEN "7122121223:AAFCCB2NjY3Z27BpPNwaM1RMFmJ1QhAiHes"
#define CHAT_ID  "1079071197"
#define CHAT_ID_yash  "6741824552"
#define chat_id_yash "6741824552"
// Secure client for Telegram
WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);

// =========================
// ThingSpeak Credentials
// =========================
unsigned long channelID = 2870729;     // Replace with your ThingSpeak channel ID
const char* apiKey = "4LDMRD21WTOE5ZIC"; // Replace with your ThingSpeak Write API key
WiFiClient thingSpeakClient;           // Used for ThingSpeak API calls

// =========================
// Google Sheets Logging URL
// =========================
const char* sheetsURL = "https://script.google.com/macros/s/AKfycbzdEdhkiBYyXYI7l-9ynTfivWdu4JxrpO_PX6VFVzagFU75zdBtnmcaC2scgWjdlR10/exec";

// =========================
// Sensor & Pump Pins
// =========================
#define MOISTURE_SENSOR_PIN 34
#define DHTPIN 15          // Pin connected to DHT11 data
#define DHTTYPE DHT11      // DHT 11 sensor
#define PUMP_PIN 5
#define TANK_SENSOR_1  14  // GPIO14 - Tank Empty Alert
#define TANK_SENSOR_2  27
#define TANK_SENSOR_3  26
#define TANK_SENSOR_4  25
#define TANK_SENSOR_5  33


DHT dht(DHTPIN, DHTTYPE);

// =========================
// Global Variables: Sensor Readings, Pump State & Thresholds
// =========================
int moistureLevel = 0;
int temperature = 0;
int humidity = 0;
bool pumpStatus = false;  // false = OFF; true = ON
bool tankEmptyNotified = false;


// Moisture threshold values for automatic pump control
int dryThreshold = 2000; // Pump should turn ON if moisture < dryThreshold (dry soil)
int wetThreshold = 700; // Pump should turn OFF if moisture > wetThreshold (wet soil)

// -------------------------
// Event Logging Structures
// -------------------------
#define MAX_HISTORY 200

struct Event {
  String timestamp;   // Fetched from ThingSpeak
  int moisture;
  int temperature;
  int humidity;
  bool pumpStatus;
};

Event history[MAX_HISTORY];
int historyCount = 0;  // Number of events stored

// -------------------------
// Previous Values for Change Detection
// -------------------------
int prevMoisture    = -1; 
int prevTemperature = -1;
int prevHumidity    = -1;
bool prevPumpStatus = false;

// -------------------------
// Timing Variables
// -------------------------
unsigned long lastReportTime = 0;  
unsigned long lastTelegramCheck = 0;
const unsigned long reportInterval = 43200000; // 12 hours (in ms)
const unsigned long telegramCheckInterval = 5;   // Check Telegram every 5 ms

// =========================
// Function Prototypes
// =========================
String fetchLocalTimeFromThingSpeak();            // Fetches real-time timestamp from ThingSpeak server
void sendTelegramAlert(String message);
void pumpControl(bool state, String chat_id);
void pumpStatusReport(String chat_id);
void sendSensorData(String chat_id);
void sendSensorStatistics(String chat_id);
void sendTodayReport(String chat_id);
void sendMonthlyReport(String chat_id);
void sendScheduleReport(String chat_id); // Full event history
void checkTelegramMessages();
void updateThingSpeak();
void updateGoogleSheets();
void logEvent();                         // Log event based on current sensor & pump state
void sendTelegramAlert();
String convertToIST(String utcTime);
void showDeveloperNames(String chat_id);
String URLEncode(const String &str);

// =========================
// Setup
// =========================
  void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
   pinMode(TANK_SENSOR_1, INPUT);
  pinMode(TANK_SENSOR_2, INPUT);
  pinMode(TANK_SENSOR_3, INPUT);
  pinMode(TANK_SENSOR_4, INPUT);
  pinMode(TANK_SENSOR_5, INPUT);

  
    // Add multiple Wi-Fi networks
  wifiMulti.addAP("OPPO A12", "00000000");
  wifiMulti.addAP("POCO M6", "00000000");
  wifiMulti.addAP("hodadmin", "hodadmin@123");

  Serial.println("📶 Connecting to Wi-Fi...");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
// Send connected Wi-Fi SSID to Telegram bot
  String message = "🤖 ESP32 connected to Wi-Fi\nSSID: " + WiFi.SSID() + "\nIP: " + WiFi.localIP().toString();
  bot.sendMessage(CHAT_ID, message, "");
  
  // Set up TLS/SSL certificate for Telegram connections
 telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
 // **Initialize Telegram Client**
    telegramClient.setInsecure();
     // **Connect to ThingSpeak**
  ThingSpeak.begin(thingSpeakClient);
  // Initialize NTP (set to IST: UTC+5:30)
  configTime(19800, 0, "pool.ntp.org");

    // Send message
 
  
  bot.sendMessage(CHAT_ID, message, "");
  bot.sendMessage(CHAT_ID_yash, message, "");
bot.sendMessage(CHAT_ID_yash, "🌱 ESP32 Connected to Telegram :Bot is online!", "");
  if (bot.sendMessage(CHAT_ID, "🌱 ESP32 Connected to Telegram :Bot is online!", "")) {
    Serial.println("Startup message sent.");
  } else {
    Serial.println("Failed to send startup message.");
  }
}

// =========================
// Main Loop
// =========================
 void loop() {
  // Read sensor values from analog pins
  int newMoisture    = analogRead(MOISTURE_SENSOR_PIN);
  int newTemperature = dht.readTemperature();  // Celsius
  int newHumidity    = dht.readHumidity();
checkTelegramMessages();

  moistureLevel = newMoisture;
  temperature   = newTemperature;
  humidity      = newHumidity;

  // Debug output - print current sensor readings, pump state and timestamp
  Serial.println("----- SENSOR UPDATE -----");
  Serial.println("Moisture:    " + String(moistureLevel));
  Serial.println("Temperature: " + String(temperature));
  Serial.println("Humidity:    " + String(humidity));
  Serial.println("Pump Status: " + String(pumpStatus ? "ON" : "OFF"));
  Serial.println("Timestamp:   " + fetchLocalTimeFromThingSpeak());
  Serial.println("-------------------------");

  // Automatic pump control based on moisture thresholds
  if (moistureLevel > dryThreshold && pumpStatus == false) {
    pumpControl(true, CHAT_ID);
  } else if (moistureLevel < wetThreshold && pumpStatus == true) {
    pumpControl(false, CHAT_ID);
  }
checkTelegramMessages();
  // If any sensor value or pump state changed, log event and update cloud immediately
  if (newMoisture != prevMoisture || newTemperature != prevTemperature ||pumpStatus != prevPumpStatus) {
    logEvent();
    updateThingSpeak();
    updateGoogleSheets();
    prevMoisture = newMoisture;
    prevTemperature = newTemperature;
    prevHumidity = newHumidity;
    prevPumpStatus = pumpStatus;

  }
  checkTelegramMessages();
  // Scheduled full history report (every 12 hours)
  if (millis() - lastReportTime > reportInterval) {
    sendScheduleReport(CHAT_ID);
    lastReportTime = millis();
  }
  
  // Check for incoming Telegram commands almost continuously
 // if (millis() - lastTelegramCheck > telegramCheckInterval) {
    checkTelegramMessages();
   // lastTelegramCheck = millis();
  //}
   //delay(5);  // Very short delay to yield

 // Check sensor 1 every 5s and send alert if tank is empty
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();

    int sensor1 = digitalRead(TANK_SENSOR_1);
    if (sensor1 == HIGH && !tankEmptyNotified) {
      bot.sendMessage(CHAT_ID, "🚨 ALERT: Water Tank is EMPTY!", "");
      tankEmptyNotified = true;
    } else if (sensor1 == LOW) {
      tankEmptyNotified = false;  // Reset alert flag
    }
  }

}

// =========================
// Utility & Cloud Functions
// =========================

// =========================
// Fetch Local Time
// =========================
 String fetchLocalTimeFromThingSpeak() {
  HTTPClient http;
  String url = String("https://api.thingspeak.com/channels/") + String(channelID) + "/feeds.json?results=1";
  http.begin(url);
  int httpCode = http.GET();
  String localTime = "Error";
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String utcTime = doc["feeds"][0]["created_at"].as<String>();
      localTime = convertToIST(utcTime);  // Convert UTC to IST
    }
  }
  
  http.end();
  return localTime;
}

String convertToIST(String utcTime) {
  int year = utcTime.substring(0, 4).toInt();
  int month = utcTime.substring(5, 7).toInt();
  int day = utcTime.substring(8, 10).toInt();
  int hour = utcTime.substring(11, 13).toInt();
  int minute = utcTime.substring(14, 16).toInt();
  int second = utcTime.substring(17, 19).toInt();

  // Adjust UTC to IST (+5:30)
  hour += 5;
  minute += 30;

  if (minute >= 60) {
    minute -= 60;
    hour += 1;
  }

  if (hour >= 24) {
    hour -= 24;
    day += 1;
  }

  char formattedTime[20];
  sprintf(formattedTime, "%02d/%02d/%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
  return String(formattedTime);
}
 void sendTelegramAlert(String message) {
  bot.sendMessage(CHAT_ID, message, "");
  bot.sendMessage(CHAT_ID_yash, message, "");
 }

  void updateThingSpeak() {
  ThingSpeak.setField(1, moistureLevel);
  ThingSpeak.setField(2, temperature);
  ThingSpeak.setField(3, humidity);
  ThingSpeak.setField(4, pumpStatus ? 1 : 0);
  int response = ThingSpeak.writeFields(channelID, apiKey);
  if (response == 200) {
    Serial.println("✅ Data sent to ThingSpeak successfully!");
  } else {
    Serial.println("❌ ThingSpeak Update Failed. Error Code: " + String(response));
  }
}

void updateGoogleSheets() {
  HTTPClient http;
  String url = String(sheetsURL) +
               "?moisture="    + String(moistureLevel) +
               "&temperature=" + String(temperature) +
               "&humidity="    + String(humidity) +
               "&pump="        + String(pumpStatus ? 1 : 0);

  Serial.println("Google Sheets URL: " + url);
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.println("✅ Google Sheets Update Sent. Response code: " + String(httpCode));
    String response = http.getString();
    Serial.println("Response: " + response);
  } else {
    Serial.println("❌ Google Sheets Update Failed. Error: " + http.errorToString(httpCode));
  }
  
  http.end();
}


// =========================
// Event & Serial Handling
// =========================

// Log an event with the current sensor readings, pump state, and timestamp
void logEvent() {
  Event ev;
  ev.timestamp = fetchLocalTimeFromThingSpeak();
  ev.moisture = moistureLevel;
  ev.temperature = temperature;
  ev.humidity = humidity;
  ev.pumpStatus = pumpStatus;
  
  if (historyCount < MAX_HISTORY) {
    history[historyCount++] = ev;
  } else {
    // Shift left if history full, then append the new event
    for (int i = 1; i < MAX_HISTORY; i++) {
      history[i - 1] = history[i];
    }
    history[MAX_HISTORY - 1] = ev;
  }
  Serial.println("Event logged: " + ev.timestamp);
}


// =========================A
// Pump Control & Reporting via Telegram Commands
// =========================

void pumpControl(bool state, String chat_id) {
  pumpStatus = state;
  digitalWrite(PUMP_PIN, state ? HIGH : LOW);
  String msg = state ? "🚨 PUMP TURN ON 🚨\n" : "Pump turned OFF.\n";
  msg += "🌱 Moisture: " + String(moistureLevel) + "\n";
  msg += "🌡️ Temperature: " + String(temperature) + "\n";
  msg += "💧 Humidity:" + String(humidity) + "\n";
  msg += "Timestamp: " + fetchLocalTimeFromThingSpeak();
  bot.sendMessage(chat_id, msg, "");
  bot.sendMessage(chat_id_yash, msg, "");
  
  updateThingSpeak();
  updateGoogleSheets();
  logEvent();
  prevPumpStatus = pumpStatus;
}

void pumpStatusReport(String chat_id) {
  String status = pumpStatus ? "ON" : "OFF";
  bot.sendMessage(chat_id, "Current pump status: " + status, "");
  bot.sendMessage(chat_id_yash, "Current pump status: " + status, "");
}

void sendSensorData(String chat_id) {
  String msg = String("🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
              "Current Sensor Data:\n") +
               "🌱 Soil Moisture:  " + String(moistureLevel) + "\n" +
               "🌡️ Temperature: " + String(temperature) + "\n" +
               "💧 Humidity: " + String(humidity) + "\n" +
               "🚰 Pump: " + (pumpStatus ? "ON" : "OFF") + "\n" +
               "Timestamp: " + fetchLocalTimeFromThingSpeak();
  bot.sendMessage(chat_id, msg, "");
  bot.sendMessage(chat_id_yash, msg, "");
}

void sendSensorStatistics(String chat_id) {
  int count = (historyCount < 10) ? historyCount : 10;
  String msg = "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n" 
   "Last " + String(count) + " events:\n";
  int pumpChangeCount = 0;
  for (int i = historyCount - count; i < historyCount; i++) {
    msg += "[" + history[i].timestamp + "] \n🌱 Moisture:" + String(history[i].moisture) +"\n" +
           " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
           " 💧 Humidity:" + String(history[i].humidity) +"\n" +
           " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
    if (i > historyCount - count && history[i].pumpStatus != history[i - 1].pumpStatus) {
      pumpChangeCount++;
    }
  }
  msg += "Pump state changed " + String(pumpChangeCount) + " times in these events.";
  bot.sendMessage(chat_id, msg, "");
  bot.sendMessage(chat_id_yash, msg, "");
}

void sendTodayReport(String chat_id) {
  String currentTime = fetchLocalTimeFromThingSpeak();
  String today = currentTime.substring(0, 10);
  String msg = "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
   "Today's Events (" + today + "):\n";
  for (int i = 0; i < historyCount; i++) {
    if (history[i].timestamp.startsWith(today)) {
      msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
             " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
             " 💧 Humidity: " + String(history[i].humidity) +"\n" +
             " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
    }
  }
  msg += "\n🔗 Graph : https://thingspeak.com/channels/" + String(channelID) + "/charts";
  msg += "\n📊 Report Sheet: https://docs.google.com/spreadsheets/d/e/2PACX-1vQEFRp4uLweyKxa7MrYbVdbuLC_Dm-9AnYlwRqPDxnfg5vW8cKm9o-BjSC6Y0rO5-v4PzF6vC4SBibh/pubhtml";
  bot.sendMessage(chat_id, msg, "");
  bot.sendMessage(chat_id_yash, msg, "");
}

void sendMonthlyReport(String chat_id) {
  int count = (historyCount < 200) ? historyCount : 30;
  String msg = "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
  "Last " + String(count) + " events (Monthly Stats):\n";
  for (int i = historyCount - count; i < historyCount; i++) {
    msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
           " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
           " 💧 Humidity: " + String(history[i].humidity) +"\n" +
           " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
  }
  msg += "🔗 Graph : https://thingspeak.com/channels/" + String(channelID) + "/charts";
   msg += "\n📊 Report Sheet: https://docs.google.com/spreadsheets/d/e/2PACX-1vQEFRp4uLweyKxa7MrYbVdbuLC_Dm-9AnYlwRqPDxnfg5vW8cKm9o-BjSC6Y0rO5-v4PzF6vC4SBibh/pubhtml";
  bot.sendMessage(chat_id, msg, "");
   bot.sendMessage(chat_id_yash, msg, "");
}

void sendScheduleReport(String chat_id) {
  String msg =  "📊 SCHEDULED REPORT 📊\n"
                  "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n";
  for (int i = 0; i < historyCount; i++) {
    msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
           " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
           " 💧 Humidity: " + String(history[i].humidity) +"\n" +
           " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
  }
  msg += "🔗 Graph: https://thingspeak.com/channels/" + String(channelID) + "/charts";
   msg += "\n📊 Report Sheet: https://docs.google.com/spreadsheets/d/e/2PACX-1vQEFRp4uLweyKxa7MrYbVdbuLC_Dm-9AnYlwRqPDxnfg5vW8cKm9o-BjSC6Y0rO5-v4PzF6vC4SBibh/pubhtml";
  bot.sendMessage(chat_id, msg, "");
  bot.sendMessage(chat_id_yash, msg, "");
}

// =========================
// Telegram Command Handling
// =========================
void checkTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text    = bot.messages[i].text;
      Serial.printf("Received from %s: %s\n", chat_id.c_str(), text.c_str());

      if (chat_id != CHAT_ID && chat_id != chat_id_yash) {          // version feature add yash id access
      bot.sendMessage(chat_id, "⛔ Access Denied : Unauthorized Access!");
      return;
    }   else if (text == "/New_WiFi_Add") {
      bot.sendMessage(chat_id, "📶 Send new Wi-Fi SSID:");
      awaitingSSID = true;
      awaitingPASS = false;
    }

    else if (awaitingSSID) {
      newSSID = text;
      bot.sendMessage(chat_id, "🔒 Now send Wi-Fi Password:");
      awaitingSSID = false;
      awaitingPASS = true;
    }

    else if (awaitingPASS) {
  newPASS = text;
  bot.sendMessage(chat_id, "🔄 Trying to connect to: " + newSSID);
  awaitingPASS = false;

  wifiMulti.addAP(newSSID.c_str(), newPASS.c_str());

  int retry = 0;
  const int maxRetries = 10;
  Serial.print("Connecting");
  while (wifiMulti.run() != WL_CONNECTED && retry < maxRetries) {
    delay(1000);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(chat_id, "✅ Connected to " + newSSID + "\n🌐 IP: " + WiFi.localIP().toString());
  } else {
    bot.sendMessage(chat_id, "❌ Failed to connect. Please check SSID/Password or signal strength.");
  }
}



      
     else if (text == "/PUMP_ON") {
         pumpControl(true, chat_id);
      } else if (text == "/PUMP_OFF") {
         pumpControl(false, chat_id);
      } else if (text == "/PUMP_STATUS") {
         pumpStatusReport(chat_id);
      } else if (text == "/CURRENT_SENSOR_DATA") {
         sendSensorData(chat_id);
      } else if (text == "/SENSOR_STATISTICS") {
         sendSensorStatistics(chat_id);
      } else if (text == "/TODAY_REPORT") {
         sendTodayReport(chat_id);
      } else if (text == "/MONTHALY_REPORTS") {
         sendMonthlyReport(chat_id);
      }else if (text == "/developer_information") {
      showDeveloperNames(chat_id);
     }
       // ----- Show current Wi-Fi -----
      else if (text == "/wifi_status") {
        if (WiFi.status() == WL_CONNECTED) {
          bot.sendMessage(chat_id, "✅ Connected to: " + WiFi.SSID() + "\n🌐 IP: " + WiFi.localIP().toString());
        } else {
          bot.sendMessage(chat_id, "❌ Not connected to Wi-Fi.");
        }
      }

      // ----- Show tank water level -----
      else if (text == "/tank_level") {
        int s1 = digitalRead(TANK_SENSOR_1);
        int s2 = digitalRead(TANK_SENSOR_2);
        int s3 = digitalRead(TANK_SENSOR_3);
        int s4 = digitalRead(TANK_SENSOR_4);
        int s5 = digitalRead(TANK_SENSOR_5);

        String status = "💧 Water Tank Levels:\n";
        status += "Sensor 1 (Empty Detect): " + String(s1 == HIGH ? "⚠️ EMPTY" : "✅ Please fill the water tank") + "\n";
        status += "Sensor 2: " + String(s2 == HIGH ? "HIGH" : "LOW") + "\n";
        status += "Sensor 3: " + String(s3 == HIGH ? "HIGH" : "LOW") + "\n";
        status += "Sensor 4: " + String(s4 == HIGH ? "HIGH" : "LOW") + "\n";
        status += "Sensor 5: " + String(s5 == HIGH ? "HIGH" : "LOW");
        bot.sendMessage(chat_id, status, "");}
  else {
         bot.sendMessage(chat_id, "Hi I Am Bot For Your Help\n"
                      "Your Available Commands Here :\n"
                     "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃    \n"
                     "🌱🌿 🫧/PUMP_ON,🫧 🌿🌱 \n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃    \n"
                     "🌱🌿 🫧/PUMP_OFF,🫧 🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃    \n"
                     "🌱🌿 🫧/PUMP_STATUS,🫧 🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"                                            
                     "🌱🌿/CURRENT_SENSOR_DATA,🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿 /SENSOR_STATISTICS, 🌿🌱\n" 
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿 🫧/TODAY_REPORT,🫧 🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿🫧/MONTHALY_REPORTS,🫧🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿🫧 /New_WiFi_Add -Change Wi-Fi via chat🫧🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿 🫧/wifi_status,🫧 🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃     \n"
                     "🌱🌿 🫧/tank_level,🫧 🌿🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃      \n"
                     " 🫧🌱🌿🫧  Please Wait 🫧 🌱🌿🫧\n"
                     " 🌱🫧  Till Update The Bot Status 🫧🌱\n"
                     "                 𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃      \n"
                     "                                  \n"
                     "        ❣️ 😊 Thank You 😊❣️     \n"
                     "                                    \n"
                     "  🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
                     "    /developer_information, ", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}
void showDeveloperNames(String chat_id) {
  String developerNames = "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n"
                          "                       \n"
                          " 🫧This Project Is Made by:🫧 \n"
                          "                           \n"
                          "🫧🌿  Pawan Kumar Maurya  \n"
                          "🫧🌿  Himanshu Chaudhary  \n"
                          "🫧🌿  Yash Kumar          \n"
                          "                      \n  "
                          " Submitted To : Electronics & Communication Engineering\n"
                          " Department SCRIET CCS University\n "
                          "🌱🌼🌿 🫧🌿🫧🌿🌼🌱\n";
  bot.sendMessage(chat_id, developerNames, "");
}

String URLEncode(const String &str) {
  String encodedString = "";
  char c;
  char code0, code1;
  
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}




