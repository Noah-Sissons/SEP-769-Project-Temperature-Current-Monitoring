/*

 Temperature and Current Monitoring of a Motor

 This program reads voltage at three different locations on the circuit
 It uses those numbers to calculate a current, as well as temperature from
 a thermistor.

 created June 8th 2021
 by Noah Sissons
 
 */

#include <ArduinoHttpClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <RTCZero.h>
#include <time.h>
#include "arduino_secrets.h"

///////you may enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

char serverAddress[] = "dull-mule-98.loca.lt";  // CHANGE TO YOUR SERVER ADDRESS

WiFiClient wifi;
HttpClient client = HttpClient(wifi, serverAddress);
int status = WL_IDLE_STATUS;

RTCZero rtc;

unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  rtc.begin();

  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  }
  Serial.println("You're connected to the network");
  Serial.println();

  // change timeout to deal with long distance to server (Canada to China)
  client.setHttpResponseTimeout(60000);

  // Start UDP for time synchronization
  Serial.println("\nStarting connection to NTP server...");
  Udp.begin(localPort);

  // synchronize arduino with UTC time
  sync_time();
  
}

void sync_time() {
  // based on https://github.com/arduino-libraries/WiFi101/blob/master/examples/WiFiUdpNtpClient/WiFiUdpNtpClient.ino
  sendNTPpacket(timeServer); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  while (!Udp.parsePacket());
  Serial.println("packet received");
  // We've received a packet, read the data from it
  Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:

  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  Serial.print("Seconds since Jan 1 1900 = " );
  Serial.println(secsSince1900);

  // now convert NTP time into everyday time:
  Serial.print("Unix time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;
  // print Unix time:
  Serial.println(epoch);

  // based on https://stackoverflow.com/questions/43063071/the-arduino-ntp-i-want-print-out-datadd-mm-yyyy
  uint16_t year;      
  rtc.setSeconds(epoch % 60);  /* Get seconds from unix */
  epoch /= 60;                 /* Go to minutes */
  rtc.setMinutes(epoch % 60);  /* Get minutes */
  epoch /= 60;                 /* Go to hours */
  rtc.setHours(epoch % 24);    /* Get hours */
  epoch /= 24;                 /* Go to days */

  year = 1970;                /* Process year */
  while (1) {
      if (year % 4 == 0) {
          if (epoch >= 366) {
              epoch -= 366;
          } else {
              break;
          }
      } else if (epoch >= 365) {
          epoch -= 365;
      } else {
          break;
      }
      year++;
  }
  /* Get year in xx format */
  rtc.setYear(year - 2000);
  /* Get month */
  const byte month_sizes[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const byte month_sizes_ly[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  byte month;
  for (month = 0; month < 12; month++) {
      if (year % 4 == 0) {
          if (epoch >= month_sizes_ly[month]) {
              epoch -= month_sizes_ly[month];
          } else {
              break;
          }
      } else if (epoch >= month_sizes[month]) {
          epoch -= month_sizes[month];
      } else {
          break;
      }
  }

  rtc.setMonth(month+1);            /* Month starts with 1 */
  rtc.setDay(epoch + 1);            /* Date starts with 1 */

}

// From https://github.com/arduino-libraries/WiFi101/blob/master/examples/WiFiUdpNtpClient/WiFiUdpNtpClient.ino
// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  //Serial.println("1");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  //Serial.println("2");
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  //Serial.println("3");

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  //Serial.println("4");
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  //Serial.println("5");
  Udp.endPacket();
  //Serial.println("6");
}

// thermistor calculation
float getTemperature(float thermistor) {
  // Calculate voltage
  float v = 5.0 - thermistor * 5.0 / 1024;
  // Calculate resistance value of thermistor
  float Rt = 10000 * v / (5 - v);
  // Don't know exact thermistor, best guess is 01C4001J based on behaviour
  float A = 0.00133604417625971, B = 0.00023673779954822, C = 0.0000000954250480439953;
  float tempK = 1 / (A + B * log(Rt) + C * log(Rt) * log(Rt) * log(Rt));
  // Calculate temperature (Celsius)
  float tempC = tempK - 273.15;
  
  return tempC;
}

float getCurrent(float v1, float v2) {
  // Calculate current from the voltage drop over 1.2 Ohm resistor
  float resistance = 1.2;

  return abs(v2 - v1) / resistance;
}

void loop() {
  float voltage1    = 0;
  float voltage2    = 0;
  float thermistor  = 0;

  // collect data while we wait
  for (int i = 0; i < 100; i++) {
    voltage1 += analogRead(A1) * 5.0 / 1024.0;
    voltage2 += analogRead(A2) * 5.0 / 1024.0;
    thermistor += analogRead(A3);
    delay(5000 / 100);
  }
  voltage1 /= 100;
  voltage2 /= 100;
  thermistor /= 100;

  float temperature = getTemperature(thermistor);
  float current     = getCurrent(voltage1, voltage2);

  // construct timestamp
  char timestamp[25];
  snprintf(timestamp, 25, "20%02d-%02d-%02dT%02d:%02d:%02d.000Z", rtc.getYear(), rtc.getMonth(),
           rtc.getDay(), rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());

  // create string to send to elastic cloud
  char postData[128];
  // \"@timestamp\":%d,
  snprintf(postData, 128, "{\"@timestamp\":\"%s\",\"temperature\":%f,\"current\":%f,\"voltage\":%f}", timestamp, temperature, current, voltage2);

  /
  Serial.println("making POST request");
  String contentType = "application/json";

  Serial.print("Posting data: ");
  Serial.println(postData);

  // send data through HTTP
  client.beginRequest();
  client.post("/my_index/_doc", contentType, postData);
  client.endRequest();
  
  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  
}
