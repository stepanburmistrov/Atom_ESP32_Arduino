# Arduino Nano проект

Файл скетча: `arduino/RobotX_Nano_UART_Control/RobotX_Nano_UART_Control.ino`

## Что делает Arduino Nano

Arduino Nano является нижним уровнем управления роботом. Он принимает UART-команды и управляет:

- двумя моторами движения;
- двумя сервоприводами клешней/манипуляторов.

Команды могут приходить двумя способами:

1. напрямую с компьютера через USB и Python-пульт;
2. от ESP32-C3 Wi-Fi модуля через UART.

## Основные настройки в коде

### Отладка

```cpp
#define DEBUG_SERIAL 1
```

- `1` — Arduino печатает отладочные сообщения в Serial;
- `0` — отладка отключена.

При прямой отладке с компьютера можно оставить `1`.
При работе через ESP32 лучше поставить `0`, потому что Serial используется для приёма команд, а лишний текст может мешать анализу обмена.

### Скорость UART

```cpp
const int UART_BAUD = 9600;
```

Это скорость связи. Она должна совпадать во всех местах:

- Arduino Nano: `9600`;
- Python-пульт: `9600`;
- ESP32-C3 `Serial1`: `9600`.

### Пины моторов

```cpp
const int LEFT_DIR_PIN  = 5;
const int LEFT_PWM_PIN  = 6;

const int RIGHT_DIR_PIN = 8;
const int RIGHT_PWM_PIN = 9;
```

Что менять:

- `LEFT_DIR_PIN` и `RIGHT_DIR_PIN` — пины направления;
- `LEFT_PWM_PIN` и `RIGHT_PWM_PIN` — пины скорости, должны поддерживать PWM.

### Скорость моторов

```cpp
int motorSpeed = 120;
```

Диапазон: `0...255`.

- меньше значение — робот едет медленнее;
- больше значение — робот едет быстрее;
- для отладки лучше начинать с `80...120`.

### Пины сервоприводов

```cpp
const int LEFT_SERVO_PIN  = A4;
const int RIGHT_SERVO_PIN = 7;
```

Если сервы подключены к другим пинам, менять нужно эти две строки.

### Начальные углы сервоприводов

```cpp
int leftServoAngle  = 90;
int rightServoAngle = 90;
```

Это стартовое положение серв. Если при включении клешни стоят неудобно, можно подобрать другие значения.

### Ограничения углов

```cpp
const int SERVO_MIN_ANGLE = 0;
const int SERVO_MAX_ANGLE = 180;
```

Если механизм упирается раньше полного хода, лучше ограничить диапазон, например:

```cpp
const int SERVO_MIN_ANGLE = 20;
const int SERVO_MAX_ANGLE = 160;
```

### Плавность движения серв

```cpp
const int SERVO_STEP = 1;
const unsigned long SERVO_INTERVAL = 5;
```

- `SERVO_STEP` — насколько градусов менять положение за один шаг;
- `SERVO_INTERVAL` — пауза между шагами в миллисекундах.

Меньше интервал и больше шаг — быстрее движение. Больше интервал и меньше шаг — плавнее движение.

### Отключение серв после отпускания кнопки

```cpp
const unsigned long SERVO_DETACH_DELAY = 200;
```

Через 200 мс после окончания движения серва отключается методом `detach()`.
Это снижает дрожание, писк и лишнее потребление тока.

## Зачем нужна библиотека ServoTimer2

В обычных проектах часто используют стандартную библиотеку:

```cpp
#include <Servo.h>
```

Но на Arduino Nano стандартная `Servo` использует таймер `Timer1`. Из-за этого перестаёт нормально работать PWM на пинах `9` и `10`.

В этом роботе правый мотор использует PWM на пине `9`:

```cpp
const int RIGHT_PWM_PIN = 9;
```

Поэтому стандартная `Servo` конфликтовала бы с управлением мотором.

Решение — использовать:

```cpp
#include <ServoTimer2.h>
```

`ServoTimer2` использует другой таймер и не ломает PWM на 9 пине. Именно поэтому в коде сервоприводы управляются через `ServoTimer2`, а не через стандартную `Servo`.

## Как установить ServoTimer2

1. Возьмите архив `arduino/libraries/ServoTimer2-master.zip`.
2. В Arduino IDE откройте `Sketch -> Include Library -> Add .ZIP Library...`.
3. Выберите этот архив.
4. После установки перезапустите Arduino IDE, если библиотека не подтянулась сразу.

## Логика движения

```cpp
void forward() {
  setLeftMotor(motorSpeed);
  setRightMotor(motorSpeed);
}
```

```cpp
void back() {
  setLeftMotor(-motorSpeed);
  setRightMotor(-motorSpeed);
}
```

```cpp
void turnLeft() {
  setLeftMotor(-motorSpeed);
  setRightMotor(motorSpeed);
}
```

```cpp
void turnRight() {
  setLeftMotor(motorSpeed);
  setRightMotor(-motorSpeed);
}
```

Если робот едет назад вместо вперёд, можно поменять знак у соответствующего мотора или поменять логику в `setLeftMotor()` / `setRightMotor()`.

## Частые проблемы

### Робот не реагирует на команды

Проверьте:

- скорость Serial = `9600`;
- выбран правильный COM-порт;
- команды отправляются с переводом строки `
`;
- при работе через ESP32 есть общий GND между ESP32 и Nano;
- ESP32 TX GPIO4 подключен к Nano RX D0.

### Скетч не загружается в Arduino Nano

Если к пину `D0/RX` подключен ESP32, он может мешать загрузчику Nano. На время прошивки отключите провод `ESP32 TX -> Nano RX`.

### Мотор на 9 пине перестал работать после добавления серв

Нужно использовать `ServoTimer2`, а не стандартную `Servo`. Стандартная библиотека Servo конфликтует с PWM на 9 пине.
