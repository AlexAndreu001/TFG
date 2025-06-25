// Llibreries necessàries per al radar i comunicació
#include <60ghzbreathheart.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Pins UART per a la connexió amb el radar
#define RX 16
#define TX 17

// Dades de connexió WiFi
const char* ssid = "";  
const char* password = ""; 

// Configuració d'InfluxDB per enviar les dades recollides
#define INFLUXDB_URL ""
#define INFLUXDB_TOKEN ""  
#define RADAR_ID 1
#define USER_ID 1

// Constants per als filtres exponencials
#define ALPHA 0.9
#define ALPHA2 0.2

// Temps d'espera inicial després d'encendre el radar
unsigned long initDelay = 30000; 
unsigned long startTime;

// Detecció Apnees
// Variables de control d'apnees
bool apneaPossible = false;
bool apneaDetected = false;
bool descensDetected = false;
bool senyalBaixaContinua = false;
bool puntMenor1_5 = false;

// Variables de temps per controlar l'inici de diferents fases de l'apnea
unsigned long apneaStartTime = 0;
unsigned long lastApneaTime = 0;
unsigned long descensStart = 0;
unsigned long tempsBaixInici = 0;

// Llindars per a la detecció d'apnees
const float llindar_apnea = -1;
const float llindar_apnea2 = 0;
const float llindar_descens = 3;

float signalAnterior = 0;
int comptadorRecuperacio = 0;

// Durada mínima i màxima d'una apnea i el temps mínim entre apnees
const int maxApneaDuration = 31000; 
const int minApneaDuration = 10000;
const int timeBetweenApneas = 120000;
const int tempsBaixMinim = 3000; 
const int recuperacioSostinguda = 3;

// Detecció distancia i control
bool distancia = false;
float llindar_distancia= 0.12;
float ultimadistancia = 0;

// Controla el temps entre canvis de posició
unsigned long ultimcanvideposicio = 0;

// Detecció moviments i control de bloqueig per moviment
bool bloqueigmoviment = false;
bool movimentfort = true;
unsigned long tempsmoviment = 0;
unsigned long tempsbloqueig = 0;

// Control missatge
bool missatgeBloqueigMostrat = false;

// Detecció hiperventilació
const float signalfast = 30;

// Valors filtrats del senyal de respiració
float rm1 = 0, rm2 = 0, rm3 = 0;

// Prefix comú per a les dades de InfluxDB
String lineProtocolPrefix = "radar_data,radar_id=" + String(RADAR_ID) + ",user_id=" + String(USER_ID);

// Inicialització del radar
BreathHeart_60GHz radar = BreathHeart_60GHz(&Serial2);

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX, TX);
  startTime = millis();
  
   // Connexió a la xarxa WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");
}
// Funció per enviar dades a InfluxDB
void enviarDadesAInfluxDB(String lineProtocol) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(INFLUXDB_URL);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("Authorization", "Token " + String(INFLUXDB_TOKEN));

    int httpResponseCode = http.POST(lineProtocol);

    if (httpResponseCode > 0) {
      Serial.print("Dades enviades a InfluxDB. Codi resposta: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Error enviant dades: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("Error: No hi ha connexió WiFi");
  }
}
void loop() {
  // Espera inicial de 30s abans de començar la lectura de dades
  if (millis() - startTime < initDelay) {
    return; 
  }
  String lineProtocol = lineProtocolPrefix;

  // Lectura de distancia o moviment
  radar.HumanExis_Func();

  if (radar.sensor_report != 0x00) {
    switch(radar.sensor_report){

      case DISVAL:
        // Detecta canvis de distància
        Serial.print("Persona detectada a una distància de: ");
        Serial.print(radar.distance);
        Serial.println(" cm");
        Serial.println("----------------------------");
        lineProtocol += " distancia=" + String(radar.distance);

        if(abs(radar.distance - ultimadistancia) >= llindar_distancia){
          distancia = true;
        }
        else{
          distancia = false;
        }
        ultimadistancia = radar.distance;
        break;

      // Detecta moviment 
      case BODYVAL:
        Serial.print("Moviment de l'usuari: ");
        Serial.print(radar.bodysign_val);
        Serial.println("----------------------------");
        lineProtocol += " moviment=" + String(radar.bodysign_val);

        if(radar.bodysign_val > 15){
          lineProtocol += ",moviment_detected=1";
        }
        // S'estableix el bloqueig segons la intensitat del moviment
        if(!bloqueigmoviment || millis() - tempsmoviment > tempsbloqueig){

          if(radar.bodysign_val > 25){
          Serial.println("Moviment detectat (25)");
          movimentfort = true;
          tempsbloqueig = 300000; // 5 minuts
          bloqueigmoviment = true;
          tempsmoviment = millis();
          }
          else if (radar.bodysign_val > 15){
            Serial.println("Moviment detectat (15)");
            tempsbloqueig = 150000; // 2.5 minuts
            bloqueigmoviment = true;
            tempsmoviment = millis();
            }
          else if (radar.bodysign_val > 5){
            Serial.println("Moviment detectat (5)");
            tempsbloqueig = 30000;  // 30 segons
            bloqueigmoviment = true;
            tempsmoviment = millis();
            }
          else{
            bloqueigmoviment = false;
            movimentfort = false;
              }
        }
        break;
    }
  }

  // Contem el número de canvis de posicio
  if(distancia && movimentfort && (millis() - ultimcanvideposicio > 60000)){
    Serial.println("Canvi de posició");
    lineProtocol += ",canvi_posicio=1";
    ultimcanvideposicio = millis();
  }


  
  // Si hi ha bloqueig per moviment, saltem a l'inici
  if(bloqueigmoviment && (millis() - tempsmoviment < tempsbloqueig)){
    if (!missatgeBloqueigMostrat) {
    Serial.println("Bloqueig per moviment");
    missatgeBloqueigMostrat = true;
    }
    if (lineProtocol != lineProtocolPrefix) { 
    enviarDadesAInfluxDB(lineProtocol); // Només enviem dades de moviment/distància
     }
    return;
  }
    else{
      missatgeBloqueigMostrat = false;
    }

  // Lectura de dades respiratòries i cardíaques
  radar.Breath_Heart();

  if(radar.sensor_report != 0x00){
    switch(radar.sensor_report){
      // Lectura freqüència cardíaca
      case HEARTRATEVAL:
      if (radar.heart_rate != 0){
        Serial.print("Freqüència cardíaca: ");
        Serial.println(radar.heart_rate, DEC);
        Serial.println("----------------------------");
        // Aplicació del filtre IIR
        rm1 = (rm1 * ALPHA) + ((1 - ALPHA) * radar.heart_rate);
        // Afegim les dades al lineProtocol per enviar-les a InfluxDB
        lineProtocol += " heart_rate=" + String(radar.heart_rate);
        lineProtocol += ",heart_filtered=" + String(rm1);
      }
      else{
        Serial.println("Valor erroni");
      }
        break;
      // Lectura freqüència respiratòria
      case BREATHVAL:
      if(radar.breath_rate != 0){
        Serial.print("Freqüència respiratòria: ");
        Serial.println(radar.breath_rate, DEC);
        Serial.println("----------------------------");
        
        // Aplicació dels filtres IIR
        rm2 = (rm2 * ALPHA) + ((1 - ALPHA) * radar.breath_rate);
        rm3 = (rm3 * ALPHA2) + ((1 - ALPHA2) * radar.breath_rate);  
        float signal=(rm3 - rm2);
        
        // Afegim les dades al lineProtocol per enviar-les a InfluxDB
        lineProtocol += " breath_rate=" + String(radar.breath_rate);
        lineProtocol += ",breath_filtered=" + String(rm2);
        lineProtocol += ",breath_filtered2=" + String(rm3);
        lineProtocol += ",breath_signal=" + String(signal);

      unsigned long time = millis();

      // Lógica per detectar un descens sobtat de la respiració
      if ((signalAnterior-signal) >= llindar_descens && !descensDetected && !apneaDetected){
          descensDetected = true;
          descensStart = time;
          puntMenor1_5 = false;
        }

      // Busquem algun valor més petit o igual a -1.5
      if (signal <= -1.5) {
        puntMenor1_5 = true;
      }

      // Si es manté baix després de 3s, iniciem possible apnea 
      if(descensDetected && (time - descensStart >= 3000)){
        if (!apneaPossible && (time - lastApneaTime > timeBetweenApneas) && puntMenor1_5) {
            apneaPossible = true;
            apneaStartTime = descensStart;
            comptadorRecuperacio = 0;
            puntMenor1_5 = false;
        }
        descensDetected = false;
      }

       // Lógica per detectar un descens per sota d'un llindar
      if (!apneaPossible && !apneaDetected && (time - lastApneaTime > timeBetweenApneas)) {
        if(signal < llindar_apnea){
           if (!senyalBaixaContinua) {
            senyalBaixaContinua = true;
            tempsBaixInici = time;
            puntMenor1_5 = false;
          } else if (time - tempsBaixInici >= tempsBaixMinim && puntMenor1_5) {
            apneaPossible = true;
            apneaStartTime = tempsBaixInici;
            comptadorRecuperacio = 0;
            puntMenor1_5 = false;
          }
        } else {
          senyalBaixaContinua = false;
          puntMenor1_5 = false;
          }
      }
      
      //Validació final
      // Si la senyal millora, considerem apnea acabada
       if (apneaPossible) {
          if (signal > llindar_apnea2) {
            comptadorRecuperacio++;
          } else {
            comptadorRecuperacio = 0;
          }

          if (comptadorRecuperacio >= recuperacioSostinguda) {
            unsigned long apneaDuration = time - apneaStartTime;
          
            if (apneaDuration >= minApneaDuration && apneaDuration <= maxApneaDuration) {
              Serial.println("Apnea detectada");
              lineProtocol += ",apnea_detected=1";
              lastApneaTime = time;
              apneaDetected = true;
            } else {
              Serial.println("Falsa apnea detectada");
            }
            apneaPossible = false;
            puntMenor1_5 = false;
          }
        }

         // Sortim de l'estat d'apnea si el senyal puja per sobre del llindar
        if (apneaDetected && signal > 0.5) {
            apneaDetected = false;
        }
        signalAnterior = signal;

      // Detecció de hiperventilació
      if (radar.breath_rate >= signalfast) {
        Serial.println("Hiperventilació detectada");
        lineProtocol += ",fastbreath_detected=1";
        }
      }
        break;
    }
  }
  // Enviar dades si s'han recollit noves mesures
   if (lineProtocol != lineProtocolPrefix) { 
    enviarDadesAInfluxDB(lineProtocol);
     }
  delay(200);
  }

