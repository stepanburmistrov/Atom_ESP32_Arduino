/*
  RobotX Nano UART Control
  ------------------------
  Управляет двумя моторами и двумя сервоприводами по UART-командам.
  Команды приходят по Serial на скорости 9600 бод: от Python-пульта через USB
  или от ESP32-C3 Wi-Fi модуля через GPIO4 -> RX Arduino Nano.

  Подробнее: см. docs/arduino_project.md и docs/uart_protocol.md
*/

#include <ServoTimer2.h>

// =====================
// ОТЛАДКА
// 1 — включить отладку в Serial
// 0 — выключить отладку
// ВАЖНО: при работе с ESP32 лучше ставить 0,
// потому что команды тоже приходят в RX Serial.
// =====================

#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
  #define DBG_BEGIN(x) Serial.begin(x)
  #define DBG_PRINT(x) Serial.print(x)
  #define DBG_PRINTLN(x) Serial.println(x)
#else
  #define DBG_BEGIN(x)
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif

// =====================
// UART ОТ ESP32
// =====================

const int UART_BAUD = 9600;

// =====================
// МОТОРЫ
// =====================

const int LEFT_DIR_PIN  = 5;
const int LEFT_PWM_PIN  = 6;

const int RIGHT_DIR_PIN = 8;
const int RIGHT_PWM_PIN = 9;

int motorSpeed = 160;

// =====================
// СЕРВЫ
// =====================

ServoTimer2 leftServo;
ServoTimer2 rightServo;

const int LEFT_SERVO_PIN  = A4;
const int RIGHT_SERVO_PIN = 7;

int leftServoAngle  = 90;
int rightServoAngle = 90;

const int SERVO_MIN_ANGLE = 0;
const int SERVO_MAX_ANGLE = 180;

const int SERVO_STEP = 1;
const unsigned long SERVO_INTERVAL = 5;

// Через сколько мс после окончания движения отключать серву
const unsigned long SERVO_DETACH_DELAY = 200;

unsigned long lastServoTime = 0;

bool leftServoAttached = false;
bool rightServoAttached = false;

unsigned long leftServoLastActiveTime = 0;
unsigned long rightServoLastActiveTime = 0;

// =====================
// СОСТОЯНИЕ СЕРВО
// =====================

bool leftServoUp     = false;
bool leftServoDown   = false;
bool rightServoUp    = false;
bool rightServoDown  = false;

// =====================
// ОТЛАДОЧНЫЙ ТАЙМЕР
// =====================

#if DEBUG_SERIAL
unsigned long lastDebugTime = 0;
const unsigned long DEBUG_INTERVAL = 500;
#endif

// =====================
// СЕРВО TIMER2
// =====================

int angleToPulse(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, 750, 2250);
}

void writeServoAngle(ServoTimer2 &s, int angle) {
  s.write(angleToPulse(angle));
}

void attachLeftServoIfNeeded() {
  if (!leftServoAttached) {
    leftServo.attach(LEFT_SERVO_PIN);
    leftServoAttached = true;

    // Сразу записываем текущий угол,
    // чтобы серва не дернулась в случайное положение
    writeServoAngle(leftServo, leftServoAngle);

    DBG_PRINTLN("LEFT SERVO ATTACH");
  }

  leftServoLastActiveTime = millis();
}

void attachRightServoIfNeeded() {
  if (!rightServoAttached) {
    rightServo.attach(RIGHT_SERVO_PIN);
    rightServoAttached = true;

    // Сразу записываем текущий угол,
    // чтобы серва не дернулась в случайное положение
    writeServoAngle(rightServo, rightServoAngle);

    DBG_PRINTLN("RIGHT SERVO ATTACH");
  }

  rightServoLastActiveTime = millis();
}

void detachServosIfNeeded() {
  unsigned long now = millis();

  bool leftMoving = leftServoUp || leftServoDown;
  bool rightMoving = rightServoUp || rightServoDown;

  if (leftServoAttached && !leftMoving) {
    if (now - leftServoLastActiveTime >= SERVO_DETACH_DELAY) {
      leftServo.detach();
      leftServoAttached = false;

      DBG_PRINTLN("LEFT SERVO DETACH");
    }
  }

  if (rightServoAttached && !rightMoving) {
    if (now - rightServoLastActiveTime >= SERVO_DETACH_DELAY) {
      rightServo.detach();
      rightServoAttached = false;

      DBG_PRINTLN("RIGHT SERVO DETACH");
    }
  }
}

// =====================
// SETUP
// =====================

void setup() {
  Serial.begin(UART_BAUD);
  Serial.setTimeout(10);

  DBG_PRINTLN("================================");
  DBG_PRINTLN("RobotX Arduino Nano UART control");
  DBG_PRINTLN("RX commands from ESP32");
  DBG_PRINTLN("================================");

  pinMode(LEFT_DIR_PIN, OUTPUT);
  pinMode(LEFT_PWM_PIN, OUTPUT);

  pinMode(RIGHT_DIR_PIN, OUTPUT);
  pinMode(RIGHT_PWM_PIN, OUTPUT);

  // ВАЖНО:
  // Сервы НЕ подключаем постоянно.
  // Они подключатся только при команде движения.
  leftServoAttached = false;
  rightServoAttached = false;

  stopMotors();
  stopServoButtons();

  DBG_PRINTLN("Setup complete");
}

// =====================
// LOOP
// =====================

void loop() {
  readUartCommands();
  updateServos();
  debugState();
}

// =====================
// ПРИЁМ UART ОТ ESP32
// =====================

void readUartCommands() {
  while (Serial.available()) {
    String cmdLine = Serial.readStringUntil('\n');
    cmdLine.trim();

    if (cmdLine.length() == 0) return;

    char cmd = cmdLine.charAt(0);

    DBG_PRINT("RX: ");
    DBG_PRINTLN(cmd);

    handleCommand(cmd);
  }
}

// =====================
// КОМАНДЫ ОТ ESP32
//
// F — вперед
// B — назад
// L — влево
// R — вправо
// S — стоп
//
// Q/q — левая серва вверх / стоп
// A/a — левая серва вниз / стоп
// W/w — правая серва вверх / стоп
// D/d — правая серва вниз / стоп
//
// ВАЖНО:
// Правая серва стоит зеркально,
// поэтому W/D специально включают обратные направления.
// =====================

void handleCommand(char cmd) {
  switch (cmd) {
    case 'F':
      DBG_PRINTLN("CMD: FORWARD");
      forward();
      break;

    case 'B':
      DBG_PRINTLN("CMD: BACK");
      back();
      break;

    case 'L':
      DBG_PRINTLN("CMD: LEFT");
      turnLeft();
      break;

    case 'R':
      DBG_PRINTLN("CMD: RIGHT");
      turnRight();
      break;

    case 'S':
      DBG_PRINTLN("CMD: STOP MOTORS");
      stopMotors();
      break;

    case 'Q':
      DBG_PRINTLN("CMD: LEFT SERVO UP START");
      attachLeftServoIfNeeded();
      leftServoUp = true;
      leftServoDown = false;
      break;

    case 'q':
      DBG_PRINTLN("CMD: LEFT SERVO UP STOP");
      leftServoUp = false;
      leftServoLastActiveTime = millis();
      break;

    case 'A':
      DBG_PRINTLN("CMD: LEFT SERVO DOWN START");
      attachLeftServoIfNeeded();
      leftServoDown = true;
      leftServoUp = false;
      break;

    case 'a':
      DBG_PRINTLN("CMD: LEFT SERVO DOWN STOP");
      leftServoDown = false;
      leftServoLastActiveTime = millis();
      break;

    case 'W':
      DBG_PRINTLN("CMD: RIGHT SERVO UP START MIRRORED");
      attachRightServoIfNeeded();

      // Правая серва стоит зеркально,
      // поэтому команда W включает rightServoDown
      rightServoDown = true;
      rightServoUp = false;
      break;

    case 'w':
      DBG_PRINTLN("CMD: RIGHT SERVO UP STOP MIRRORED");
      rightServoDown = false;
      rightServoLastActiveTime = millis();
      break;

    case 'D':
      DBG_PRINTLN("CMD: RIGHT SERVO DOWN START MIRRORED");
      attachRightServoIfNeeded();

      // Правая серва стоит зеркально,
      // поэтому команда D включает rightServoUp
      rightServoUp = true;
      rightServoDown = false;
      break;

    case 'd':
      DBG_PRINTLN("CMD: RIGHT SERVO DOWN STOP MIRRORED");
      rightServoUp = false;
      rightServoLastActiveTime = millis();
      break;

    default:
      DBG_PRINT("UNKNOWN CMD: ");
      DBG_PRINTLN(cmd);
      break;
  }
}

// =====================
// МОТОРЫ
// =====================

void setLeftMotor(int speed) {
  speed = constrain(speed, -255, 255);

  if (speed > 0) {
    digitalWrite(LEFT_DIR_PIN, LOW);
    analogWrite(LEFT_PWM_PIN, speed);
  } 
  else if (speed < 0) {
    digitalWrite(LEFT_DIR_PIN, HIGH);
    analogWrite(LEFT_PWM_PIN, 255+speed);
  } 
  else {
    digitalWrite(LEFT_DIR_PIN, LOW);
    analogWrite(LEFT_PWM_PIN, 0);
  }
}

void setRightMotor(int speed) {
  speed = constrain(speed, -255, 255);

  if (speed > 0) {
    digitalWrite(RIGHT_DIR_PIN, LOW);
    analogWrite(RIGHT_PWM_PIN, speed);
  } 
  else if (speed < 0) {
    digitalWrite(RIGHT_DIR_PIN, HIGH);
    analogWrite(RIGHT_PWM_PIN, 255+speed);
  } 
  else {
    digitalWrite(RIGHT_DIR_PIN, LOW);
    analogWrite(RIGHT_PWM_PIN, 0);
  }
}

void forward() {
  setLeftMotor(motorSpeed);
  setRightMotor(motorSpeed);
}

void back() {
  setLeftMotor(-motorSpeed);
  setRightMotor(-motorSpeed);
}

void turnLeft() {
  setLeftMotor(-motorSpeed);
  setRightMotor(motorSpeed);
}

void turnRight() {
  setLeftMotor(motorSpeed);
  setRightMotor(-motorSpeed);
}

void stopMotors() {
  setLeftMotor(0);
  setRightMotor(0);
}

// =====================
// СЕРВЫ
// =====================

void stopServoButtons() {
  leftServoUp = false;
  leftServoDown = false;
  rightServoUp = false;
  rightServoDown = false;

  leftServoLastActiveTime = millis();
  rightServoLastActiveTime = millis();
}

void updateServos() {
  unsigned long now = millis();

  // Даже если ещё не пора двигать сервы,
  // всё равно проверяем, не пора ли отключить их
  if (now - lastServoTime < SERVO_INTERVAL) {
    detachServosIfNeeded();
    return;
  }

  lastServoTime = now;

  bool changedLeft = false;
  bool changedRight = false;

  bool leftMoving = leftServoUp || leftServoDown;
  bool rightMoving = rightServoUp || rightServoDown;

  // ---------- Левая серва ----------
  if (leftMoving) {
    attachLeftServoIfNeeded();

    if (leftServoUp) {
      leftServoAngle -= SERVO_STEP;
      changedLeft = true;
    }

    if (leftServoDown) {
      leftServoAngle += SERVO_STEP;
      changedLeft = true;
    }

    leftServoAngle = constrain(leftServoAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);

    if (changedLeft) {
      writeServoAngle(leftServo, leftServoAngle);
    }

    leftServoLastActiveTime = now;
  }

  // ---------- Правая серва ----------
  if (rightMoving) {
    attachRightServoIfNeeded();

    if (rightServoUp) {
      rightServoAngle -= SERVO_STEP;
      changedRight = true;
    }

    if (rightServoDown) {
      rightServoAngle += SERVO_STEP;
      changedRight = true;
    }

    rightServoAngle = constrain(rightServoAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);

    if (changedRight) {
      writeServoAngle(rightServo, rightServoAngle);
    }

    rightServoLastActiveTime = now;
  }

  detachServosIfNeeded();
}

// =====================
// ОТЛАДКА СОСТОЯНИЯ
// =====================

void debugState() {
#if DEBUG_SERIAL
  if (millis() - lastDebugTime < DEBUG_INTERVAL) return;
  lastDebugTime = millis();

  DBG_PRINT("LeftServo=");
  DBG_PRINT(leftServoAngle);

  DBG_PRINT(" RightServo=");
  DBG_PRINT(rightServoAngle);

  DBG_PRINT(" | L_UP=");
  DBG_PRINT(leftServoUp);

  DBG_PRINT(" L_DOWN=");
  DBG_PRINT(leftServoDown);

  DBG_PRINT(" R_UP=");
  DBG_PRINT(rightServoUp);

  DBG_PRINT(" R_DOWN=");
  DBG_PRINT(rightServoDown);

  DBG_PRINT(" | L_ATT=");
  DBG_PRINT(leftServoAttached);

  DBG_PRINT(" R_ATT=");
  DBG_PRINTLN(rightServoAttached);
#endif
}