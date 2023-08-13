/*
   Test Arduino Manager for iPad / iPhone / Mac

   A simple test program to show the Arduino Manager
   features.

   Author: Fabrizio Boco - fabboco@gmail.com

   Version: 1.0

   08/13/2023

   All rights reserved

*/

/*

   AMController libraries, example sketches (The Software) and the related documentation (The Documentation) are supplied to you
   by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation
   constitutes acceptance of these terms.
   If you do not agree with these terms, please do not use or install The Software.
   The Author grants you a personal, non-exclusive license, under authors copyrights in this original software, to use The Software.
   Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any
   patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
   The Software and the Documentation are provided by the Author on an AS IS basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
   LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION
   ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
   REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
   STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <Servo.h>
#include <AM_UnoR4WiFi.h>
#include "Arduino_LED_Matrix.h"
#include "arduino_secrets.h"


#define SD_SELECT 10

/*

   WIFI Library configuration

*/
IPAddress ip(x, y, x, w);
IPAddress dns(8, 8, 8, 8);
IPAddress gateway(x, y, z, w);
IPAddress subnet(255, 255, 255, 0);

char ssid[] = SECRET_SSID;  // your network SSID (name) e.g. "MYNETWORK"
char pass[] = SECRET_PASS;  // your network password e.g. "MYPASSWORD"

int status = WL_IDLE_STATUS;

WiFiServer server(80);

/**

   Other initializations

*/






/*

   Callbacks Prototypes

*/
void doWork();
void doSync();
void processIncomingMessages(char *variable, char *value);
void processOutgoingMessages();
void processAlarms(char *variable);
void deviceConnected();
void deviceDisconnected();

/*

   AMController Library initialization

*/
#ifdef ALARMS_SUPPORT
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &processAlarms, &deviceConnected, &deviceDisconnected);
#else
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &deviceConnected, &deviceDisconnected);
#endif

void setup() {

  Serial.begin(115200);
  Serial.println("Starting...");

#if (defined(SD_SUPPORT))
  Serial.println("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(SD_SELECT)) {
    Serial.println("SD card failed, or not present");
  } else {
    Serial.println("SD card initialized");
  }

#endif

  // attempt to connect to Wifi network
  Serial.println("Configuring network...");
  WiFi.config(ip, dns, gateway, subnet);
  Serial.println("Wifi Configured");

  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network
    status = WiFi.begin(ssid, pass);

    // wait 1 seconds for connection:
    delay(1000);
  }

  // print your WiFi shields IP address

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

#if defined(ALARMS_SUPPORT)

  // Set a new NTP Server
  //
  // Choose your NTP Server here: www.pool.ntp.org
  //
  amController.setNTPServerAddress(IPAddress(162, 159, 200, 123));

#endif

  /**

    Other initializations

  */
}

/**

   Standard loop function

*/
void loop() {

  amController.loop(20);
}

/**


  This function is called periodically and its equivalent to the standard loop() function

*/
void doWork() {
  //Serial.println("doWork");
}


/**


  This function is called when the ios device connects and needs to initialize the position of switches and knobs

*/
void doSync() {
  // Serial.println("doSync");
}

/**


  This function is called when a new message is received from the iOS device

*/
void processIncomingMessages(char *variable, char *value) {
  // Serial.print(variable);
  // Serial.print(" : ");
  // Serial.println(value);

  // if (strcmp(variable, <value>) == 0) {
  //
  //  Do something !
  // }
}

/**


  This function is called periodically and messages can be sent to the iOS device

*/
void processOutgoingMessages() {
  //amController.writeMessage(<variable>, <value>);
}

/**


  This function is called when a Alarm is fired

*/
void processAlarms(char *alarm) {
  //Serial.print(alarm);
  //Serial.println(" fired");
}

/**


  This function is called when the iOS device connects

*/
void deviceConnected() {
  //Serial.println("Device connected");
}

/**


  This function is called when the iOS device disconnects

*/
void deviceDisconnected() {
  //Serial.println("Device disconnected");
}

/**

  Auxiliary functions

*/
