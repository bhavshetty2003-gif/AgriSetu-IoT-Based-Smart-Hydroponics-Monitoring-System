/********************* LIBRARIES *********************/
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>

/********************* WIFI *********************/
#define WIFI_SSID     "BHAVESH"
#define WIFI_PASSWORD "12345678"

/********************* FIREBASE *********************/
#define API_KEY "AIzaSyBmN-CWBa4jhucWUQmunih-lZC-BvLA_WA"
#define DATABASE_SECRET "iXsqeXFZtSgrKreupRtUVmGxflMOaC5Q91qg9fVT"
#define DATABASE_URL "https://agri-setu-default-rtdb.asia-southeast1.firebasedatabase.app/"

/********************* PINS *********************/
#define PH_PIN         34
#define TDS_PIN        35
#define DHT_PIN        14

#define MOTOR_RELAY    26    // 5V Pump

/********************* 12V RGB LED MOSFET PINS *********************/
#define RED_MOSFET_PIN   32    // GPIO32 for Red channel
#define GREEN_MOSFET_PIN 33    // GPIO33 for Green channel
#define BLUE_MOSFET_PIN  25    // GPIO25 for Blue channel

/********************* MOSFET CONSTANTS *********************/
#define MOSFET_ON  255
#define MOSFET_OFF 0

/********************* I2C LCD SETTINGS *********************/
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLUMNS 20
#define LCD_ROWS 4
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

/********************* OBJECTS *********************/
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

BH1750 lightMeter;
bool bh1750_ok = false;

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

/********************* CONSTANTS *********************/
#define ADC_MAX     4095.0
#define ADC_REF     3.3
#define TDS_FACTOR  500.0

/********************* CALIBRATED PH PARAMETERS *********************/
// Calibration points from your data:
// pH 4.06 at 1.3669V
// pH 7.00 at 1.7273V
// pH 9.18 at 1.9943V
#define PH_CALIBRATED_SLOPE   -5.542  // Calculated from two-point calibration
#define PH_CALIBRATED_OFFSET  19.67   // Calculated from two-point calibration

/********************* TDS RANGE FOR PUMP CONTROL *********************/
#define TDS_MIN    560
#define TDS_MAX    840

/********************* LUX RANGES FOR LED CONTROL *********************/
#define LUX_RED_MAX    3000
#define LUX_GREEN_MIN  3000
#define LUX_GREEN_MAX  7000
#define LUX_BLUE_MIN   7000

/********************* RELAY LOGIC - CHANGE THIS BASED ON YOUR RELAY *********************/
// TRY BOTH SETTINGS:
// OPTION 1: If relay LED is ON when GPIO is LOW (common for most relays)
// OPTION 2: If relay LED is ON when GPIO is HIGH (less common)
#define ACTIVE_LOW_RELAY false  // CHANGE TO false IF RELAY BEHAVES OPPOSITE

#if ACTIVE_LOW_RELAY
  #define RELAY_ON  LOW
  #define RELAY_OFF HIGH
#else
  #define RELAY_ON  HIGH
  #define RELAY_OFF LOW
#endif

/********************* TIMING *********************/
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 3000;
unsigned long lastPumpCheck = 0;
const unsigned long PUMP_CHECK_INTERVAL = 1000;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 1000;

/********************* PUMP CONTROL *********************/
bool pumpStatus = false;
bool lastPumpState = false;
String pumpStateStr = "OFF";
String tdsRangeStr = "OK";

/********************* LED STATE *********************/
String ledColor = "OFF";

/********************* VARIABLES *********************/
float smoothTDS = 0;
float currentPH = 0;
float currentTDS = 0;
float currentTemp = 0;
float currentHum = 0;
float currentLux = -1;

/********************* PH CALIBRATION VARIABLES *********************/
// Calibration constants based on your measurements
const float cal_ph4_voltage = 1.3669;   // Voltage at pH 4.06
const float cal_ph7_voltage = 1.7273;   // Voltage at pH 7.00
const float cal_ph10_voltage = 1.9943;  // Voltage at pH 9.18

/********************* LCD VARIABLES *********************/
bool lcdInitialized = false;

/********************* FUNCTION PROTOTYPES *********************/
void updateRGBLEDBasedOnLux(float lux);
void testRGBLED();
void testBlueLED();
float readPH();
float calculatePH(float voltage);
float readTDS();
void controlPumpBasedOnTDS(float tds);
void connectWiFiWithRetry();
void quickRelayTest();
bool sendToFirebaseSimple(float ph, float tds, float temp, float hum, float lux);
void setRGBColor(uint8_t red, uint8_t green, uint8_t blue);
void checkBH1750();
void readAllSensors();
void setupPWMpins();
bool setupLCD();
void updateLCDDisplay();
void testLCD();
void findI2CAddress();
void initSensors();
void checkLCDConnection();
void fixRelayLogic();

/********************* SETUP *********************/
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n🌱 Agri-Setu System Starting...");
  Serial.println("=========================================");
  
  // Find I2C devices first
  findI2CAddress();
  
  // Initialize LCD
  if (!setupLCD()) {
    Serial.println("❌ LCD initialization failed!");
    Serial.println("   Will continue without LCD...");
  } else {
    lcdInitialized = true;
  }
  
  // Initialize relay pin for pump
  pinMode(MOTOR_RELAY, OUTPUT);
  digitalWrite(MOTOR_RELAY, RELAY_OFF);
  pumpStatus = false;
  lastPumpState = false;
  pumpStateStr = "OFF";
  tdsRangeStr = "OK";
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Agri-Setu System");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");
    delay(2000);
  }
  
  // Setup PWM pins for RGB LED
  setupPWMpins();
  
  // Initialize all sensors
  initSensors();
  
  // Quick relay test
  Serial.println("\n🔧 Quick Relay Test...");
  quickRelayTest();
  
  // Test RGB LED
  Serial.println("\n🔦 Testing 12V RGB LED...");
  testRGBLED();
  
  // Connect to WiFi
  connectWiFiWithRetry();
  
  // Time Sync
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("🕐 Syncing time...");
    configTime(19800, 0, "pool.ntp.org");
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      Serial.println(&timeinfo, "✅ Time Synced: %H:%M:%S");
    } else {
      Serial.println("❌ Time sync failed");
    }
  }
  
  // Firebase Configuration
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n🔥 Configuring Firebase...");
    
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.token.uid = "esp32";
    config.signer.tokens.legacy_token = DATABASE_SECRET;
    config.timeout.serverResponse = 10 * 1000;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    delay(1000);
    
    if (Firebase.RTDB.setInt(&fbdo, "/test_connection", 123)) {
      Serial.println("✅ Firebase connected");
    } else {
      Serial.printf("❌ Firebase failed: %s\n", fbdo.errorReason().c_str());
    }
  }
  
  Serial.println("\n=========================================");
  Serial.println("✅ SYSTEM READY");
  Serial.println("=========================================\n");
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready");
    delay(1000);
    updateLCDDisplay();
  }
}

/********************* INITIALIZE SENSORS *********************/
void initSensors() {
  Serial.println("\n🔧 Initializing sensors...");
  
  dht.begin();
  Serial.println("✅ DHT11 initialized");
  
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  Serial.println("✅ ADC configured");
  
  Serial.println("🔍 Initializing BH1750...");
  Wire.begin(21, 22);
  delay(200);
  
  Wire.beginTransmission(0x23);
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("✅ BH1750 detected at 0x23");
    bh1750_ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    
    if (bh1750_ok) {
      Serial.println("✅ BH1750 Initialized");
      delay(100);
    } else {
      Serial.println("❌ BH1750 begin() failed");
    }
  } else {
    Serial.println("❌ BH1750 NOT detected");
  }
}

/********************* SETUP PWM PINS *********************/
void setupPWMpins() {
  Serial.println("🔧 Setting up PWM for RGB LED...");
  
  pinMode(RED_MOSFET_PIN, OUTPUT);
  pinMode(GREEN_MOSFET_PIN, OUTPUT);
  pinMode(BLUE_MOSFET_PIN, OUTPUT);
  
  analogWrite(RED_MOSFET_PIN, MOSFET_OFF);
  analogWrite(GREEN_MOSFET_PIN, MOSFET_OFF);
  analogWrite(BLUE_MOSFET_PIN, MOSFET_OFF);
  
  Serial.println("✅ PWM setup complete");
}

/********************* SETUP LCD *********************/
bool setupLCD() {
  Serial.println("\n🔧 Initializing I2C LCD...");
  
  lcd.display();
  delay(100);
  
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.printf("✅ LCD found at 0x%02X\n", LCD_I2C_ADDRESS);
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LCD Test OK");
    delay(1000);
    lcd.clear();
    return true;
  } else {
    Serial.printf("❌ LCD not found at 0x%02X\n", LCD_I2C_ADDRESS);
    return false;
  }
}

/********************* UPDATE LCD DISPLAY - FIXED FOR TDS *********************/
void updateLCDDisplay() {
  if (!lcdInitialized) return;
  
  lcd.clear();
  
  // Row 0: pH and Temperature
  lcd.setCursor(0, 0);
  lcd.print("pH:");
  if (currentPH >= 0 && currentPH <= 14) {
    lcd.print(currentPH, 1);
  } else {
    lcd.print("---");
  }
  
  lcd.setCursor(10, 0);
  lcd.print("Temp:");
  if (!isnan(currentTemp) && currentTemp >= -50 && currentTemp <= 100) {
    lcd.print(currentTemp, 0);
    lcd.print("C");
  } else {
    lcd.print("---");
  }
  
  // Row 1: Humidity and TDS - FIXED TDS DISPLAY
  lcd.setCursor(0, 1);
  lcd.print("Hum:");
  if (!isnan(currentHum) && currentHum >= 0 && currentHum <= 100) {
    lcd.print(currentHum, 0);
    lcd.print("%");
  } else {
    lcd.print("---");
  }
  
  lcd.setCursor(10, 1);
  lcd.print("TDS:");
  if (currentTDS >= 0 && currentTDS <= 2000) {
    // Display TDS with proper formatting
    if (currentTDS < 10) {
      lcd.print(currentTDS, 1);  // 1 decimal for very small values
    } else if (currentTDS < 100) {
      lcd.print(currentTDS, 0);  // No decimal
    } else if (currentTDS < 1000) {
      lcd.print(currentTDS, 0);  // No decimal, 3 digits
    } else {
      lcd.print(currentTDS/1000.0, 1);  // Show as k with 1 decimal
      lcd.print("k");
    }
  } else {
    lcd.print("---");
  }
  
  // Row 2: Lux and LED Color
  lcd.setCursor(0, 2);
  lcd.print("Lux:");
  if (currentLux >= 0 && currentLux <= 50000) {
    if (currentLux < 1000) {
      lcd.print(currentLux, 0);
    } else if (currentLux < 10000) {
      lcd.print(currentLux/1000.0, 1);
      lcd.print("k");
    } else {
      lcd.print(currentLux/1000.0, 0);
      lcd.print("k");
    }
  } else {
    lcd.print("---");
  }
  
  lcd.setCursor(12, 2);
  lcd.print("LED:");
  lcd.print(ledColor.substring(0, 3));
  
  // Row 3: Pump State and TDS Range
  lcd.setCursor(0, 3);
  lcd.print("Pump:");
  lcd.print(pumpStateStr);
  
  lcd.setCursor(10, 3);
  lcd.print("TDS:");
  lcd.print(tdsRangeStr.substring(0, 5));
}

/********************* FIND I2C ADDRESS *********************/
void findI2CAddress() {
  Serial.println("\n🔍 Scanning I2C bus...");
  
  Wire.begin(21, 22);
  delay(100);
  
  byte found = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.printf("Found at 0x%02X\n", address);
      found++;
    }
  }
  
  if (found == 0) {
    Serial.println("❌ No I2C devices!");
  }
  delay(1000);
}

/********************* QUICK RELAY TEST - IMPROVED *********************/
void quickRelayTest() {
  Serial.println("\n🔧 RELAY TEST:");
  Serial.printf("Relay logic: %s\n", ACTIVE_LOW_RELAY ? "ACTIVE LOW" : "ACTIVE HIGH");
  Serial.printf("RELAY_ON = %s, RELAY_OFF = %s\n", 
               RELAY_ON == HIGH ? "HIGH" : "LOW",
               RELAY_OFF == HIGH ? "HIGH" : "LOW");
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Relay Test");
  }
  
  Serial.println("\n1. Turning relay ON for 2 seconds...");
  digitalWrite(MOTOR_RELAY, RELAY_ON);
  Serial.println("   Relay LED should be ON");
  
  if (lcdInitialized) {
    lcd.setCursor(0, 1);
    lcd.print("Relay: ON");
    lcd.setCursor(0, 2);
    lcd.print("LED should be ON");
  }
  
  delay(2000);
  
  Serial.println("\n2. Turning relay OFF for 2 seconds...");
  digitalWrite(MOTOR_RELAY, RELAY_OFF);
  Serial.println("   Relay LED should be OFF");
  
  if (lcdInitialized) {
    lcd.setCursor(0, 1);
    lcd.print("Relay: OFF");
    lcd.setCursor(0, 2);
    lcd.print("LED should be OFF");
  }
  
  delay(2000);
  
  Serial.println("\n✅ Relay test complete");
  Serial.println("If relay behavior is opposite:");
  Serial.println("Change ACTIVE_LOW_RELAY to false in code\n");
  
  if (lcdInitialized) {
    updateLCDDisplay();
  }
}

/********************* FIX RELAY LOGIC *********************/
void fixRelayLogic() {
  Serial.println("\n🔄 Testing relay logic...");
  
  // Test both possibilities
  Serial.println("Testing ACTIVE LOW (LOW = ON)...");
  digitalWrite(MOTOR_RELAY, LOW);
  Serial.println("GPIO26 set to LOW");
  Serial.println("Is relay LED ON? (y/n)");
  
  delay(3000);
  
  Serial.println("Testing ACTIVE HIGH (HIGH = ON)...");
  digitalWrite(MOTOR_RELAY, HIGH);
  Serial.println("GPIO26 set to HIGH");
  Serial.println("Is relay LED ON? (y/n)");
  
  digitalWrite(MOTOR_RELAY, RELAY_OFF); // Back to off
}

/********************* RGB LED CONTROL *********************/
void setRGBColor(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(RED_MOSFET_PIN, red);
  analogWrite(GREEN_MOSFET_PIN, green);
  analogWrite(BLUE_MOSFET_PIN, blue);
}

void testRGBLED() {
  Serial.println("Testing RGB LED...");
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RGB LED Test");
  }
  
  setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_OFF);
  delay(500);
  
  Serial.println("RED...");
  if (lcdInitialized) lcd.setCursor(0, 1), lcd.print("RED          ");
  setRGBColor(MOSFET_ON, MOSFET_OFF, MOSFET_OFF);
  ledColor = "RED";
  delay(2000);
  
  Serial.println("GREEN...");
  if (lcdInitialized) lcd.setCursor(0, 1), lcd.print("GREEN        ");
  setRGBColor(MOSFET_OFF, MOSFET_ON, MOSFET_OFF);
  ledColor = "GREEN";
  delay(2000);
  
  Serial.println("BLUE...");
  if (lcdInitialized) lcd.setCursor(0, 1), lcd.print("BLUE         ");
  setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_ON);
  ledColor = "BLUE";
  delay(2000);
  
  setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_OFF);
  ledColor = "OFF";
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.print("RGB Test OK");
    delay(1000);
    updateLCDDisplay();
  }
  
  Serial.println("✅ RGB LED test complete\n");
}

/********************* UPDATE LED BASED ON LUX *********************/
void updateRGBLEDBasedOnLux(float lux) {
  if (lux < 0) {
    setRGBColor(MOSFET_ON, MOSFET_OFF, MOSFET_OFF);
    ledColor = "ERR";
    return;
  }
  
  if (lux < LUX_RED_MAX) {
    setRGBColor(MOSFET_ON, MOSFET_OFF, MOSFET_OFF);
    ledColor = "RED";
  } 
  else if (lux >= LUX_GREEN_MIN && lux < LUX_GREEN_MAX) {
    setRGBColor(MOSFET_OFF, MOSFET_ON, MOSFET_OFF);
    ledColor = "GREEN";
  } 
  else if (lux >= LUX_BLUE_MIN) {
    setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_ON);
    ledColor = "BLUE";
  }
}

/********************* READ ALL SENSORS FUNCTION *********************/
void readAllSensors() {
  // Read pH
  currentPH = readPH();
  
  // Read TDS
  currentTDS = readTDS();
  
  // Read temperature and humidity
  currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  
  // Read light sensor - FIXED BH1750 READING
  if (bh1750_ok) {
    currentLux = lightMeter.readLightLevel();
    if (currentLux < 0) {
      currentLux = -1;
      Serial.println("BH1750 read error!");
    }
  } else {
    currentLux = -1;
  }
  
  // Handle errors
  if (isnan(currentTemp)) currentTemp = NAN;
  if (isnan(currentHum)) currentHum = NAN;
}

/********************* IMPROVED WIFI CONNECTION *********************/
void connectWiFiWithRetry() {
  Serial.print("📡 Connecting to WiFi...");
  
  if (lcdInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
  }
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
    
    if (lcdInitialized) {
      static int dots = 0;
      lcd.setCursor(0, 1);
      for (int i = 0; i < dots; i++) lcd.print(".");
      dots = (dots + 1) % 4;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected");
    if (lcdInitialized) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Connected");
      delay(1000);
    }
  } else {
    Serial.println("\n❌ WiFi Failed");
    if (lcdInitialized) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Failed");
      delay(1000);
    }
  }
}

/********************* CALCULATE PH USING TWO-POINT CALIBRATION *********************/
float calculatePH(float voltage) {
  // Using two-point calibration with your measured values
  // Points: (1.7273V, 7.00) and (1.9943V, 9.18)
  
  // Calculate slope (m) using formula: m = (pH2 - pH1) / (V2 - V1)
  // Since voltage increases as pH increases (opposite of normal pH sensor behavior)
  
  float m = (9.18 - 7.00) / (1.9943 - 1.7273);
  
  // Calculate offset (b) using: pH = m * V + b  => b = pH - m * V
  // Using point 1 (7.00 at 1.7273V)
  float b = 7.00 - m * 1.7273;
  
  // Calculate pH
  float ph = m * voltage + b;
  
  // Constrain to valid pH range
  if (ph < 0) ph = 0;
  if (ph > 14) ph = 14;
  
  return ph;
}

/********************* READ PH FUNCTION WITH CALIBRATION *********************/
float readPH() {
  // Take multiple samples for stability
  int samples = 10;
  float sum_voltage = 0;
  
  for (int i = 0; i < samples; i++) {
    int raw = analogRead(PH_PIN);
    float voltage = (raw / ADC_MAX) * ADC_REF;
    sum_voltage += voltage;
    delay(10);
  }
  
  float avg_voltage = sum_voltage / samples;
  
  // Calculate pH using calibrated function
  float ph = calculatePH(avg_voltage);
  
  return ph;
}

/********************* READ TDS *********************/
float readTDS() {
  int raw = analogRead(TDS_PIN);
  float voltage = (raw / ADC_MAX) * ADC_REF;
  float tds = voltage * TDS_FACTOR;
  
  // Smoothing
  if (smoothTDS == 0) {
    smoothTDS = tds;
  } else {
    smoothTDS = 0.85 * smoothTDS + 0.15 * tds;
  }
  
  return smoothTDS;
}

/********************* PUMP CONTROL LOGIC - FIXED *********************/
void controlPumpBasedOnTDS(float tds) {
  bool tdsInRange = (tds >= TDS_MIN && tds <= TDS_MAX);
  bool newPumpState = !tdsInRange;  // Pump ON if TDS out of range
  
  if (newPumpState != lastPumpState) {
    lastPumpState = newPumpState;
    
    if (newPumpState) {
      // Pump should turn ON
      digitalWrite(MOTOR_RELAY, RELAY_ON);
      pumpStatus = true;
      pumpStateStr = "ON";
      
      if (tds < TDS_MIN) {
        Serial.printf("🔴 PUMP ON: TDS %.0f < %d (LOW)\n", tds, TDS_MIN);
        tdsRangeStr = "LOW";
      } else if (tds > TDS_MAX) {
        Serial.printf("🔴 PUMP ON: TDS %.0f > %d (HIGH)\n", tds, TDS_MAX);
        tdsRangeStr = "HIGH";
      }
    } else {
      // Pump should turn OFF
      digitalWrite(MOTOR_RELAY, RELAY_OFF);
      pumpStatus = false;
      pumpStateStr = "OFF";
      tdsRangeStr = "OK";
      Serial.printf("🟢 PUMP OFF: TDS %.0f in range\n", tds);
    }
  }
}

/********************* LOOP *********************/
void loop() {
  // Check for manual commands
  if (Serial.available()) {
    char command = Serial.read();
    
    if (command == 'h' || command == 'H') {
      Serial.println("\n💡 HELP MENU:");
      Serial.println("T = Test pump relay");
      Serial.println("S = System status");
      Serial.println("O = Force pump ON");
      Serial.println("F = Force pump OFF");
      Serial.println("L = Test RGB LED");
      Serial.println("B = Test Blue LED only");
      Serial.println("W = WiFi status");
      Serial.println("R = Reconnect WiFi");
      Serial.println("1 = Set LED RED");
      Serial.println("2 = Set LED GREEN");
      Serial.println("3 = Set LED BLUE");
      Serial.println("0 = Turn LED OFF");
      Serial.println("C = Check BH1750 sensor");
      Serial.println("P = Read sensors NOW");
      Serial.println("U = Update LCD NOW");
      Serial.println("M = Test LCD");
      Serial.println("A = Find I2C Address");
      Serial.println("I = Relay logic info");
      Serial.println("X = Fix relay logic test");
      Serial.println("Y = Toggle relay logic");
      Serial.println("V = Show PH voltage");
    }
    else if (command == 't' || command == 'T') {
      quickRelayTest();
    }
    else if (command == 's' || command == 'S') {
      Serial.println("\n📊 SYSTEM STATUS:");
      Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      Serial.printf("Pump Status: %s\n", pumpStatus ? "ON" : "OFF");
      Serial.printf("Relay Pin State: %d\n", digitalRead(MOTOR_RELAY));
      Serial.printf("LCD: %s\n", lcdInitialized ? "OK" : "FAIL");
      Serial.printf("BH1750: %s\n", bh1750_ok ? "OK" : "FAIL");
      Serial.printf("Current TDS: %.0f ppm\n", currentTDS);
      Serial.printf("Current pH: %.2f\n", currentPH);
    }
    else if (command == 'o' || command == 'O') {
      Serial.println("🔴 MANUAL: Pump ON");
      digitalWrite(MOTOR_RELAY, RELAY_ON);
      pumpStatus = true;
      pumpStateStr = "ON";
      tdsRangeStr = "MANUAL";
      if (lcdInitialized) updateLCDDisplay();
    }
    else if (command == 'f' || command == 'F') {
      Serial.println("🟢 MANUAL: Pump OFF");
      digitalWrite(MOTOR_RELAY, RELAY_OFF);
      pumpStatus = false;
      pumpStateStr = "OFF";
      tdsRangeStr = "MANUAL";
      if (lcdInitialized) updateLCDDisplay();
    }
    else if (command == 'v' || command == 'V') {
      Serial.println("\n📊 PH VOLTAGE READINGS:");
      int raw = analogRead(PH_PIN);
      float voltage = (raw / ADC_MAX) * ADC_REF;
      Serial.printf("Raw: %d, Voltage: %.4fV\n", raw, voltage);
      Serial.printf("Calculated pH: %.2f\n", calculatePH(voltage));
      
      // Test calibration points
      Serial.println("\n📊 CALIBRATION POINTS:");
      Serial.printf("At 1.3669V: pH = %.2f (expected 4.06)\n", calculatePH(1.3669));
      Serial.printf("At 1.7273V: pH = %.2f (expected 7.00)\n", calculatePH(1.7273));
      Serial.printf("At 1.9943V: pH = %.2f (expected 9.18)\n", calculatePH(1.9943));
    }
    else if (command == 'x' || command == 'X') {
      fixRelayLogic();
    }
    else if (command == 'y' || command == 'Y') {
      Serial.println("⚠️  To change relay logic:");
      Serial.println("Edit line 59: #define ACTIVE_LOW_RELAY true/false");
      Serial.println("Then re-upload the code");
    }
    else if (command == 'p' || command == 'P') {
      Serial.println("\n📊 MANUAL SENSOR READ:");
      readAllSensors();
      Serial.printf("pH: %.2f\n", currentPH);
      Serial.printf("TDS: %.0f ppm\n", currentTDS);
      Serial.printf("Temp: %.1f°C\n", currentTemp);
      Serial.printf("Hum: %.1f%%\n", currentHum);
      Serial.printf("Lux: %.0f\n", currentLux);
      if (lcdInitialized) updateLCDDisplay();
    }
    else if (command == 'u' || command == 'U') {
      Serial.println("🔄 MANUAL LCD UPDATE");
      if (lcdInitialized) updateLCDDisplay();
    }
    else if (command == 'c' || command == 'C') {
      checkBH1750();
    }
    else if (command == '\n' || command == '\r') {
      // Ignore
    }
  }

  // Read sensors every 3 seconds
  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = millis();
    
    readAllSensors();
    
    Serial.println("\n📊 SENSOR READINGS:");
    Serial.printf("pH: %.2f | TDS: %.0f ppm\n", currentPH, currentTDS);
    Serial.printf("Temp: %.1f°C | Hum: %.1f%%\n", currentTemp, currentHum);
    Serial.printf("Lux: %.0f | LED: %s\n", currentLux, ledColor.c_str());
    Serial.printf("Pump: %s | TDS Range: %s\n", pumpStateStr.c_str(), tdsRangeStr.c_str());
    Serial.println("────────────────────────────");
    
    // Update LED
    if (currentLux >= 0) {
      updateRGBLEDBasedOnLux(currentLux);
    }
    
    // Firebase
    if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
      sendToFirebaseSimple(currentPH, currentTDS, currentTemp, currentHum, currentLux);
    }
    
    lastSend = millis();
  }

  // Pump control every 1 second
  if (millis() - lastPumpCheck >= PUMP_CHECK_INTERVAL) {
    lastPumpCheck = millis();
    
    if (currentTDS >= 0) {
      controlPumpBasedOnTDS(currentTDS);
    }
  }

  // Update LCD every 1 second
  if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
    lastLCDUpdate = millis();
    if (lcdInitialized) {
      updateLCDDisplay();
    }
  }

  delay(10);
}

/********************* FIREBASE FUNCTION *********************/
bool sendToFirebaseSimple(float ph, float tds, float temp, float hum, float lux) {
  if (!Firebase.ready()) return false;
  
  FirebaseJson json;
  json.set("ph", ph);
  json.set("tds", tds);
  json.set("temp", temp);
  json.set("hum", hum);
  json.set("light", lux);
  json.set("pump", pumpStatus);
  json.set("timestamp", millis());
  
  if (Firebase.RTDB.setJSON(&fbdo, "/agrisetu/readings/latest", &json)) {
    Firebase.RTDB.pushJSON(&fbdo, "/agrisetu/readings/history", &json);
    return true;
  }
  
  return false;
}

/********************* MISSING FUNCTIONS *********************/
void testLCD() {
  if (!lcdInitialized) return;
  
  Serial.println("Testing LCD...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LCD Test");
  for (int i = 1; i <= 3; i++) {
    lcd.setCursor(0, i);
    lcd.print("Row ");
    lcd.print(i);
    lcd.print(": OK");
    delay(500);
  }
  delay(1000);
  updateLCDDisplay();
}

void testBlueLED() {
  Serial.println("\n🔵 BLUE LED TEST:");
  setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_ON);
  ledColor = "BLUE";
  delay(5000);
  setRGBColor(MOSFET_OFF, MOSFET_OFF, MOSFET_OFF);
  ledColor = "OFF";
  Serial.println("Blue test complete!");
}

void checkBH1750() {
  Serial.println("\n🔍 CHECKING BH1750:");
  
  if (!bh1750_ok) {
    Serial.println("❌ BH1750 not initialized");
    return;
  }
  
  float lux = lightMeter.readLightLevel();
  Serial.printf("Lux reading: %.0f\n", lux);
  
  if (lux < 0) {
    Serial.println("BH1750 read error!");
    // Try reinitializing
    bh1750_ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    if (bh1750_ok) {
      Serial.println("BH1750 reinitialized");
    }
  }
}
