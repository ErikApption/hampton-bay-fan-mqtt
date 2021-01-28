#include "rf-fans.h"


#define BASE_TOPIC FANIMATION_BASE_TOPIC

#define CMND_BASE_TOPIC CMND_TOPIC BASE_TOPIC
#define STAT_BASE_TOPIC STAT_TOPIC BASE_TOPIC

#define SUBSCRIBE_TOPIC_CMND_FAN CMND_BASE_TOPIC "/+/fan"
#define SUBSCRIBE_TOPIC_CMND_SPEED CMND_BASE_TOPIC "/+/speed"     
#define SUBSCRIBE_TOPIC_CMND_LIGHT CMND_BASE_TOPIC "/+/light"  
#define SUBSCRIBE_TOPIC_CMND_DIR CMND_BASE_TOPIC "/+/direction"

#define SUBSCRIBE_TOPIC_STAT_SETUP STAT_BASE_TOPIC "/#"

#define TX_FREQ 303.870 // Fanimation 
            
// RC-switch settings
#define RF_PROTOCOL 13
#define RF_REPEATS  7 
                        
// Keep track of states for all dip settings
static fan fans[16];

static int long lastvalue;
static unsigned long lasttime;

static void postStateUpdate(int id) {
  char outTopic[100];
  sprintf(outTopic, "%s/%s/direction", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].directionState ? "UP":"DOWN", true);
  sprintf(outTopic, "%s/%s/fan", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].fanState ? "ON":"OFF", true);
  sprintf(outTopic, "%s/%s/speed", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fanStateTable[fans[id].fanSpeed], true);
  sprintf(outTopic, "%s/%s/light", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].lightState ? "ON":"OFF", true);
}

static void transmitState(int fanId, int code) {
  mySwitch.disableReceive();         // Receiver off
  ELECHOUSE_cc1101.setMHZ(TX_FREQ);
  ELECHOUSE_cc1101.SetTx();           // set Transmit on
  mySwitch.enableTransmit(TX_PIN);   // Transmit on
  mySwitch.setRepeatTransmit(RF_REPEATS); // transmission repetitions.
  mySwitch.setProtocol(RF_PROTOCOL);        // send Received Protocol

  // Build out RF code
  //   Code follows the 12 bit pattern, built ontop of harberbreeze?
  //   0aaaadccc1cl
  //   Where a is the inversed/reversed dip setting, 
  //     ccc1c is the command, no idea what the 1 does yet, fanimation and harberbreeze don't seem to use that bit
  // 01111 VI	H
  // 01110 V	H+Off
  // 10011 IV	M+L
  // 10111 III	M
  // 11010 II	L+Off
  // 11011 I	L
  // 11110 Off	Off
  //     l is the light toggle
  //     d is safe to use fade for the light

  // Harber Breeze UC-9050T and UC-7070T
  //   0aaaa1hml1fl
  //   Where a is the inversed/reversed dip setting, 
  //     set 0 to command, h=high, m=med, l=low, f=fan off, l=light
        
  int rfCode = 0x0000 | (!(fans[fanId].fade&0x01) << 6) | ((~fanId) & 0x0f) << 7 | (code&0x3f);
  
  mySwitch.send(rfCode, 12);      // send 12 bit code
  mySwitch.disableTransmit();   // set Transmit off
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();      // set Receive on
  mySwitch.enableReceive(RX_PIN);   // Receiver on
  Serial.print("Sent command fanimation: ");
  Serial.print(fanId);
  Serial.print(" ");
  for(int b=12; b>0; b--) {
    Serial.print(bitRead(rfCode,b-1));
  }
  Serial.println("");
  postStateUpdate(fanId);
}

void fanimationMQTT(char* topic, byte* payload, unsigned int length) {
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
          switch(fans[idint].fanSpeed) {
            case FAN_VI:
              transmitState(idint,0x1f);
              break;
            case FAN_V:
              transmitState(idint,0x1d);
              break;
            case FAN_IV:
              transmitState(idint,0x27);
              break;
            case FAN_III:
              transmitState(idint,0x2f);
              break;
            case FAN_II:
              transmitState(idint,0x35);
              break;
            case FAN_I:
              transmitState(idint,0x37);
              break;
          }
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0x3d);
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"+") ==0) {
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_I:
              fans[idint].fanSpeed=FAN_II;
              break;
            case FAN_II:
              fans[idint].fanSpeed=FAN_III;
              break;
            case FAN_III:
              fans[idint].fanSpeed=FAN_IV;
              break;
            case FAN_IV:
              fans[idint].fanSpeed=FAN_V;
              break;
            case FAN_V:
              fans[idint].fanSpeed=FAN_VI;
              break;
            case FAN_VI:
              fans[idint].fanSpeed=FAN_VI;
              break;
            default:
              if(fans[idint].fanSpeed>FAN_VI)
                fans[idint].fanSpeed--;
              break;
          }
        } else if(strcmp(payloadChar,"-") ==0) {
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_I:
              fans[idint].fanSpeed=FAN_I;
              break;
            case FAN_II:
              fans[idint].fanSpeed=FAN_I;
              break;
            case FAN_III:
              fans[idint].fanSpeed=FAN_II;
              break;
            case FAN_IV:
              fans[idint].fanSpeed=FAN_III;
              break;
            case FAN_V:
              fans[idint].fanSpeed=FAN_IV;
              break;
            case FAN_VI:
              fans[idint].fanSpeed=FAN_V;
              break;
            default:
              if(fans[idint].fanSpeed>FAN_I)
                fans[idint].fanSpeed++;
              break;
          }
        } else if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_HI;
          transmitState(idint,0x1f);
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_MED;
          transmitState(idint,0x2f);
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_LOW;
          transmitState(idint,0x37);
        } else if(strcmp(payloadChar,"i") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_I;
          transmitState(idint,0x37);
        } else if(strcmp(payloadChar,"ii") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_II;
          transmitState(idint,0x35);
        } else if(strcmp(payloadChar,"iii") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_III;
          transmitState(idint,0x2f);
        } else if(strcmp(payloadChar,"iv") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_IV;
          transmitState(idint,0x27);
        } else if(strcmp(payloadChar,"v") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_V;
          transmitState(idint,0x1d);
        } else if(strcmp(payloadChar,"vi") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_VI;
          transmitState(idint,0x1f);
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0x3d);
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].lightState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = !fans[idint].lightState;
          transmitState(idint,0x3e);
        } else {
          fans[idint].lightState = !fans[idint].lightState;
          transmitState(idint,0x3e);
        }
      } else if(strcmp(attr,"direction") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].directionState)
            strcpy(payloadChar,"down");
          else
            strcpy(payloadChar,"up");
        }
        if(strcmp(payloadChar,"up") == 0) {
          fans[idint].directionState = !fans[idint].directionState;
          transmitState(idint,0x3b);
        } else {
          fans[idint].directionState = !fans[idint].directionState;
          transmitState(idint,0x3b);
        }
      }
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
        } else if(strcmp(payloadChar,"i") ==0) {
          fans[idint].fanSpeed = FAN_I;
        } else if(strcmp(payloadChar,"ii") ==0) {
          fans[idint].fanSpeed = FAN_II;
        } else if(strcmp(payloadChar,"iii") ==0) {
          fans[idint].fanSpeed = FAN_III;
        } else if(strcmp(payloadChar,"iv") ==0) {
          fans[idint].fanSpeed = FAN_IV;
        } else if(strcmp(payloadChar,"v") ==0) {
          fans[idint].fanSpeed = FAN_V;
        } else if(strcmp(payloadChar,"vi") ==0) {
          fans[idint].fanSpeed = FAN_VI;
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      } else if(strcmp(attr,"direction") ==0) {
        if(strcmp(payloadChar,"up") == 0) {
          fans[idint].directionState = true;
        } else {
          fans[idint].directionState = false;
        }
      }
    } else {
      // Invalid ID
      return;
    }
  }
}

void fanimationRF(int long value, int prot, int bits) {
    if( (prot >10 && prot<14)  && bits == 12 && ((value&0x800)==0x000)) {
      unsigned long t=millis();
      if(value == lastvalue) {
        if(t - lasttime < NO_RF_REPEAT_TIME)
          return;
        lasttime=t;
      }
      lastvalue=value;
      lasttime=t;
      int dipId = (~value >> 7)&0x0f;
      // Got a correct id in the correct protocol
      if(dipId < 16) {
        // Convert to dip id
        if((value&0x40) == 0x40)
          fans[dipId].fade=false;
        else
          fans[dipId].fade=true;
        switch(value&0x3f) {
          case 0x3b: // Direction
            fans[dipId].directionState=!(fans[dipId].directionState);
            break;
          case 0x3e: // Light
            fans[dipId].lightState = !(fans[dipId].lightState);
            break;
          case 0x37: // Fan I
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_I;
            break;
          case 0x35: // Fan II
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_II;
            break;
          case 0x2f: // Fan III
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_III;
            break;
          case 0x27: // Fan IV
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_IV;
            break;
          case 0x1d: // Fan V
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_V;
            break;
          case 0x1f: // Fan VI
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_VI;
            break;
          case 0x3d: // Fan Off
            fans[dipId].fanState = false;
            break;
          case 0x2d: // Set
            break;
          case 0x3f: // transmit by mistake? (no button pressed)
            break;
          default:
            break;
        }
        postStateUpdate(dipId);
      }
    }
}

void fanimationMQTTSub(boolean setup) {
  client.subscribe(SUBSCRIBE_TOPIC_CMND_DIR);
  client.subscribe(SUBSCRIBE_TOPIC_CMND_FAN);  
  client.subscribe(SUBSCRIBE_TOPIC_CMND_SPEED);
  client.subscribe(SUBSCRIBE_TOPIC_CMND_LIGHT);

  if(setup) client.subscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}

void fanimationSetup() {
  lasttime=0;
  lastvalue=0;
  // initialize fan struct 
  for(int i=0; i<16; i++) {
    fans[i].fade = false;
    fans[i].directionState = false;
    fans[i].lightState = false;
    fans[i].fanState = false;  
    fans[i].fanSpeed = FAN_LOW;
  }
}

void fanimationSetupEnd() {
  client.unsubscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}
