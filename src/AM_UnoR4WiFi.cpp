#include "r_rtc_api.h"
/*
 *
 * AMController libraries, example sketches (“The Software”) and the related documentation (“The Documentation”) are supplied to you 
 * by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation 
 * constitutes acceptance of these terms.  
 * If you do not agree with these terms, please do not use or install The Software.
 * The Author grants you a personal, non-exclusive license, under author's copyrights in this original software, to use The Software. 
 * Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any 
 * patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
 * The Software and the Documentation are provided by the Author on an "AS IS" basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT 
 * LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION 
 * ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, 
 * REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE), 
 * STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Author: Fabrizio Boco - fabboco@gmail.com
 *
 * All rights reserved
 *
 */

#include "AM_UnoR4WiFi.h"

char *dtostrf(double val, signed char width, unsigned char prec, char *sout);

#if defined(ALARMS_SUPPORT)

// bool check(uint8_t *pRecord, void *pData) {
//   // Alarm a;
//   // memcpy(&a, pRecord, sizeof(a));
//   // if (strcmp(a.id, (char *)pData) == 0)
//   //   return true;
//   return false;
// }

#endif

#if defined(ALARMS_SUPPORT)

static bool checkAlarmsNow = false;  // true if it's time to check alarms

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
#if defined(ALARMS_SUPPORT)
                           void (*processAlarms)(char *alarm),
#endif
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void))
  : AMController(server, doWork, doSync, processIncomingMessages, processOutgoingMessages, deviceConnected, deviceDisconnected) {
#ifdef ALARMS_SUPPORT
  _timeServerAddress = IPAddress(129, 6, 15, 28);  // time.nist.gov

  _processAlarms = processAlarms;
  _sendNtpRequest = false;
  _lastAlarmCheck = 0;
#endif
}
#endif

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void)) {
  _var = true;
  _idx = 0;
  _server = server;
  _doWork = doWork;
  _doSync = doSync;
  _processIncomingMessages = processIncomingMessages;
  _processOutgoingMessages = processOutgoingMessages;
  _deviceConnected = deviceConnected;
  _deviceDisconnected = deviceDisconnected;
  _initialized = false;
  _connected = true;
  _pClient = NULL;

  _variable[0] = '\0';
  _value[0] = '\0';

#if defined(ALARMS_SUPPORT) || defined(SDLOGGEDATAGRAPH_SUPPORT)
  _rtc = new RTClock();
   _timeServerAddress = IPAddress(129, 6, 15, 28);  // time.nist.gov
  _syncTime = true;
#endif
#ifdef ALARMS_SUPPORT
  _processAlarms = NULL;
#endif  
}

void AMController::loop() {
  this->loop(20);
}

void AMController::loop(unsigned long _delay) {

  if (!_initialized) {
    _initialized = true;
    _server->begin();
    delay(1000);
    return;
  }

#if defined(ALARMS_SUPPORT) || defined(SDLOGGEDATAGRAPH_SUPPORT)
  if (_syncTime) {
    _syncTime = false;
    this->syncTime();
    return;
  }

  if (_udp.parsePacket()) {
    this->readTime();
#ifdef ALARMS_SUPPORT    
    inizializeAlarms();
    if (_processAlarms != NULL) {
      _rtc->setPeriodicCallback(enableCheckAlarms, Period::ONCE_EVERY_2_SEC);
    }
#endif    
    return;
  }
#endif

  // Do work when disconnected
  _doWork();

#ifdef ALARMS_SUPPORT
  // CheckAlarms
  if (checkAlarmsNow) {
    checkAndFireAlarms();
  }
#endif

  WiFiClient localClient = _server->available();
  _pClient = &localClient;

  if (localClient) {
    //PRINTLN("Device Connected");
    if (_deviceConnected != NULL) {
      _deviceConnected();
    }    
    while (_pClient->connected()) {
      if (_pClient->available()) {
        this->readVariable();
        if (strlen(_value) > 0 && strcmp(_variable, "Sync") == 0) {
          // Process sync messages for the variable _value
          _doSync();
        } else {
#ifdef ALARMS_SUPPORT
          if (strlen(_value) > 0 && (strcmp(_variable, "$AlarmId$") == 0 || strcmp(_variable, "$AlarmT$") == 0 || strcmp(_variable, "$AlarmR$") == 0)) {
            manageAlarms(_variable, _value);
          } else
#endif
#ifdef SD_SUPPORT
            if (strlen(_variable) > 0 && (strcmp(_variable, "SD") == 0 || strcmp(_variable, "$SDDL$") == 0)) {
            manageSD(_variable, _value);
          } else
#endif
            if (strlen(_variable) > 0 && strlen(_value) > 0) {
            // Process incoming messages
            _processIncomingMessages(_variable, _value);
          }
        }
      }

      // Write outgoing messages
      _processOutgoingMessages();

      // Do work when connected
      _doWork();

#ifdef ALARMS_SUPPORT
      // CheckAlarms
      if (checkAlarmsNow) {
        checkAndFireAlarms();
      }
#endif

      delay(_delay);
    }
    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    //PRINTLN("Device Disconnected");
    _pClient->stop();
    _pClient = NULL;
    if (_deviceDisconnected != NULL) {
      _deviceDisconnected();
    }
  }
}


void AMController::readVariable(void) {

  _variable[0] = '\0';
  _value[0] = '\0';
  _var = true;
  _idx = 0;

  while (_pClient->available()) {

    char c = _pClient->read();

    if (isprint(c)) {

      if (c == '=') {
        _variable[_idx] = '\0';
        _var = false;
        _idx = 0;
      } else {

        if (c == '#') {
          _value[_idx] = '\0';
          _var = true;
          _idx = 0;

          PRINT(">");
          PRINT(_variable);
          PRINT(":");
          PRINT(_value);
          PRINTLN("<");

          return;
        } else {
          if (_var) {
            if (_idx == VARIABLELEN)
              _variable[_idx] = '\0';
            else
              _variable[_idx++] = c;
          } else {
            if (_idx == VALUELEN) {
              _var = true;
              _idx = 0;
              _value[_idx] = '\0';
              PRINT("OFL >");
              PRINT(_variable);
              PRINT(":");
              PRINT(_value);
              PRINTLN("<");
            } else
              _value[_idx++] = c;
          }
        }
      }
    }
  }
}

void AMController::writeMessage(const char *variable, int value) {
  char buffer[VARIABLELEN + VALUELEN + 3];

  if (_pClient == NULL)
    return;

  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%d#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer) * sizeof(char));
}

void AMController::writeMessage(const char *variable, float value) {
  char buffer[VARIABLELEN + VALUELEN + 3];
  if (_pClient == NULL)
    return;
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%.3f#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer) * sizeof(char));
}

void AMController::writeTripleMessage(const char *variable, float vX, float vY, float vZ) {
  char buffer[VARIABLELEN + VALUELEN + 3];
  char vbufferAx[VALUELEN];
  char vbufferAy[VALUELEN];
  char vbufferAz[VALUELEN];

  dtostrf(vX, 0, 2, vbufferAx);
  dtostrf(vY, 0, 2, vbufferAy);
  dtostrf(vZ, 0, 2, vbufferAz);
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%s:%s:%s#", variable, vbufferAx, vbufferAy, vbufferAz);

  _pClient->write((const uint8_t *)buffer, strlen(buffer) * sizeof(char));
}

void AMController::writeTxtMessage(const char *variable, const char *value) {
  char buffer[128];

  snprintf(buffer, 128, "%s=%s#", variable, value);

  _pClient->write((const uint8_t *)buffer, strlen(buffer) * sizeof(char));
}

void AMController::log(const char *msg) {
  this->writeTxtMessage("$D$", msg);
}

void AMController::log(int msg) {
  char buffer[11];
  itoa(msg, buffer, 10);

  this->writeTxtMessage("$D$", buffer);
}

void AMController::logLn(const char *msg) {
  this->writeTxtMessage("$DLN$", msg);
}

void AMController::logLn(int msg) {
  char buffer[11];
  itoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(long msg) {
  char buffer[11];
  ltoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(unsigned long msg) {

  char buffer[11];
  ltoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::temporaryDigitalWrite(uint8_t pin, uint8_t value, unsigned long ms) {
  boolean previousValue = digitalRead(pin);
  digitalWrite(pin, value);
  delay(ms);
  digitalWrite(pin, previousValue);
}

// Time Management

#if defined(ALARMS_SUPPORT) || defined(SDLOGGEDATAGRAPH_SUPPORT)

void AMController::setNTPServerAddress(IPAddress address) {
  _timeServerAddress = address;
}

void AMController::syncTime() {
  //Send Request to NTP Server
  _sendNtpRequest = false;
  _udp.begin(2390);  // Local Port to listen for UDP packets
  sendNTPpacket(_timeServerAddress, _udp);
  PRINT("NTP Request Sent to address ");
  PRINTLN(_timeServerAddress);
}

void AMController::readTime() {

  // Packet Received from NTP Server
  _udp.read(_packetBuffer, NTP_PACKET_SIZE);  // read the packet into the buffer

  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:

  unsigned long highWord = word(_packetBuffer[40], _packetBuffer[41]);
  unsigned long lowWord = word(_packetBuffer[42], _packetBuffer[43]);

  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;

  // now convert NTP time into everyday time:
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years to get Unix time:
  unsigned long unixTime = secsSince1900 - seventyYears;

  _rtc->begin();
  RTCTime timeToSet = RTCTime(unixTime);
  _rtc->setTime(timeToSet);

  PRINT("NTP Response [");
  PRINT(unixTime);
  PRINT("] ");
#ifdef DEBUG  
  printTime(unixTime);
#endif  
  RTCTime currentTime;
  _rtc->getTime(currentTime);
  PRINTLN("The RTC was just set to: " + String(currentTime));
}

// send an NTP request to the time server at the given address
void AMController::sendNTPpacket(IPAddress &address, WiFiUDP udp) {

  // set all bytes in the buffer to 0
  memset(_packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  //Serial.println("2");
  _packetBuffer[0] = 0b11100011;  // LI, Version, Mode
  _packetBuffer[1] = 0;           // Stratum, or type of clock
  _packetBuffer[2] = 6;           // Polling Interval
  _packetBuffer[3] = 0xEC;        // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  _packetBuffer[12] = 49;
  _packetBuffer[13] = 0x4E;
  _packetBuffer[14] = 49;
  _packetBuffer[15] = 52;

  //Serial.println("3");

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123);  //NTP requests are to port 123
  //Serial.println("4");
  udp.write(_packetBuffer, NTP_PACKET_SIZE);
  //Serial.println("5");
  udp.endPacket();
}

#ifdef DEBUG

void AMController::printTime(unsigned long time) {

  RTCTime currentTime = RTCTime(time);
  PRINTLN(String(currentTime));
}

#endif

#endif

#ifdef ALARMS_SUPPORT

void AMController::manageAlarms(char *variable, char *value) {
  PRINT("Manage Alarm variable: ");
  PRINT(variable);
  PRINT(" value: ");
  PRINTLN(value);

  if (strcmp(variable, "$AlarmId$") == 0) {
    strcpy(_alarmId, value);
  } else if (strcmp(variable, "$AlarmT$") == 0) {
    _alarmTime = atol(value);
  } else if (strcmp(variable, "$AlarmR$") == 0) {
    if (_alarmTime == 0) {
      PRINT("Deleting Alarm ");
      PRINTLN(_alarmId);
      removeAlarm(_alarmId);
    } else {
      PRINT("Adding/Updating Alarm ");
      PRINTLN(_alarmId);
      createUpdateAlarm(_alarmId, _alarmTime, atoi(value));
    }
#ifdef DEBUG    
    dumpAlarms();
#endif    
  }
}

void AMController::inizializeAlarms() {

  PRINTLN("Initialize Alarms");

  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm a;

    EEPROM.get(i * sizeof(a), a);
    if (a.id[0] != 'A') {
      a.id[0] = 'A';
      a.id[1] = '\0';
      a.time = 0;
      a.repeat = 0;
      EEPROM.put(i * sizeof(a), a);
    }
  }
#ifdef DEBUG 
  dumpAlarms();
#endif  
}

void AMController::createUpdateAlarm(char *id, unsigned long time, bool repeat) {
  char lid[12];
  lid[0] = 'A';
  strcpy(&lid[1], id);

  // Update
  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm a;
    EEPROM.get(i * sizeof(a), a);
    if (strcmp(a.id, lid) == 0) {
      a.time = time;
      a.repeat = repeat;
      EEPROM.put(i * sizeof(a), a);
      return;
    }
  }

  // Create
  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm a;
    EEPROM.get(i * sizeof(a), a);
    if (a.id[1] == '\0') {
      strcpy(a.id, lid);
      a.time = time;
      a.repeat = repeat;
      EEPROM.put(i * sizeof(a), a);
      return;
    }
  }

#ifdef DEBUG
  dumpAlarms();
#endif
}

void AMController::removeAlarm(char *id) {
  char lid[12];
  lid[0] = 'A';
  strcpy(&lid[1], id);

  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm a;
    EEPROM.get(i * sizeof(a), a);
    if (strcmp(a.id, lid) == 0) {
      a.id[1] = '\0';
      a.time = 0;
      a.repeat = 0;
      EEPROM.put(i * sizeof(a), a);
    }
  }
}

#ifdef DEBUG
void AMController::dumpAlarms() {

  Serial.println("\t----Current Alarms -----");

  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm al;
    EEPROM.get(i * sizeof(al), al);
    //eeprom_read_block((void*)&al, (void*)(i * sizeof(al)), sizeof(al));

    Serial.print("\t");
    Serial.print(al.id);
    Serial.print(" ");
    RTCTime currentTime = RTCTime(al.time);
    PRINT(String(currentTime));
    Serial.print(" ");
    Serial.println(al.repeat);
  }
}
#endif

void AMController::enableCheckAlarms() {
  checkAlarmsNow = true;
}

void AMController::checkAndFireAlarms() {
  checkAlarmsNow = false;

  RTCTime currentTime;
  _rtc->getTime(currentTime);
  unsigned long now = currentTime.getUnixTime();

  PRINTLN("checkAndFireAlarms @" + String(currentTime));

#ifdef DEBUG
  dumpAlarms();
#endif

  for (int i = 0; i < MAX_ALARMS; i++) {
    alarm a;

    EEPROM.get(i * sizeof(a), a);
    if (a.id[1] != '\0' && a.time < now) {
      PRINTLN(a.id);
      // First character of id is A and has to be removed
      _processAlarms(&a.id[1]);
      if (a.repeat) {
        a.time += 86400;  // Scheduled again tomorrow
#ifdef DEBUG
        PRINTLN("Alarm rescheduled at ");
        this->printTime(a.time);
        Serial.println();
#endif
      } else {
        //     Alarm removed
        a.id[1] = '\0';
        a.time = 0;
        a.repeat = 0;
      }

      EEPROM.put(i * sizeof(a), a);
#ifdef DEBUG
      this->dumpAlarms();
#endif
    }
  }
}

#endif

#ifdef SD_SUPPORT

void AMController::manageSD(char *variable, char *value) {
  PRINTLN("Manage SD");

  if (strcmp(variable, "SD") == 0) {
    PRINTLN("\tFile List");

    File dir = SD.open("/");
    if (!main) {
      PRINTLN("Failed to open /");
      return;
    }
    dir.rewindDirectory();
    File entry = dir.openNextFile();
    if (!entry) {
      PRINTLN("\tFailed to get first file");
      return;
    }

    while (entry) {
      if (!entry.isDirectory()) {
        this->writeTxtMessage("SD", entry.name());
        PRINTLN("\t" + String(entry.name()));
      }
      entry.close();
      entry = dir.openNextFile();
    }

    dir.close();

    uint8_t buffer[10];
    strcpy((char *)&buffer[0], "SD=$EFL$#");
    _pClient->write(buffer, 9 * sizeof(uint8_t));
    PRINTLN("\tFile list sent");
  }
  if (strcmp(_variable, "$SDDL$") == 0) {
    PRINT("File: ");
    PRINTLN(value);

    File dataFile = SD.open(value, FILE_READ);
    if (dataFile) {
      unsigned long n = 0;
      uint8_t buffer[64];
      strcpy((char *)&buffer[0], "SD=$C$#");
      _pClient->write(buffer, 7 * sizeof(uint8_t));

      delay(500);  // OK

      while (dataFile.available()) {
        n = dataFile.read(buffer, sizeof(buffer));
        _pClient->write(buffer, n * sizeof(uint8_t));
      }
      strcpy((char *)&buffer[0], "SD=$E$#");
      _pClient->write(buffer, 7 * sizeof(uint8_t));
      delay(150);
      dataFile.close();

      PRINTLN("Fine Sent");
    }
    _pClient->flush();
  }
}
#endif

#if defined(ALARMS_SUPPORT) || defined(SDLOGGEDATAGRAPH_SUPPORT)
  unsigned long AMController::now() {
    RTCTime currentTime;
    _rtc->getTime(currentTime);
    return currentTime.getUnixTime();
  }
#endif

#ifdef SDLOGGEDATAGRAPH_SUPPORT

void AMController::sdLogLabels(const char *variable, const char *label1) {

  this->sdLogLabels(variable, label1, NULL, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2) {

  this->sdLogLabels(variable, label1, label2, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3) {

  this->sdLogLabels(variable, label1, label2, label3, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4) {

  this->sdLogLabels(variable, label1, label2, label3, label4, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4, const char *label5) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile) {
    dataFile.print("-");
    dataFile.print(";");
    dataFile.print(label1);
    dataFile.print(";");

    if (label2 != NULL)
      dataFile.print(label2);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label3 != NULL)
      dataFile.print(label3);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label4 != NULL)
      dataFile.print(label4);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label5 != NULL)
      dataFile.println(label5);
    else
      dataFile.println("-");

    dataFile.flush();
    dataFile.close();
  } else {
#ifdef DEBUG
    Serial.print("Error opening");
    Serial.println(variable);
#endif
  }
}


void AMController::sdLog(const char *variable, unsigned long time, float v1) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);

    dataFile.print(";-;-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  } else {
    PRINT("Error opening");
    PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);

    dataFile.print(";-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  } else {
    PRINT("Error opening");
    PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);

    dataFile.print(";-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  } else {

    PRINT("Error opening");
    PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);

    dataFile.println(";-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  } else {
    PRINT("Error opening");
    PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4, float v5) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);
    dataFile.print(";");

    dataFile.println(v5);

    dataFile.println();

    dataFile.flush();
    dataFile.close();
  } else {
    PRINT("Error opening");
    PRINTLN(variable);
  }
}

void AMController::sdSendLogData(const char *variable) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile) {

    char c;
    char buffer[128];
    int i = 0;

    dataFile.seek(0);

    while (dataFile.available()) {

      c = dataFile.read();

      if (c == '\n') {

        buffer[i++] = '\0';
        PRINTLN(buffer);

        this->writeTxtMessage(variable, buffer);

        i = 0;
      } else
        buffer[i++] = c;
    }

    PRINTLN("All data sent");

    dataFile.close();
  } else {

    PRINT("Error opening ");
    PRINTLN(variable);
  }

  this->writeTxtMessage(variable, "");
}

// Size in bytes
uint16_t AMController::sdFileSize(const char *variable) {

  File dataFile = SD.open(variable, FILE_WRITE);

  if (dataFile) {
    return dataFile.size();
  }

  return -1;
}

void AMController::sdPurgeLogData(const char *variable) {

  noInterrupts();

  SD.remove(variable);

  interrupts();
}

#endif
