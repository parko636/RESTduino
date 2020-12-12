/*
 RESTduino
 
 A REST-style interface to the Arduino via the 
 Wiznet Ethernet shield.
 
 Based on David A. Mellis's "Web Server" ethernet
 shield example sketch.
 
 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 
 created 04/12/2011
 by Jason J. Gullickson
 
 added 10/16/2011
 by Edward M. Goldberg - Optional Debug flag

 added 2020-12-11
 by Alex Parkinson - Randomise MAC address on first run and save to EEPROM
                   - Refactor URL parsing logic
                   - Allow querying PIN without setting mode to input to preserve state
                   - Add mode to JSON output
 
 */

#define DEBUG false
#define STATICIP false

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <EEPROM.h>

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[6] = { 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00 };
#if DEBUG
char macstr[18];
#endif

#if STATICIP
byte ip[] = {10,0,1,100};
#endif

int pins[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// Initialize the Ethernet server library
// with the IP address and port you want to use 
// (port 80 is default for HTTP):
#if defined(ARDUINO) && ARDUINO >= 100
EthernetServer server(80);
#else
Server server(80);
#endif

void setup()
{
#if DEBUG
  //  turn on serial (for debuggin)
  Serial.begin(115200);
#endif

  // Random MAC address stored in EEPROM
  if (EEPROM.read(1) == '#') {
    for (int i = 2; i < 6; i++) {
      mac[i] = EEPROM.read(i);
    }
  } else {
    randomSeed(analogRead(0));
    for (int i = 2; i < 6; i++) {
      mac[i] = random(0, 255);
      EEPROM.write(i, mac[i]);
    }
    EEPROM.write(1, '#');
  }

#if DEBUG
  snprintf(macstr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MAC: ");
  Serial.println(macstr);
#endif

  // start the Ethernet connection and the server:
#if STATICIP
  Ethernet.begin(mac, ip);
#else
  if (Ethernet.begin(mac) == 0) {
#if DEBUG
    Serial.println("Unable to set server IP address using DHCP");
#endif
    for(;;)
      ;
  }
#if DEBUG
  // report the dhcp IP address:
  Serial.println(Ethernet.localIP());
#endif
#endif
  server.begin();
  
  EthernetBonjour.begin("restduino");
}

//  url buffer size
#define BUFSIZE 255

// Toggle case sensitivity
#define CASESENSE true

void loop()
{
  // needed to continue Bonjour/Zeroconf name registration
  EthernetBonjour.run();
  
  char clientline[BUFSIZE];
  int index = 0;
  int startPin = 0;
  int startVal = 0;
  bool done = false;
  char pin[2] = "";
  char value[4] = "";
  // listen for incoming clients
#if defined(ARDUINO) && ARDUINO >= 100
  EthernetClient client = server.available();
#else
  Client client = server.available();
#endif
  if (client) {

    //  reset input buffer
    index = 0;
    startPin = 0;
    startVal = 0;
    done = false;
    pin[0] = NULL;
    pin[1] = NULL;
    value[0] = NULL;
    value[1] = NULL;
    value[2] = NULL;
    value[3] = NULL;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        //  fill url the buffer
        if(c != '\n' && c != '\r' && index < BUFSIZE){ // Reads until either an eol character is reached or the buffer is full
          clientline[index++] = c;
          if (!done) {
#if DEBUG
        Serial.print(c);
#endif        
            if (startVal) {
              if (c == '/' || c == ' ') {
                done = true;
              } else {
                if (index - startVal - 2 < 4) {                  
                  value[index - startVal - 2] = c;
                }
              }
            } else if (startPin) {
              if (c == '/') {
                  startVal = index - 1;
              } else if (c == ' ') {
                done = true;
              } else {
                if (index - startPin - 2 < 2) {
                  pin[index - startPin - 2] = c;
                }
              }
            } else {
              if (c == '/') {
                  startPin = index - 1;
              }
            }
          }
          continue;
        }  

        // Flush any remaining bytes from the client buffer
        client.flush();

#if DEBUG
        Serial.print("\nPin: ");
        Serial.println(pin);
        Serial.print("Value: ");
        Serial.println(value);
#endif

        //  this is where we actually *do something*!
        char outValue[10] = "MU";

        if(pin[0] != NULL){
          //  select the pin
            int selectedPin = 0;
            if (pin[0] == 'a' || pin[0] == 'A') {
              selectedPin = pin[1] - '0' + A0;
            } else if (pin[0] == 'd' || pin[0] == 'D') {
              selectedPin = pin[1] - '0';
            } else {
              client.println("HTTP/1.1 400 Bad Request");
              client.println("Content-Type: text/html");
              client.println("Access-Control-Allow-Origin: *");
              client.println();
              client.println("Invalid pin");
              break;
            }
            
          if(value[0] != NULL) {

#if DEBUG
            //  set the pin value
            Serial.println("setting pin");
            Serial.println(selectedPin);
#endif

            //  set the pin for output
            pinMode(selectedPin, OUTPUT);

            //  determine digital or analog (PWM)
            if(strncmp(value, "HIGH", 4) == 0 || strncmp(value, "LOW", 3) == 0){

#if DEBUG
              //  digital
              Serial.println("digital");
#endif

              if(strncmp(value, "HIGH", 4) == 0){
#if DEBUG
                Serial.println("HIGH");
#endif
                digitalWrite(selectedPin, HIGH);
                pins[selectedPin] = 1;
                sprintf(outValue,"%s","HIGH");
              }

              if(strncmp(value, "LOW", 3) == 0){
#if DEBUG
                Serial.println("LOW");
                pins[selectedPin] = 0;
#endif
                digitalWrite(selectedPin, LOW);
                sprintf(outValue,"%s","LOW");
              }

            } 
            else {

#if DEBUG
              //  analog
              Serial.println("analog");
#endif
              //  get numeric value
              int selectedValue = atoi(value);              
#if DEBUG
              Serial.println(selectedValue);
#endif
              analogWrite(selectedPin, selectedValue);
              sprintf(outValue,"%i",selectedValue);

            }

            char jsonOut[100] = "";
            if (pin[0] == 'a' || pin[0] == 'A') {
              sprintf(jsonOut, "{\"pin\":\"A%i\",\"mode\":\"output\",\"value\":\"%s\"}", selectedPin - A0, outValue); 
            } else {
              sprintf(jsonOut, "{\"pin\":\"D%i\",\"mode\":\"output\",\"value\":\"%s\"}", selectedPin, outValue); 
            }

            //  return status
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            client.println(jsonOut);
          } 
          else {
#if DEBUG
            //  read the pin value
            Serial.println("reading pin");
#endif
            //  determine analog or digital
            if(pin[0] == 'a' || pin[0] == 'A'){
              //  analog
#if DEBUG
              Serial.println(selectedPin);
              Serial.println("analog");
#endif

              sprintf(outValue,"%d",analogRead(selectedPin));

#if DEBUG
              Serial.println(outValue);
#endif

            } 
            else if(pin[0]== 'd' || pin[0] == 'D') {
              //  digital

#if DEBUG
              Serial.println(selectedPin);
              Serial.println("digital");
#endif
              int inValue = 0;
              if (pins[selectedPin] == -1) {
                inValue = digitalRead(selectedPin);
              } else {
                inValue = pins[selectedPin];
              }

              if(inValue == 0){
                sprintf(outValue,"%s","LOW");
              }

              if(inValue == 1){
                sprintf(outValue,"%s","HIGH");
              }

#if DEBUG
              Serial.println(outValue);
#endif
            }

            //  assemble the json output
            char jsonOut[100] = "";
            char mode[6] = "";
            if (pins[selectedPin] < 0) {
              sprintf(mode, "%s", "input");
            } else {
              sprintf(mode, "%s", "output");
            }
            if (pin[0] == 'a' || pin[0] == 'A') {
              sprintf(jsonOut, "{\"pin\":\"A%i\",\"mode\":\"%s\",\"value\":\"%s\"}", selectedPin - A0, mode, outValue); 
            } else {
              sprintf(jsonOut, "{\"pin\":\"D%i\",\"mode\":\"%s\",\"value\":\"%s\"}", selectedPin, mode, outValue); 
            }

            //  return value with wildcarded Cross-origin policy
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            client.println(jsonOut);
          }
        } 
        else {

          //  error
#if DEBUG
          Serial.println("erroring");
#endif
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/html");
          client.println();

        }
        break;
      }
    }

    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    //client.stop();
    client.stop();
      while(client.status() != 0){
      delay(5);
    }
  }
}
