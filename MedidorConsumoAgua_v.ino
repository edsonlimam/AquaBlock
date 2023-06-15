#include <WiFi.h>
#include <Firebase_ESP_Client.h>
// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#define WIFI_SSID "Douglas-Laptop"
#define WIFI_PASSWORD "87654321abc"

//Configurações do Firebase
#define API_KEY "AIzaSyD9SNWF_myOtm6w-AkrywTuAXGIy58z8sY"
#define USER_EMAIL "edson.matos2@senaisp.edu.br"
#define USER_PASSWORD "edson123"
#define DATABASE_URL "https://aquablock-49d08-default-rtdb.firebaseio.com/"

// Defininção dos objetos do Firebase
FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

// Path do bando de dados medications
String listenerPath = "aquabox/commands/";
String measuringRef = "aquabox/measurement/";
#define c1Sensor 25
#define modoCaptura 14
#define solenoide 4
#define reset 5
#define ledAlarme 13

double flow; //Liters of passing water volume
unsigned long pulse_freq;
unsigned long lastTime;
unsigned long lastTimeAlarme;
double consumoPadrao[6];
double consumoReal[6];
double desvios[6];
int contador = 0;
double fluxoHi = 30;
double fluxoHiHi = 80;
double desvio = 1;
bool ultimoEstadoAlarme = false;
int tempoDeVarredura = 10000;
int tempoTemporizador = 0;
bool alarme = false;
bool inicio = true;
bool estadoAlarmeEnviado = false;
bool estadoSolenoideEnviado = false;

void IRAM_ATTR pulse ()
{
  pulse_freq++;
}

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void getDatabaseData() {
  if(inicio){
    for (int i = 0; i < 6; i++) {
      String ref = String(measuringRef + String(i));
      Firebase.RTDB.getJSON(&stream, ref);
      FirebaseJson json = stream.jsonObject();
      size_t count = json.iteratorBegin();
      for (size_t j = 0; j < count; j++){
        FirebaseJson::IteratorValue value = json.valueAt(j);
        if(value.key.equals("pattern")){
          consumoPadrao[i] = value.value.toDouble();
        }
      }
      json.iteratorEnd(); // required for free the used memory in iteration (node data collection)
    }
    inicio = false;
  }
  
}

void setFirebasePatternValue(double value, int position){
  if(  Firebase.RTDB.setDouble(&fbdo, String(measuringRef + String(position) + "/pattern"), value)){
    Serial.print("Enviado pattern value");
  }else {
    Serial.println("FAILED Pattern value");
    Serial.println("REASON: " + fbdo.errorReason());
  }
  delay(100);
}

void setFirebaseRealTimeValue(double value, int position){
  if(Firebase.RTDB.setDouble(&fbdo, String(measuringRef + String(position) + "/realtime"), value)){
    Serial.print("Enviado RealTime value");
  }else {
    Serial.println("FAILED Real time value");
    Serial.println("REASON: " + fbdo.errorReason());
  }
  delay(100);
}

void setFirebaseAlarmStatus(bool status){
  if(!estadoAlarmeEnviado){
    String alarmRef = "aquabox/commands/alarm";
    if( Firebase.RTDB.setBool(&fbdo, alarmRef, status)){
      Serial.print("Enviado alarmStatus");
    }else {
      Serial.println("FAILED Alarm status");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  delay(100);
  estadoAlarmeEnviado = true;
  }
}

void setFirebaseSolenoidStatus(bool status){
  if(!estadoSolenoideEnviado){
    String solenoidRef = "aquabox/commands/solenoid";
    if(Firebase.RTDB.setBool(&fbdo, solenoidRef, status)){
      Serial.print("Enviado solenóide status");
    }else {
      Serial.println("FAILED Solenóide status");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  delay(100);
  estadoSolenoideEnviado = true;
  }
}

void streamCallback(FirebaseStream data){
  getDatabaseData();
  if(data.dataPath().equals("/alarm")){
    if(!data.boolData()){
      digitalWrite(ledAlarme, LOW);
      tempoTemporizador = 0;
      alarme = false;
      estadoAlarmeEnviado = false;
    }
  }
   if(data.dataPath().equals("/solenoid")){
    if(!data.boolData()){
      digitalWrite(solenoide, HIGH);
      digitalWrite(ledAlarme, LOW);
      tempoTemporizador = 0;
      alarme = false;
      estadoAlarmeEnviado = false;
      estadoSolenoideEnviado = false;
    }else {
      digitalWrite(solenoide, LOW);
    }
  }
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());
}

// Função de timeoutCalback do Firebase
void streamTimeoutCallback(bool timeout){
  if (timeout)
    Serial.println("stream timeout, resuming...\n");
  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}

void setup()
{
  Serial.begin(115200);
  initWiFi();
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  config.max_token_generation_retry = 5;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
  if (!Firebase.RTDB.beginStream(&stream, listenerPath.c_str()))
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  pinMode(c1Sensor, INPUT);
  pinMode(modoCaptura, INPUT_PULLUP);
  pinMode(reset, INPUT_PULLUP);
  pinMode(solenoide, OUTPUT);
  pinMode(ledAlarme, OUTPUT);
  attachInterrupt(c1Sensor, pulse, FALLING); // Setup Interrupt
  lastTime = millis();
  lastTimeAlarme = millis();
}

void loop ()
{
   if (Firebase.isTokenExpired()){
    Firebase.refreshToken(&config);
    Serial.println("Refresh token");
  }
  // Reset do sistema
  if(!digitalRead(reset)){
    digitalWrite(solenoide, HIGH);
    digitalWrite(ledAlarme, LOW);
    tempoTemporizador = 0;
    alarme = false;
    setFirebaseAlarmStatus(false);
    setFirebaseSolenoidStatus(false);
    estadoSolenoideEnviado = false;
    estadoAlarmeEnviado = false;
  }
  if(alarme){
    alarmar();
  }
  if(tempoTemporizador > 15){
    digitalWrite(solenoide, LOW);
    setFirebaseSolenoidStatus(true);
  }
  if((millis() - lastTime) > tempoDeVarredura){
    flow = 2.25 * pulse_freq;
    if(!digitalRead(modoCaptura)) {
      consumoPadrao[contador] = flow;
      setFirebasePatternValue(flow, contador);
    }else {
      setFirebaseRealTimeValue(flow, contador);
      comparar();
      consumoReal[contador] = flow;
    }
    pulse_freq = 0;
    lastTime = millis();
    printSerial();
    contador++;
    if(contador > 5){
      contador = 0;
    }
  }
}

void printSerial() {
  Serial.print("Fluxo atual: ");
  Serial.print(flow, DEC);
  Serial.println("L");
  Serial.print("Contador: ");
  Serial.println(contador);
  Serial.print("Consumo Padrão:   ");
  Serial.print("Consumo Real:            ");
  Serial.println("Desvios:        ");
  Serial.print(consumoPadrao[0]);
  Serial.print("                    ");
  Serial.print(consumoReal[0]);
  Serial.print("                    ");
  Serial.println(desvios[0]);
  Serial.print(consumoPadrao[1]);
  Serial.print("                    ");
  Serial.print(consumoReal[1]);
  Serial.print("                    ");
  Serial.println(desvios[1]);
  Serial.print(consumoPadrao[2]);
  Serial.print("                    ");
  Serial.print(consumoReal[2]);
  Serial.print("                    ");
  Serial.println(desvios[2]);
  Serial.print(consumoPadrao[3]);
  Serial.print("                    ");
  Serial.print(consumoReal[3]);
  Serial.print("                    ");
  Serial.println(desvios[3]);
  Serial.print(consumoPadrao[4]);
  Serial.print("                    ");
  Serial.print(consumoReal[4]);
  Serial.print("                    ");
  Serial.println(desvios[4]);
  Serial.print(consumoPadrao[5]);
  Serial.print("                    ");
  Serial.print(consumoReal[5]);
  Serial.print("                    ");
  Serial.println(desvios[5]);
  Serial.println("========================");
}

void comparar(){
  desvio = (flow/consumoPadrao[contador])*100;
  desvios[contador] = desvio;
  if(desvio > fluxoHi + 100){
    alarme = true;
    setFirebaseAlarmStatus(true);
  }
  if(desvio > fluxoHiHi + 100){
    digitalWrite(solenoide, LOW);
    setFirebaseSolenoidStatus(true);
  }
}

void alarmar() {
  if(millis() - lastTimeAlarme > 1000) {
    ultimoEstadoAlarme = !ultimoEstadoAlarme;
    lastTimeAlarme = millis();
    tempoTemporizador = tempoTemporizador + 1;
  }
  if(ultimoEstadoAlarme){
    digitalWrite(ledAlarme, HIGH);
  }else {
    digitalWrite(ledAlarme, LOW);
  }
}
