// ********************************************
// ****           Include Libraries        ****
// ********************************************
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <sqlite3.h>
#include <WiFi.h>
#include "time.h"

// ********************************************
// ****               Define               ****
// ********************************************
// FREERTOS
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// SERIAL
#define SERIAL_BAUD_RATE 9600
#define SERIAL2_BAUD_RATE 115200
#define SERIAL_FINGER_SENSOR 57600

// KEYPAD
#define KEYPAD_SCL_PIN 27
#define KEYPAD_SDO_PIN 14
#define KEYPAD_NUM_OF_KEYS 16
#define KEYPAD_ZERO_NUMBER_KEY 10
#define KEYPAD_CONFIRM_NUM 11
#define KEYPAD_BACK_NUM 12
#define KEYPAD_ADD_USER_NUM 13
#define KEYPAD_REMOVE_USER_NUM 14
#define KEYPAD_INFO_USER_NUM 15
#define KEYPAD_CHANGE_PASSWORD_NUM 16
#define KEYPAD_INVALID_VALUE -1

// CHARACTER LCD
#define LCD_ROWS 4
#define LCD_COLUMNS 20
#define LCD_I2C_ADDRESS 0X27

// ACTIONS
#define ACTION_BACK -1
#define ACTION_WRONG_PASS 0
#define ACTION_ALLOW_ACCESS 1

// PASSWORD
#define PASSWORD_MAX_LENGTH 8
#define PASSWORD_MIN_LENGTH 5

// SPI
// SCLK = 18, MISO = 19, MOSI = 23
#define SPI_SD_SLAVE_PIN 5
#define SPI_RFID_SLAVE_PIN 13
#define SPI_RFID_SCLK_PIN 18

// WiFi
#define WIFI_SSID "********"                   // Your network SSID (name of wifi network)
#define WIFI_PASSWORD "********"               // Your network password
#define WIFI_NTPServer "pool.ntp.org"
#define WIFI_GmtOffset_Sec 12600               // Tehran +3:30h ::>> +12600s
#define WIFI_DayLightOffset_Sec 3600           // If Daylight Saving Time (DST) is off, WIFI_DayLightOffset_Sec = 0

// RELAY
#define RELAY_PIN 12

// BUZZER
#define BUZZER_PIN 15
#define BUZZER_ALLOW_REPEAT 3
#define BUZZER_ERROR_REPEAT 1

// LED
#define LED_GREEN_PIN 32
#define LED_RED_PIN 33

// CONFIGURE
#define CONFIGURE_PRIMITIVE_PASSWORD "11111111"
#define CONFIGURE_TIME_ZONE WIFI_DayLightOffset_Sec == 0 ? "IRST" : "IRDT"    // Iran Standard Time (IRST), Iran Daylight Time (IRDT)
#define CONFIGURE_LANGUAGE "EN"

// FINGER PRINT
#define FINGER_INVALID "0"

// DELAY
#define DELAY_BUZZER_ALLOW 60
#define DELAY_BUZZER_ERROR 400
#define DELAY_WAIT_USER 500
#define DELAY_DISPLAY_MESSAGE 2000
#define DELAY_DISPLAY_INFO 4000

// ********************************************
// ****          Global Variables          ****
// ********************************************
// CHARACTER LCD
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// KEYPAD
int8_t key = KEYPAD_INVALID_VALUE;
TaskHandle_t keypadHandle = NULL;

// RFID
MFRC522 mfrc522(SPI_RFID_SLAVE_PIN, SPI_RFID_SCLK_PIN);
TaskHandle_t checkAccessHandle = NULL;

// SQLite
sqlite3 *dbAccessControl;
bool userExists;
String userIDCard;

// CONFIGURE
String password;

// FINGER PRINT
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);

// ********************************************
// ****                Setup               ****
// ********************************************
void setup()
{
  // SERIAL
  Serial.begin(SERIAL_BAUD_RATE);
  Serial2.begin(SERIAL2_BAUD_RATE);

  // CHARACTER LCD
  lcd.begin();

  // KEYPAD
  pinMode(KEYPAD_SCL_PIN, OUTPUT);
  pinMode(KEYPAD_SDO_PIN, INPUT_PULLUP);
  digitalWrite(KEYPAD_SCL_PIN, HIGH);

  // RFID
  pinMode(SPI_RFID_SLAVE_PIN, OUTPUT);
  digitalWrite(SPI_RFID_SLAVE_PIN, HIGH);
  SPI.begin();    // Initiate  SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522

  // BUZZER
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // LED
  pinMode(LED_GREEN_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, LOW);
  pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_RED_PIN, LOW);

  // SD
  if(! storageInit())
    return;

  // SQLite
  if(! sqliteInit())
    return;

  // WiFi
  wifiInit();
  configTime(WIFI_GmtOffset_Sec, WIFI_DayLightOffset_Sec, WIFI_NTPServer);

  // CONFIGURE
  if (! configInit())
    return;

  // FINGER PRINT
  finger.begin(SERIAL_FINGER_SENSOR);          // Set the data rate for the sensor serial port
  if (! fingerInit())
    return;

  // TASKS
  xTaskCreatePinnedToCore(keypadTask, "Keypad Task", 4096, NULL, 2, &keypadHandle, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(checkAccessTask, "Check Access Task", 8192, NULL, 1, &checkAccessHandle, ARDUINO_RUNNING_CORE);
}

// ********************************************
// ****                 Loop               ****
// ********************************************
void loop()
{
}

// ********************************************
// ****       Initialize Functions         ****
// ********************************************
bool storageInit() {
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  if (!SD.begin(SPI_SD_SLAVE_PIN))
    return false;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  return true;
}

bool sqliteInit() {
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  const char* createTbUsers = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY NOT NULL, name VARCHAR(100) NULL, cardID VARCHAR(100) NOT NULL, fingerID VARCHAR(100) NULL, date VARCHAR(100) NOT NULL)";
  const char* createTbLogs = "CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY NOT NULL, cardID VARCHAR(100) NOT NULL, date VARCHAR(100) NOT NULL, action VARCHAR(100) NOT NULL)";
  const char* createTbConfig = "CREATE TABLE IF NOT EXISTS config (id INTEGER PRIMARY KEY NOT NULL, wifiName VARCHAR(100) NOT NULL, password VARCHAR(20) NOT NULL, timezone VARCHAR(10) NOT NULL, language VARCHAR(5) NOT NULL)";

  sqlite3_initialize();
  if (! databaseOpen(&dbAccessControl, "/sd/Access_Control.db"))
    return false;

  if (! databaseExec(dbAccessControl, createTbUsers, NULL))
    return false;

  if (! databaseExec(dbAccessControl, createTbLogs, NULL))
    return false;

  if (! databaseExec(dbAccessControl, createTbConfig, NULL))
     return false;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  return true;
}

void wifiInit() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
}

bool configInit() {
  const char* sql;
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  sql = "SELECT password FROM config WHERE id = 1";
  if (! databaseExec(dbAccessControl, sql, callbackSetPassword))
    return false;

  if (password == NULL) {
    password = CONFIGURE_PRIMITIVE_PASSWORD;
    sql = ("INSERT INTO config (wifiName, password, timezone, language) VALUES ('" + String(WIFI_SSID) +  "', '" +  String(CONFIGURE_PRIMITIVE_PASSWORD) + "', '" + String(CONFIGURE_TIME_ZONE) + "', '" + String(CONFIGURE_LANGUAGE) + "')").c_str ();
    if (! databaseExec(dbAccessControl, sql, NULL))
      return false;
  }

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  return true;
}

bool fingerInit() {
  if (finger.verifyPassword())
    return true;

  return false;
}

// ********************************************
// ****         Keypad Functions           ****
// ********************************************
void enableKeypadInterrupt()
{
  attachInterrupt(KEYPAD_SDO_PIN, readKey, FALLING);
}

void readKey()
{
  detachInterrupt(KEYPAD_SDO_PIN);
  int8_t keyStatus = KEYPAD_INVALID_VALUE;
  for (int8_t number = 1; number <= KEYPAD_NUM_OF_KEYS; number++)
  {
    digitalWrite(KEYPAD_SCL_PIN, LOW);
    vTaskDelay(1);

    if (!digitalRead(KEYPAD_SDO_PIN))
      keyStatus = number;

    digitalWrite(KEYPAD_SCL_PIN, HIGH);
    vTaskDelay(1);
  }
  if (keyStatus == KEYPAD_ZERO_NUMBER_KEY)
    keyStatus = 0;

  key = keyStatus;
  vTaskResume(keypadHandle);
}

void keypadTask(void *parameters)
{
  while (true)
  {
    enableKeypadInterrupt();
    vTaskSuspend(keypadHandle);
    int8_t number = key;
    if (number >= 13 && number <= 16 && checkAuth(number))
    {
      switch (number)
      {
        case KEYPAD_ADD_USER_NUM:
          vTaskSuspend(checkAccessHandle);
          addUser();
          vTaskResume(checkAccessHandle);
          break;
        case KEYPAD_REMOVE_USER_NUM:
          vTaskSuspend(checkAccessHandle);
          removeUser();
          vTaskResume(checkAccessHandle);
          break;
        case KEYPAD_INFO_USER_NUM:
          vTaskSuspend(checkAccessHandle);
          infoUser();
          vTaskResume(checkAccessHandle);
          break;
        case KEYPAD_CHANGE_PASSWORD_NUM:
          changePassword();
          break;
      }
    }
  }
}

// ********************************************
// ****           Auth Functions           ****
// ********************************************
String getPassword(String message) {
  lcd.clear();
  lcd.print(message);
  String inputPassword;

  int8_t i = 0;
  int8_t number = KEYPAD_INVALID_VALUE;
  lcd.setCursor(LCD_COLUMNS - 1, 0);
  lcd.rightToLeft();

  while (i < PASSWORD_MAX_LENGTH) {
    enableKeypadInterrupt();
    vTaskSuspend(keypadHandle);
    number = key;

    if (number == KEYPAD_BACK_NUM || number == KEYPAD_CONFIRM_NUM)
      break;

    if (number >= 0 && number <= 9) {
      inputPassword += number;
      lcd.print("*");
      i++;
    }
  }

  lcd.clear();
  lcd.leftToRight();
  if (number == KEYPAD_BACK_NUM)
    inputPassword.clear();
  else if (inputPassword.length() == 0)
    inputPassword = " ";

  return inputPassword;
}

void changePassword() {
  String newPassword = getPassword("New Pass: ");

  if (newPassword != NULL) {     // number != KEYPAD_BACK_NUM
    if (newPassword.length() < PASSWORD_MIN_LENGTH) {
      char tmp[32];

      sprintf(tmp, "to have %d-%d numbers", PASSWORD_MIN_LENGTH, PASSWORD_MAX_LENGTH);
      printMessage("The password needs ", tmp);
    }

    else if (password == newPassword) {
      printMessage("The new password ", "must be different");
    }
    else {
      String confirmPassword = getPassword("Conf Pass: ");
      if (confirmPassword != NULL) {    // number != KEYPAD_BACK_NUM
        if (newPassword != confirmPassword) {
          printMessage("The password not", "match");
        }
        else {
          const char* sql = ("UPDATE config SET password = '" + newPassword + "' WHERE id = 1;").c_str();
          if (! databaseExec(dbAccessControl, sql, NULL)) {
            return;
          }
          sql = "SELECT password FROM config WHERE id = 1";
          if (! databaseExec(dbAccessControl, sql, callbackSetPassword)) {
            return;
          }
          printMessage("Successfully", "");
        } // newPassword == confirmPassword
      } // confirmPassword != NULL
    } // newPassword.length() >= PASSWORD_MIN_LENGTH
  } // newPassword != NULL

  lcd.clear();
}

int checkPassword() {
  String inputPassword = getPassword("Password: ");

  if (inputPassword == NULL)      // number == KEYPAD_BACK_NUM
    return ACTION_BACK;
  else
    return password == inputPassword ? ACTION_ALLOW_ACCESS : ACTION_WRONG_PASS;
}

bool checkAuth(int8_t selectedNumber)
{
  switch (checkPassword())
  {
    case ACTION_BACK:
      Serial.println("Back To Menu");
      return false;
    case ACTION_WRONG_PASS:
      Serial.println("Wrong Password");
      return checkAuth(selectedNumber);
    case ACTION_ALLOW_ACCESS:
      Serial.println("Allow");
      return true;
  }
}

// ********************************************
// ****        Database Functions          ****
// ********************************************
bool databaseOpen(sqlite3 **db, const char *filename) {
  if (sqlite3_open(filename, db))
    return false;

  return true;
}

bool databaseExec(sqlite3 *db, const char *sql, int (*callback)(void *data, int argc, char **argv, char **azColName)) {
  char *zErrMsg = 0;
  int rc = sqlite3_exec(db, sql, callback, NULL, &zErrMsg);

  if (rc != SQLITE_OK) {
    sqlite3_free(zErrMsg);
    sqlite3_close(db);
    return false;
  }
  return true;
}

static int callbackUserExist(void *data, int argc, char **argv, char **azColName) {
  userIDCard = argv[0];
  userExists = true;
  return 0;
}

static int callbackInfoUser(void *data, int argc, char **argv, char **azColName) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.printf("Name   : %s", argv[0]);

  lcd.setCursor(0, 1);
  lcd.printf("Card ID: %s", argv[1]);

  lcd.setCursor(0, 2);
  lcd.printf("Adding : %s", ((String)argv[2]).substring(15,23));    // print time

  lcd.setCursor(0, 3);
  lcd.printf("      %s%s", ((String)argv[2]).substring(0, 10), ((String)argv[2]).substring(10, 13));    // print date

  userExists = true;
  delay(DELAY_DISPLAY_INFO);
  lcd.clear();
  return 0;
}

static int callbackFingerAddUser(void *data, int argc, char **argv, char **azColName) {
  int id = atoi(argv[0]);
  const char* sql;

  while (true) {
    while(! takeFingerImage("Waiting for finger", 1));
    while(! takeFingerImage("Place same finger", 2));

    if (finger.createModel() == FINGERPRINT_OK) {
      if (finger.storeModel(id) == FINGERPRINT_OK) {
        sql = ("UPDATE users SET fingerID = '" + String(id) + "' WHERE id = '" + String(id) + "';").c_str();
        if (! databaseExec(dbAccessControl, sql, NULL))
          return 0;
        return 0;
      }
      else {
        lcd.print("Try again");
        delay(DELAY_DISPLAY_MESSAGE);
      }
    }
    else {
      lcd.print("Fingers not match");
      delay(DELAY_DISPLAY_MESSAGE);
    }
  }
}

static int callbackFingerDeleteUser(void *data, int argc, char **argv, char **azColName) {
  finger.deleteModel(atoi(argv[0]));
  return 0;
}

static int callbackSetPassword(void *data, int argc, char **argv, char **azColName) {
  password = argv[0];
  return 0;
}

// ********************************************
// ****         Control Functions          ****
// ********************************************
void setSlaveSelect(int enablePin, int disablePin) {
  digitalWrite(enablePin, LOW);
  digitalWrite(disablePin, HIGH);
}

String waitingForUser() {
  String content;

  while (true) {
    delay(DELAY_WAIT_USER);
    // Look for new cards
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));     // Saves the ID as hexadecimal, not decimal
      }
      content.toUpperCase();
      return content.substring(1);
    }
    // Look for new fingers
    if (finger.getImage() == FINGERPRINT_OK)
      if (finger.image2Tz() == FINGERPRINT_OK) {
        if (finger.fingerFastSearch() == FINGERPRINT_OK)
          return String(finger.fingerID);
        else
          return FINGER_INVALID;
      }
  }
}

bool checkUser(String id) {
  userExists = false;
  const char* sql;

  if (id != FINGER_INVALID) {
    if (id.length() == 11)    // If the ID type is cardID
      sql = ("SELECT cardID FROM users WHERE cardID = '" + id + "'").c_str();
    else
      sql = ("SELECT cardID FROM users WHERE fingerID = '" + id + "'").c_str();

    if (! databaseExec(dbAccessControl, sql, callbackUserExist))
      return false;
  }
  return userExists;
}

void checkAccessTask(void *parameters) {
  while (true) {
    String id, message;
    const char* sql;

    id = waitingForUser();
    setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

    if (checkUser(id)) {
      message = ("Access verified");
      id = userIDCard;    // If the ID type is fingerID, set the valid ID (cardID) to id
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_GREEN_PIN, HIGH);
      buzzerRun(BUZZER_ALLOW_REPEAT, DELAY_BUZZER_ALLOW);
      printMessage(message, "");
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(LED_GREEN_PIN, LOW);
    }
    else {
      message = ("Access denied");
      digitalWrite(LED_RED_PIN, HIGH);
      buzzerRun(BUZZER_ERROR_REPEAT, DELAY_BUZZER_ERROR);
      printMessage(message, "");
      digitalWrite(LED_RED_PIN, LOW);
    }

    sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      vTaskSuspend(checkAccessHandle);

    setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  }
}

void addUser() {
  String id, message, lcdMessage;
  const char* sql;

  lcd.print("Waiting for card");
  id = waitingForUser();
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  lcd.clear();
  if (! checkUser(id)) {
    message = ("User added");
    lcdMessage = message;
    sql = ("INSERT INTO users (name, cardID, date) VALUES ('Other', '" + id + "', '" + getTime() + "')").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      return;

    sql = ("SELECT id FROM users WHERE cardID = '" + id + "'").c_str();
    if (! databaseExec(dbAccessControl, sql, callbackFingerAddUser))
      return;
  }
  else {
    message = ("Unauthorized attempts to user re-add");
    lcdMessage = ("User is there");
  }

  sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
  if (! databaseExec(dbAccessControl, sql, NULL))
    return;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  printMessage(lcdMessage, "");
}

void removeUser() {
  String id, message, lcdMessage;
  const char* sql;

  id = waitingForUser();
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  if (checkUser(id)) {
    message = ("User removed");
    lcdMessage = message;

    if (id.length() == 11)    // If the ID type is cardID
      sql = ("SELECT fingerID FROM users WHERE cardID = '" + id + "'").c_str();
    else
      sql = ("SELECT fingerID FROM users WHERE fingerID = '" + id + "'").c_str();
    if (! databaseExec(dbAccessControl, sql, callbackFingerDeleteUser))
      return;

    id = userIDCard;    // If the ID type is fingerID, set the valid ID (cardID) to id
    sql = ("DELETE FROM users WHERE cardID = '" + id + "'").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      return;
  }
  else {
    message = ("Unauthorized attempt to remove missing user");
    lcdMessage = ("User does not exist");
  }

  sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
  if (! databaseExec(dbAccessControl, sql, NULL))
    return;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  printMessage(lcdMessage, "");
}

void infoUser() {
  String id, message;
  const char* sql;

  id = waitingForUser();
  userExists = false;
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  if (checkUser(id)) {
    message = ("Display user information");
    id = userIDCard;    // If the ID type is fingerID, set the valid ID (cardID) to id
    sql = ("SELECT name, cardID, date FROM users WHERE cardID = '" + id + "'").c_str();
    if (! databaseExec(dbAccessControl, sql, callbackInfoUser))
      return;
  }
  else
    message = ("Invalid user to display user information");

  sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
  if (! databaseExec(dbAccessControl, sql, NULL))
    return;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  if (! userExists)
    printMessage(("Invalid user"), "");
}

// ********************************************
// ****          Other Functions           ****
// ********************************************
String getTime() {
  struct tm timeInfo;
  char myDatetimeString[50];
  if(!getLocalTime(& timeInfo))
    return "Failed to obtain time";

  strftime(myDatetimeString, sizeof(myDatetimeString), "%B %d %Y, %H:%M:%S %Z, %A", &timeInfo);
  return myDatetimeString;
}

void printMessage(String messageOne, String messageTwo) {
  lcd.print(messageOne);
  lcd.setCursor(0, 1);
  lcd.print(messageTwo);
  delay(DELAY_DISPLAY_MESSAGE);
  lcd.clear();
}

bool takeFingerImage(String message, int valueConvertImage) {
  int p;
  lcd.clear();
  lcd.print(message);

  do {
    delay(DELAY_WAIT_USER);
    p = finger.getImage();
  }while (p != FINGERPRINT_OK);

  p = finger.image2Tz(valueConvertImage);
  if (p != FINGERPRINT_OK)
    return false;

  lcd.clear();
  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    lcd.print("Finger is there");
    delay(DELAY_DISPLAY_MESSAGE);
    return false;
  }

  lcd.print("Image taken");
  delay(DELAY_DISPLAY_MESSAGE);
  lcd.clear();
  lcd.print("Pick up finger");

  while (finger.getImage() != FINGERPRINT_NOFINGER);

  lcd.clear();
  return true;
}

void buzzerRun(int soundRepeat, int buzzerDelay) {
    for (int i = 0; i < soundRepeat; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(buzzerDelay);
      digitalWrite(BUZZER_PIN, LOW);
      delay(buzzerDelay);
  }
}