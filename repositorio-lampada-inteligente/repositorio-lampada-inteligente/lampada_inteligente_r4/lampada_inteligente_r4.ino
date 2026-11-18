/*
  Lâmpada Inteligente - Arduino UNO R4 WiFi + Arduino IoT Cloud

  Variáveis do Thing (todas READ, geradas em thingProperties.h, NÃO edite aquele arquivo):
    CloudColor LedRGB;      -> cor que o LED RGB está mostrando
    float      Fotoresitor; -> iluminação ambiente em % (0 = escuro, 100 = muito claro)
    float      temperatura; -> temperatura do DHT22 em °C
    bool       Botao;       -> true = sistema ligado, false = sistema desligado

  Funcionamento:
    - O botão físico liga/desliga o sistema inteiro (com debounce, sem interrupção).
    - Com o sistema ligado: lê DHT22, fotoresistor e potenciômetro; o potenciômetro
      escolhe a cor do LED RGB percorrendo o círculo de cores (vermelho > amarelo >
      verde > ciano > azul > magenta > vermelho).
    - Com o sistema desligado: LED apagado, sensores param de atualizar o painel.
    - Nada usa delay() no loop: tudo é temporizado com millis() para não travar
      ArduinoCloud.update().

  Bibliotecas necessárias (Library Manager):
    - ArduinoIoTCloud (e dependências, instaladas automaticamente)
    - "DHT sensor library" da Adafruit (+ "Adafruit Unified Sensor")

  Pinagem sugerida (UNO R4 WiFi, lógica de 5 V):
    Botão ........... D2  <-> GND (usa o pull-up interno, não precisa de resistor)
    DHT22 (dados) ... D7  (resistor de 10 kΩ entre dados e 5 V se for o sensor "nu";
                           módulos de 3 pinos já têm esse resistor)
    Fotoresistor .... A0  (divisor: 5 V -> LDR -> A0 -> resistor 10 kΩ -> GND)
    Potenciômetro ... A1  (pino do meio no A1, extremidades em 5 V e GND)
    LED RGB vermelho  D9  (via resistor de 220 Ω)
    LED RGB verde ... D10 (via resistor de 220 Ω)
    LED RGB azul .... D11 (via resistor de 220 Ω)
    LED RGB comum ... GND se for catodo comum / 5 V se for anodo comum
*/

#include <DHT.h>
#include "thingProperties.h"

// ---------------- Pinos ----------------
const int BUTTON_PIN            = 2;
const int DHT_PIN               = 7;
const int PHOTORESISTOR_PIN     = A0;
const int POTENTIOMETER_PIN     = A1;
const int LED_RED_PIN           = 9;
const int LED_GREEN_PIN         = 10;
const int LED_BLUE_PIN          = 11;

// ---------------- Configurações ----------------
// Mude para true se o seu LED RGB for de ANODO comum (pino comum no 5 V).
const bool LED_IS_COMMON_ANODE = false;

// Mude para true se a porcentagem de luz estiver "ao contrário"
// (ex.: se você montou o resistor de 10 kΩ no lado do 5 V em vez do GND).
const bool INVERT_PHOTORESISTOR = false;

const int           ANALOG_RESOLUTION_BITS     = 14;     // UNO R4 lê até 14 bits (0..16383)
const float         ANALOG_MAX_VALUE           = 16383.0;
const unsigned long BUTTON_DEBOUNCE_MS         = 50;
const unsigned long DHT_READ_INTERVAL_MS       = 2000;   // DHT22 não aceita leituras mais rápidas
const unsigned long ANALOG_READ_INTERVAL_MS    = 50;
const unsigned long CLOUD_PUBLISH_INTERVAL_MS  = 1000;   // frequência máxima de envio ao painel
const unsigned long SERIAL_REPORT_INTERVAL_MS  = 2000;
const float         SMOOTHING_FACTOR           = 0.2;    // filtro das leituras analógicas (0..1)
const float         HUE_STEP_DEGREES           = 5.0;    // evita a cor "tremer" com ruído do potenciômetro
const float         LIGHT_STEP_PERCENT         = 1.0;    // arredonda a luz para 1 %
const float         TEMPERATURE_STEP_CELSIUS   = 0.1;    // arredonda a temperatura para 0,1 °C

DHT temperatureSensor(DHT_PIN, DHT22);

// ---------------- Estado ----------------
bool systemIsOn = true;

int           lastRawButtonReading    = HIGH;   // pull-up: solto = HIGH, pressionado = LOW
int           stableButtonState       = HIGH;
unsigned long lastButtonChangeTime    = 0;

float smoothedPhotoresistorReading  = 0;
float smoothedPotentiometerReading  = 0;
bool  analogFiltersInitialized      = false;

float currentHueDegrees = 0;

unsigned long lastDhtReadTime       = 0;
unsigned long lastAnalogReadTime    = 0;
unsigned long lastCloudPublishTime  = 0;
unsigned long lastSerialReportTime  = 0;

// ---------------- Utilitários ----------------
float roundToStep(float value, float step) {
  return round(value / step) * step;
}

void writeLedChannel(int pin, uint8_t intensity) {
  analogWrite(pin, LED_IS_COMMON_ANODE ? 255 - intensity : intensity);
}

void setLedColor(uint8_t redIntensity, uint8_t greenIntensity, uint8_t blueIntensity) {
  writeLedChannel(LED_RED_PIN, redIntensity);
  writeLedChannel(LED_GREEN_PIN, greenIntensity);
  writeLedChannel(LED_BLUE_PIN, blueIntensity);
}

// Converte matiz (0..360°) com saturação e brilho máximos em RGB (0..255).
void hueToRgb(float hueDegrees, uint8_t &redIntensity, uint8_t &greenIntensity, uint8_t &blueIntensity) {
  float hueSector       = fmod(hueDegrees, 360.0) / 60.0;
  float secondaryAmount = 1.0 - fabs(fmod(hueSector, 2.0) - 1.0);
  float redFraction = 0, greenFraction = 0, blueFraction = 0;

  if      (hueSector < 1) { redFraction = 1;               greenFraction = secondaryAmount; }
  else if (hueSector < 2) { redFraction = secondaryAmount; greenFraction = 1; }
  else if (hueSector < 3) { greenFraction = 1;             blueFraction = secondaryAmount; }
  else if (hueSector < 4) { greenFraction = secondaryAmount; blueFraction = 1; }
  else if (hueSector < 5) { redFraction = secondaryAmount; blueFraction = 1; }
  else                    { redFraction = 1;               blueFraction = secondaryAmount; }

  redIntensity   = (uint8_t)(redFraction * 255);
  greenIntensity = (uint8_t)(greenFraction * 255);
  blueIntensity  = (uint8_t)(blueFraction * 255);
}

// ---------------- Botão (debounce por polling) ----------------
// Retorna true uma única vez a cada aperto confirmado.
bool buttonWasPressed() {
  int rawButtonReading = digitalRead(BUTTON_PIN);
  unsigned long currentTime = millis();

  if (rawButtonReading != lastRawButtonReading) {
    lastRawButtonReading = rawButtonReading;
    lastButtonChangeTime = currentTime;
  }

  if (currentTime - lastButtonChangeTime >= BUTTON_DEBOUNCE_MS && rawButtonReading != stableButtonState) {
    stableButtonState = rawButtonReading;
    return stableButtonState == LOW;  // borda de descida = botão apertado
  }
  return false;
}

// ---------------- Liga / desliga ----------------
void applySystemState() {
  Botao = systemIsOn;

  if (systemIsOn) {
    Serial.println(">>> SISTEMA LIGADO <<<");
  } else {
    setLedColor(0, 0, 0);
    LedRGB = Color(currentHueDegrees, 0, 0);  // painel mostra o LED apagado
    Serial.println(">>> SISTEMA DESLIGADO <<<");
  }
}

// ---------------- Leituras ----------------
void readAnalogSensors() {
  float rawPhotoresistorReading = analogRead(PHOTORESISTOR_PIN);
  float rawPotentiometerReading = analogRead(POTENTIOMETER_PIN);

  if (!analogFiltersInitialized) {
    smoothedPhotoresistorReading = rawPhotoresistorReading;
    smoothedPotentiometerReading = rawPotentiometerReading;
    analogFiltersInitialized = true;
  } else {
    smoothedPhotoresistorReading += SMOOTHING_FACTOR * (rawPhotoresistorReading - smoothedPhotoresistorReading);
    smoothedPotentiometerReading += SMOOTHING_FACTOR * (rawPotentiometerReading - smoothedPotentiometerReading);
  }

  // Cor do LED segue o potenciômetro em tempo real (o painel é atualizado em publishToCloud).
  float potentiometerFraction = smoothedPotentiometerReading / ANALOG_MAX_VALUE;
  currentHueDegrees = roundToStep(potentiometerFraction * 360.0, HUE_STEP_DEGREES);
  if (currentHueDegrees >= 360.0) currentHueDegrees = 0;

  uint8_t redIntensity, greenIntensity, blueIntensity;
  hueToRgb(currentHueDegrees, redIntensity, greenIntensity, blueIntensity);
  setLedColor(redIntensity, greenIntensity, blueIntensity);
}

void readTemperature() {
  float temperatureReading = temperatureSensor.readTemperature();
  if (isnan(temperatureReading)) {
    Serial.println("[DHT22] Falha na leitura, verifique a ligação do pino de dados.");
    return;  // mantém o último valor válido no painel
  }
  temperatura = roundToStep(temperatureReading, TEMPERATURE_STEP_CELSIUS);
}

// As variáveis são ON_CHANGE, então só atribuímos valores já arredondados
// e no máximo uma vez por segundo, para não inundar a nuvem com ruído.
void publishToCloud() {
  float lightPercent = smoothedPhotoresistorReading / ANALOG_MAX_VALUE * 100.0;
  if (INVERT_PHOTORESISTOR) lightPercent = 100.0 - lightPercent;
  Fotoresitor = roundToStep(lightPercent, LIGHT_STEP_PERCENT);

  LedRGB = Color(currentHueDegrees, 100, 100);
}

void printSerialReport() {
  Serial.println("----------------------------------");
  Serial.print("Sistema: ");      Serial.println(systemIsOn ? "LIGADO" : "DESLIGADO");
  Serial.print("Temperatura: ");  Serial.print(temperatura); Serial.println(" C");
  Serial.print("Iluminacao: ");   Serial.print(Fotoresitor); Serial.println(" %");
  Serial.print("Cor (matiz): ");  Serial.print(currentHueDegrees); Serial.println(" graus");
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(9600);
  delay(1500);  // só no setup: dá tempo de abrir o Monitor Serial

  analogReadResolution(ANALOG_RESOLUTION_BITS);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  setLedColor(0, 0, 0);

  temperatureSensor.begin();

  initProperties();  // definido em thingProperties.h
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  temperatura = 0;
  Fotoresitor = 0;
  applySystemState();

  Serial.println("==================================");
  Serial.println(" LAMPADA INTELIGENTE - UNO R4 WiFi");
  Serial.println("==================================");
}

// ---------------- Loop ----------------
void loop() {
  ArduinoCloud.update();
  unsigned long currentTime = millis();

  if (buttonWasPressed()) {
    systemIsOn = !systemIsOn;
    applySystemState();
  }

  if (systemIsOn) {
    if (currentTime - lastAnalogReadTime >= ANALOG_READ_INTERVAL_MS) {
      lastAnalogReadTime = currentTime;
      readAnalogSensors();
    }

    if (currentTime - lastDhtReadTime >= DHT_READ_INTERVAL_MS) {
      lastDhtReadTime = currentTime;
      readTemperature();
    }

    if (currentTime - lastCloudPublishTime >= CLOUD_PUBLISH_INTERVAL_MS) {
      lastCloudPublishTime = currentTime;
      publishToCloud();
    }
  }

  if (currentTime - lastSerialReportTime >= SERIAL_REPORT_INTERVAL_MS) {
    lastSerialReportTime = currentTime;
    printSerialReport();
  }
}
