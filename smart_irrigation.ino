#include <ESP8266WiFiMulti.h>
#include <Wire.h>
#include <BH1750.h>
#include "DHT.h"
#include "ThingSpeak.h"

ESP8266WiFiMulti wifiMulti;
BH1750 lightMeter;


/*definiamo costanti per l'accesso al WiFi*/
#define WIFI_SSID "Inserisci SSID"
#define WIFI_PASSWORD "Inserisci Password"

ESP8266WiFiMulti multiWifi;
WiFiClient client;

/*definiamo costanti per l'accesso a ThingSpeak*/
unsigned long myTalkBackID = "Inserisci ID TALKBACK, SENZA GLI APICI";
const char * myTalkBackKey = "inserisci talkbackkey";

#define CH_ID "Inserisci canale ThingSpeak"
#define WRITE_KEY "write key thingspeak"


/*definiamo costanti per l'accesso a DHT11*/
#define DHTPIN D5
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE); /*creo l'istanza di DHT11 */


/*definisco i pin per la pompa connessa al relay e al sensore di umidita terreno */
int relaypin = D2;
const int SensorPin = A0;


/*definiamo variabili globali*/
float t = 0.0;
float h = 0.0;

float lux = 0.0;
int soilMoistureValue = 0;
int soilmoisturepercent=0;


bool isManual = false;
bool isActivate = false;



void setup() {
  
  Serial.begin(115200);
  
  pinMode(relaypin, OUTPUT);
  pinMode(SensorPin, INPUT);

  digitalWrite(relaypin, HIGH);
  
  Wire.begin(D7,D6);

  dht.begin();
  lightMeter.begin();
  
  /*Nelle librerie consigliando un delay perchè impiega tempo a inizializzarle*/
  delay(4000);

  /*setup WiFi*/
  WiFi.mode(WIFI_STA);
  multiWifi.addAP(WIFI_SSID, WIFI_PASSWORD); /*aggiungo l'accessoPoint*/

  /*iniziamo facendo la connessione*/
  Serial.println("Start connecting to wifi");
  while(multiWifi.run() != WL_CONNECTED){ /*fin tanto che non è connesso */
    Serial.print(".");
    delay(100);
  } /*possiamo bloccarci per sempre, ma se non riesce a connettersi non ci sara' utile al raccoglimento dei dati*/
  Serial.println("Connected!!");

  ThingSpeak.begin(client);

  

}

void loop() {
   // Create the TalkBack URI
  String tbURI = String("/talkbacks/") + String(myTalkBackID) + String("/commands/execute");
  
  // Create the message body for the POST out of the values
  String postMessage =  String("api_key=") + String(myTalkBackKey);                      
                       
   // Make a string for any commands that might be in the queue
  String newCommand = String();

  // Make the POST to ThingSpeak
  int x = httpPOST(tbURI, postMessage, newCommand);
  client.stop();

  isManual = false;
  isActivate = false;

  // Check the result
  if(x == 200){
    Serial.println("checking queue..."); 
    // Check for a command returned from TalkBack
    if(newCommand.length() != 0){

      Serial.print("  Latest command from queue: ");
      Serial.println(newCommand);
      
      if(newCommand.indexOf("START")>0){
        Serial.print("POMPA START");
        isManual = true; //attivo la modalità manuale
        isActivate = true;
        digitalWrite(relaypin, LOW);
      }

    }
    else{
      Serial.println("  Nothing new.");  
    }
    
  }
  else{
    Serial.println("Problem checking queue. HTTP error code " + String(x));
  }

  /*leggo i sensori */
  readSensors(isManual);

  /*scrivo su thingSpeak */
  writeToThingSpeak();
  
  if(isActivate){
    /*
    * se la pompa è attiva, inserire un tempo adeguato per il deepSleep, ovvero:
    * se si tratta di una piccola pianta allora potrebbero bastare pochi secondi per far arrivare l'umidità alla soglia desiderata
    * se si tratta di un vasto terreno potrebbero volerci anche ore prima che l'umidità arrivi alla soglia desiderata
    */
    ESP.deepSleep("Inserire un tempo adeguato");
  } else {
    /*
    * anche in questo caso dipende dal tipo di pianta / terreno in analisi ma anche dal luogo in cui è posto
    * se siamo in ambienti umidi dove l'umidità del terreno non scende velocemente allora è consigliato un tempo più lungo 
    * se siamo in ambienti asciutti con alte temperature allora è consigliato un tempo di corto
    */
    ESP.deepSleep("Inserire un tempo adeguato");
  }
}


void readSensors(bool isManual){

  //Leggo i dati del DHT11
  t = dht.readTemperature();
  Serial.print(t);
  h = dht.readHumidity();
  Serial.print(h);

  //Leggo l'intesità di luminosità
  lux = lightMeter.readLightLevel();
  Serial.print(lux);

  
  //leggo i dati  dell'umidità del terreno e la mappo in percentuale
  soilMoistureValue = 1024 - analogRead(SensorPin);
  soilmoisturepercent = map(soilMoistureValue, 0, 1024, 0, 100); 
  
  Serial.printf("Umidità Terreno: %d\n ", soilmoisturepercent);

  
  //Se siamo in modali manuale allora non controllo la percentuale di umidità del terreno
  if(!isManual){
    //siamo in modalità manuale, allora decido se accendere o spegnere la pompa in base alla percentuale di umidità del terreno
    // si consiglia prima di impostare un valore di verificare a quanto corrisponde la percentuale di terreno asciutta e la percentuale di terreno umido.
      if(soilmoisturepercent < 25){
    
      digitalWrite(relaypin, LOW);
      isActivate = true;
    } else {
      isActivate = false;
      digitalWrite(relaypin, HIGH);
    }
    
  }

 
} 


//richiesta HTTP per cercare messaggi in coda nella TalkBack di Thingspeak
int httpPOST(String uri, String postMessage, String &response){

  bool connectSuccess = false;
  connectSuccess = client.connect("api.thingspeak.com",80);

  if(!connectSuccess){
      return -301;   
  }
  
  postMessage += "&headers=false";
  
  String Headers =  String("POST ") + uri + String(" HTTP/1.1\r\n") +
                    String("Host: api.thingspeak.com\r\n") +
                    String("Content-Type: application/x-www-form-urlencoded\r\n") +
                    String("Connection: close\r\n") +
                    String("Content-Length: ") + String(postMessage.length()) +
                    String("\r\n\r\n");

  client.print(Headers);
  client.print(postMessage);

  long startWaitForResponseAt = millis();
  while(client.available() == 0 && millis() - startWaitForResponseAt < 5000){
      delay(100);
  }

  if(client.available() == 0){       
    return -304; // Didn't get server response in time
  }

  if(!client.find(const_cast<char *>("HTTP/1.1"))){
      return -303; // Couldn't parse response (didn't find HTTP/1.1)
  }
  
  int status = client.parseInt();
  if(status != 200){
    return status;
  }

  if(!client.find(const_cast<char *>("\n\r\n"))){
    return -303;
  }

  String tempString = String(client.readString());
  response = tempString;

  return status;
    
}


//scrivo i dati su ThingSpeak
void  writeToThingSpeak(){
  ThingSpeak.setField(1, soilmoisturepercent);
  ThingSpeak.setField(2, h);
  ThingSpeak.setField(3, t);
  ThingSpeak.setField(4, lux);
  ThingSpeak.setField(5, isActivate);
  

  /* scrivo i dati sul cloud via HTTP */
  int httpCode = ThingSpeak.writeFields(CH_ID, WRITE_KEY);
  
  /*controlliamo se abbiamo scritto controllando il valore di ritorno */
  if(httpCode = 200) {
    Serial.println("Server write ok!!");
  } else {
    Serial.print("Problem writing to the server!  ");
    Serial.println(httpCode);
  }
  
}
