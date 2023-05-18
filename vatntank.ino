#include <ESP8266WiFiMulti.h>
#define DEVICE "ESP8266"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <ESP_Mail_Client.h>
#include "config.h"

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

const char wifi_ssid[] = WIFI_SSID;
const char wifi_password[] = WIFI_PASSWORD;
const char influxdb_url[] = INFLUXDB_URL;
const char influxdb_token[] = INFLUXDB_TOKEN;
const char influx_org[] = INFLUXDB_ORG;
const char influx_bucket[] = INFLUXDB_BUCKET;

const char smtp_host[] = SMTP_HOST;
const int smtp_port = SMTP_PORT;
const char author_email[] = AUTHOR_EMAIL;
const char author_password[] = AUTHOR_PASSWORD;
const char recipient_email[] = RECIPIENT_EMAIL;
const char recipient_email2[] = RECIPIENT_EMAIL2;

const int criticalLed = D1;
const int relayPin = D0;
const int sensorPin=A0;
const int wifiLed=D2;

float sensorValue = 0;
const float warningResetLimit = 900;
const float warningLimitLow = 800;
const float criticalLimitLow = 200;
boolean warningSent = false;
boolean criticalWarningSent = false;

ESP8266WiFiMulti wifiMulti;
SMTPSession smtp;

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point sensor("Vanntank");

void setup() {
  Serial.begin(115200);
  //smtp.debug(1);
  pinMode(relayPin, OUTPUT);
  pinMode(criticalLed, OUTPUT);
  pinMode(wifiLed, OUTPUT);
 
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(wifiLed, LOW);
    delay(100);
  }
  digitalWrite(wifiLed, HIGH);
  Serial.println();

  // Add tags
  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}
void loop() {
  // Clear fields for reusing the point. Tags will remain untouched
  sensor.clearFields();
  float measuredValue = 0;
  for (int i = 0; i< 15; i++) {
    measuredValue = measuredValue + analogRead(sensorPin);
    delay(25);
  }
  sensorValue = measuredValue / 15;
  sensorValue = sensorValue - 247;
  sensorValue = sensorValue / 777 * 1024;
  sensor.addField("level", sensorValue);

  // Print what are we exactly writing
  Serial.print("Writing: ");
  Serial.println(sensorValue);
  Serial.println(sensor.toLineProtocol());

  // If no Wifi signal, try to reconnect it
  if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED)) {
    Serial.println("Wifi connection lost");
  }

  // Write point
  if (!client.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  resetWarning(sensorValue);
  checkIfWarningShouldBeSent(sensorValue);
  checkIfCriticalLevel(sensorValue);
  //printDebug();

  //Wait 1 mins
  Serial.println("Wait 1 min");
  delay(60000);
}

void checkIfCriticalLevel(float level) {
  if(level < criticalLimitLow && criticalWarningSent == false) {
  sendEmail(level, "Vanntanknivå er kritisk lavt, pumpe er skrudd av!");
  Serial.println("send email critical");
  criticalWarningSent = true;
  digitalWrite(criticalLed, HIGH);
  digitalWrite(relayPin, HIGH);
  }
}

void checkIfWarningShouldBeSent(float level) {
  
if(level < warningLimitLow && warningSent == false) {
  sendEmail(level, "Vanntanknivå er lavt");
  Serial.println("send email lavt");
  warningSent = true;
  }   
}
void resetWarning(float level) {
  if(level > warningResetLimit && warningSent == true) {
    sendEmail(level, "Vanntanknivå er normalt");
    Serial.println("send email normalt");
    digitalWrite(criticalLed, LOW);
    digitalWrite(relayPin, LOW);
    warningSent = false;
    criticalWarningSent = false;
  }
}

void printDebug() {
  Serial.print("warningsent: ");
  Serial.println(warningSent);
  Serial.print("criticalWarningsent: ");
  Serial.println(criticalWarningSent);
}

void sendEmail(float msg, String textMsg){

  char result[8];
  dtostrf(msg, 6, 2, result); 

  ESP_Mail_Session session;

  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  SMTP_Message message;

  message.sender.name = "Vasstank";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = result;
  message.addRecipient("Eldar", RECIPIENT_EMAIL);
  message.addRecipient("Melissa", RECIPIENT_EMAIL2);

  message.text.content = textMsg.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

  if (!smtp.connect(&session))
    return;

  if (!MailClient.sendMail(&smtp, &message))
    Serial.println("Error sending Email, " + smtp.errorReason());
}
