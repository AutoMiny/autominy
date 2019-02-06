// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include <Arduino.h>
#include "I2Cdev.h"
#include <avr/pgmspace.h>
#include "MPU6050_6Axis_MotionApps20.h"
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
#include "Wire.h"
#endif

#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#include <PacketSerial.h>

#define LED_PIN 6
#define NUMPIXELS 21

#define DIR_PIN 4
#define SERVO_PIN 10
#define MOTOR_SPEED_PIN 11

#define ENCODER_PIN 3
#define INTERRUPT_PIN 2  // use pin 2 on Arduino Uno & most boards
#define SERVO_FEEDBACK_MOTOR_PIN 0

//Voltmeter
#define NEWLED_PIN 13
#define BATTERY_PIN A6
#define ENABLE_PIN 7
int ledState = HIGH;                // ledState used to set the LED
unsigned long previousMillis = 0;   // will store last time LED was updated
const float referenceVolts = 4.7; //Default reference on Teensy is 3.3V
const float R1 = 3300.0;
const float R2 = 1490.0;
bool powered = false;

void onLedCommand(const char* cmd_msg);
void onSteeringCommand(const int16_t cmd_msg);
void onSpeedCommand(const int16_t cmd_msg);

MPU6050 mpu;

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
Servo myservo; // create servo object to control a servo
int servo_pw = 1500;    // variable to set the angle of servo motor
int last_pw = 0;
bool servo_initialized = false;
volatile uint8_t ticks = 0;

int8_t direction_motor = 1;

#define VOLTAGE_BUFFER_SIZE 256
int voltageBuffer[VOLTAGE_BUFFER_SIZE] = {0};
int voltageIndex = 0;
int16_t currentSpeed = 0;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint8_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

PacketSerial packetSerial;

// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================
volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high

enum class MessageType :uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    SPEED_CMD,
    STEERING_CMD,
    LED_CMD,
    STEERING_ANGLE,
    TICKS,
    SPEED,
    IMU,
    VOLTAGE
};

enum class VoltageStatus {
    DISABLED,
    BAD,
    WARN,
    GOOD
};

enum class Direction {
    NONE,
    REVERSE,
    FORWARD
};

VoltageStatus currentVoltageStatus = VoltageStatus::DISABLED;
Direction currentDirection = Direction::NONE;

void dmpDataReady() {
    mpuInterrupt = true;
}

/*void StartTimer2(void) {
    pinMode(MOTOR_SPEED_PIN, OUTPUT); //976.5625Hz
    TCNT2 = 0;
    TIFR2 = 0x00;
    TIMSK2 = TIMSK2 | 0x01;
    TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(CS22);
    TCCR2B = (TCCR2B & 0b11111000) | 0x02;//62500HZ= 16MHz/1024/2=7812
    sei();
}

ISR(TIMER2_OVF_vect) {
    TIFR2 = 0x00;
    T1Ovs2++;//INCREMENTING OVERFLOW COUNTER
}*/

void encoder() {
    cli();
    ticks++;
    sei();
}


// ================================================================
// ===               SUBSCRIBERS                                ===
// ================================================================
void onSteeringCommand(const int16_t cmd_msg) {
    // scale it to use it with the servo (value between 0 and 180)
    servo_pw = cmd_msg;

    if (last_pw != servo_pw) {
        myservo.writeMicroseconds(servo_pw);
    }

    if (!servo_initialized) {
        // attaches the servo on pin 9 to the servo object
        myservo.attach(SERVO_PIN);
        servo_initialized = true;
    
        if (last_pw != servo_pw) {
            myservo.writeMicroseconds(servo_pw);
        }
    }
}


void displayReverseLed() {
    if (currentDirection == Direction::REVERSE) {
        return;
    }

    pixels.setBrightness(16);

    pixels.setPixelColor(2, 255, 255, 255);
    pixels.setPixelColor(3, 255, 255, 255);
    pixels.setPixelColor(7, 255, 255, 255);
    pixels.setPixelColor(8, 255, 255, 255);

    pixels.setPixelColor(13, 0, 0, 0);
    pixels.setPixelColor(14, 0, 0, 0);
    pixels.setPixelColor(17, 0, 0, 0);
    pixels.setPixelColor(18, 0, 0, 0);

    pixels.show();

    currentDirection = Direction::REVERSE;
}

void displayForwardLed() {
    if (currentDirection == Direction::FORWARD) {
        return;
    }

    pixels.setBrightness(16);

    pixels.setPixelColor(2, 0, 0, 0);
    pixels.setPixelColor(3, 0, 0, 0);
    pixels.setPixelColor(7, 0, 0, 0);
    pixels.setPixelColor(8, 0, 0, 0);

    pixels.setPixelColor(11, 255, 255, 255);
    pixels.setPixelColor(12, 255, 255, 255);
    pixels.setPixelColor(13, 255, 255, 255);
    pixels.setPixelColor(14, 255, 255, 255);
    pixels.setPixelColor(17, 255, 255, 255);
    pixels.setPixelColor(18, 255, 255, 255);
    pixels.setPixelColor(19, 255, 255, 255);
    pixels.setPixelColor(18, 255, 255, 255);
    pixels.show();

    currentDirection = Direction::FORWARD;
}

void disableDirectionLed() {
    if (currentDirection == Direction::NONE) {
        return;
    }

    pixels.setBrightness(16);

    pixels.setPixelColor(2, 0, 0, 0);
    pixels.setPixelColor(3, 0, 0, 0);
    pixels.setPixelColor(7, 0, 0, 0);
    pixels.setPixelColor(8, 0, 0, 0);

    pixels.setPixelColor(13, 0, 0, 0);
    pixels.setPixelColor(14, 0, 0, 0);
    pixels.setPixelColor(17, 0, 0, 0);
    pixels.setPixelColor(18, 0, 0, 0);

    pixels.show();

    currentDirection = Direction::NONE;
}

void onSpeedCommand(const int16_t cmd_msg) {
    int16_t motor_val = cmd_msg / 4;

    if (abs(motor_val) > 255) {
        motor_val = 255;
    }

    if (currentSpeed == motor_val) {
        return;
    }
    currentSpeed = motor_val;

    uint8_t servo_val = (uint8_t) abs(motor_val);

    // if speed is set to 0 we keep the old direction
    // and just do nothing but set the val
    // else the speed direction might get inversed
    if (motor_val < 0) {
        digitalWrite(DIR_PIN, HIGH);
        direction_motor = -1;
        displayReverseLed();
    } else if (motor_val > 0) {
        digitalWrite(DIR_PIN, LOW);
        direction_motor = 1;
        displayForwardLed();
    }

    if (servo_val < 15) {
        servo_val = 15;
        disableDirectionLed();
    }

    analogWrite(MOTOR_SPEED_PIN, servo_val);
}

void onLedCommand(const char* cmd_msg) {
    pixels.setBrightness(16);
    if (strcmp_P(cmd_msg, PSTR("left")) == 0) {
        for (uint8_t i = 18; i < 20; i++)
            pixels.setPixelColor(i, pixels.Color(255, 80, 0)); //yellow
        for (uint8_t i = 0; i < 2; i++)
            pixels.setPixelColor(i, pixels.Color(255, 80, 0)); //yellow
    } else if (strcmp_P(cmd_msg, PSTR("right")) == 0) {
        for (uint8_t i = 11; i < 13; i++)
            pixels.setPixelColor(i, pixels.Color(255, 80, 0)); //yellow
        for (uint8_t i = 9; i < 11; i++)
            pixels.setPixelColor(i, pixels.Color(255, 80, 0)); //yellow
    } else if (strcmp_P(cmd_msg, PSTR("brake")) == 0) {
        for (uint8_t i = 0; i < 3; i++)
            pixels.setPixelColor(i, pixels.Color(255, 0, 0)); //red

        for (uint8_t i = 8; i < 10; i++)
            pixels.setPixelColor(i, pixels.Color(255, 0, 0)); //red
    } else if (strcmp_P(cmd_msg, PSTR("reverse")) == 0) {
        for (uint8_t i = 0; i < 3; i++)
            pixels.setPixelColor(i, pixels.Color(255, 0, 0)); //red

        for (uint8_t i = 8; i < 10; i++)
            pixels.setPixelColor(i, pixels.Color(255, 0, 0)); //red
    } else if (strcmp_P(cmd_msg, PSTR("disable")) == 0) {
        for (uint8_t i = 0; i < 21; i++)
            pixels.setPixelColor(i, pixels.Color(0, 0, 0)); //disable
    }
    pixels.show(); // This sends the updated pixel color to the hardware.
}


void displayVoltageGoodLed() {
    if (currentVoltageStatus == VoltageStatus::GOOD) {
        return;
    }

    pixels.setBrightness(16);
    pixels.setPixelColor(5, 0, 255, 0);
    pixels.setPixelColor(15, 0, 255, 0);
    pixels.setPixelColor(16, 0, 255, 0);
    pixels.show();

    currentVoltageStatus = VoltageStatus::GOOD;
}

void displayVoltageWarningLed() {
    if (currentVoltageStatus == VoltageStatus::WARN) {
        return;
    }
    pixels.setBrightness(16);
    pixels.setPixelColor(5, 255, 255, 0);
    pixels.setPixelColor(15, 255, 255, 0);
    pixels.setPixelColor(16, 255, 255, 0);
    pixels.show();

    currentVoltageStatus = VoltageStatus::WARN;
}

void displayVoltageBadLed() {
    if (currentVoltageStatus == VoltageStatus::BAD) {
        return;
    }
    pixels.setBrightness(16);
    pixels.setPixelColor(5, 255, 0, 0);
    pixels.setPixelColor(15, 255, 0, 0);
    pixels.setPixelColor(16, 255, 0, 0);
    pixels.show();

    currentVoltageStatus = VoltageStatus::BAD;

}

void disableLed() {
    if (currentVoltageStatus == VoltageStatus::DISABLED) {
        return;
    }

    pixels.setPixelColor(5, 0, 0, 0);
    pixels.setPixelColor(15, 0, 0, 0);
    pixels.setPixelColor(16, 0, 0, 0);
    pixels.show();

    currentVoltageStatus = VoltageStatus::DISABLED;
}


int readEEPROMInt(int addr) {
    byte low, high;
    low=EEPROM.read(addr);
    high=EEPROM.read(addr+1);
    return low + ((high << 8)&0xFF00);
}

void onPacketReceived(const uint8_t* message, size_t size)
{
    auto type = static_cast<MessageType>(message[0]);

    switch(type) {

        case MessageType::DEBUG:break;
        case MessageType::INFO:break;
        case MessageType::WARN:break;
        case MessageType::ERROR:break;
        case MessageType::STEERING_ANGLE:break;
        case MessageType::TICKS:break;
        case MessageType::SPEED:break;
        case MessageType::IMU:break;
        case MessageType::VOLTAGE:break;

        case MessageType::SPEED_CMD:
            int16_t speed;
            memcpy(&speed, &message[1], sizeof(int16_t));
            onSpeedCommand(speed);
            break;
        case MessageType::STEERING_CMD:
            int16_t steering;
            memcpy(&steering, &message[1], sizeof(int16_t));
            onSteeringCommand(steering);
            break;
        case MessageType::LED_CMD:
            char cmd[size];
            memcpy(cmd, &message[1], size);
            onLedCommand(cmd);

            break;
    }
}

void log(MessageType type, const __FlashStringHelper *str) {
    uint8_t size = sizeof(type) + strlen_P((PGM_P)str) + 1;
    uint8_t buf[size];
    buf[0] = (uint8_t)type;
    strcpy_P(&buf[1], (PGM_P)str);
    packetSerial.send(buf, size);
}

void log(MessageType type, char *str) {
    uint8_t size = sizeof(type) + strlen(str) + 1;
    uint8_t buf[size];
    buf[0] = (uint8_t)type;
    strcpy(&buf[1], str);
    packetSerial.send(buf, size);
}

void logerror(const __FlashStringHelper *str) {
    log(MessageType::ERROR, str);
}

void logerror(char *str) {
    log(MessageType::ERROR, str);
}

void loginfo(const __FlashStringHelper *str) {
    log(MessageType::INFO, str);
}

void loginfo(char *str) {
    log(MessageType::INFO, str);
}

void logdebug(const __FlashStringHelper *str) {
    log(MessageType::DEBUG, str);
}

void logdebug(char *str) {
    log(MessageType::DEBUG, str);
}

void logwarn(const __FlashStringHelper *str) {
    log(MessageType::WARN, str);
}

void logwarn(char *str) {
    log(MessageType::WARN, str);
}

void sendIMU(uint8_t* fifoBuffer, int16_t temperature) {
    uint8_t size = sizeof(MessageType) + 22;
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::IMU;
    uint8_t lowTemp, highTemp;
    lowTemp = temperature >> 8;
    highTemp = temperature & 0xFF;
    memcpy(&buf[1], &fifoBuffer[0], sizeof(uint8_t));
    memcpy(&buf[2], &fifoBuffer[1], sizeof(uint8_t));
    memcpy(&buf[3], &fifoBuffer[4], sizeof(uint8_t));
    memcpy(&buf[4], &fifoBuffer[5], sizeof(uint8_t));
    memcpy(&buf[5], &fifoBuffer[8], sizeof(uint8_t));
    memcpy(&buf[6], &fifoBuffer[9], sizeof(uint8_t));
    memcpy(&buf[7], &fifoBuffer[12], sizeof(uint8_t));
    memcpy(&buf[8], &fifoBuffer[13], sizeof(uint8_t));
    memcpy(&buf[9], &fifoBuffer[16], sizeof(uint8_t));
    memcpy(&buf[10], &fifoBuffer[17], sizeof(uint8_t));
    memcpy(&buf[11], &fifoBuffer[20], sizeof(uint8_t));
    memcpy(&buf[12], &fifoBuffer[21], sizeof(uint8_t));
    memcpy(&buf[13], &fifoBuffer[24], sizeof(uint8_t));
    memcpy(&buf[14], &fifoBuffer[25], sizeof(uint8_t));
    memcpy(&buf[15], &fifoBuffer[28], sizeof(uint8_t));
    memcpy(&buf[16], &fifoBuffer[29], sizeof(uint8_t));
    memcpy(&buf[17], &fifoBuffer[32], sizeof(uint8_t));
    memcpy(&buf[18], &fifoBuffer[33], sizeof(uint8_t));
    memcpy(&buf[19], &fifoBuffer[36], sizeof(uint8_t));
    memcpy(&buf[20], &fifoBuffer[37], sizeof(uint8_t));
    memcpy(&buf[21], &lowTemp, sizeof(uint8_t));
    memcpy(&buf[22], &highTemp, sizeof(uint8_t));

    packetSerial.send(buf, size);
}

void sendVoltage(float voltage) {
    uint8_t size = sizeof(MessageType) + sizeof(voltage);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::VOLTAGE;
    memcpy(&buf[1], &voltage, sizeof(voltage));
    packetSerial.send(buf, size);
}

void sendSpeed(float speed) {
    uint8_t size = sizeof(MessageType) + sizeof(speed);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::SPEED;
    memcpy(&buf[1], &speed, sizeof(speed));
    packetSerial.send(buf, size);
}

void sendTicks(uint8_t ticks) {
    uint8_t size = sizeof(MessageType) + sizeof(ticks);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::TICKS;
    memcpy(&buf[1], &ticks, sizeof(ticks));
    packetSerial.send(buf, size);
}

void sendSteeringAngle(uint16_t steeringAngle) {
    uint8_t size = sizeof(MessageType) + sizeof(steeringAngle);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::STEERING_ANGLE;
    memcpy(&buf[1], &steeringAngle, sizeof(steeringAngle));
    packetSerial.send(buf, size);
}

void setup() {
    packetSerial.begin(115200);
    packetSerial.setStream(&Serial);
    packetSerial.setPacketHandler(&onPacketReceived);

    // join I2C bus (I2Cdev library doesn't do this automatically)
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    Wire.begin();
    Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
    Fastwire::setup(400, true);
#endif

    // initialize device
    logerror(F("Initializing I2C devices..."));
    mpu.initialize();
    pinMode(INTERRUPT_PIN, INPUT);

    // verify connection
    loginfo(F("Testing device connections..."));
    loginfo(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // load and configure the DMP
    loginfo(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();
    loginfo(F("Initialized DMP..."));

    loginfo(F("Reading calibration..."));
    // supply your own gyro offsets here, scaled for min sensitivity
    int xAccel = readEEPROMInt(0);
    int yAccel = readEEPROMInt(2);
    int zAccel = readEEPROMInt(4);
    int xGyro = readEEPROMInt(6);
    int yGyro = readEEPROMInt(8);
    int zGyro = readEEPROMInt(10);

    loginfo(F("Applying calibration"));
    mpu.setXAccelOffset(xAccel);
    mpu.setYAccelOffset(yAccel);
    mpu.setZAccelOffset(zAccel);
    mpu.setXGyroOffset(xGyro);
    mpu.setYGyroOffset(yGyro);
    mpu.setZGyroOffset(zGyro);

    char msg[32];
    loginfo(F("IMU Calibration"));
    logerror(F("XAccel: "));
    itoa(xAccel, msg, 10);
    logerror(msg);
    logerror(F("YAccel: "));
    itoa(yAccel, msg, 10);
    logerror(msg);
    logerror(F("ZAccel: "));
    itoa(zAccel, msg, 10);
    logerror(msg);
    logerror(F("XGyro: "));
    itoa(xGyro, msg, 10);
    logerror(msg);
    logerror(F("YGyro: "));
    itoa(yGyro, msg, 10);
    logerror(msg);
    logerror(F("ZGyro: "));
    itoa(zGyro, msg, 10);
    logerror(msg);

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        //Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        loginfo(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        loginfo(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        logerror(F("DMP Initialization failed (code"));
        logerror("" + devStatus);
        logerror(F(")"));
    }

    pinMode(MOTOR_SPEED_PIN, OUTPUT);
    pinMode(BATTERY_PIN, INPUT_PULLUP);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENCODER_PIN, INPUT_PULLUP);
    pinMode(SERVO_FEEDBACK_MOTOR_PIN, INPUT);
    digitalWrite(ENCODER_PIN, HIGH);             //pull up
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN), encoder, RISING);
    pixels.begin(); // This initializes the NeoPixel library.
    //Voltmeter
    pinMode(NEWLED_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);
}

float meanVoltage() {
    uint32_t sum = 0;
    for (int i = 0; i < VOLTAGE_BUFFER_SIZE; i++) {
        sum += voltageBuffer[i];
    }

    float avg = sum / (float)VOLTAGE_BUFFER_SIZE;
    float vout = (avg * referenceVolts) / 1024.0;
    return vout / (float)(R2/(R1+R2));
}

void turnOnCar() {
    if (!powered) {
        digitalWrite(ENABLE_PIN, HIGH);
    }

    powered = true;
}

void turnOffCar() {
    if (powered) {
        digitalWrite(ENABLE_PIN, LOW);
    }
    powered = false;
}

// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================

void loop() {
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    // wait for MPU interrupt or extra packet(s) available
    //while (!mpuInterrupt && fifoCount < packetSize) {}

    if (mpuInterrupt || fifoCount >= packetSize) {
        // reset interrupt flag and get INT_STATUS byte
        mpuInterrupt = false;
        mpuIntStatus = mpu.getIntStatus();

        // get current FIFO count
        fifoCount = mpu.getFIFOCount();

        // check for overflow (this should never happen unless our code is too inefficient)
        if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
            // reset so we can continue cleanly
            mpu.resetFIFO();
            // otherwise, check for DMP data ready interrupt (this should happen frequently)
        } else if (mpuIntStatus & 0x02) {
            // wait for correct available data length, should be a VERY short wait
            uint16_t fifoCount = mpu.getFIFOCount();
            if (fifoCount >= packetSize) {

                // read a packet from FIFO
                mpu.getFIFOBytes(fifoBuffer, packetSize);

                // track FIFO count here in case there is > 1 packet available
                // (this lets us immediately read more without waiting for an interrupt)
                fifoCount -= packetSize;

                uint16_t temperature = mpu.getTemperature();

                sendIMU(fifoBuffer, temperature);
                sendTicks(ticks);
                ticks = 0;
                sendSteeringAngle(analogRead(SERVO_FEEDBACK_MOTOR_PIN));

                /***Voltmeter**/
                voltageBuffer[voltageIndex++] = 1023;//;analogRead(BATTERY_PIN);
                if (voltageIndex >= VOLTAGE_BUFFER_SIZE) {
                    voltageIndex = 0;
                }

                float voltage = meanVoltage();

                if (millis() > 3000 && voltage > 12.8){
                    turnOnCar();
                }
                else {
                    turnOffCar();
                }

                if (voltage > 14.4) {
                    displayVoltageGoodLed();
                } else if (voltage <= 14.4 && voltage >= 13.0) {
                    displayVoltageWarningLed();
                } else if (voltage < 13.0) {
                    displayVoltageBadLed();
                }

                sendVoltage(voltage);
            }
        }
    }
    packetSerial.update();
}
