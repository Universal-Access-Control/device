// ********************************************
// ****           Include Libraries        ****
// ********************************************
#include <LiquidCrystal.h>
#include <Password.h>

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
#define KEYPAD_SCL_PIN 26
#define KEYPAD_SDO_PIN 25
#define KEYPAD_NUM_OF_KEYS 16
#define KEYPAD_CONFIRM_NUM 11
#define KEYPAD_BACK_NUM 12
#define KEYPAD_ADD_CARD_NUM 13
#define KEYPAD_REMOVE_CARD_NUM 14
#define KEYPAD_CARD_INFO_NUM 15
#define KEYPAD_CHANGE_PASSWORD_NUM 16
#define KEYPAD_INVALID_VALUE -1
#define KEYPAD_ZERO_NUMBER_KEY 10

// CHARACTER LCD
#define LCD_ROWS 4
#define LCD_COLUMNS 20
#define LCD_RS_PIN 17
#define LCD_EN_PIN 16
#define LCD_D4_PIN 27
#define LCD_D5_PIN 14
#define LCD_D6_PIN 5
#define LCD_D7_PIN 23

// ACTIONS
#define ACTION_BACK -1
#define ACTION_WRONG_PASS 0
#define ACTION_ALLOW_ACCESS 1

// PASSWORD
#define PASSWORD_MAX_LENGTH 8

// ********************************************
// ****          Global Variables          ****
// ********************************************
// CHARACTER LCD
LiquidCrystal lcd(LCD_RS_PIN, LCD_EN_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

// KEYPAD
int8_t key = KEYPAD_INVALID_VALUE;
TaskHandle_t keypadHandle = NULL;

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
  xTaskCreatePinnedToCore(keypadTask, "Keypad Task", 4096, NULL, 2, &keypadHandle, ARDUINO_RUNNING_CORE);
}

// ********************************************
// ****                 Loop               ****
// ********************************************
void loop()
{
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
        case KEYPAD_ADD_CARD_NUM:
          Serial.println("Add Card");
          break;
        case KEYPAD_REMOVE_CARD_NUM:
          Serial.println("Remove Card");
          break;
        case KEYPAD_CARD_INFO_NUM:
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
