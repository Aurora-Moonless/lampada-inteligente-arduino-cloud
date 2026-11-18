#include <Arduino.h>
#include <DHT.h>
#include "thingProperties.h"

// ---------------- Pinos ----------------
const int PIN_DHT     = 4;
const int PIN_LDR_AO  = 5;
const int PIN_LDR_DO  = 6;
const int PIN_POT     = 7;
const int PIN_BUZZER  = 10;
const int PIN_LED_B   = 11;
const int PIN_LED_G   = 12;
const int PIN_LED_R   = 13;
const int PIN_BOTAO   = 14;

DHT dht(PIN_DHT, DHT22);

// ---------------- Parâmetros ----------------
const int   LIMITE_ESCURO = 4000;  // ajuste conforme o seu LDR (veja o valor no Monitor Serial)
const float TEMP_MIN      = 0.0;
const float TEMP_MAX      = 25.0;

// ---------------- Estado ----------------
enum ModoLed { MODO_AUTO, MODO_LIGADO, MODO_DESLIGADO };
ModoLed modoLed = MODO_AUTO;

// Cor temporária (comandos Vermelho/Amarelo/Azul por 1 s)
unsigned long corTempAte = 0;
int corTemp[3] = {0, 0, 0};
String nomeCorTemp = "";

volatile bool botaoPressionado = false;
unsigned long ultimoClique = 0;

unsigned long ultimaLeituraDHT = 0;
unsigned long ultimoCiclo = 0;

void IRAM_ATTR interrupcaoBotao() {
  botaoPressionado = true;
}

// ---------------- LED ----------------
void definirCor(int r, int g, int b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}

void desligarLED() {
  definirCor(0, 0, 0);
  ledLigado = false;
  corAtual = "Desligado";
}

// Cor definida pelo potenciômetro (vermelho > laranja > amarelo > branco > turquesa > azul)
void corPeloPot(int valorPot) {
  if (valorPot < 683)       { definirCor(255, 0, 0);     corAtual = "Vermelho"; }
  else if (valorPot < 1365) { definirCor(255, 100, 0);   corAtual = "Laranja"; }
  else if (valorPot < 2048) { definirCor(255, 255, 0);   corAtual = "Amarelo"; }
  else if (valorPot < 2730) { definirCor(255, 255, 255); corAtual = "Branco"; }
  else if (valorPot < 3413) { definirCor(0, 255, 255);   corAtual = "Turquesa"; }
  else                      { definirCor(0, 0, 255);     corAtual = "Azul"; }
  ledLigado = true;
}

void iniciarCorTemporaria(int r, int g, int b, const char* nome) {
  corTemp[0] = r; corTemp[1] = g; corTemp[2] = b;
  nomeCorTemp = nome;
  corTempAte = millis() + 1000;
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1500);

  analogReadResolution(12);
  dht.begin();

  pinMode(PIN_LDR_AO, INPUT);
  pinMode(PIN_LDR_DO, INPUT);
  pinMode(PIN_POT, INPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTAO), interrupcaoBotao, RISING);

  // Valores iniciais das propriedades
  cmdLigar = cmdDesligar = cmdAuto = false;
  cmdVermelho = cmdAmarelo = cmdAzul = false;
  temperaturaAtiva = true;
  detectorAtivo = true;
  buzzerAtivo = true;
  sistemaAtivo = true;
  perigo = false;
  temperatura = 0;
  umidade = 0;
  luminosidade = 0;
  potenciometro = 0;

  desligarLED();
  noTone(PIN_BUZZER);

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  Serial.println();
  Serial.println("==================================");
  Serial.println(" SISTEMA DE LAMPADA INTELIGENTE");
  Serial.println("==================================");
}

// ---------------- Loop ----------------
void loop() {
  ArduinoCloud.update();  // mantém a conexão e sincroniza o painel (não use delay longo!)

  // Botão (interrupção externa + debounce de 250 ms)
  if (botaoPressionado) {
    botaoPressionado = false;
    if (millis() - ultimoClique > 250) {
      ultimoClique = millis();
      sistemaAtivo = !sistemaAtivo;
      if (!sistemaAtivo) {
        desligarLED();
        noTone(PIN_BUZZER);
        Serial.println(">>> SISTEMA DESLIGADO PELO BOTAO <<<");
      } else {
        Serial.println(">>> SISTEMA REATIVADO PELO BOTAO <<<");
      }
    }
  }

  // Leitura do DHT22 a cada 2 s (limite do sensor)
  if (temperaturaAtiva && millis() - ultimaLeituraDHT >= 2000) {
    ultimaLeituraDHT = millis();
    float t = dht.readTemperature(false);
    float h = dht.readHumidity();
    if (!isnan(t)) temperatura = t;
    if (!isnan(h)) umidade = h;
  }

  // Leituras analógicas
  int valorLDR = analogRead(PIN_LDR_AO);
  int valorPot = analogRead(PIN_POT);
  luminosidade = valorLDR;
  potenciometro = valorPot;

  bool ambienteEscuro = detectorAtivo && (valorLDR >= LIMITE_ESCURO);
  bool temperaturaPerigosa = temperaturaAtiva && (temperatura < TEMP_MIN || temperatura > TEMP_MAX);
  perigo = temperaturaPerigosa;

  // Atuadores
  if (!sistemaAtivo) {
    desligarLED();
    noTone(PIN_BUZZER);
  } else if (temperaturaPerigosa) {
    desligarLED();
    if (buzzerAtivo) tone(PIN_BUZZER, 1000);
    else noTone(PIN_BUZZER);
  } else {
    noTone(PIN_BUZZER);

    if (millis() < corTempAte) {
      definirCor(corTemp[0], corTemp[1], corTemp[2]);
      ledLigado = true;
      corAtual = nomeCorTemp;
    } else if (modoLed == MODO_LIGADO || (modoLed == MODO_AUTO && ambienteEscuro)) {
      corPeloPot(valorPot);
    } else {
      desligarLED();
    }
  }

  // Monitor Serial a cada 1 s
  if (millis() - ultimoCiclo >= 1000) {
    ultimoCiclo = millis();

    Serial.println("----------------------------------");
    Serial.print("LED RGB: ");        Serial.println(ledLigado ? "ON" : "OFF");
    Serial.print("Cor: ");            Serial.println(corAtual);
    Serial.print("Temperatura: ");
    if (temperaturaAtiva) { Serial.print(temperatura); Serial.println(" C"); }
    else Serial.println("DESATIVADA");
    Serial.print("Umidade: ");        Serial.print(umidade); Serial.println(" %");
    Serial.print("LDR analogico: ");  Serial.println(valorLDR);
    Serial.print("Ambiente: ");
    Serial.println(!detectorAtivo ? "DETECTOR DESATIVADO" : (ambienteEscuro ? "ESCURO" : "CLARO"));
    Serial.print("Potenciometro: ");  Serial.println(valorPot);
    Serial.print("Buzzer: ");         Serial.println(buzzerAtivo ? "ATIVO" : "DESATIVADO");
    Serial.print("Sistema: ");        Serial.println(sistemaAtivo ? "ATIVO" : "DESATIVADO");
    if (temperaturaPerigosa) Serial.println("Perigo! Desligar!");
    Serial.println("----------------------------------");
  }
}

// ---------------- Callbacks do Arduino IoT Cloud ----------------
void onCmdLigarChange() {
  if (cmdLigar) { modoLed = MODO_LIGADO; cmdLigar = false; Serial.println("[Cloud] Ligar"); }
}
void onCmdDesligarChange() {
  if (cmdDesligar) { modoLed = MODO_DESLIGADO; cmdDesligar = false; Serial.println("[Cloud] Desligar"); }
}
void onCmdAutoChange() {
  if (cmdAuto) { modoLed = MODO_AUTO; cmdAuto = false; Serial.println("[Cloud] Modo automatico"); }
}
void onCmdVermelhoChange() {
  if (cmdVermelho) { iniciarCorTemporaria(255, 0, 0, "Vermelho"); cmdVermelho = false; Serial.println("[Cloud] Vermelho 1s"); }
}
void onCmdAmareloChange() {
  if (cmdAmarelo) { iniciarCorTemporaria(255, 255, 0, "Amarelo"); cmdAmarelo = false; Serial.println("[Cloud] Amarelo 1s"); }
}
void onCmdAzulChange() {
  if (cmdAzul) { iniciarCorTemporaria(0, 0, 255, "Azul"); cmdAzul = false; Serial.println("[Cloud] Azul 1s"); }
}
void onTemperaturaAtivaChange() {
  Serial.println(temperaturaAtiva ? "[Cloud] Ativar Temperatura" : "[Cloud] Desativar Temperatura");
}
void onDetectorAtivoChange() {
  Serial.println(detectorAtivo ? "[Cloud] Ativar Detector" : "[Cloud] Desativar Detector");
}
void onBuzzerAtivoChange() {
  Serial.println(buzzerAtivo ? "[Cloud] Ativar Buzzer" : "[Cloud] Desativar Buzzer");
  if (!buzzerAtivo) noTone(PIN_BUZZER);
}
