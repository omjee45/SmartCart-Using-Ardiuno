#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <HX711_ADC.h>

// === LCD Setup ===
LiquidCrystal_I2C lcd(0x27, 20, 4);

// === RFID Setup ===
#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

// === Barcode Setup ===
SoftwareSerial scannerSerial(2, 3); // TX from scanner -> D2, RX -> D3

// === Buzzer ===
#define BUZZER_PIN 8

// === HX711 Weight Sensor Setup ===
const int HX711_dout = 4;
const int HX711_sck = 5;
HX711_ADC LoadCell(HX711_dout, HX711_sck);
float lastWeight = 0.0;
float weightBeforeScan = 0.0;
unsigned long lastCheatCheck = 0;
bool cheatDetected = false;

// === Item & Product Structs ===
struct Item {
  String name;
  float price;
  int quantity;
};

struct Product {
  String id;
  String name;
  float price;
};

const int MAX_ITEMS = 20;
Item cart[MAX_ITEMS];
int cartCount = 0;

unsigned long lastScanTime = 0;
bool showingTotal = false;

String lastScannedName = "";
float lastScannedPrice = 0.0;
int lastScannedQuantity = 1;

// === Product Database ===
Product database[] = {
  {"123456789", "Milk", 32.00},
  {"ABC123", "Bread", 5.50},
  {"RFID_04AABBCCDD", "Eggs", 7.25},
  {"RFID_02FF336677", "Juice", 3.75},
  {"6973224080711", "Butter", 4.99},
  {"RFID_D3CF52C5", "Daru", 6900.00},
  {"8901396062417", "JootaPolish", 1000.00}
};
const int DB_SIZE = sizeof(database) / sizeof(Product);

// === Functions ===
Product* findProduct(String id) {
  for (int i = 0; i < DB_SIZE; i++) {
    if (database[i].id == id) {
      return &database[i];
    }
  }
  return NULL;
}

void updateCart(String name, float price, int deltaQty) {
  for (int i = 0; i < cartCount; i++) {
    if (cart[i].name == name) {
      cart[i].quantity += deltaQty;
      if (cart[i].quantity <= 0) {
        // Remove item by shifting array
        for (int j = i; j < cartCount - 1; j++) {
          cart[j] = cart[j + 1];
        }
        cartCount--;
      }
      lastScannedQuantity = (i < cartCount) ? cart[i].quantity : 0;
      return;
    }
  }
  if (deltaQty > 0 && cartCount < MAX_ITEMS) {
    cart[cartCount].name = name;
    cart[cartCount].price = price;
    cart[cartCount].quantity = deltaQty;
    lastScannedQuantity = deltaQty;
    cartCount++;
  }
}

void beepBuzzer(int duration = 200) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepMultiple(int times, int duration = 100, int gap = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    delay(gap);
  }
}

void showReadyMessage() {
  lcd.clear();
  lcd.setCursor(2, 1);
  lcd.print("Ready to Scan...");
}

void showLatestItemOnLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Item: ");
  lcd.print(lastScannedName);

  lcd.setCursor(0, 1);
  lcd.print("Rs");
  lcd.print(lastScannedPrice, 2);
  lcd.print(" x");
  lcd.print(lastScannedQuantity);
}

void showTotalBill() {
  float total = 0;
  int totalItems = 0;
  for (int i = 0; i < cartCount; i++) {
    total += cart[i].price * cart[i].quantity;
    totalItems += cart[i].quantity;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Items: ");
  lcd.print(totalItems);

  lcd.setCursor(0, 1);
  lcd.print("Total: Rs");
  lcd.print(total, 2);
}

void setup() {
  Serial.begin(57600);
  scannerSerial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  showReadyMessage();

  // === HX711 Setup ===
  LoadCell.begin();
  unsigned long stabilizingTime = 2000;
  bool _tare = true;
  LoadCell.start(stabilizingTime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("HX711 Timeout! Check wiring.");
    while (1);
  } else {
    LoadCell.setCalFactor(445.03);  // Your calibrated value
    Serial.println("HX711 Ready.");
  }

  Serial.println("Smart Cart Ready. Scan item...");
}

void loop() {
  static boolean newDataReady = false;
  bool scanned = false;

  // === RFID Scan ===
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    weightBeforeScan = lastWeight;

    String uid = "RFID_";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    Product* product = findProduct(uid);
    if (product != NULL) {
      delay(1000); // Wait for weight to stabilize
      float weightAfterScan = lastWeight;
      float diff = weightAfterScan - weightBeforeScan;

      int deltaQty = 0;
      if (diff > 20.0) deltaQty = 1;
      else if (diff < -20.0) deltaQty = -1;

      if (deltaQty != 0) {
        updateCart(product->name, product->price, deltaQty);
        beepBuzzer();
        lastScannedName = product->name;
        lastScannedPrice = product->price;
        Serial.print("RFID Scanned: ");
        Serial.println(lastScannedName);
        showLatestItemOnLCD();
        lastScanTime = millis();
        showingTotal = false;
        scanned = true;
      } else {
        Serial.println("No weight change. Ignoring scan.");
        beepMultiple(3);
      }
    } else {
      Serial.print("Unknown RFID: ");
      Serial.println(uid);
      beepMultiple(5);
    }

    rfid.PICC_HaltA();
    delay(500);
  }

  // === Barcode Scan ===
  if (scannerSerial.available()) {
    weightBeforeScan = lastWeight;

    String code = scannerSerial.readStringUntil('\n');
    code.trim();
    if (code.length() > 0) {
      Product* product = findProduct(code);
      if (product != NULL) {
        delay(1000); // Wait for weight to stabilize
        float weightAfterScan = lastWeight;
        float diff = weightAfterScan - weightBeforeScan;

        int deltaQty = 0;
        if (diff > 20.0) deltaQty = 1;
        else if (diff < -20.0) deltaQty = -1;

        if (deltaQty != 0) {
          updateCart(product->name, product->price, deltaQty);
          beepBuzzer();
          lastScannedName = product->name;
          lastScannedPrice = product->price;
          Serial.print("Barcode Scanned: ");
          Serial.println(lastScannedName);
          showLatestItemOnLCD();
          lastScanTime = millis();
          showingTotal = false;
          scanned = true;
        } else {
          Serial.println("No weight change. Ignoring scan.");
          beepMultiple(3);
        }
      } else {
        Serial.print("Unknown Barcode: ");
        Serial.println(code);
        beepMultiple(5);
      }
    }
  }

  // === Show Total After 3 Seconds ===
  if (!scanned && !showingTotal && cartCount > 0 && millis() - lastScanTime >= 3000) {
    showTotalBill();
    showingTotal = true;
  }

  // === If no items yet ===
  if (cartCount == 0 && !scanned) {
    showReadyMessage();
  }

  // === Weight Update & Cheat Detection ===
  if (LoadCell.update()) newDataReady = true;

  if (newDataReady) {
    float currentWeight = LoadCell.getData();
    Serial.print("Cart Weight: ");
    Serial.print(currentWeight, 2);
    Serial.println(" g");

    if (millis() - lastCheatCheck > 500) {
      float weightDiff = currentWeight - lastWeight;
      if ((weightDiff > 40.0) && (millis() - lastScanTime > 5000)) {
        cheatDetected = true;
        Serial.println("CHEATING DETECTED!");
        unsigned long cheatStart = millis();
        while (millis() - cheatStart < 10000) {
          digitalWrite(BUZZER_PIN, HIGH);
          delay(100);
          digitalWrite(BUZZER_PIN, LOW);
          delay(100);
        }
      }
      lastWeight = currentWeight;
      lastCheatCheck = millis();
    }

    newDataReady = false;
  }
}