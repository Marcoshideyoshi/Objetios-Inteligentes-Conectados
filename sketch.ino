/*
  Trabalho: Objetos inteligentes conectados (ADS - Mackenzie)
  Marcos Hideyoshi (TIA = 21510733)
*/

#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define LCD_ADDR 0x27
#define LCD_COLUMNS 20
#define LCD_ROWS 4
//objs
Servo dispenserServo;
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLUMNS, LCD_ROWS);
HX711 scale;

//pins
int servoPin = 19;
int statusLedPin = 4;
int hx711A = 12;
int hx711B = 14;

//mqtt broker
#define ID_MQTT "esp32_mqtt"

WiFiClient espClient;
PubSubClient MQTT(espClient);
const char *BROKER_MQTT = "broker.hivemq.com";
int BROKER_PORT = 1883;

//topicos
#define DISPENSER_PARAMETERS       "3141592_dispenser_parameters"
#define PUBLISH_FOOD_DATA "3141592_publish_food_data"

//values
int initialPos = 90; //posicao inicial do Servo
int foodGramsPerSecond = 50; //quantidade de racao que cai por segundo

int intervalSeconds = 5; //intervalo entre verificacoes
int totalDispensed = 0; //total dispensado
boolean dispensing = true; 
int foodToServeGrams = 50; //quantidade inicial de racao para servir
int currentServedFoodGrams = 0; //quantidade de racao servida;

void setup() {
  Serial.begin(9600);
  lcdWrite(0, 0, "Iniciando...");
  scaleSetup();
  pinMode(statusLedPin, OUTPUT);
  servoSetup();
  lcdSetup();
  delay(2000);
  wifiSetup();
  initMQTT();
  lcdReset();
  updateDisplayValues(intervalSeconds, foodToServeGrams);
}

void loop() {
  MQTT.loop();
  readCurrentServedFood(); //le a quantidade atual de racao na balanca
  if (dispensing) {
    refil();
  }
  publishCurrentServedFood();
  delay(intervalSeconds * 1000);
}

void scaleSetup() {
  scale.begin(hx711A, hx711B);
  scale.set_scale();
  scale.set_scale(420.0983);
}

void servoSetup() {
  dispenserServo.attach(servoPin);
  dispenserServo.write(initialPos);
}

void wifiSetup(void)
{
  lcdWrite(0, 0, "Conectando ao WiFi");
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.begin("Wokwi-GUEST", "");

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  lcd.clear();
  lcdWrite(0, 0, "Conectado ao WiFi");
  delay(3000);
  lcd.clear();
}

void initMQTT(void)
{
  lcdWrite(0, 0, "Conectando ao MQTT");
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(callbackMQTT);
  lcd.clear();

  while (!MQTT.connected()) {
    Serial.print("*Tentando conectar ao Broker MQTT: ");
    Serial.println(BROKER_MQTT);
    if (MQTT.connect(ID_MQTT)) {
      lcdWrite(0, 0, "Conectado com sucesso ao broker MQTT");
      Serial.print("Conectado com sucesso ao broker MQTT");
      MQTT.subscribe(DISPENSER_PARAMETERS);
    } else {
      Serial.println("Falha ao reconectar no broker.");
      Serial.println("Nova tentativa de conexao em 2 segundos.");
      delay(2000);
    }
  }

  delay(3000);
  lcd.clear();
}

void callbackMQTT(char *topic, byte *payload, unsigned int length)
{
  String msg;

  // Obtem a string do payload recebido
  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    msg += c;
  }
  Serial.printf("MQTT: %s do topico: %s\n", msg, topic);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  intervalSeconds = doc["intervalSeconds"];
  foodToServeGrams = doc["foodToServeGrams"];
  dispensing = doc["dispensing"];
  Serial.print(intervalSeconds);
  Serial.print(foodToServeGrams);
  if (!dispensing) {
    setPausedStatus();
  } else {
    updateDisplayValues(intervalSeconds, foodToServeGrams);
  }
}

void refil() {
  if (currentServedFoodGrams < foodToServeGrams) {
    Serial.print("serving");
    int foodToRefilGrams = foodToServeGrams - currentServedFoodGrams;
    //int foodToRefilGrams = 500;
    Serial.print(foodToRefilGrams);
    open();
    totalDispensed += foodToServeGrams;
    setLcdTotalDispensed(totalDispensed);
    publishServingFood(totalDispensed);
    delay((foodToRefilGrams / foodGramsPerSecond) * 1000);
    close();
  }
}

int readCurrentServedFood() {
  int current = scale.get_units() * 1000;
  Serial.println("current:");
  Serial.println(current);
  lcdWrite(9, 3, String(current) + " g");
  currentServedFoodGrams = current;
  return current;
}

void updateDisplayValues(int intervalSeconds, int foodToServeGrams) {
  lcdReset();
  lcdWrite(9, 0, String(intervalSeconds) + " s");
  lcdWrite(12, 1, String(foodToServeGrams) + " g");
}

void lcdSetup() {
  Wire.begin(22, 23);
  lcd.begin(LCD_COLUMNS, LCD_ROWS);
  lcd.backlight();
}

void setPausedStatus() {
  lcd.clear();
  lcdWrite(3, 1, "***Pausado***");
}

void lcdReset() {
  lcd.clear();
  lcdWrite(0, 0, "Periodo:");
  lcdWrite(0, 1, "Quantidade:");
  lcdWrite(0, 2, "Dispensado:");
  lcdWrite(0, 3, "Atual:");
}

void setLcdTotalDispensed(int value) {
  String dispensedText = String(totalDispensed) + "g";
  lcdWrite(12, 2, dispensedText.c_str());
}

void open() {
  dispenserServo.write(180);
}

void publishCurrentServedFood() {
  StaticJsonDocument<200> doc;
  doc["currentServedFoodGrams"] = currentServedFoodGrams;
  String jsonOutput;
  serializeJson(doc, jsonOutput);
  MQTT.publish(PUBLISH_FOOD_DATA, jsonOutput.c_str());
}

void publishServingFood(int servingPortionGrams) {
  StaticJsonDocument<200> doc;
  doc["message"] = "Serving food";
  doc["servingPortionGrams"] = servingPortionGrams;
  String jsonOutput;
  serializeJson(doc, jsonOutput);
  MQTT.publish(PUBLISH_FOOD_DATA, jsonOutput.c_str());
}

void close() {
  dispenserServo.write(90);
}

void lcdWrite(int column, int row, String text) {
  lcd.setCursor(column, row);
  lcd.print(text);
}