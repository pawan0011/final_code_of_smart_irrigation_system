#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <ThingSpeak.h>
#include <ArduinoJson.h>

// =========================
// WiFi Credentials
// =========================
const char* ssid = "hodadmin";
const char* password = "hodadmin@123";

// =========================
// Telegram Credentials
// =========================
#define BOT_TOKEN "7122121223:AAGIGODrbdrYAspFFGf4lFquyBL_03HCmvc"
#define CHAT_ID  "7122121223"

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
const char* sheetsURL = "https://script.google.com/macros/s/AKfycby4T3lCQXBkeXp2FZspEkUdw9Sf9pSnk5S1TcdurBCyfNO1GXQ0DIXa2xXy9XkR25sE/exec";

// =========================
// Sensor & Pump Pins
// =========================
#define MOISTURE_SENSOR_PIN 32
#define TEMPERATURE_SENSOR_PIN 33
#define HUMIDITY_SENSOR_PIN 34
#define PUMP_PIN 5

// =========================
// Global Variables: Sensor Readings, Pump State & Thresholds
// =========================
int moistureLevel = 0;
int temperature = 0;
int humidity = 0;
bool pumpStatus = false;  // false = OFF; true = ON

// Moisture threshold values for automatic pump control
int dryThreshold = 300; // Pump should turn ON if moisture < dryThreshold (dry soil)
int wetThreshold = 700; // Pump should turn OFF if moisture > wetThreshold (wet soil)

// -------------------------
// Event Logging Structures
// -------------------------
#define MAX_HISTORY 100

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
void handleSerialInput();                // Process serial input for testing
void sendTelegramAlert();
String convertToIST(String utcTime);
void showDeveloperNames(String chat_id);

// =========================
// Setup
// =========================
  void setup() {
  Serial.begin(115200);
  
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  
  // Set up TLS/SSL certificate for Telegram connections
 telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
 // **Initialize Telegram Client**
    telegramClient.setInsecure();
     // **Connect to ThingSpeak**
  ThingSpeak.begin(thingSpeakClient);
  // Initialize NTP (set to IST: UTC+5:30)
  configTime(19800, 0, "pool.ntp.org");

  if (bot.sendMessage(CHAT_ID, "ESP32 Connected: Bot is online!", "")) {
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
  int newTemperature = analogRead(TEMPERATURE_SENSOR_PIN);
  int newHumidity    = analogRead(HUMIDITY_SENSOR_PIN);


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
  if (moistureLevel < dryThreshold && pumpStatus == false) {
    pumpControl(true, CHAT_ID);
  } else if (moistureLevel > wetThreshold && pumpStatus == true) {
    pumpControl(false, CHAT_ID);
  }

  // If any sensor value or pump state changed, log event and update cloud immediately
  if (newMoisture != prevMoisture || newTemperature != prevTemperature ||
      newHumidity != prevHumidity || pumpStatus != prevPumpStatus) {
    logEvent();
    updateThingSpeak();
    updateGoogleSheets();
    prevMoisture = newMoisture;
    prevTemperature = newTemperature;
    prevHumidity = newHumidity;
    prevPumpStatus = pumpStatus;

     // Handle Serial Input for Testing
  handleSerialInput();
  }
  
  // Scheduled full history report (every 12 hours)
  if (millis() - lastReportTime > reportInterval) {
    sendScheduleReport(CHAT_ID);
    lastReportTime = millis();
  }
  
  // Check for incoming Telegram commands almost continuously
  if (millis() - lastTelegramCheck > telegramCheckInterval) {
    checkTelegramMessages();
    lastTelegramCheck = millis();
  }
  
  // Also process any serial input from the monitor (for testing purposes)
  handleSerialInput();

  delay(5);  // Very short delay to yield
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
               "&pump="        + String(pumpStatus ? 1 : 0) +
               "&timestamp="   + fetchLocalTimeFromThingSpeak();
  Serial.println("Google Sheets URL: " + url);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.println("✅ Google Sheets Update Sent. Response code: " + String(httpCode));
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

// Process serial input from the monitor for testing. Two formats are accepted:
// 1. "SET_THRESHOLD <dry> <wet>"
// 2. "<moisture> <temperature> <humidity>"
void handleSerialInput() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    // Check if input is to set thresholds
    if (input.startsWith("SET_THRESHOLD")) {
      int firstSpace = input.indexOf(' ');
      if (firstSpace != -1) {
        String args = input.substring(firstSpace + 1);
        args.trim();
        int spaceIndex = args.indexOf(' ');
        if (spaceIndex != -1) {
          String dryStr = args.substring(0, spaceIndex);
          String wetStr = args.substring(spaceIndex + 1);
          int newDry = dryStr.toInt();
          int newWet = wetStr.toInt();
          if (newDry != 0 && newWet != 0) {
            dryThreshold = newDry;
            wetThreshold = newWet;
            Serial.println("Thresholds updated: Dry = " + String(dryThreshold) + ", Wet = " + String(wetThreshold));
            String msg = "New thresholds set:\nDry: " + String(dryThreshold) +
                         "\nWet: " + String(wetThreshold) +
                         "\nData: [M:" + String(moistureLevel) + ", T:" + String(temperature) + ", H:" + String(humidity) +
                         ", Pump:" + (pumpStatus ? "ON" : "OFF") + "]\nTimestamp: " + fetchLocalTimeFromThingSpeak();
            bot.sendMessage(CHAT_ID, msg, "");
            logEvent();
            updateThingSpeak();
            updateGoogleSheets();
          } else {
            Serial.println("Invalid threshold values received.");
          }
        }
      }
    } 
    // Otherwise, treat it as sensor data update in the format: "<moisture> <temperature> <humidity>"
    else {
      int firstSpace = input.indexOf(' ');
      int secondSpace = input.indexOf(' ', firstSpace + 1);
      if (firstSpace != -1 && secondSpace != -1) {
        String moistureStr = input.substring(0, firstSpace);
        String tempStr = input.substring(firstSpace + 1, secondSpace);
        String humStr = input.substring(secondSpace + 1);
        int mVal = moistureStr.toInt();
        int tVal = tempStr.toInt();
        int hVal = humStr.toInt();
        moistureLevel = mVal;
        temperature = tVal;
        humidity = hVal;
        Serial.println("Serial sensor values updated: M=" + String(moistureLevel) + " T=" + String(temperature) + " H=" + String(humidity));
        logEvent();
        updateThingSpeak();
        updateGoogleSheets();
        String msg = "Serial Input Update:\nMoisture: " + String(moistureLevel) +
                     "\nTemperature: " + String(temperature) +
                     "\nHumidity: " + String(humidity) +
                     "\nPump: " + (pumpStatus ? "ON" : "OFF") +
                     "\nTimestamp: " + fetchLocalTimeFromThingSpeak();
        bot.sendMessage(CHAT_ID, msg, "");
      } else {
        Serial.println("Invalid serial input. Use 'SET_THRESHOLD <dry> <wet>' or '<moisture> <temperature> <humidity>'");
      }
    }
  }
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
  
  updateThingSpeak();
  updateGoogleSheets();
  logEvent();
  prevPumpStatus = pumpStatus;
}

void pumpStatusReport(String chat_id) {
  String status = pumpStatus ? "ON" : "OFF";
  bot.sendMessage(chat_id, "Current pump status: " + status, "");
}

void sendSensorData(String chat_id) {
  String msg = String("🌱🌿 🫧🍃🛖🍃🌿🌼🌱\n"
              "Current Sensor Data:\n") +
               "🌱 Soil Moisture:  " + String(moistureLevel) + "\n" +
               "🌡️ Temperature: " + String(temperature) + "\n" +
               "💧 Humidity: " + String(humidity) + "\n" +
               "🚰 Pump: " + (pumpStatus ? "ON" : "OFF") + "\n" +
               "Timestamp: " + fetchLocalTimeFromThingSpeak();
  bot.sendMessage(chat_id, msg, "");
}

void sendSensorStatistics(String chat_id) {
  int count = (historyCount < 10) ? historyCount : 10;
  String msg = "🌱🌿 🫧🍃🛖🍃🌿🌼🌱\n" 
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
}

void sendTodayReport(String chat_id) {
  String currentTime = fetchLocalTimeFromThingSpeak();
  String today = currentTime.substring(0, 10);
  String msg = "🌱🌿 🫧🍃🛖🍃🌿🌼🌱\n"
   "Today's Events (" + today + "):\n";
  for (int i = 0; i < historyCount; i++) {
    if (history[i].timestamp.startsWith(today)) {
      msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
             " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
             " 💧 Humidity: " + String(history[i].humidity) +"\n" +
             " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
    }
  }
  msg += "🔗 Graph : https://thingspeak.com/channels/" + String(channelID) + "/charts";
  bot.sendMessage(chat_id, msg, "");
}

void sendMonthlyReport(String chat_id) {
  int count = (historyCount < 30) ? historyCount : 30;
  String msg = "🌱🌿 🫧🍃🛖🍃🌿🌼🌱\n"
  "Last " + String(count) + " events (Monthly Stats):\n";
  for (int i = historyCount - count; i < historyCount; i++) {
    msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
           " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
           " 💧 Humidity: " + String(history[i].humidity) +"\n" +
           " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
  }
  msg += "🔗 Graph : https://thingspeak.com/channels/" + String(channelID) + "/charts";
  bot.sendMessage(chat_id, msg, "");
}

void sendScheduleReport(String chat_id) {
  String msg =  "📊 SCHEDULED REPORT 📊\n"
                  "🌱🌿 🫧🍃🛖🍃🌿🌼🌱\n";
  for (int i = 0; i < historyCount; i++) {
    msg += "[" + history[i].timestamp + "] \n🌱 Soil Moisture:" + String(history[i].moisture) +"\n" +
           " 🌡️ Temperature:" + String(history[i].temperature) +"\n" +
           " 💧 Humidity: " + String(history[i].humidity) +"\n" +
           " 🚰 Pump:" + (history[i].pumpStatus ? "ON" : "OFF") + "\n";
  }
  msg += "🔗 Graph: https://thingspeak.com/channels/" + String(channelID) + "/charts";
  bot.sendMessage(chat_id, msg, "");
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
      
      if (text == "/PUMP_ON") {
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
      } else if (text == "/MONTHALY_REPORTS") {
         sendScheduleReport(chat_id);
      }else if (text == "/developer_information") {
      showDeveloperNames(chat_id);
     }
  else {
         bot.sendMessage(chat_id, "Hi I Am Bot For Your Help\n"
                      "Your Available Commands Here :\n"
                     "🌱🌼🌿 🫧🍃🍃🍃🍃🍃🍃🫧🌿🌼🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃               \n"
                     "🌱🌿 🫧🍃/PUMP_ON,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃               \n"
                     "🌱🌿 🫧🍃/PUMP_OFF,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃              \n"
                     "🌱🌿 🫧🍃/PUMP_STATUS,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃              \n"                                            
                     "🌱🌿 🫧🍃/CURRENT_SENSOR_DATA,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃              \n"
                     "🌱🌿 🫧🍃/SENSOR_STATISTICS,🍃🫧 🌿🌱\n" 
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃             \n"
                     "🌱🌿 🫧🍃/TODAY_REPORT,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃             \n"
                     "🌱🌿 🫧🍃/MONTHALY_REPORTS,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃              \n"
                     "🌱🌿 🫧🍃/MONTHALY_REPORTS,🍃🫧 🌿🌱\n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃             \n"
                     " 🫧🍃🍃🫧  Please Wait 🫧 🍃🍃🫧\n"
                     "🫧🫧Few Second To Update The Bot Status🫧🫧 \n"
                     "          𝄃𝄃𝄂𝄂𝄀𝄁𝄃𝄂𝄂𝄃             \n"
                     "                                  \n"
                     "      ❣️ 😊 Thank You 😊❣️                       \n"
                     "                                    \n"
                     "  🌱🌼🌿 🫧🍃🍃🍃🍃🍃🍃🫧🌿🌼🌱   \n"
                     "    /developer_information, ", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}
void showDeveloperNames(String chat_id) {
  String developerNames = "🌱🌼🌿 🫧🍃🍃🍃🍃🍃🍃🫧🌿🌼🌱\n"
                          "                       \n"
                          " 🫧This Project Is Made by:🫧 \n"
                          "                           \n"
                          "🍃🍃  Pawan Kumar Maurya  \n"
                          "🍃🍃  Himanshu Chaudhary  \n"
                          "🍃🍃  Yash Kumar  \n"
                          "                      \n  "
                          " Submitted To : Electronics & Communication Engineering\n"
                          " Department SCRIET CCS University\n "
                          "🌱🌼🌿 🫧🍃🍃🍃🍃🍃🍃🫧🌿🌼🌱\n";
  bot.sendMessage(chat_id, developerNames, "");
}





