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

#define NUMPIXELS 21

#define LED_PIN 6
#define DIR_PIN 4
#define SERVO_PIN 10
#define MOTOR_SPEED_PIN 11
#define ENCODER_PIN 3
#define INTERRUPT_PIN 2  // use pin 2 on Arduino Uno & most boards
#define SERVO_FEEDBACK_MOTOR_PIN 0
#define NEWLED_PIN 13
#define BATTERY_PIN A6
#define ENABLE_PIN 7
#define HEARTBEAT_TIMEOUT 100

#define REFERENCE_VOLTAGE 5.2 //Default reference on Teensy is 3.3V
#define R1 3300.0
#define R2 1490.0
#define VOLTAGE_BUFFER_SIZE 128

#define VOLTAGE_GOOD 14.8
#define VOLTAGE_BAD 13.0
#define VOLTAGE_SHUTDOWN 12.8

// IMU Calibration
#define iAx 0
#define iAy 1
#define iAz 2
#define iGx 3
#define iGy 4
#define iGz 5

#define usDelay 3150   // empirical, to hold sampling to 200 Hz
#define NFast 1000   // the bigger, the better (but slower)
#define NSlow 10000    // ..
#define LinesBetweenHeaders 5
int16_t LowValue[6];
int16_t HighValue[6];
int16_t Smoothed[6];
int16_t LowOffset[6];
int16_t HighOffset[6];
int16_t Target[6];
int16_t LinesOut;
int16_t N;
uint16_t ledOffset = 0;
int16_t ledIncreaseVal = 1;
uint8_t imubuf[23];

int ledState = HIGH;                // ledState used to set the LED
unsigned long previousMillis = 0;   // will store last time LED was updated
bool powered = false;

MPU6050 mpu;

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
Servo myservo; // create servo object to control a servo
int servo_pw = 1550;    // variable to set the angle of servo motor
int last_pw = 0;
bool servo_initialized = false;
volatile uint8_t ticks = 0;

int8_t direction_motor = 1;

uint16_t voltageBuffer[VOLTAGE_BUFFER_SIZE] = {0};
int voltageIndex = 0;
int16_t currentSpeed = 0;
uint8_t voltageCycle = 0;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint8_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[128]; // FIFO storage buffer

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
unsigned long lastHeartbeat = 0;

PacketSerial packetSerial;

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
    IMU,
    VOLTAGE,
    HEARTBEAT,
    IMU_CALIBRATION
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

void onLedCommand(const char* cmd_msg);
void onSteeringCommand(const int16_t cmd_msg);
void onSpeedCommand(const int16_t cmd_msg);

void dmpDataReady() {
    mpuInterrupt = true;
}

void encoder() {
    ticks++;
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
    pixels.setPixelColor(4, 255, 255, 255);
    pixels.setPixelColor(6, 255, 255, 255);
    pixels.setPixelColor(7, 255, 255, 255);
    pixels.setPixelColor(8, 255, 255, 255);

    pixels.setPixelColor(12, 0, 0, 0);
    pixels.setPixelColor(13, 0, 0, 0);
    pixels.setPixelColor(14, 0, 0, 0);
    pixels.setPixelColor(17, 0, 0, 0);
    pixels.setPixelColor(18, 0, 0, 0);
    pixels.setPixelColor(19, 0, 0, 0);

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
    pixels.setPixelColor(4, 0, 0, 0);
    pixels.setPixelColor(6, 0, 0, 0);
    pixels.setPixelColor(7, 0, 0, 0);
    pixels.setPixelColor(8, 0, 0, 0);

    pixels.setPixelColor(12, 255, 255, 255);
    pixels.setPixelColor(13, 255, 255, 255);
    pixels.setPixelColor(14, 255, 255, 255);
    pixels.setPixelColor(17, 255, 255, 255);
    pixels.setPixelColor(18, 255, 255, 255);
    pixels.setPixelColor(19, 255, 255, 255);
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
    pixels.setPixelColor(4, 0, 0, 0);
    pixels.setPixelColor(6, 0, 0, 0);
    pixels.setPixelColor(7, 0, 0, 0);
    pixels.setPixelColor(8, 0, 0, 0);

    pixels.setPixelColor(12, 0, 0, 0);
    pixels.setPixelColor(13, 0, 0, 0);
    pixels.setPixelColor(14, 0, 0, 0);
    pixels.setPixelColor(17, 0, 0, 0);
    pixels.setPixelColor(18, 0, 0, 0);
    pixels.setPixelColor(19, 0, 0, 0);

    pixels.show();

    currentDirection = Direction::NONE;
}

void displayIMUCalibrationWorkingLed(uint16_t offset) {
    pixels.clear();
    pixels.setBrightness(16);
    pixels.setPixelColor(0 + offset, 255, 255, 0);
    pixels.setPixelColor(10 - offset, 255, 255, 0);
    pixels.setPixelColor(11 + offset, 255, 255, 0);
    pixels.setPixelColor(20 - offset, 255, 255, 0);
    pixels.show();
}

void displayIMUCalibrationInitializeLed() {
    pixels.clear();
    pixels.setBrightness(16);
    for (uint8_t i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, 255, 0, 0);
    }
    pixels.show();
}

void displayIMUCalibrationSuccessLed() {
    pixels.clear();
    pixels.setBrightness(16);
    for (uint8_t i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, 255, 255, 0);
    }
    pixels.show();
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

    if (servo_val < 5) {
        servo_val = 5;
        disableDirectionLed();
    }

    OCR2A = servo_val;
}

void GetSmoothed() {
    int16_t RawValue[6];
    int i;
    long Sums[6];
    for (i = iAx; i <= iGz; i++) {
         Sums[i] = 0;
    }

    for (i = 1; i <= N; i++) {
        mpu.getMotion6(&RawValue[iAx], &RawValue[iAy], &RawValue[iAz], &RawValue[iGx], &RawValue[iGy], &RawValue[iGz]);
        delayMicroseconds(usDelay);
        for (int j = iAx; j <= iGz; j++)
            Sums[j] = Sums[j] + RawValue[j];
    }

    for (i = iAx; i <= iGz; i++) { 
        Smoothed[i] = (Sums[i] + N/2) / N ; 
    }
}

void SetOffsets(int16_t* TheOffsets) {
    mpu.setXAccelOffset(TheOffsets [iAx]);
    mpu.setYAccelOffset(TheOffsets [iAy]);
    mpu.setZAccelOffset(TheOffsets [iAz]);
    mpu.setXGyroOffset (TheOffsets [iGx]);
    mpu.setYGyroOffset (TheOffsets [iGy]);
    mpu.setZGyroOffset (TheOffsets [iGz]);
}

void ShowProgress() {
    if (LinesOut >= LinesBetweenHeaders)
        LinesOut++;

    displayIMUCalibrationWorkingLed(ledOffset);

    if (ledOffset >= 5) {
        ledIncreaseVal = -1;
    }

    if (ledOffset <= 0) {
        ledIncreaseVal = 1;
    }

    ledOffset += ledIncreaseVal;
}

void WriteEEPROMInt(int addr, int val) {
    byte low, high;
    low=val&0xFF;
    high=(val>>8)&0xFF;
    EEPROM.write(addr, low);
    EEPROM.write(addr+1, high);
    return;
}

void WriteEEPROM() {
    int addr = 0;
    // XAccel
    WriteEEPROMInt(addr, LowOffset[0]);
    addr += 2;
    // YAccel
    WriteEEPROMInt(addr, LowOffset[1]);
    addr += 2;
    // ZAccel
    WriteEEPROMInt(addr, LowOffset[2]);
    addr += 2;
    // XGyro
    WriteEEPROMInt(addr, LowOffset[3]);
    addr += 2;
    // YGyro
    WriteEEPROMInt(addr, LowOffset[4]);
    addr += 2;
    // ZGyro
    WriteEEPROMInt(addr, LowOffset[5]);
}

void SetAveraging(int16_t NewN) { 
    N = NewN;
}

void PullBracketsIn() {
    bool AllBracketsNarrow;
    bool StillWorking;
    int16_t NewOffset[6];

    AllBracketsNarrow = false;
    StillWorking = true;
    while (StillWorking) {
        StillWorking = false;
        if (AllBracketsNarrow && (N == NFast)) {
            SetAveraging(NSlow);
        } else {
            AllBracketsNarrow = true;
        }
        
        for (int i = iAx; i <= iGz; i++) {
             if (HighOffset[i] <= (LowOffset[i]+1)) {
                NewOffset[i] = LowOffset[i];
            }
            else {
                StillWorking = true;
                NewOffset[i] = (LowOffset[i] + HighOffset[i]) / 2;
                if (HighOffset[i] > (LowOffset[i] + 10)) {
                    AllBracketsNarrow = false;
                }
            }
        }
        SetOffsets(NewOffset);
        GetSmoothed();
        for (int i = iAx; i <= iGz; i++) {
            if (Smoothed[i] > Target[i]) {
                HighOffset[i] = NewOffset[i];
                HighValue[i] = Smoothed[i];
            }
            else {
                LowOffset[i] = NewOffset[i];
                LowValue[i] = Smoothed[i];
            }
        }
        ShowProgress();
    }
}

void PullBracketsOut()
{ boolean Done = false;
    int NextLowOffset[6];
    int NextHighOffset[6];

    while (!Done) { 
        Done = true;
        SetOffsets(LowOffset);
        GetSmoothed();
        for (int i = iAx; i <= iGz; i++) {
            LowValue[i] = Smoothed[i];
            if (LowValue[i] >= Target[i]) {
                Done = false;
                NextLowOffset[i] = LowOffset[i] - 1000;
            }
            else { 
                NextLowOffset[i] = LowOffset[i];
             }
        }

        SetOffsets(HighOffset);
        GetSmoothed();
        for (int i = iAx; i <= iGz; i++) {
            HighValue[i] = Smoothed[i];
            if (HighValue[i] <= Target[i]) {
                Done = false;
                NextHighOffset[i] = HighOffset[i] + 1000;
            }
            else { 
                NextHighOffset[i] = HighOffset[i];
            }
        }
        ShowProgress();
        for (int i = iAx; i <= iGz; i++) {
            LowOffset[i] = NextLowOffset[i];   // had to wait until ShowProgress done
            HighOffset[i] = NextHighOffset[i]; // ..
        }
    }
}

void onLedCommand(const char* cmd_msg) {
    pixels.setBrightness(16);
    if (strcmp_P(cmd_msg, PSTR("left")) == 0) {
        pixels.setPixelColor(0, pixels.Color(255, 80, 0)); //yellow
        pixels.setPixelColor(1, pixels.Color(255, 80, 0)); //yellow
        pixels.setPixelColor(20, pixels.Color(255, 80, 0)); //yellow
    } else if (strcmp_P(cmd_msg, PSTR("right")) == 0) {
        pixels.setPixelColor(9, pixels.Color(255, 80, 0)); //yellow
        pixels.setPixelColor(10, pixels.Color(255, 80, 0)); //yellow
        pixels.setPixelColor(11, pixels.Color(255, 80, 0)); //yellow
    } else if (strcmp_P(cmd_msg, PSTR("brake")) == 0) {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0)); //red
        pixels.setPixelColor(1, pixels.Color(255, 0, 0)); //red
        pixels.setPixelColor(9, pixels.Color(255, 0, 0)); //red
        pixels.setPixelColor(10, pixels.Color(255, 0, 0)); //red
    } else if (strcmp_P(cmd_msg, PSTR("disable")) == 0) {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.setPixelColor(1, pixels.Color(0, 0, 0));
        pixels.setPixelColor(9, pixels.Color(0, 0, 0));
        pixels.setPixelColor(10, pixels.Color(0, 0, 0));
        pixels.setPixelColor(11, pixels.Color(0, 0, 0));
        pixels.setPixelColor(20, pixels.Color(0, 0, 0));
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

    pixels.clear();
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
        case MessageType::IMU:break;
        case MessageType::VOLTAGE:break;
        case MessageType::HEARTBEAT:break;

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
        case MessageType::IMU_CALIBRATION:
            displayIMUCalibrationInitializeLed();
            for (int i = iAx; i <= iGz; i++) {
                Target[i] = 0; // must fix for ZAccel
                HighOffset[i] = 0;
                LowOffset[i] = 0;
            }
            Target[iAz] = 16384;
            SetAveraging(NFast);

            PullBracketsOut();
            PullBracketsIn();
            WriteEEPROM();
            displayIMUCalibrationSuccessLed();
            delay(1000);
            mpu.resetFIFO();
            disableLed();
            break;

        case MessageType::LED_CMD:
            char cmd[size];
            memcpy(&cmd, &message[1], size);
            onLedCommand(cmd);
            break;
    }

    lastHeartbeat = millis();
}

void log(MessageType type, const __FlashStringHelper *str) {
    uint8_t size = 1 + strlen_P((PGM_P)str) + 1;
    uint8_t buf[size];
    buf[0] = (uint8_t)type;
    strcpy_P(&buf[1], (PGM_P)str);
    packetSerial.send(buf, size);
}

void log(MessageType type, char *str) {
    uint8_t size = 1 + strlen(str) + 1;
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
    imubuf[0] = (uint8_t)MessageType::IMU;
    imubuf[1] = fifoBuffer[0];
    imubuf[2] = fifoBuffer[1];
    imubuf[3] = fifoBuffer[4];
    imubuf[4] = fifoBuffer[5];
    imubuf[5] = fifoBuffer[8];
    imubuf[6] = fifoBuffer[9];
    imubuf[7] = fifoBuffer[12];
    imubuf[8] = fifoBuffer[13];
    imubuf[9] = fifoBuffer[16];
    imubuf[10] = fifoBuffer[17];
    imubuf[11] = fifoBuffer[20];
    imubuf[12] = fifoBuffer[21];
    imubuf[13] = fifoBuffer[24];
    imubuf[14] = fifoBuffer[25];
    imubuf[15] = fifoBuffer[28];
    imubuf[16] = fifoBuffer[29];
    imubuf[17] = fifoBuffer[32];
    imubuf[18] = fifoBuffer[33];
    imubuf[19] = fifoBuffer[36];
    imubuf[20] = fifoBuffer[37];
    imubuf[21] = temperature >> 8;
    imubuf[22] = temperature & 0xFF;
    packetSerial.send(imubuf, 23);
}

void sendVoltage(float voltage) {
    uint8_t size = 1 + sizeof(voltage);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::VOLTAGE;
    memcpy(&buf[1], &voltage, sizeof(voltage));
    packetSerial.send(buf, size);
}

void sendTicks(uint8_t ticks) {
    uint8_t size = 1 + sizeof(ticks);
    uint8_t buf[size];
    buf[0] = (uint8_t)MessageType::TICKS;
    memcpy(&buf[1], &ticks, sizeof(ticks));
    packetSerial.send(buf, size);
}

void sendSteeringAngle(uint16_t steeringAngle) {
    uint8_t size = 1 + sizeof(steeringAngle);
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
    pinMode(BATTERY_PIN, INPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENCODER_PIN, INPUT);
    pinMode(SERVO_FEEDBACK_MOTOR_PIN, INPUT);
    //digitalWrite(ENCODER_PIN, HIGH);             //pull up
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN), encoder, CHANGE);
    pixels.begin(); // This initializes the NeoPixel library.
    //Voltmeter
    pinMode(NEWLED_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);

    // Set up timer2 with frequency of ~8kHz on pin 11
    TCCR2B = _BV(CS21); // fast PWM, prescaler of 8
    TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
    OCR2A = 5;
}

float meanVoltage() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < VOLTAGE_BUFFER_SIZE; i++) {
        sum += voltageBuffer[i];
    }

    float avg = sum / VOLTAGE_BUFFER_SIZE;
    float vout = (avg * REFERENCE_VOLTAGE) / 1024.0;
    return vout / (R2/(R1+R2));
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
    unsigned long ms = millis();

    if (ms - lastHeartbeat > HEARTBEAT_TIMEOUT) {
        onSpeedCommand(0);
    }

    packetSerial.update();

    if (mpuInterrupt || fifoCount >= packetSize) {
        // reset interrupt flag and get INT_STATUS byte
        mpuInterrupt = false;
        mpuIntStatus = mpu.getIntStatus();

        // get current FIFO count
        fifoCount = mpu.getFIFOCount();

        // check for overflow (this should never happen unless our code is too inefficient)
        if ((mpuIntStatus & _BV(MPU6050_INTERRUPT_FIFO_OFLOW_BIT)) || fifoCount >= 1024)  {
            // reset so we can continue cleanly
            mpu.resetFIFO();
            fifoCount = mpu.getFIFOCount();
            logerror("FIFO Overflow");
            // otherwise, check for DMP data ready interrupt (this should happen frequently)
        } else if (mpuIntStatus & _BV(MPU6050_INTERRUPT_DMP_INT_BIT)) {
            // wait for correct available data length, should be a VERY short wait
            while (fifoCount < packetSize) {
                fifoCount = mpu.getFIFOCount();
            }

            // read a packet from FIFO
            mpu.getFIFOBytes(fifoBuffer, packetSize);
            //mpu.resetFIFO();

            // track FIFO count here in case there is > 1 packet available
            // (this lets us immediately read more without waiting for an interrupt)
            fifoCount -= packetSize;

            uint16_t temperature = mpu.getTemperature();

            sendIMU(fifoBuffer, temperature);
            sendTicks(ticks);
            ticks = 0;
            uint16_t steeringAngle = analogRead(SERVO_FEEDBACK_MOTOR_PIN);
            steeringAngle = analogRead(SERVO_FEEDBACK_MOTOR_PIN);
            sendSteeringAngle((uint16_t)steeringAngle);

            /***Voltmeter**/
            voltageCycle++;
            if (voltageCycle >= 2) {
                uint16_t vol = analogRead(BATTERY_PIN);
                vol = analogRead(BATTERY_PIN);
                voltageBuffer[voltageIndex++] = vol;
                if (voltageIndex >= VOLTAGE_BUFFER_SIZE) {
                    voltageIndex = 0;
                }
                voltageCycle = 0;
            }

            float voltage = meanVoltage();

            if (ms > 2600 && voltage > VOLTAGE_SHUTDOWN){
                turnOnCar();
            } else if (voltage <= VOLTAGE_SHUTDOWN && powered) {
                // This means the car was already on and the voltage dropped below 12.8
                // Empty the voltage measurements so that the car does not turn on immediately again because the voltage went up again
                memset(voltageBuffer, 0, sizeof(voltageBuffer));
                turnOffCar();
            } else {
                turnOffCar();
            }

            if (voltage > VOLTAGE_GOOD) {
                displayVoltageGoodLed();
            } else if (voltage <= VOLTAGE_GOOD && voltage >= VOLTAGE_BAD) {
                displayVoltageWarningLed();
            } else if (voltage < VOLTAGE_BAD) {
                displayVoltageBadLed();
            }

            sendVoltage(voltage);
        }
        
    }
}


