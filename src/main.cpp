#include <WiFi.h>
#include <ESP32Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ctime>

// Function Prototypes
void handleSetSchedule(String request);
void sendHTML(WiFiClient &client);
String getTimeString(int hour, int minute);
void updateScheduleHistory(int hour, int minute, String date);

// WiFi Configuration
const char *ssid = "IQDQP";
const char *password = "farelfeiza";

#define GMT_OFFSET_SEC 25200   // UTC+7 (7 * 3600)
#define DAYLIGHT_OFFSET_SEC 0  // No daylight savings

WiFiServer server(80);
Servo servo;
const int servoPin = 18;
const int openAngle = 85;
const int closeAngle = 0;

unsigned long feedingDuration = 800;
unsigned long feedingInterval = 1800;
unsigned long lastFeedingTime = 0;
bool automaticFeedingEnabled = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);

int scheduledHour = -1;
int scheduledMinute = -1;
String scheduledDate = "";

const int maxHistoryCount = 5;
String scheduleHistory[maxHistoryCount];

void connectWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, password);

    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed to connect. Retrying...");
    }
}

void feed() {
    Serial.println("Feeding now...");
    servo.write(openAngle);
    delay(feedingDuration);
    servo.write(closeAngle);
}

void updateScheduleHistory(int hour, int minute, String date) {
    struct tm timeStruct;
    strptime(date.c_str(), "%Y-%m-%d", &timeStruct);
    char dayBuffer[10];
    strftime(dayBuffer, sizeof(dayBuffer), "%A", &timeStruct);

    String dateTimeString = String(dayBuffer) + ", " + date + " - " + getTimeString(hour, minute);

    for (int i = maxHistoryCount - 1; i > 0; i--) {
        scheduleHistory[i] = scheduleHistory[i - 1];
    }
    scheduleHistory[0] = dateTimeString;
}

String getTimeString(int hour, int minute) {
    char buffer[6];
    sprintf(buffer, "%02d:%02d", hour, minute);
    return String(buffer);
}

void setup() {
    Serial.begin(115200);
    connectWiFi();
    server.begin();
    Serial.print("Running at: ");
    Serial.println(WiFi.localIP());
    servo.attach(servoPin);
    servo.write(closeAngle);
    
    // Initialize NTP Client
    timeClient.begin();
    timeClient.setTimeOffset(GMT_OFFSET_SEC); // Set timezone offset
}

void loop() {
    timeClient.update();
    WiFiClient client = server.available();

    // Print current date and time
    time_t currentEpoch = timeClient.getEpochTime();
    struct tm *timeinfo = localtime(&currentEpoch);
    
    if (timeinfo) {
        Serial.printf("Current Time: %02d:%02d:%02d | Date: %04d-%02d-%02d\n",
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    } else {
        Serial.println("Failed to get time");
    }

    if (client) {
        Serial.println("New Client.");
        String request = client.readStringUntil('\r');
        client.flush();

        if (request.indexOf("GET /open") != -1) feed();
        else if (request.indexOf("GET /enable") != -1) automaticFeedingEnabled = true;
        else if (request.indexOf("GET /disable") != -1) automaticFeedingEnabled = false;
        else if (request.indexOf("GET /set-schedule?") != -1) handleSetSchedule(request);

        sendHTML(client);
        client.stop();
        Serial.println("Client Disconnected.");
    }

    // Check for automatic feeding
    if (automaticFeedingEnabled && millis() - lastFeedingTime >= feedingInterval) {
        feed();
        lastFeedingTime = millis();
    }

    // Check for scheduled feeding
    if (scheduledHour != -1 && scheduledMinute != -1) {
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        char todayDate[11];
        strftime(todayDate, sizeof(todayDate), "%Y-%m-%d", timeinfo);
        String currentDate = String(todayDate);

        if (currentDate == scheduledDate &&
            timeinfo->tm_hour == scheduledHour &&
            timeinfo->tm_min == scheduledMinute) {
            feed();
            scheduledHour = -1;
            scheduledMinute = -1; // Reset after feeding
            scheduledDate = ""; // Reset scheduled date
        }
    }
}

void handleSetSchedule(String request) {
    int dateIndex = request.indexOf("date=");
    int hourIndex = request.indexOf("hour=");
    int minuteIndex = request.indexOf("&minute=");

    if (dateIndex != -1 && hourIndex != -1 && minuteIndex != -1) {
        String date = request.substring(dateIndex + 5, hourIndex - 1);
        int hour = request.substring(hourIndex + 5, minuteIndex).toInt();
        int minute = request.substring(minuteIndex + 8).toInt();

        if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
            scheduledDate = date;
            scheduledHour = hour;
            scheduledMinute = minute;
            updateScheduleHistory(hour, minute, date);
            Serial.println("Scheduled: " + date + " " + getTimeString(hour, minute));
        }
    }
}

void sendHTML(WiFiClient &client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.println("<html><head><title>Smart Feeder</title>");
    client.println("<style>body { text-align: center; font-family: Arial; }");
    client.println(".button { display: block; margin: 10px auto; padding: 10px; width: 200px; text-align: center; border-radius: 5px; }");
    client.println(".green { background-color: #4CAF50; color: white; }");
    client.println(".red { background-color: #f44336; color: white; }");
    client.println("</style></head><body>");
    client.println("<h1>Smart Pet Feeder</h1>");
    client.println("<a href='/open' class='button green'>Feed Now</a>");
    client.println(automaticFeedingEnabled ? "<a href='/disable' class='button red'>Disable Auto-Feeding</a>" : "<a href='/enable' class=' button green'>Enable Auto-Feeding</a>");
    client.println("<form action='/set-schedule' method='GET'>");
    client.println("<label>Date: <input type='date' name='date' required></label><br>");
    client.println("<label>Hour: <input type='number' name='hour' min='0' max='23' required></label>");
    client.println("<label>Minute: <input type='number' name='minute' min='0' max='59' required></label>");
    client.println("<input type='submit' value='Set Schedule' class='button green'></form>");
    client.println("<h2>Schedule History</h2>");
    for (int i = 0; i < maxHistoryCount; i++) {
        if (scheduleHistory[i].length() > 0)
            client.println("<p>" + scheduleHistory[i] + "</p>");
    }
    client.println("</body></html>");
}