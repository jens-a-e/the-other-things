#include <RTClib.h>

#include <Adafruit_APDS9960.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_LSM6DS33.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_Sensor.h>
#include <PDM.h>

#include <SPI.h>
#include <SD.h>

const int chipSelect = 10; // 10 for the logger SD card on the Feather
File db;

RTC_PCF8523 rtc;

Adafruit_APDS9960 apds9960; // proximity, light, color, gesture
Adafruit_BMP280 bmp280;     // temperautre, barometric pressure
Adafruit_LIS3MDL lis3mdl;   // magnetometer
Adafruit_LSM6DS33 lsm6ds33; // accelerometer, gyroscope
Adafruit_SHT31 sht30;       // humidity

uint8_t proximity;
uint16_t r, g, b, c;
float temperature, pressure, altitude;
float magnetic_x, magnetic_y, magnetic_z;
float accel_x, accel_y, accel_z;
int steps;
float gyro_x, gyro_y, gyro_z;
float humidity;
int32_t mic;

extern PDMClass PDM;
short sampleBuffer[256];  // buffer to read samples into, each sample is 16-bits
volatile int samplesRead; // number of samples read

int32_t getPDMwave(int32_t samples);

void flash() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);  
}

void setup(void) {
  Serial.begin(115200);
  // while (!Serial) delay(10);
  Serial.println("Feather Sense Sensor Demo");

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  rtc.start();

  Serial.print("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    while (1);
  }

  // initialize the sensors
  apds9960.begin();
  apds9960.enableProximity(true);
  apds9960.enableColor(true);
  bmp280.begin();
  lis3mdl.begin_I2C();
  lsm6ds33.begin_I2C();
  lsm6ds33.enablePedometer(true); // Magic happends here :)

  sht30.begin();
  PDM.onReceive(onPDMdata);
  PDM.begin(1, 16000);

  db = SD.open("datalog.txt", FILE_WRITE);
  if (db) {
    db.println("# New record " + date_fmt());
    db.flush();
  }
}

void loop(void) {
  proximity = apds9960.readProximity();
  while (!apds9960.colorDataReady()) {
    delay(5);
  }
  apds9960.getColorData(&r, &g, &b, &c);

  temperature = bmp280.readTemperature();
  pressure = bmp280.readPressure();
  altitude = bmp280.readAltitude(1013.25);

  lis3mdl.read();
  magnetic_x = lis3mdl.x;
  magnetic_y = lis3mdl.y;
  magnetic_z = lis3mdl.z;

  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;
  lsm6ds33.getEvent(&accel, &gyro, &temp);
  accel_x = accel.acceleration.x;
  accel_y = accel.acceleration.y;
  accel_z = accel.acceleration.z;
  steps = lsm6ds33.readPedometer();
  gyro_x = gyro.gyro.x;
  gyro_y = gyro.gyro.y;
  gyro_z = gyro.gyro.z;

  humidity = sht30.readHumidity();

  samplesRead = 0;
  mic = getPDMwave(4000);

  // Log the data to the CSV file

  Serial.println("\nFeather Sense Sensor Demo" + date_fmt());
  Serial.println("---------------------------------------------");
  
  db = SD.open("datalog.txt", FILE_WRITE);

  String date =
  date_fmt()
  // Serial.print("Proximity: ");
  + String(apds9960.readProximity())
  + ","
  // Serial.print("Red: ");
  + String(r)
  + ","
  // Serial.print(" Green: ");
  + String(g)
  + ","
  // Serial.print(" Blue :");
  + String(b)
  + ","
  // Serial.print(" Clear: ");
  + String(c)
  + ","
  // Serial.print("Temperature: ");
  + String(temperature)
  + ","
  // Serial.println(" C");
  // Serial.print("Barometric pressure: ");
  + String(pressure)
  + ","
  // Serial.print("Altitude: ");
  + String(altitude)
  + ","
  // Serial.println(" m");
  // Serial.print("Magnetic: ");
  + String(magnetic_x)
  + ","
  // Serial.print(" ");
  + String(magnetic_y)
  + ","
  // Serial.print(" ");
  + String(magnetic_z)
  + ","
  // Serial.println(" uTesla");
  // Serial.print("Acceleration: ");
  + String(accel_x)
  + ","
  // Serial.print(" ");
  + String(accel_y)
  + ","
  // Serial.print(" ");
  + String(accel_z)
  + ","
  // Serial.println(" m/s^2");
  // Serial.print("Gyro: ");
  + String(gyro_x)
  + ","
  // Serial.print(" ");
  + String(gyro_y)
  + ","
  // Serial.print(" ");
  + String(gyro_z)
  + ","
  // Serial.println(" dps");
  // Serial.print("Humidity: ");
  + String(humidity)
  + ","
  // Serial.println(" %");
  // Serial.print("Mic: ");
  + String(mic);
  db.println(date);
  db.flush(); //; make it sure it is written to disk
  flash();
  delay(1000);
}

String date_fmt() {
  return rtc.now().timestamp(DateTime::TIMESTAMP_FULL);
}

/*****************************************************************/
int32_t getPDMwave(int32_t samples) {
  short minwave = 30000;
  short maxwave = -30000;

  while (samples > 0) {
    if (!samplesRead) {
      yield();
      continue;
    }
    for (int i = 0; i < samplesRead; i++) {
      minwave = min(sampleBuffer[i], minwave);
      maxwave = max(sampleBuffer[i], maxwave);
      samples--;
    }
    // clear the read count
    samplesRead = 0;
  }
  return maxwave - minwave;
}

void onPDMdata() {
  // query the number of bytes available
  int bytesAvailable = PDM.available();

  // read into the sample buffer
  PDM.read(sampleBuffer, bytesAvailable);

  // 16-bit, 2 bytes per sample
  samplesRead = bytesAvailable / 2;
}