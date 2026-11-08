#include <Wire.h>
#include <math.h>

const uint8_t MPU = 0x68;
const uint32_t PERIOD_US = 20000; // 50 Hz
uint32_t lastSample = 0;
float pitch = 0, roll = 0, yaw = 0;
float biasX = 0, biasY = 0, biasZ = 0;
bool initialized = false;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool readMotion(int16_t &ax, int16_t &ay, int16_t &az,
                int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU, (uint8_t)14) != 14) return false;
  ax = (int16_t)((Wire.read() << 8) | Wire.read());
  ay = (int16_t)((Wire.read() << 8) | Wire.read());
  az = (int16_t)((Wire.read() << 8) | Wire.read());
  Wire.read(); Wire.read(); // temperatura
  gx = (int16_t)((Wire.read() << 8) | Wire.read());
  gy = (int16_t)((Wire.read() << 8) | Wire.read());
  gz = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

void calibrateGyro() {
  // Mantener completamente quieto durante esta fase.
  float sx=0, sy=0, sz=0;
  int count=0;
  for (int i=0; i<300; i++) {
    int16_t ax,ay,az,gx,gy,gz;
    if (readMotion(ax,ay,az,gx,gy,gz)) {
      sx += gx / 131.0f;
      sy += gy / 131.0f;
      sz += gz / 131.0f;
      count++;
    }
    delay(5);
  }
  if (count) { biasX=sx/count; biasY=sy/count; biasZ=sz/count; }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(100000);
  delay(250);
  if (!writeReg(0x6B, 0x01) || // reloj PLL, despertar
      !writeReg(0x1A, 0x03) || // filtro digital
      !writeReg(0x1B, 0x00) || // giroscopio +/-250 deg/s
      !writeReg(0x1C, 0x00)) { // acelerometro +/-2g
    Serial.println("ERROR: I2C");
    return;
  }
  Serial.println("CALIBRANDO: NO MOVER SENSOR");
  calibrateGyro();
  Serial.println("LISTO");
  lastSample = micros();
}

void loop() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastSample) < PERIOD_US) return;
  float dt = (uint32_t)(now - lastSample) * 1e-6f;
  lastSample = now;
  if (dt > 0.1f) dt = 0.02f;

  int16_t ax,ay,az,gx,gy,gz;
  if (!readMotion(ax,ay,az,gx,gy,gz)) return;
  float x = ax/16384.0f, y=ay/16384.0f, z=az/16384.0f;
  float wx=gx/131.0f-biasX, wy=gy/131.0f-biasY, wz=gz/131.0f-biasZ;
  float pAcc=atan2f(-x, sqrtf(y*y+z*z))*180.0f/PI;
  float rAcc=atan2f(y,z)*180.0f/PI;
  if (!initialized) { pitch=pAcc; roll=rAcc; initialized=true; }
  // Convencion inicial: sensor horizontal, cara superior hacia arriba.
  // El mapeo de ejes puede necesitar ajustes segun montaje.
  float pPred=pitch+wy*dt;
  float rPred=roll+wx*dt;
  float mag=sqrtf(x*x+y*y+z*z);
  if (mag>0.85f && mag<1.15f && fabsf(z)>0.25f) {
    pitch=0.98f*pPred+0.02f*pAcc;
    // Diferencia angular corta para evitar saltos al cruzar +/-180.
    float delta=remainderf(rAcc-rPred,360.0f);
    roll=rPred+0.02f*delta;
  } else { pitch=pPred; roll=rPred; }
  yaw+=wz*dt; // deriva inevitable sin referencia absoluta
  Serial.print("DATA,");
  Serial.print(pitch,2); Serial.print(',');
  Serial.print(roll,2); Serial.print(',');
  Serial.println(yaw,2);
}
