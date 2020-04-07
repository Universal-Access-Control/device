// ********************************************
// ****           Include Libraries        ****
// ********************************************
#include <LiquidCrystal.h>
#include <Password.h>
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

// KEYPAD
#define KEYPAD_SCL_PIN 22
#define KEYPAD_SDO_PIN 21
#define KEYPAD_NUM_OF_KEYS 16
#define KEYPAD_ZERO_NUMBER_KEY 10
#define KEYPAD_CONFIRM_NUM 11
#define KEYPAD_BACK_NUM 12
#define KEYPAD_ADD_USER_NUM 13
#define KEYPAD_REMOVE_USER_NUM 14
#define KEYPAD_INFO_CARD_NUM 15
#define KEYPAD_CHANGE_PASSWORD_NUM 16
#define KEYPAD_INVALID_VALUE -1

// CHARACTER LCD
#define LCD_ROWS 4
#define LCD_COLUMNS 20
#define LCD_RS_PIN 26
#define LCD_EN_PIN 25
#define LCD_D4_PIN 17
#define LCD_D5_PIN 16
#define LCD_D6_PIN 27
#define LCD_D7_PIN 14

// ACTIONS
#define ACTION_BACK -1
#define ACTION_WRONG_PASS 0
#define ACTION_ALLOW_ACCESS 1

// PASSWORD
#define PASSWORD_MAX_LENGTH 8

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

// DELAY
#define DELAY_WAIT_USER 500
#define DELAY_DISPLAY_MESSAGE 2000
#define DELAY_OPEN_LOCK 3000

// ********************************************
// ****          Global Variables          ****
// ********************************************
// CHARACTER LCD
LiquidCrystal lcd(LCD_RS_PIN, LCD_EN_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

// KEYPAD
int8_t key = KEYPAD_INVALID_VALUE;
TaskHandle_t keypadHandle = NULL;

// RFID
MFRC522 mfrc522(SPI_RFID_SLAVE_PIN, SPI_RFID_SCLK_PIN);
TaskHandle_t checkAccessHandle = NULL;

// SQLite
sqlite3 *dbAccessControl;
bool userExists;

// ********************************************
// ****                Setup               ****
// ********************************************
void setup()
{
  // SERIAL
  Serial.begin(SERIAL_BAUD_RATE);

  // CHARACTER LCD
  lcd.begin(LCD_COLUMNS, LCD_ROWS);

  // KEYPAD
  pinMode(KEYPAD_SCL_PIN, OUTPUT);
  pinMode(KEYPAD_SDO_PIN, INPUT_PULLUP);
  digitalWrite(KEYPAD_SCL_PIN, HIGH);

  // RFID
  pinMode(SPI_RFID_SLAVE_PIN, OUTPUT);
  digitalWrite(SPI_RFID_SLAVE_PIN, HIGH);
  SPI.begin();    // Initiate  SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522

  // SD
  if(! storageInit())
    return;

  // SQLite
  if(! sqliteInit())
    return;

  // WiFi
  wifiInit();
  configTime(WIFI_GmtOffset_Sec, WIFI_DayLightOffset_Sec, WIFI_NTPServer);

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

  const char* createTbUsers = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY NOT NULL, name VARCHAR(100) NULL, cardID VARCHAR(100) NOT NULL, date VARCHAR(100) NOT NULL)";
  const char* createTbLogs = "CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY NOT NULL, cardID VARCHAR(100) NOT NULL, date VARCHAR(100) NOT NULL, action VARCHAR(100) NOT NULL)";

  sqlite3_initialize();
  if (! databaseOpen(&dbAccessControl, "/sd/Access_Control.db"))
    return false;

  if (! databaseExec(dbAccessControl, createTbUsers, NULL))
    return false;

  if (! databaseExec(dbAccessControl, createTbLogs, NULL))
    return false;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  return true;
}

void wifiInit() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
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
        case KEYPAD_INFO_CARD_NUM:
          Serial.println("Show Card Info");
          break;
        case KEYPAD_CHANGE_PASSWORD_NUM:
          Serial.println("Change Password");
          break;
      }
    }
  }
}

// ********************************************
// ****           Auth Functions           ****
// ********************************************
int checkPassword()
{
  lcd.clear();
  lcd.print("Password: ");
  Password password = Password("12345677");

  int8_t i = 0;
  int8_t number = KEYPAD_INVALID_VALUE;
  while (i < PASSWORD_MAX_LENGTH)
  {
    enableKeypadInterrupt();
    vTaskSuspend(keypadHandle);
    number = key;

    if (number == KEYPAD_BACK_NUM || number == KEYPAD_CONFIRM_NUM)
      break;

    if (number >= 0 && number <= 9)
    {
      password.append(number + 48);
      lcd.print(number);
      i++;
    }
  }
  
  lcd.clear();
  if (number == KEYPAD_BACK_NUM)
    return ACTION_BACK;
  else
    return password.evaluate() ? ACTION_ALLOW_ACCESS : ACTION_WRONG_PASS;
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
  userExists = true;
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
  do {
    delay(DELAY_WAIT_USER);
    // Look for new cards
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));     // Saves the ID as hexadecimal, not decimal
      }
      content.toUpperCase();
    }
  }while (content == NULL);

  return content.substring(1);
}

bool checkUser(String id) {
  userExists = false;
  const char* sql = ("SELECT cardID FROM users WHERE cardID = '" + id + "'").c_str();
  if (! databaseExec(dbAccessControl, sql, callbackUserExist))
    return false;
  return userExists;
}

void checkAccessTask(void *parameters) {
  while (true) {
    String id, message;
    const char* sql;

    id = waitingForUser();
    setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);
    
    if(checkUser(id)) {
      message = ("Access verified");
      digitalWrite(RELAY_PIN, LOW);
      delay(DELAY_OPEN_LOCK);
      digitalWrite(RELAY_PIN, HIGH);
    }
    else
      message = ("Access denied");
    
    sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      vTaskSuspend(checkAccessHandle);

    setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  }
}

void addUser() {
  String id, message;
  const char* sql;

  id = waitingForUser();
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  if(! checkUser(id)) {
    message = ("User added");
    sql = ("INSERT INTO users (name, cardID, date) VALUES ('Other', '" + id + "', '" + getTime() + "')").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      return;
  }
  else
    message = ("Unauthorized attempts to user re-add");

  sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
  if (! databaseExec(dbAccessControl, sql, NULL))
    return;
  
  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  delay(DELAY_APPLY_FUNC);
}

void removeUser() {
  String id, message;
  const char* sql;

  id = waitingForUser();
  setSlaveSelect(SPI_SD_SLAVE_PIN, SPI_RFID_SLAVE_PIN);

  if(checkUser(id)) {
    message = ("User removed");
    sql = ("DELETE FROM users WHERE cardID = '" + id + "'").c_str();
    if (! databaseExec(dbAccessControl, sql, NULL))
      return;
  }
  else
    message = ("Unauthorized attempt to remove missing user");
  
  sql = ("INSERT INTO logs (cardID, date, action) VALUES ('" + id + "', '" + getTime() + "', '" + message + "')").c_str();
  if (! databaseExec(dbAccessControl, sql, NULL))
    return;

  setSlaveSelect(SPI_RFID_SLAVE_PIN, SPI_SD_SLAVE_PIN);
  delay(DELAY_APPLY_FUNC);
}

// ********************************************
// ****          Other Functions           ****
// ********************************************
String getTime() {
  struct tm timeInfo;
  char myDatetimeString[50];
  if(!getLocalTime(& timeInfo))
    return "Failed to obtain time";
  
  strftime(myDatetimeString, sizeof(myDatetimeString), "%B %d %Y, %A, %H:%M:%S %Z", &timeInfo);
  return myDatetimeString;
}