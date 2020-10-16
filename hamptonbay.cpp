#include "rf-fans.h"


#define BASE_TOPIC HAMPTONBAY_BASE_TOPIC

#define CMND_BASE_TOPIC "cmnd/" BASE_TOPIC
#define STAT_BASE_TOPIC "stat/" BASE_TOPIC

#define SUBSCRIBE_TOPIC_CMND_FAN CMND_BASE_TOPIC "/+/fan"
#define SUBSCRIBE_TOPIC_CMND_SPEED CMND_BASE_TOPIC "/+/speed"     
#define SUBSCRIBE_TOPIC_CMND_LIGHT CMND_BASE_TOPIC "/+/light"  

#define SUBSCRIBE_TOPIC_STAT_SETUP STAT_BASE_TOPIC "/#"

#define TX_FREQ     303.631 // FAN-9T
            
// RC-switch settings
#define RF_PROTOCOL 6
#define RF_REPEATS 8 
                                    
// Keep track of states for all dip settings
static fan fans[16];

static void postStateUpdate(int id) {
  char outTopic[100];
  sprintf(outTopic, "%s/%s/fan", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].fanState ? "ON":"OFF", true);
  sprintf(outTopic, "%s/%s/speed", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fanStateTable[fans[id].fanSpeed], true);
  sprintf(outTopic, "%s/%s/light", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].lightState ? "ON":"OFF", true);
}

static void transmitState(int fanId) {
  mySwitch.disableReceive();         // Receiver off
  ELECHOUSE_cc1101.setMHZ(TX_FREQ);
  ELECHOUSE_cc1101.SetTx();           // set Transmit on
  mySwitch.enableTransmit(TX_PIN);   // Transmit on
  mySwitch.setRepeatTransmit(RF_REPEATS); // transmission repetitions.
  mySwitch.setProtocol(RF_PROTOCOL);        // send Received Protocol

  // Build out RF code
  //   Code follows the 21 bit pattern
  //   000aaaa000000lff00000
  //   Where a is the inversed/reversed dip setting, 
  //     l is light state, ff is fan speed
  int fanRf = fans[fanId].fanState ? fans[fanId].fanSpeed : 0;
  int rfCode = dipToRfIds[((~fanId)&0x0f)] << 14 | fans[fanId].lightState << 7 | fanRf << 5;
            
  mySwitch.send(rfCode, 21);      // send 21 bit code
  mySwitch.disableTransmit();   // set Transmit off
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();      // set Receive on
  mySwitch.enableReceive(RX_PIN);   // Receiver on
  Serial.print("Sent command hamptonbay: ");
  Serial.print(fanId);
  Serial.print(" ");
  for(int b=21; b>0; b--) {
    Serial.print(bitRead(rfCode,b-1));
  }
  Serial.println("");
  postStateUpdate(fanId);
}

void hamptonbayMQTT(char* topic, byte* payload, unsigned int length) {
  if(strncmp(topic, CMND_BASE_TOPIC, sizeof(CMND_BASE_TOPIC)-1) == 0) {
    char payloadChar[length + 1];
    sprintf(payloadChar, "%s", payload);
    payloadChar[length] = '\0';
  
    // Get ID after the base topic + a slash
    char id[5];
    memcpy(id, &topic[sizeof(CMND_BASE_TOPIC)], 4);
    id[4] = '\0';
    if(strspn(id, idchars)) {
      uint8_t idint = strtol(id, (char**) NULL, 2);
      char *attr;
      // Split by slash after ID in topic to get attribute and action
    
      attr = strtok(topic+sizeof(CMND_BASE_TOPIC)-1 + 5, "/");
          // Convert payload to lowercase
      for(int i=0; payloadChar[i]; i++) {
        payloadChar[i] = tolower(payloadChar[i]);
      }

      if(strcmp(attr,"fan") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].fanState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].fanState = true;
        } else {
          fans[idint].fanState = false;
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_HI;
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_MED;
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_LOW;
        } else {
          fans[idint].fanState = false;
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].lightState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      }
      transmitState(idint);
    } else {
      // Invalid ID
      return;
    }
  }
  if(strncmp(topic, STAT_BASE_TOPIC, sizeof(STAT_BASE_TOPIC)-1) == 0) {
    char payloadChar[length + 1];
    sprintf(payloadChar, "%s", payload);
    payloadChar[length] = '\0';
  
    // Get ID after the base topic + a slash
    char id[5];
    memcpy(id, &topic[sizeof(STAT_BASE_TOPIC)], 4);
    id[4] = '\0';
    if(strspn(id, idchars)) {
      uint8_t idint = strtol(id, (char**) NULL, 2);
      char *attr;
      // Split by slash after ID in topic to get attribute and action
    
      attr = strtok(topic+sizeof(STAT_BASE_TOPIC)-1 + 5, "/");
          // Convert payload to lowercase
      for(int i=0; payloadChar[i]; i++) {
        payloadChar[i] = tolower(payloadChar[i]);
      }

      if(strcmp(attr,"fan") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].fanState = true;
        } else {
          fans[idint].fanState = false;
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanSpeed = FAN_HI;
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanSpeed = FAN_MED;
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanSpeed = FAN_LOW;
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      }
    }
  }
}

void hamptonbayRF(int long value, int prot, int bits) {
    if( prot == 6  && bits == 21 && ((value&0x1c3f1f)==0x000000)) {
      int id = (~value >> 14)&0x0f;
      // Got a correct id in the correct protocol
      if(id < 16) {
        // reverse order of bits
        int dipId = dipToRfIds[id];
        // Blank out id in message to get light state
        int states = value & 0b11111111;
        fans[dipId].lightState = states >> 7;
        // Blank out light state to get fan state
        switch((states & 0b01111111) >> 5) {
          case 0:
            fans[dipId].fanState = false;
            break;
          case 1:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_HI;
            break;
          case 2:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_MED;
            break;
          case 3:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_LOW;
            break;
        }
        postStateUpdate(dipId);
      }
    }
}

void hamptonbayMQTTSub(boolean setup) {
  client.subscribe(SUBSCRIBE_TOPIC_CMND_FAN);  
  client.subscribe(SUBSCRIBE_TOPIC_CMND_SPEED);
  client.subscribe(SUBSCRIBE_TOPIC_CMND_LIGHT);
  if(setup)   client.subscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}

void hamptonbaySetup() {
  // initialize fan struct 
  for(int i=0; i<16; i++) {
    fans[i].lightState = false;
    fans[i].fanState = false;  
    fans[i].fanSpeed = FAN_LOW;
  }
}

void hamptonbaySetupEnd() {
  client.unsubscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}
