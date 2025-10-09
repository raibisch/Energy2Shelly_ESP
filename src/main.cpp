// Energy2Shelly_ESP v0.5.2
#include <Arduino.h>
#include <Preferences.h>
#ifndef ESP32
  #define WEBSERVER_H "fix WifiManager conflict"
#endif
#ifdef ESP32
  #include <HTTPClient.h>
  #include <AsyncTCP.h>
  #include <ESPmDNS.h>
  #include <WiFi.h>
#else
  #include <ESP8266HTTPClient.h>
  #include <ESPAsyncTCP.h>
  #include <ESP8266mDNS.h>
#endif
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <ModbusIP_ESP8266.h>

#define DEBUG false // set to false for no DEBUG output
#define DEBUG_SERIAL if(DEBUG)Serial

unsigned long startMillis = 0;
unsigned long startMillis_sunspec = 0;
unsigned long currentMillis;
unsigned long startMillis_tibberpulse = 0; // for Tibber

// define your default values here, if there are different values in config.json, they are overwritten.
char input_type[40];
char mqtt_server[80];
char mqtt_port[6] = "1883";
char mqtt_topic[90] = "tele/meter/SENSOR";
char mqtt_user[40] = "";
char mqtt_passwd[40] = "";
char power_path[60] = "";
char pwr_export_path[60] = "";
char power_l1_path[60] = "";
char power_l2_path[60] = "";
char power_l3_path[60] = "";
char energy_in_path[60] = "";
char energy_out_path[60] = "";
char shelly_gen[2] = "2";
char shelly_fw_id[32] = "20241011-114455/1.4.4-g6d2a586";
char shelly_mac[13];
char shelly_name[26] = "shellypro3em-";
char query_period[10] = "1000";
char modbus_dev[10] = "71"; // default for KSEM
char shelly_port[6] = "2220"; // old: 1010; new (FW>=226): 2220; Venus-E V3: 1010 !
char force_pwr_decimals[6] = "true"; // to fix Marstek bug
bool forcePwrDecimals = true; // to fix Marstek bug
char sma_id[17] = "";

char tibber_url  [32] = "192.168.2.xx";         // replace with your IP from Tibber-Pulse
char tibber_user[32] = "admin";                 // fixed!
char tibber_password[32] = "xxxx-xxxx";         // replace with password printed on Tibbel-Pulse-Adapter
char tibber_rpc[32]  =  "/data.json?node_id=1"; // fixed!

IPAddress modbus_ip;
ModbusIP modbus1;
int16_t modbus_result[256];

const uint8_t defaultVoltage = 230;
const uint8_t defaultFrequency = 50;
const uint8_t defaultPowerFactor = 1;

// LED blink default values
unsigned long ledOffTime = 0;
uint8_t led = 0;
bool led_i = false;
const uint8_t ledblinkduration = 50;
char led_gpio[3] = "";
char led_gpio_i[6];

unsigned long period = 1000;
int rpcId = 1;
char rpcUser[20] = "user_1";

// SMA Multicast IP and Port
unsigned int multicastPort = 9522;  // local port to listen on
IPAddress multicastIP(239, 12, 255, 254);

// flags for saving/resetting WifiManager data
bool shouldSaveConfig = false;
bool shouldResetConfig = false;

Preferences preferences;

// flags for data sources
bool dataMQTT = false;
bool dataSMA = false;
bool dataSHRDZM = false;
bool dataHTTP = false;
bool dataSUNSPEC = false;
bool dataTIBBERPULSE = false;

struct PowerData {
  double current;
  double voltage;
  double power;
  double apparentPower;
  double powerFactor;
  double frequency;
};

struct EnergyData {
  double gridfeedin;
  double consumption;
};

PowerData PhasePower[3];
EnergyData PhaseEnergy[3];
String serJsonResponse;

#ifndef ESP32
  MDNSResponder::hMDNSService hMDNSService = 0; // handle of the http service in the MDNS responder
  MDNSResponder::hMDNSService hMDNSService2 = 0; // handle of the shelly service in the MDNS responder
#endif

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
static AsyncWebServer server(80);
static AsyncWebSocket webSocket("/rpc");
WiFiUDP Udp;
HTTPClient http;
WiFiUDP UdpRPC;
#ifdef ESP32
#define UDPPRINT print
#else
#define UDPPRINT write
#endif

double round2(double value) {
  int ivalue = (int)(value * 100.0 + (value > 0.0 ? 0.5 : -0.5));

  // fix Marstek bug: make sure to have decimal numbers
  if(forcePwrDecimals && (ivalue % 100 == 0)) ivalue++;
  
  return ivalue / 100.0;
}

double round1(double value) {
  int ivalue = (int)(value * 10.0 + (value > 0.0 ? 0.5 : -0.5));

  // fix Marstek bug: make sure to have decimal numbers
  if(forcePwrDecimals && (ivalue % 100 == 0)) ivalue++;
  
  return ivalue / 10.0;
}



JsonVariant resolveJsonPath(JsonVariant variant, const char *path) {
  for (size_t n = 0; path[n]; n++) {
    // Not a full array support, but works for Shelly 3EM emeters array!
    if (path[n] == '[') {
      variant = variant[JsonString(path, n)][atoi(&path[n+1])];
      path += n + 4;
      n = 0;
    }
    if (path[n] == '.') {
      variant = variant[JsonString(path, n)];
      path += n + 1;
      n = 0;
    }
  }
  return variant[path];
}

void setPowerData(double totalPower) {
  for (int i = 0; i <= 2; i++) {
    PhasePower[i].power         = round1(totalPower * 0.3333);
    PhasePower[i].voltage       = round1(defaultVoltage);
    PhasePower[i].current       = round2(PhasePower[i].power / PhasePower[i].voltage);
    PhasePower[i].apparentPower = round2(PhasePower[i].power);
    PhasePower[i].powerFactor   = defaultPowerFactor;
    PhasePower[i].frequency     = defaultFrequency;
  }
  //DEBUG_SERIAL.print("Current total power: ");
  DEBUG_SERIAL.printf("   %.1f[W]\r\n",totalPower);
}

void setPowerData(double phase1Power, double phase2Power, double phase3Power) {
  PhasePower[0].power = phase1Power;
  PhasePower[1].power = phase2Power;
  PhasePower[2].power = phase3Power;
  for (int i = 0; i <= 2; i++) {
    PhasePower[i].voltage = round1(defaultVoltage); 
    PhasePower[i].current = round2(PhasePower[i].power / PhasePower[i].voltage);
    PhasePower[i].apparentPower = round2(PhasePower[i].power);
    PhasePower[i].powerFactor = defaultPowerFactor;
    PhasePower[i].frequency   = defaultFrequency;
  }
  DEBUG_SERIAL.print("Current power L1: ");
  DEBUG_SERIAL.print(phase1Power);
  DEBUG_SERIAL.print(" - L2: ");
  DEBUG_SERIAL.print(phase2Power);
  DEBUG_SERIAL.print(" - L3: ");
  DEBUG_SERIAL.print(phase3Power);
}

void setEnergyData(double totalEnergyGridSupply, double totalEnergyGridFeedIn) {
  for (int i = 0; i <= 2; i++) {
    PhaseEnergy[i].consumption = round2(totalEnergyGridSupply * 0.3333);
    PhaseEnergy[i].gridfeedin = round2(totalEnergyGridFeedIn * 0.3333);
  }
  DEBUG_SERIAL.print("Total consuption: ");
  DEBUG_SERIAL.print(totalEnergyGridSupply);
  DEBUG_SERIAL.print(" - Total Grid Feed-In: ");
  DEBUG_SERIAL.println(totalEnergyGridFeedIn);
}

//callback notifying us of the need to save WifiManager config
void saveConfigCallback() {
  DEBUG_SERIAL.println("Should save config");
  shouldSaveConfig = true;
}

void setJsonPathPower(JsonDocument json) {
  if (strcmp(power_path, "TRIPHASE") == 0) {
    DEBUG_SERIAL.println("resolving triphase");
    double power1 = resolveJsonPath(json, power_l1_path);
    double power2 = resolveJsonPath(json, power_l2_path);
    double power3 = resolveJsonPath(json, power_l3_path);
    setPowerData(power1, power2, power3);
  } else {
    // Check if BOTH paths (Import = power_path, Export = pwr_export_path) are defined
    if ((strcmp(power_path, "") != 0) && (strcmp(pwr_export_path, "") != 0)) {
      DEBUG_SERIAL.println("Resolving net power (import - export)");
      double importPower = resolveJsonPath(json, power_path).as<double>();
      double exportPower = resolveJsonPath(json, pwr_export_path).as<double>();
      double netPower = importPower - exportPower;
      setPowerData(netPower);
    }
    // (FALLBACK): Only the normal power_path (import path) is defined (old logic)
    else if (strcmp(power_path, "") != 0) {
      DEBUG_SERIAL.println("Resolving monophase (single path only)");
      double power = resolveJsonPath(json, power_path).as<double>();
      setPowerData(power);
    }
  }
  if ((strcmp(energy_in_path, "") != 0) && (strcmp(energy_out_path, "") != 0)) {
    double energyIn = resolveJsonPath(json, energy_in_path);
    double energyOut = resolveJsonPath(json, energy_out_path);
    setEnergyData(energyIn, energyOut);
  }
}

void rpcWrapper() {
  JsonDocument jsonResponse;
  JsonDocument doc;
  deserializeJson(doc, serJsonResponse);
  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  if (strcmp(rpcUser, "EMPTY") != 0) {
    jsonResponse["dst"] = rpcUser;
  }
  jsonResponse["result"] = doc;
  serializeJson(jsonResponse, serJsonResponse);
}

void blinkled(int duration) {
  if (led > 0) {
    if (led_i) {
      digitalWrite(led, HIGH);
    } else {
      digitalWrite(led, LOW);
    }
    ledOffTime = millis() + duration;
  }
}

void handleblinkled() {
  if (led > 0) {
    if (ledOffTime > 0 && millis() > ledOffTime) {
      if (led_i) {
        digitalWrite(led, LOW);
      } else {
        digitalWrite(led, HIGH);
      }
      ledOffTime = 0;
    }
  }
}

void GetDeviceInfo() {
  JsonDocument jsonResponse;
  jsonResponse["name"] = shelly_name;
  jsonResponse["id"] = shelly_name;
  jsonResponse["mac"] = shelly_mac;
  jsonResponse["slot"] = 1;
  jsonResponse["model"] = "SPEM-003CEBEU";
  jsonResponse["gen"] = shelly_gen;
  jsonResponse["fw_id"] = shelly_fw_id;
  jsonResponse["ver"] = "1.4.4";
  jsonResponse["app"] = "Pro3EM";
  jsonResponse["auth_en"] = false;
  jsonResponse["profile"] = "triphase";
  serializeJson(jsonResponse, serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
  blinkled(ledblinkduration);
}


/*
Example Data for EM3 Pro
id	0
a_current	0.593
a_voltage	230.5
a_act_power	-75.4
a_aprt_power	136.9
a_pf	0.68
a_freq	50
b_current	11.608
b_voltage	228.5
b_act_power	2655.2
b_aprt_power	2656.6
b_pf	1
b_freq	50
c_current	0.058
c_voltage	232.1
c_act_power	2.1
c_aprt_power	13.5
c_pf	0.54
c_freq	50
n_current	null
total_current	12.259
total_act_power	2581.781
total_aprt_power	2806.935
user_calibrated_phase	[]

*/

void EMGetStatus() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = 0;
  jsonResponse["a_current"] = PhasePower[0].current;
  jsonResponse["a_voltage"] = PhasePower[0].voltage;
  jsonResponse["a_act_power"] = PhasePower[0].power;
  jsonResponse["a_aprt_power"] = PhasePower[0].apparentPower;
  jsonResponse["a_pf"] = PhasePower[0].powerFactor;
  jsonResponse["a_freq"] = PhasePower[0].frequency;
  jsonResponse["b_current"] = PhasePower[1].current;
  jsonResponse["b_voltage"] = PhasePower[1].voltage;
  jsonResponse["b_act_power"] = PhasePower[1].power;
  jsonResponse["b_aprt_power"] = PhasePower[1].apparentPower;
  jsonResponse["b_pf"] = PhasePower[1].powerFactor;
  jsonResponse["b_freq"] = PhasePower[1].frequency;
  jsonResponse["c_current"] = PhasePower[2].current;
  jsonResponse["c_voltage"] = PhasePower[2].voltage;
  jsonResponse["c_act_power"] = PhasePower[2].power;
  jsonResponse["c_aprt_power"] = PhasePower[2].apparentPower;
  jsonResponse["c_pf"] = PhasePower[2].powerFactor;
  jsonResponse["c_freq"] = PhasePower[2].frequency;
  jsonResponse["total_current"] = round2((PhasePower[0].power + PhasePower[1].power + PhasePower[2].power) / ((float)defaultVoltage));
  jsonResponse["total_act_power"] = round2(PhasePower[0].power + PhasePower[1].power + PhasePower[2].power);
  jsonResponse["total_aprt_power"] = round2(PhasePower[0].apparentPower + PhasePower[1].apparentPower + PhasePower[2].apparentPower);
  serializeJson(jsonResponse, serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
  blinkled(ledblinkduration);
}

void EMDataGetStatus() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = 0;
  jsonResponse["a_total_act_energy"] = PhaseEnergy[0].consumption;
  jsonResponse["a_total_act_ret_energy"] = PhaseEnergy[0].gridfeedin;
  jsonResponse["b_total_act_energy"] = PhaseEnergy[1].consumption;
  jsonResponse["b_total_act_ret_energy"] = PhaseEnergy[1].gridfeedin;
  jsonResponse["c_total_act_energy"] = PhaseEnergy[2].consumption;
  jsonResponse["c_total_act_ret_energy"] = PhaseEnergy[2].gridfeedin;
  jsonResponse["total_act"] = PhaseEnergy[0].consumption + PhaseEnergy[1].consumption + PhaseEnergy[2].consumption;
  jsonResponse["total_act_ret"] = PhaseEnergy[0].gridfeedin + PhaseEnergy[1].gridfeedin + PhaseEnergy[2].gridfeedin;
  serializeJson(jsonResponse, serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
  blinkled(ledblinkduration);
}

void EMGetConfig() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = 0;
  jsonResponse["name"] = nullptr;
  jsonResponse["blink_mode_selector"] = "active_energy";
  jsonResponse["phase_selector"] = "a";
  jsonResponse["monitor_phase_sequence"] = true;
  jsonResponse["ct_type"] = "120A";
  serializeJson(jsonResponse, serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
  blinkled(ledblinkduration);
}

void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  JsonDocument json;
  switch (type) {
    case WS_EVT_DISCONNECT:
      DEBUG_SERIAL.printf("[%u] Websocket: disconnected!\n", client->id());
      break;
    case WS_EVT_CONNECT:
      DEBUG_SERIAL.printf("[%u] Websocket: connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DATA:
      {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          data[len] = 0;
          deserializeJson(json, data);
          rpcId = json["id"];
          if (json["method"] == "Shelly.GetDeviceInfo") {
            strcpy(rpcUser, "EMPTY");
            GetDeviceInfo();
            rpcWrapper();
            webSocket.textAll(serJsonResponse);
          } else if (json["method"] == "EM.GetStatus") {
            strcpy(rpcUser, json["src"]);
            EMGetStatus();
            rpcWrapper();
            webSocket.textAll(serJsonResponse);
          } else if (json["method"] == "EMData.GetStatus") {
            strcpy(rpcUser, json["src"]);
            EMDataGetStatus();
            rpcWrapper();
            webSocket.textAll(serJsonResponse);
          } else if (json["method"] == "EM.GetConfig") {
            EMGetConfig();
            rpcWrapper();
            webSocket.textAll(serJsonResponse);
          } else {
            DEBUG_SERIAL.printf("Websocket: unknown request: %s\n", data);
          }
        }
        break;
      }
    case WS_EVT_PING:
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  JsonDocument json;
  deserializeJson(json, payload, length);
  setJsonPathPower(json);
}

void mqtt_reconnect() {
  DEBUG_SERIAL.print("Attempting MQTT connection...");
  if (mqtt_client.connect(shelly_name, String(mqtt_user).c_str(), String(mqtt_passwd).c_str())) {
    DEBUG_SERIAL.println("connected");
    mqtt_client.subscribe(mqtt_topic);
  } else {
    DEBUG_SERIAL.print("failed, rc=");
    DEBUG_SERIAL.print(mqtt_client.state());
    DEBUG_SERIAL.println(" try again in 5 seconds");
    delay(5000);
  }
}

void parseUdpRPC() {
  uint8_t buffer[1024];
  int packetSize = UdpRPC.parsePacket();
  if (packetSize) {
    JsonDocument json;
    int rSize = UdpRPC.read(buffer, 1024);
    buffer[rSize] = 0;
    DEBUG_SERIAL.print("Received UDP packet on port 1010: ");
    DEBUG_SERIAL.println((char *)buffer);
    deserializeJson(json, buffer);
    if (json["method"].is<JsonVariant>()) {
      rpcId = json["id"];
      strcpy(rpcUser, "EMPTY");
      UdpRPC.beginPacket(UdpRPC.remoteIP(), UdpRPC.remotePort());
      if (json["method"] == "Shelly.GetDeviceInfo") {
        GetDeviceInfo();
        rpcWrapper();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if (json["method"] == "EM.GetStatus") {
        EMGetStatus();
        rpcWrapper();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if (json["method"] == "EMData.GetStatus") {
        EMDataGetStatus();
        rpcWrapper();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if (json["method"] == "EM.GetConfig") {
        EMGetConfig();
        rpcWrapper();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else {
        DEBUG_SERIAL.printf("RPC over UDP: unknown request: %s\n", buffer);
      }
      UdpRPC.endPacket();
    }
  }
}

void parseSMA() {
  uint8_t buffer[1024];
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int rSize = Udp.read(buffer, 1024);
    if (buffer[0] != 'S' || buffer[1] != 'M' || buffer[2] != 'A') {
      DEBUG_SERIAL.println("Not an SMA packet?");
      return;
    }
    uint16_t grouplen;
    uint16_t grouptag;
    uint8_t *offset = buffer + 4;
    do {
      grouplen = (offset[0] << 8) + offset[1];
      grouptag = (offset[2] << 8) + offset[3];
      offset += 4;
      if (grouplen == 0xffff) return;
      if (grouptag == 0x02A0 && grouplen == 4) {
        offset += 4;
      } else if (grouptag == 0x0010) {
        uint8_t *endOfGroup = offset + grouplen;
        // uint16_t protocolID = (offset[0] << 8) + offset[1];
        offset += 2;
        // uint16_t susyID = (offset[0] << 8) + offset[1];
        offset += 2;
        uint32_t serial = (offset[0] << 24) + (offset[1] << 16) + (offset[2] << 8) + offset[3];
        DEBUG_SERIAL.print("Received SMA multicast from ");
        DEBUG_SERIAL.println(serial);
        if ((strcmp(sma_id, "") != 0) && (String(sma_id).toInt() != serial)) {
          DEBUG_SERIAL.println("SMA serial not matching - ignoring packet");
          break;
        }
        offset += 4;
        // uint32_t timestamp = (offset[0] << 24) + (offset[1] << 16) + (offset[2] << 8) + offset[3];
        offset += 4;
        while (offset < endOfGroup) {
          uint8_t channel = offset[0];
          uint8_t index = offset[1];
          uint8_t type = offset[2];
          // uint8_t tarif = offset[3];
          offset += 4;
          if (type == 8) {
            uint64_t data = ((uint64_t)offset[0] << 56) + ((uint64_t)offset[1] << 48) + ((uint64_t)offset[2] << 40) + ((uint64_t)offset[3] << 32) + ((uint64_t)offset[4] << 24) + ((uint64_t)offset[5] << 16) + ((uint64_t)offset[6] << 8) + offset[7];
            offset += 8;
            switch (index) {
              case 21:
                PhaseEnergy[0].consumption = data / 3600000;
                break;
              case 22:
                PhaseEnergy[0].gridfeedin = data / 3600000;
                break;
              case 41:
                PhaseEnergy[1].consumption = data / 3600000;
                break;
              case 42:
                PhaseEnergy[1].gridfeedin = data / 3600000;
                break;
              case 61:
                PhaseEnergy[2].consumption = data / 3600000;
                break;
              case 62:
                PhaseEnergy[2].gridfeedin = data / 3600000;
                break;
            }
          } else if (type == 4) {
            uint32_t data = (offset[0] << 24) + (offset[1] << 16) + (offset[2] << 8) + offset[3];
            offset += 4;
            switch (index) {
              case 1:
                // 1.4.0 Total grid power in dW - unused
                break;
              case 2:
                // 2.4.0 Total feed-in power in dW - unused
                break;
              case 21:
                PhasePower[0].power = round2(data * 0.1);
                PhasePower[0].frequency = defaultFrequency;
                break;
              case 22:
                PhasePower[0].power -= round2(data * 0.1);
                break;
              case 29:
                PhasePower[0].apparentPower = round2(data * 0.1);
                break;
              case 30:
                PhasePower[0].apparentPower -= round2(data * 0.1);
                break;
              case 31:
                PhasePower[0].current = round2(data * 0.001);
                break;
              case 32:
                PhasePower[0].voltage = round2(data * 0.001);
                break;
              case 33:
                PhasePower[0].powerFactor = round2(data * 0.001);
                break;
              case 41:
                PhasePower[1].power = round2(data * 0.1);
                PhasePower[1].frequency = defaultFrequency;
                break;
              case 42:
                PhasePower[1].power -= round2(data * 0.1);
                break;
              case 49:
                PhasePower[1].apparentPower = round2(data * 0.1);
                break;
              case 50:
                PhasePower[1].apparentPower -= round2(data * 0.1);
                break;
              case 51:
                PhasePower[1].current = round2(data * 0.001);
                break;
              case 52:
                PhasePower[1].voltage = round2(data * 0.001);
                break;
              case 53:
                PhasePower[1].powerFactor = round2(data * 0.001);
                break;
              case 61:
                PhasePower[2].power = round2(data * 0.1);
                PhasePower[2].frequency = defaultFrequency;
                break;
              case 62:
                PhasePower[2].power -= round2(data * 0.1);
                break;
              case 69:
                PhasePower[2].apparentPower = round2(data * 0.1);
                break;
              case 70:
                PhasePower[2].apparentPower -= round2(data * 0.1);
                break;
              case 71:
                PhasePower[2].current = round2(data * 0.001);
                break;
              case 72:
                PhasePower[2].voltage = round2(data * 0.001);
                break;
              case 73:
                PhasePower[2].powerFactor = round2(data * 0.001);
                break;
              default:
                break;
            }
          } else if (channel == 144) {
            // optional handling of version number
            offset += 4;
          } else {
            offset += type;
            DEBUG_SERIAL.println("Unknown measurement");
          }
        }
      } else if (grouptag == 0) {
        // end marker
        offset += grouplen;
      } else {
        DEBUG_SERIAL.print("unhandled group ");
        DEBUG_SERIAL.print(grouptag);
        DEBUG_SERIAL.print(" with len=");
        DEBUG_SERIAL.println(grouplen);
        offset += grouplen;
      }
    } while (grouplen > 0 && offset + 4 < buffer + rSize);
  }
}

void parseSHRDZM() {
  JsonDocument json;
  uint8_t buffer[1024];
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int rSize = Udp.read(buffer, 1024);
    buffer[rSize] = 0;
    deserializeJson(json, buffer);
    if (json["data"]["16.7.0"].is<JsonVariant>()) {
      double power = json["data"]["16.7.0"];
      setPowerData(power);
    }
    if (json["data"]["1.8.0"].is<JsonVariant>() && json["data"]["2.8.0"].is<JsonVariant>()) {
      double energyIn = 0.001 * json["data"]["1.8.0"].as<double>();
      double energyOut = 0.001 * json["data"]["2.8.0"].as<double>();
      setEnergyData(energyIn, energyOut);
    }
  }
}

double SUNSPEC_scale(int n)
{
  double val=1.0;
  switch (n) {
    case -3: val=0.001; break;
    case -2: val=0.01; break;
    case -1: val=0.1; break;
    case 0: val=1.0; break;
    case 1: val=10.0; break;
    case 2: val=100.0; break;
    default:
    val=1.0;
  }
  return val;
}

void parseSUNSPEC() {
  #define SUNSPEC_BASE 40072
  #define SUNSPEC_VOLTAGE 40077
  #define SUNSPEC_VOLTAGE_SCALE 40084
  #define SUNSPEC_REAL_POWER 40088
  #define SUNSPEC_REAL_POWER_SCALE 40091
  #define SUNSPEC_APPARANT_POWER 40093
  #define SUNSPEC_APPARANT_POWER_SCALE 40096
  #define SUNSPEC_CURRENT 40072
  #define SUNSPEC_CURRENT_SCALE 40075
  #define SUNSPEC_POWER_FACTOR 40103
  #define SUNSPEC_POWER_FACTOR_SCALE 40106
  #define SUNSPEC_FREQUENCY 40085
  #define SUNSPEC_FREQUENCY_SCALE 40086
  
  modbus_ip.fromString(mqtt_server);
  if (!modbus1.isConnected(modbus_ip)) {
    modbus1.connect(modbus_ip, String(mqtt_port).toInt());
  } else {
    uint16_t transaction = modbus1.readHreg(modbus_ip, SUNSPEC_BASE, (uint16_t*) &modbus_result[0], 64, nullptr, String(modbus_dev).toInt());
    delay(10);
    modbus1.task();
    int t = 0;
    while (modbus1.isTransaction(transaction)) {
      modbus1.task();
      delay(10);
      t++;
      if (t > 50) {
        DEBUG_SERIAL.println("Timeout SUNSPEC");
        //prolong=10;
        modbus1.disconnect(modbus_ip);
        break;
      }
    }
    int32_t power = 0;
    if (t<=50) {
      double scale_V=SUNSPEC_scale(modbus_result[SUNSPEC_VOLTAGE_SCALE-SUNSPEC_BASE]);
      double scale_real_power=SUNSPEC_scale(modbus_result[SUNSPEC_REAL_POWER_SCALE-SUNSPEC_BASE]);
      double scale_apparant_power=SUNSPEC_scale(modbus_result[SUNSPEC_APPARANT_POWER_SCALE-SUNSPEC_BASE]);
      double scale_current=SUNSPEC_scale(modbus_result[SUNSPEC_CURRENT_SCALE-SUNSPEC_BASE]);
      double scale_powerfactor=SUNSPEC_scale(modbus_result[SUNSPEC_POWER_FACTOR_SCALE-SUNSPEC_BASE]);
      double scale_frequency=SUNSPEC_scale(modbus_result[SUNSPEC_FREQUENCY_SCALE-SUNSPEC_BASE]);

      for (int n=0;n<3;n++) {
        PhasePower[n].power=modbus_result[SUNSPEC_REAL_POWER-SUNSPEC_BASE+n]*scale_real_power;
        PhasePower[n].apparentPower=modbus_result[SUNSPEC_APPARANT_POWER-SUNSPEC_BASE+n]*scale_apparant_power;
        PhasePower[n].current= modbus_result[SUNSPEC_CURRENT-SUNSPEC_BASE+n]*scale_current;
        PhasePower[n].powerFactor=modbus_result[SUNSPEC_POWER_FACTOR-SUNSPEC_BASE+n]*scale_powerfactor;
        PhasePower[n].voltage=modbus_result[SUNSPEC_VOLTAGE-SUNSPEC_BASE+n]*scale_V;
        PhasePower[n].frequency=modbus_result[SUNSPEC_FREQUENCY-SUNSPEC_BASE]*scale_frequency;
        power+= PhasePower[n].power;
      }

      #define SUNSPEC_REAL_ENERGY_EXPORTED 40109
      #define SUNSPEC_REAL_IMPORTED_EXPORTED 40117
      #define SUNSPEC_REAL_ENERGY_SCALE 40123
      double scale_real_energy=SUNSPEC_scale(modbus_result[SUNSPEC_REAL_ENERGY_SCALE-SUNSPEC_BASE]);
        for (int n=0;n<3;n++) {
          uint32_t p=0;
          uint8_t *p_u8=(uint8_t *)&modbus_result[SUNSPEC_REAL_IMPORTED_EXPORTED-SUNSPEC_BASE+2*n];
          p|=((uint32_t)p_u8[2])<<0;
        p|=((uint32_t)p_u8[3])<<8;
        p|=((uint32_t)p_u8[0])<<16;
        p|=((uint32_t)p_u8[1])<<24;
          PhaseEnergy[n].consumption=p/1000.0*scale_real_energy;
          p=0;
          p_u8=(uint8_t *)&modbus_result[SUNSPEC_REAL_ENERGY_EXPORTED-SUNSPEC_BASE+2*n];
          p|=((uint32_t)p_u8[2])<<0;
        p|=((uint32_t)p_u8[3])<<8;
        p|=((uint32_t)p_u8[0])<<16;
        p|=((uint32_t)p_u8[1])<<24;
          PhaseEnergy[n].gridfeedin = -p/1000.0*scale_real_energy;
        }
    }
    DEBUG_SERIAL.printf("SUNSPEC power: %d,%d\n\r", t, power);
  }
}

void queryHTTP() {
  JsonDocument json;
  DEBUG_SERIAL.println("Querying HTTP source");
  http.begin(wifi_client, mqtt_server);
  http.GET();
  deserializeJson(json, http.getStream());
  if (strcmp(power_path, "") == 0) {
    DEBUG_SERIAL.println("HTTP query: no JSONPath for power data provided");
  } else {
    setJsonPathPower(json);
  }
  http.end();
}

// functions for Tibber-Pulse 

 enum { SMLPAYLOADMAXSIZE = 300 }; 
  byte smlpayload[SMLPAYLOADMAXSIZE] {0}; 
/// @brief Helper function for parsing Tibber-pulse SML-Message
/// @param payload 
/// @param smlcode 
/// @param smlsize 
/// @param offset 
/// @return 
bool decodeSMLval(uint32_t &retval, byte * payload, byte* smlcode, uint smlsize,  uint offset) {
  byte *loc = (byte*)memmem(payload, SMLPAYLOADMAXSIZE, smlcode, smlsize);
  if (loc == NULL) {
     DEBUG_SERIAL.printf("ERROR decodeSMLval ID:'%x.%x.%x' not found\r\n", smlcode[4], smlcode[5], smlcode[6]);
    return false;
  }

  // rean 'nlen' from SML-Data , change dynamic for different Power values !!!)
  uint8_t nlen = (loc[offset-1] & 0x0F) -1;

  /*
  // for extra debugging
  debug_printf("nlen=%d\r\n", nlen);
  debug_print("HEX: ");
  for (size_t i = 0; i < offset+nlen+1; i++)
  {
    debug_printf("%02x ", loc[i]);
  }
  debug_println();
  */
  
  if ((nlen < 1) ||(nlen > 8))
  {
    DEBUG_SERIAL.printf("decodeSMLval unvalid length %d\r\n", nlen);
    return false;
  }
  
  
 byte* pval = loc + offset;
 uint32_t value=0;
 if (nlen == 1){
   value = (int8_t) *(pval);
 }
 else {
  nlen=nlen+1; 
  while (--nlen) {
      //value<<=8;
      value = value << 8;
      value|=*(pval)++;
  }
 }
 retval = value;
  return true;
}

// Tibber-Pulse Adapter
// REMARKS:
// - we need intensive validating of data, because auf weak 868Mhz transmission (no handshake!) from meter adapter to wifi-bridge
// - only tested with my ISKRA-meter
/// @brief query Tibber-Pulse SML raw message
/// @return 
bool queryTibberPulseHTTP() {
  bool ret = true;
  int getlength = 0;
  //DEBUG_SERIAL.print("Querying Tibber-Pulse raw SML: ");
  String url = "http://";
  url += String(tibber_url);
  url += String(tibber_rpc);
  //DEBUG_SERIAL.printf("Tibber URL:%s user:%s \r\n", url.c_str(), tibber_user);
  http.begin(wifi_client, url);
  http.setAuthorization(tibber_user, tibber_password);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    getlength = http.getSize();
    //DEBUG_SERIAL.printf(" message size=%d\r\n", getlength);
    if ((getlength > SMLPAYLOADMAXSIZE) || (getlength ==0)) {
      http.end();
      return false;
    }
    WiFiClient * w = http.getStreamPtr();
    w->readBytes(smlpayload, getlength);

    if (getlength < 260) // todo: change for other meter models
    {
     //DEBUG_SERIAL.printf("ERROR SML-data to short! length=%d \r\n", getlength);
     /* for extra debugging
     for (size_t i = 0; i < getlength; i++) {
       DEBUG_SERIAL.printf("%02xh ",smlpayload[i]);
     }
     DEBUG_SERIAL.println();
     */
     ret = false;
    } 
    else {
      byte sml_1_8_0[] {0x77, 0x07, 0x01, 0x00, 0x01, 0x08, 0x00, 0xff};   // Energy IN (1.8.0)
      byte sml_2_8_0[] {0x77, 0x07, 0x01, 0x00, 0x02, 0x08, 0x00, 0xff};   // Energy OUT (2.8.0)
      uint32_t inputenergy  = 0;
      uint32_t outputenergy = 0;
      if (!decodeSMLval(inputenergy,smlpayload, sml_1_8_0, sizeof(sml_1_8_0), 19))
      {
        ret = false;
      }
      if (!decodeSMLval(outputenergy, smlpayload, sml_2_8_0, sizeof(sml_2_8_0), 15)) 
      {
        ret = false;
      }

      if (ret == true)
      {
        setEnergyData(double(inputenergy/ 10000.0), double(outputenergy / 10000.0));
      }
     
      // for Power:

      // length is variable !!! (2 byte= for big values)
      //                                           53=16bit int
      // 77 07 01 00 10 07 00 ff 01 01 62 1b 52 00 53 00 91 01 =145W
      // 77 07 01 00 10 07 00 ff 01 01 62 1b 52 00 53 00 95 01 =149W
      // 77 07 01 00 10 07 00 ff 01 01 62 1b 52 00 53 00 8d 01 =141W

      // length is variable !!! (1 byte for small values)
      //                                           52=8bit int 
      // 77 07 01 00 10 07 00 ff 01 01 62 1b 52 00 52 76 01 01 
      // 77 07 01 00 10 07 00 ff 01 01 62 1b 52 00 52 66 01 01

      // Power Sum(in= pos. out= neg)
      byte sml_16_7_0[] {0x77, 0x07, 0x01, 0x00, 0x10, 0x07, 0x00, 0xff};
      uint32_t watt = 0;
      if (decodeSMLval(watt, smlpayload, sml_16_7_0, sizeof(sml_16_7_0), 15)) 
      {
        setPowerData(int16_t(watt));
      }
      else {
        //DEBUG_SERIAL.println ("SML ERROR Parsing sml_16_7_0");
        ret = false;
      }
      // extra debug info
      //if (ret == true) {
      // DEBUG_SERIAL.printf("SML OK raw Values:16.7.0:%d  1.8.0:%d  2.8.0:%d\r\n", watt, inputenergy, outputenergy);
      //}
    }
  }
  else 
  {
    //DEBUG_SERIAL.printf("TIBBER httpGETRequest Error code:%d \r\n", httpResponseCode);
    ret =false;
  }
  // Free resources
  http.end();

  return ret;
}

void WifiManagerSetup() {
  // Set Shelly ID to ESP's MAC address by default
  uint8_t mac[6];
  WiFi.macAddress(mac);
  sprintf(shelly_mac, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  preferences.begin("e2s_config", false);
  strcpy(input_type, preferences.getString("input_type", input_type).c_str());
  strcpy(mqtt_server, preferences.getString("mqtt_server", mqtt_server).c_str());
  strcpy(query_period, preferences.getString("query_period", query_period).c_str());
  strcpy(led_gpio, preferences.getString("led_gpio", led_gpio).c_str());
  strcpy(led_gpio_i, preferences.getString("led_gpio_i", led_gpio_i).c_str());
  strcpy(shelly_mac, preferences.getString("shelly_mac", shelly_mac).c_str());
  strcpy(mqtt_port, preferences.getString("mqtt_port", mqtt_port).c_str());
  strcpy(mqtt_topic, preferences.getString("mqtt_topic", mqtt_topic).c_str());
  strcpy(mqtt_user, preferences.getString("mqtt_user", mqtt_user).c_str());
  strcpy(mqtt_passwd, preferences.getString("mqtt_passwd", mqtt_passwd).c_str());
  strcpy(modbus_dev, preferences.getString("modbus_dev", modbus_dev).c_str());
  strcpy(power_path, preferences.getString("power_path", power_path).c_str());
  strcpy(pwr_export_path, preferences.getString("pwr_export_path", pwr_export_path).c_str());
  strcpy(power_l1_path, preferences.getString("power_l1_path", power_l1_path).c_str());
  strcpy(power_l2_path, preferences.getString("power_l2_path", power_l2_path).c_str());
  strcpy(power_l3_path, preferences.getString("power_l3_path", power_l3_path).c_str());
  strcpy(energy_in_path, preferences.getString("energy_in_path", energy_in_path).c_str());
  strcpy(energy_out_path, preferences.getString("energy_out_path", energy_out_path).c_str());
  strcpy(shelly_port, preferences.getString("shelly_port", shelly_port).c_str());
  strcpy(force_pwr_decimals, preferences.getString("force_pwr_decimals", force_pwr_decimals).c_str());
  strcpy(sma_id, preferences.getString("sma_id", sma_id).c_str());
   // Tibber-Pulse:
  strcpy(tibber_url,      preferences.getString("tibber_url" ,     tibber_url).c_str());
  strcpy(tibber_user,     preferences.getString("tibber_user",     tibber_user).c_str());
  strcpy(tibber_password, preferences.getString("tibber_password", tibber_password).c_str());
  
  WiFiManagerParameter custom_section1("<h3>General settings</h3>");
  WiFiManagerParameter custom_input_type("type", "<b>Data source</b><br><code>MQTT</code> for MQTT<br><code>HTTP</code> for generic HTTP<br><code>SMA</code> for SMA EM/HM multicast<br><code>SHRDZM</code> for SHRDZM UDP data<br><code>SUNSPEC</code> for Modbus TCP SUNSPEC data<br><code>TIBBERPULSE</code> for Tibber-Pulse", input_type, 40);

  WiFiManagerParameter custom_mqtt_server("server", "<b>Server</b><br>MQTT Server IP, query url for generic HTTP or Modbus TCP server IP for SUNSPEC", mqtt_server, 80);
  WiFiManagerParameter custom_mqtt_port("port", "<b>Port</b><br> for MQTT or Modbus TCP (SUNSPEC)", mqtt_port, 6);
  WiFiManagerParameter custom_query_period("query_period", "<b>Query period</b><br>for generic HTTP, SUNSPEC and TIBBERPULSE, in milliseconds", query_period, 10);
  WiFiManagerParameter custom_led_gpio("led_gpio", "<b>GPIO</b><br>of internal LED", led_gpio, 3);
  WiFiManagerParameter custom_led_gpio_i("led_gpio_i", "<b>GPIO is inverted</b><br><code>true</code> or <code>false</code>", led_gpio_i, 6);
  WiFiManagerParameter custom_shelly_mac("mac", "<b>Shelly ID</b><br>12 char hexadecimal, defaults to MAC address of ESP", shelly_mac, 13);
  WiFiManagerParameter custom_shelly_port("shelly_port", "<b>Shelly UDP port</b><br><code>1010</code> for old Marstek FW, <code>2220</code> for new Marstek FW v226+/v108+", shelly_port, 6);
  WiFiManagerParameter custom_force_pwr_decimals("force_pwr_decimals", "<b>Force decimals numbers for Power values</b><br><code>true</code> to fix Marstek bug", force_pwr_decimals, 6);
  WiFiManagerParameter custom_sma_id("sma_id", "<b>SMA serial number</b><br>optional serial number if you have more than one SMA EM/HM in your network", sma_id, 16);
  WiFiManagerParameter custom_section2("<hr><h3>MQTT options</h3>");
  WiFiManagerParameter custom_mqtt_topic("topic", "<b>MQTT Topic</b>", mqtt_topic, 90);
  WiFiManagerParameter custom_mqtt_user("user", "<b>MQTT user</b><br>optional", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_passwd("passwd", "<b>MQTT password</b><br>optional", mqtt_passwd, 40);
  WiFiManagerParameter custom_section3("<hr><h3>Modbus TCP options</h3>");
  WiFiManagerParameter custom_modbus_dev("modbus_dev", "<b>Modbus device ID</b><br><code>71</code> for Kostal SEM", modbus_dev, 60);
  WiFiManagerParameter custom_section4("<hr><h3>JSON paths for MQTT and generic HTTP</h3>");
  WiFiManagerParameter custom_power_path("power_path", "<b>Total power JSON path</b><br>e.g. <code>ENERGY.Power</code> or <code>TRIPHASE</code> for tri-phase data", power_path, 60);
  WiFiManagerParameter custom_pwr_export_path("pwr_export_path", "<b>Export power JSON path</b><br>Optional, for net calc (e.g. \"i-e\"", pwr_export_path, 60);
  WiFiManagerParameter custom_power_l1_path("power_l1_path", "<b>Phase 1 power JSON path</b><br>optional", power_l1_path, 60);
  WiFiManagerParameter custom_power_l2_path("power_l2_path", "<b>Phase 2 power JSON path</b><br>Phase 2 power JSON path<br>optional", power_l2_path, 60);
  WiFiManagerParameter custom_power_l3_path("power_l3_path", "<b>Phase 3 power JSON path</b><br>Phase 3 power JSON path<br>optional", power_l3_path, 60);
  WiFiManagerParameter custom_energy_in_path("energy_in_path", "<b>Energy from grid JSON path</b><br>e.g. <code>ENERGY.Grid</code>", energy_in_path, 60);
  WiFiManagerParameter custom_energy_out_path("energy_out_path", "<b>Energy to grid JSON path</b><br>e.g. <code>ENERGY.FeedIn</code>", energy_out_path, 60);
  // for Tibber-Pulse
  WiFiManagerParameter custom_section5("<hr><h3>Tibber-Pulse</h3>");
  WiFiManagerParameter custom_tibber_url("url"                 , "<b>url</b><br>e.g.:<code>192.168.xx.xx</code>", tibber_url, 32);
  WiFiManagerParameter custom_tibber_user("tibber_user"        , "<b>user</b><br><code>admin</code>",             tibber_user, 32);
  WiFiManagerParameter custom_tibber_password("tibber_password", "<b>password</b><br>form:<code>xxxx-xxxx</code>",tibber_password, 32);

  WiFiManager wifiManager;
  if (!DEBUG) {
    wifiManager.setDebugOutput(false);
  }
  wifiManager.setTitle("Energy2Shelly for ESP");
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_section1);
  wifiManager.addParameter(&custom_input_type);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_query_period);
  wifiManager.addParameter(&custom_led_gpio);
  wifiManager.addParameter(&custom_led_gpio_i);
  wifiManager.addParameter(&custom_shelly_mac);
  wifiManager.addParameter(&custom_shelly_port);
  wifiManager.addParameter(&custom_force_pwr_decimals);
  wifiManager.addParameter(&custom_sma_id);
  wifiManager.addParameter(&custom_section2);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_passwd);
  wifiManager.addParameter(&custom_section3);
  wifiManager.addParameter(&custom_modbus_dev);
  wifiManager.addParameter(&custom_section4);
  wifiManager.addParameter(&custom_power_path);
  wifiManager.addParameter(&custom_pwr_export_path);
  wifiManager.addParameter(&custom_power_l1_path);
  wifiManager.addParameter(&custom_power_l2_path);
  wifiManager.addParameter(&custom_power_l3_path);
  wifiManager.addParameter(&custom_energy_in_path);
  wifiManager.addParameter(&custom_energy_out_path);
  // for Tibber-Pulse:
  wifiManager.addParameter(&custom_section5);
  wifiManager.addParameter(&custom_tibber_url);
  wifiManager.addParameter(&custom_tibber_user);
  wifiManager.addParameter(&custom_tibber_password);
  

  if (!wifiManager.autoConnect("Energy2Shelly")) {
    DEBUG_SERIAL.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  DEBUG_SERIAL.println("connected");

  //read updated parameters
  strcpy(input_type, custom_input_type.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(query_period, custom_query_period.getValue());
  strcpy(led_gpio, custom_led_gpio.getValue());
  strcpy(led_gpio_i, custom_led_gpio_i.getValue());
  strcpy(shelly_mac, custom_shelly_mac.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_passwd, custom_mqtt_passwd.getValue());
  strcpy(modbus_dev, custom_modbus_dev.getValue());
  strcpy(power_path, custom_power_path.getValue());
  strcpy(pwr_export_path, custom_pwr_export_path.getValue());
  strcpy(power_l1_path, custom_power_l1_path.getValue());
  strcpy(power_l2_path, custom_power_l2_path.getValue());
  strcpy(power_l3_path, custom_power_l3_path.getValue());
  strcpy(energy_in_path, custom_energy_in_path.getValue());
  strcpy(energy_out_path, custom_energy_out_path.getValue());
  strcpy(shelly_port, custom_shelly_port.getValue());
  strcpy(force_pwr_decimals, custom_force_pwr_decimals.getValue());
  strcpy(sma_id, custom_sma_id.getValue());
   // for Tibber-Pulse:
  strcpy(tibber_url, custom_tibber_url.getValue());
  strcpy(tibber_user, custom_tibber_user.getValue());
  strcpy(tibber_password, custom_tibber_password.getValue());

  DEBUG_SERIAL.println("The values in the preferences are: ");
  DEBUG_SERIAL.println("\tinput_type : " + String(input_type));
  DEBUG_SERIAL.println("\tmqtt_server : " + String(mqtt_server));
  DEBUG_SERIAL.println("\tmqtt_port : " + String(mqtt_port));
  DEBUG_SERIAL.println("\tquery_period : " + String(query_period));
  DEBUG_SERIAL.println("\tled_gpio : " + String(led_gpio));
  DEBUG_SERIAL.println("\tled_gpio_i : " + String(led_gpio_i));
  DEBUG_SERIAL.println("\tshelly_mac : " + String(shelly_mac));
  DEBUG_SERIAL.println("\tmqtt_topic : " + String(mqtt_topic));
  DEBUG_SERIAL.println("\tmqtt_user : " + String(mqtt_user));
  DEBUG_SERIAL.println("\tmqtt_passwd : " + String(mqtt_passwd));
  DEBUG_SERIAL.println("\tmodbus_dev : " + String(modbus_dev));
  DEBUG_SERIAL.println("\tpower_path : " + String(power_path));
  DEBUG_SERIAL.println("\tpwr_export_path : " + String(pwr_export_path));
  DEBUG_SERIAL.println("\tpower_l1_path : " + String(power_l1_path));
  DEBUG_SERIAL.println("\tpower_l2_path : " + String(power_l2_path));
  DEBUG_SERIAL.println("\tpower_l3_path : " + String(power_l3_path));
  DEBUG_SERIAL.println("\tenergy_in_path : " + String(energy_in_path));
  DEBUG_SERIAL.println("\tenergy_out_path : " + String(energy_out_path));
  DEBUG_SERIAL.println("\tshelly_port : " + String(shelly_port));
  DEBUG_SERIAL.println("\tforce_pwr_decimals : " + String(force_pwr_decimals));
  DEBUG_SERIAL.println("\tsma_id : " + String(sma_id));
  // for Tibber-Pulse:
  DEBUG_SERIAL.println("\ttibber_url:" + String(tibber_url));
  DEBUG_SERIAL.println("\ttibber_user:" + String(tibber_user));
  DEBUG_SERIAL.println("\ttibber_password:" + String(tibber_password));

  if (strcmp(input_type, "SMA") == 0) {
    dataSMA = true;
    DEBUG_SERIAL.println("Enabling SMA Multicast data input");
  } else if (strcmp(input_type, "SHRDZM") == 0) {
    dataSHRDZM = true;
    DEBUG_SERIAL.println("Enabling SHRDZM UDP data input");
  } else if (strcmp(input_type, "HTTP") == 0) {
    dataHTTP = true;
    DEBUG_SERIAL.println("Enabling generic HTTP data input");
  } else if (strcmp(input_type, "SUNSPEC") == 0) {
    dataSUNSPEC = true;
    DEBUG_SERIAL.println("Enabling SUNSPEC data input");
  } 
  // for Tibber-Pulse:
  else if (strcmp(input_type, "TIBBERPULSE") == 0) {
    dataTIBBERPULSE = true;
    DEBUG_SERIAL.println("Enabling TIBBERPULSE data input");
  }
  else {
    dataMQTT = true;
    DEBUG_SERIAL.println("Enabling MQTT data input");
  }


  if (strcmp(led_gpio_i, "true") == 0) {
    led_i = true;
  } else {
    led_i = false;
  }

  if (strcmp(force_pwr_decimals, "true") == 0) {
    forcePwrDecimals = true;
  } else {
    forcePwrDecimals = false;
  }

  if (shouldSaveConfig) {
    DEBUG_SERIAL.println("saving config");
    preferences.putString("input_type", input_type);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putString("mqtt_port", mqtt_port);
    preferences.putString("query_period", query_period);
    preferences.putString("led_gpio", led_gpio);
    preferences.putString("led_gpio_i", led_gpio_i);
    preferences.putString("shelly_mac", shelly_mac);
    preferences.putString("mqtt_topic", mqtt_topic);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_passwd", mqtt_passwd);
    preferences.putString("modbus_dev", modbus_dev);
    preferences.putString("power_path", power_path);
    preferences.putString("pwr_export_path", pwr_export_path);
    preferences.putString("power_l1_path", power_l1_path);
    preferences.putString("power_l2_path", power_l2_path);
    preferences.putString("power_l3_path", power_l3_path);
    preferences.putString("energy_in_path", energy_in_path);
    preferences.putString("energy_out_path", energy_out_path);
    preferences.putString("shelly_port", shelly_port);
    preferences.putString("force_pwr_decimals", force_pwr_decimals);
    preferences.putString("sma_id", sma_id);
     // for Tibber-Pulse
    preferences.putString("tibber_url", tibber_url);
    preferences.putString("tibber_user", tibber_user);
    preferences.putString("tibber_password", tibber_password);

    wifiManager.reboot();
  }
  DEBUG_SERIAL.println("local ip");
  DEBUG_SERIAL.println(WiFi.localIP());
}

void setup(void) {
  DEBUG_SERIAL.begin(115200);
  WifiManagerSetup();

  if (String(led_gpio).toInt() > 0) {
    led = String(led_gpio).toInt();
  }

  if (led > 0) {
    pinMode(led, OUTPUT);
    if (led_i) {
      digitalWrite(led, LOW);
    } else {
      digitalWrite(led, HIGH);
    }
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "This is the Energy2Shelly for ESP converter!\r\nDevice and Energy status is available under /status\r\nTo reset configuration, goto /reset\r\n");
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    EMGetStatus();
    request->send(200, "application/json", serJsonResponse);
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    shouldResetConfig = true;
    request->send(200, "text/plain", "Resetting WiFi configuration, please log back into the hotspot to reconfigure...\r\n");
  });

  // Shelly Documentation
  // https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/EM/#emgetstatus-example
  server.on("/rpc/EM.GetStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    EMGetStatus();
    request->send(200, "application/json", serJsonResponse);
  });

  server.on("/rpc/EMData.GetStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    EMDataGetStatus();
    request->send(200, "application/json", serJsonResponse);
  });

  server.on("/rpc/EM.GetConfig", HTTP_GET, [](AsyncWebServerRequest *request) {
    EMGetConfig();
    request->send(200, "application/json", serJsonResponse);
  });

  server.on("/rpc/Shelly.GetDeviceInfo", HTTP_GET, [](AsyncWebServerRequest *request) {
    GetDeviceInfo();
    request->send(200, "application/json", serJsonResponse);
  });

  server.on("/rpc", HTTP_POST, [](AsyncWebServerRequest *request) {
    GetDeviceInfo();
    rpcWrapper();
    request->send(200, "application/json", serJsonResponse);
  });

  webSocket.onEvent(webSocketEvent);
  server.addHandler(&webSocket);
  server.begin();
#ifdef ESP32
  http.setConnectTimeout(500); // if url not found or down
#endif
  // Set up RPC over UDP for Marstek users
  UdpRPC.begin(String(shelly_port).toInt()); 

  // Set up MQTT
  if (dataMQTT) {
    mqtt_client.setBufferSize(2048);
    mqtt_client.setServer(mqtt_server, String(mqtt_port).toInt());
    mqtt_client.setCallback(mqtt_callback);
  }

  // Set Up Multicast for SMA Energy Meter
  if (dataSMA) {
    Udp.begin(multicastPort);
#ifdef ESP8266
    Udp.beginMulticast(WiFi.localIP(), multicastIP, multicastPort);
#else
    Udp.beginMulticast(multicastIP, multicastPort);
#endif
  }

  // Set Up UDP for SHRDZM smart meter interface
  if (dataSHRDZM) {
    Udp.begin(multicastPort);
  }

  // Set Up Modbus TCP for SUNSPEC register query
  if (dataSUNSPEC) {
    modbus1.client();
    modbus_ip.fromString(mqtt_server);
    if (!modbus1.isConnected(modbus_ip)) {  // reuse mqtt server adresss for modbus adress
      modbus1.connect(modbus_ip, String(mqtt_port).toInt());
      Serial.println("Trying to connect SUNSPEC powermeter data");
    }
  }

  // Set Up HTTP query
  if (dataHTTP) {
    period = atol(query_period);
    startMillis = millis();
    http.useHTTP10(true);
  }

  // Set up mDNS responder
  strcat(shelly_name, shelly_mac);
  if (!MDNS.begin(shelly_name)) {
    DEBUG_SERIAL.println("Error setting up MDNS responder!");
  }

#ifdef ESP32
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("shelly", "tcp", 80);
  mdns_txt_item_t serviceTxtData[4] = {
    { "fw_id", shelly_fw_id },
    { "arch", "esp8266" },
    { "id", shelly_name },
    { "gen", shelly_gen }
  };
  mdns_service_instance_name_set("_http", "_tcp", shelly_name);
  mdns_service_txt_set("_http", "_tcp", serviceTxtData, 4);
  mdns_service_instance_name_set("_shelly", "_tcp", shelly_name);
  mdns_service_txt_set("_shelly", "_tcp", serviceTxtData, 4);
#else
  hMDNSService = MDNS.addService(0, "http", "tcp", 80);
  hMDNSService2 = MDNS.addService(0, "shelly", "tcp", 80);
  if (hMDNSService) {
    MDNS.setServiceName(hMDNSService, shelly_name);
    MDNS.addServiceTxt(hMDNSService, "fw_id", shelly_fw_id);
    MDNS.addServiceTxt(hMDNSService, "arch", "esp8266");
    MDNS.addServiceTxt(hMDNSService, "id", shelly_name);
    MDNS.addServiceTxt(hMDNSService, "gen", shelly_gen);
  }
  if (hMDNSService2) {
    MDNS.setServiceName(hMDNSService2, shelly_name);
    MDNS.addServiceTxt(hMDNSService2, "fw_id", shelly_fw_id);
    MDNS.addServiceTxt(hMDNSService2, "arch", "esp8266");
    MDNS.addServiceTxt(hMDNSService2, "id", shelly_name);
    MDNS.addServiceTxt(hMDNSService2, "gen", shelly_gen);
  }
#endif
  DEBUG_SERIAL.println("mDNS responder started");
}

void loop() {
#ifndef ESP32
  MDNS.update();
#endif
  parseUdpRPC();
  if (shouldResetConfig) {
#ifdef ESP32
    WiFi.disconnect(true, true);
#else
    WiFi.disconnect(true);
#endif
    delay(1000);
    ESP.restart();
  }
  if (dataMQTT) {
    if (!mqtt_client.connected()) {
      mqtt_reconnect();
    }
    mqtt_client.loop();
  }
  if (dataSMA) {
    parseSMA();
  }
  if (dataSHRDZM) {
    parseSHRDZM();
  }
  if (dataSUNSPEC) {
     currentMillis = millis();
    if (currentMillis - startMillis_sunspec >= period) {
       parseSUNSPEC();
      startMillis_sunspec = currentMillis;
    }
   
  }
  if (dataHTTP) {
    currentMillis = millis();
    if (currentMillis - startMillis >= period) {
      queryHTTP();
      startMillis = currentMillis;
    }
  }
  // for Tibber-Pulse
  if (dataTIBBERPULSE) {
    currentMillis = millis();
    if (currentMillis - startMillis_tibberpulse >= period) {
      queryTibberPulseHTTP();
      startMillis_tibberpulse = currentMillis;
    }
  }
  handleblinkled();
}

