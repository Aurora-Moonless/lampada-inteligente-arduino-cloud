// Propriedades do Thing no Arduino IoT Cloud
// Se usar o Cloud Editor, ele gera este arquivo; aqui está pronto para o Arduino IDE.

#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "arduino_secrets.h"

const char DEVICE_LOGIN_NAME[] = SECRET_DEVICE_ID;
const char SSID[]              = SECRET_SSID;
const char PASS[]              = SECRET_OPTIONAL_PASS;
const char DEVICE_KEY[]        = SECRET_DEVICE_KEY;

// Callbacks (implementados no .ino)
void onCmdLigarChange();
void onCmdDesligarChange();
void onCmdAutoChange();
void onCmdVermelhoChange();
void onCmdAmareloChange();
void onCmdAzulChange();
void onTemperaturaAtivaChange();
void onDetectorAtivoChange();
void onBuzzerAtivoChange();

// Comandos do painel (botões de pressão / chaves)
bool cmdLigar;
bool cmdDesligar;
bool cmdAuto;
bool cmdVermelho;
bool cmdAmarelo;
bool cmdAzul;
bool temperaturaAtiva;
bool detectorAtivo;
bool buzzerAtivo;

// Dados enviados ao painel (somente leitura)
float temperatura;
float umidade;
int   luminosidade;
int   potenciometro;
bool  ledLigado;
bool  sistemaAtivo;
bool  perigo;
String corAtual;

void initProperties() {
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  ArduinoCloud.addProperty(cmdLigar,          READWRITE, ON_CHANGE, onCmdLigarChange);
  ArduinoCloud.addProperty(cmdDesligar,       READWRITE, ON_CHANGE, onCmdDesligarChange);
  ArduinoCloud.addProperty(cmdAuto,           READWRITE, ON_CHANGE, onCmdAutoChange);
  ArduinoCloud.addProperty(cmdVermelho,       READWRITE, ON_CHANGE, onCmdVermelhoChange);
  ArduinoCloud.addProperty(cmdAmarelo,        READWRITE, ON_CHANGE, onCmdAmareloChange);
  ArduinoCloud.addProperty(cmdAzul,           READWRITE, ON_CHANGE, onCmdAzulChange);
  ArduinoCloud.addProperty(temperaturaAtiva,  READWRITE, ON_CHANGE, onTemperaturaAtivaChange);
  ArduinoCloud.addProperty(detectorAtivo,     READWRITE, ON_CHANGE, onDetectorAtivoChange);
  ArduinoCloud.addProperty(buzzerAtivo,       READWRITE, ON_CHANGE, onBuzzerAtivoChange);

  ArduinoCloud.addProperty(temperatura,   READ, 5 * SECONDS, NULL);
  ArduinoCloud.addProperty(umidade,       READ, 5 * SECONDS, NULL);
  ArduinoCloud.addProperty(luminosidade,  READ, 5 * SECONDS, NULL);
  ArduinoCloud.addProperty(potenciometro, READ, 5 * SECONDS, NULL);
  ArduinoCloud.addProperty(ledLigado,     READ, ON_CHANGE,   NULL);
  ArduinoCloud.addProperty(sistemaAtivo,  READ, ON_CHANGE,   NULL);
  ArduinoCloud.addProperty(perigo,        READ, ON_CHANGE,   NULL);
  ArduinoCloud.addProperty(corAtual,      READ, ON_CHANGE,   NULL);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);
