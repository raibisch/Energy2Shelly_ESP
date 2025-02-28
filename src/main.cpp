// Energy2Shelly_ESP v0.4.4
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

#define DEBUG true // set to false for no DEBUG output
#define DEBUG_SERIAL if(DEBUG)Serial

unsigned long startMillis;
unsigned long currentMillis;

//define your default values here, if there are different values in config.json, they are overwritten.
char input_type[40];
char mqtt_server[80];
char mqtt_port[6] = "1883";
char mqtt_topic[60] = "tele/meter/SENSOR";
char mqtt_user[20] = "";
char mqtt_passwd[20] = "";
char power_path[60] = "";
char energy_in_path[60] ="";
char energy_out_path[60] ="";
char shelly_mac[13];
char shelly_name[26] = "shellypro3em-";
char query_period[10] = "1000";

unsigned long period = 1000;
int rpcId = 1;
char rpcUser[20] = "user_1";

// SMA Multicast IP and Port
unsigned int multicastPort = 9522;  // local port to listen on
IPAddress multicastIP(239, 12, 255, 254);

//flag for saving/resetting WifiManager data
bool shouldSaveConfig = false;
bool shouldResetConfig = false;

Preferences preferences;

//flags for data sources
bool dataMQTT = false;
bool dataSMA = false;
bool dataSHRDZM = false;
bool dataHTTP = false;

struct PowerData
{
  double current;
  double voltage;
  double power;
  double apparentPower;
  double powerFactor;
  double frequency;
};

struct EnergyData
{
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
  return (int)(value * 100 + 0.5) / 100.0;
}

JsonVariant resolveJsonPath(JsonVariant variant, const char* path) {
  for (size_t n = 0; path[n]; n++) {
    if (path[n] == '.') {
      variant = variant[JsonString(path, n)];
      path += n + 1;
      n = 0;
    }
  }
  return variant[path];
}

void setPowerData(double totalPower) {    
  PhasePower[0].power = round2(totalPower * 0.3333);
  PhasePower[1].power = round2(totalPower * 0.3333);
  PhasePower[2].power = round2(totalPower * 0.3333);
  for(int i=0;i<=2;i++) {
    PhasePower[i].voltage = 230;
    PhasePower[i].current = round2(PhasePower[i].power / PhasePower[i].voltage);
    PhasePower[i].apparentPower = PhasePower[i].power;
    PhasePower[i].powerFactor = 1;
    PhasePower[i].frequency = 50;
  }
  DEBUG_SERIAL.print("Current total power: ");
  DEBUG_SERIAL.println(totalPower);
}

void setEnergyData(double totalEnergyGridSupply, double totalEnergyGridFeedIn) {    
  for(int i=0;i<=2;i++) {
    PhaseEnergy[i].consumption = round2(totalEnergyGridSupply * 0.3333);
    PhaseEnergy[i].gridfeedin = round2(totalEnergyGridFeedIn * 0.3333);
  }
  DEBUG_SERIAL.print("Total consumption: ");
  DEBUG_SERIAL.print(totalEnergyGridSupply);
  DEBUG_SERIAL.print(" - Total Grid Feed-In: ");
  DEBUG_SERIAL.println(totalEnergyGridFeedIn);
}

//callback notifying us of the need to save WifiManager config
void saveConfigCallback () {
  DEBUG_SERIAL.println("Should save config");
  shouldSaveConfig = true;
}

void GetDeviceInfo() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  jsonResponse["result"]["name"] = shelly_name;
  jsonResponse["result"]["id"] = shelly_name;
  jsonResponse["result"]["mac"] = shelly_mac;
  jsonResponse["result"]["slot"] = 1;
  jsonResponse["result"]["model"] = "SPEM-003CEBEU";
  jsonResponse["result"]["gen"] = 2;
  jsonResponse["result"]["fw_id"] = "20241011-114455/1.4.4-g6d2a586";
  jsonResponse["result"]["ver"] = "1.4.4";
  jsonResponse["result"]["app"] = "Pro3EM";
  jsonResponse["result"]["auth_en"] = false;
  jsonResponse["result"]["profile"] = "triphase";
  serializeJson(jsonResponse,serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
}

void EMGetStatus(){
  JsonDocument jsonResponse;
  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  if (strcmp(rpcUser,"UDP") != 0) {
    jsonResponse["dst"] = rpcUser;
  }
  jsonResponse["result"]["id"] = 0;
  jsonResponse["result"]["a_current"] = PhasePower[0].current;
  jsonResponse["result"]["a_voltage"] = PhasePower[0].voltage;
  jsonResponse["result"]["a_act_power"] = PhasePower[0].power;
  jsonResponse["result"]["a_aprt_power"] = PhasePower[0].apparentPower;
  jsonResponse["result"]["a_pf"] = PhasePower[0].powerFactor;
  jsonResponse["result"]["a_freq"] = PhasePower[0].frequency;
  jsonResponse["result"]["b_current"] = PhasePower[1].current;
  jsonResponse["result"]["b_voltage"] = PhasePower[1].voltage;
  jsonResponse["result"]["b_act_power"] = PhasePower[1].power;
  jsonResponse["result"]["b_aprt_power"] = PhasePower[1].apparentPower;
  jsonResponse["result"]["b_pf"] = PhasePower[1].powerFactor;
  jsonResponse["result"]["b_freq"] = PhasePower[1].frequency;
  jsonResponse["result"]["c_current"] = PhasePower[2].current;
  jsonResponse["result"]["c_voltage"] = PhasePower[2].voltage;
  jsonResponse["result"]["c_act_power"] = PhasePower[2].power;
  jsonResponse["result"]["c_aprt_power"] = PhasePower[2].apparentPower;
  jsonResponse["result"]["c_pf"] = PhasePower[2].powerFactor;
  jsonResponse["result"]["c_freq"] = PhasePower[2].frequency;
  jsonResponse["result"]["total_current"] = round2((PhasePower[0].power + PhasePower[1].power + PhasePower[2].power) / 230);
  jsonResponse["result"]["total_act_power"] = PhasePower[0].power + PhasePower[1].power + PhasePower[2].power;
  jsonResponse["result"]["total_aprt_power"] = PhasePower[0].apparentPower + PhasePower[1].apparentPower + PhasePower[2].apparentPower;
  serializeJson(jsonResponse,serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
}

void EMDataGetStatus() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  if (strcmp(rpcUser,"UDP") != 0) {
    jsonResponse["dst"] = rpcUser;
  }
  jsonResponse["result"]["id"] = 0;
  jsonResponse["result"]["a_total_act_energy"] = PhaseEnergy[0].consumption;
  jsonResponse["result"]["a_total_act_ret_energy"] = PhaseEnergy[0].gridfeedin;
  jsonResponse["result"]["b_total_act_energy"] = PhaseEnergy[1].consumption;
  jsonResponse["result"]["b_total_act_ret_energy"] = PhaseEnergy[1].gridfeedin;
  jsonResponse["result"]["c_total_act_energy"] = PhaseEnergy[2].consumption;
  jsonResponse["result"]["c_total_act_ret_energy"] = PhaseEnergy[2].gridfeedin;
  jsonResponse["result"]["total_act"] = PhaseEnergy[0].consumption + PhaseEnergy[1].consumption + PhaseEnergy[2].consumption;
  jsonResponse["result"]["total_act_ret"] = PhaseEnergy[0].gridfeedin + PhaseEnergy[1].gridfeedin + PhaseEnergy[2].gridfeedin;
  serializeJson(jsonResponse,serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
}

void EMGetConfig() {
  JsonDocument jsonResponse;
  jsonResponse["id"] = rpcId;
  jsonResponse["name"] = nullptr;
  jsonResponse["blink_mode_selector"] = "active_energy";
  jsonResponse["phase_selector"] = "a";
  jsonResponse["monitor_phase_sequence"] = true;
  jsonResponse["ct_type"] = "120A";
  serializeJson(jsonResponse,serJsonResponse);
  DEBUG_SERIAL.println(serJsonResponse);
}

void webSocketEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  JsonDocument json;
  switch(type) {
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
              GetDeviceInfo();
              webSocket.textAll(serJsonResponse);
            } else if(json["method"] == "EM.GetStatus") {
              strcpy(rpcUser,json["src"]);
              EMGetStatus();
              webSocket.textAll(serJsonResponse);
            } else if(json["method"] == "EMData.GetStatus") {
              strcpy(rpcUser,json["src"]);
              EMDataGetStatus();
              webSocket.textAll(serJsonResponse);
            } else if(json["method"] == "EM.GetConfig") {
              EMGetConfig();
              webSocket.textAll(serJsonResponse);
            }
            else {
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

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  JsonDocument json;
  deserializeJson(json, payload, length);
  if (strcmp(power_path, "") == 0) {
    payload[length] = '\0';
    setPowerData(atof((char *)payload));
  } else {
    double power = resolveJsonPath(json, power_path);
    setPowerData(power);
    double energyIn = resolveJsonPath(json, energy_in_path);
    double energyOut = resolveJsonPath(json, energy_out_path);
    setEnergyData(energyIn, energyOut);
  }
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
      strcpy(rpcUser, "UDP");
      UdpRPC.beginPacket(UdpRPC.remoteIP(), UdpRPC.remotePort());
      if (json["method"] == "Shelly.GetDeviceInfo") {
        GetDeviceInfo();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if(json["method"] == "EM.GetStatus") {
        EMGetStatus();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if(json["method"] == "EMData.GetStatus") {
        EMDataGetStatus();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      } else if(json["method"] == "EM.GetConfig") {
        EMGetConfig();
        UdpRPC.UDPPRINT(serJsonResponse.c_str());
      }
      else {
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
      uint8_t* offset = buffer + 4;
      do {
          grouplen = (offset[0] << 8) + offset[1];
          grouptag = (offset[2] << 8) + offset[3];
          offset += 4;
          if (grouplen == 0xffff) return;
          if (grouptag == 0x02A0 && grouplen == 4) {
              offset += 4;
          } else if (grouptag == 0x0010) {
              uint8_t* endOfGroup = offset + grouplen;
              uint16_t protocolID = (offset[0] << 8) + offset[1];
              offset += 2;
              uint16_t susyID = (offset[0] << 8) + offset[1];
              offset += 2;
              uint32_t serial = (offset[0] << 24) + (offset[1] << 16) + (offset[2] << 8) + offset[3];
              offset += 4;
              uint32_t timestamp = (offset[0] << 24) + (offset[1] << 16) + (offset[2] << 8) + offset[3];
              offset += 4;
              while (offset < endOfGroup) {
                  uint8_t channel = offset[0];
                  uint8_t index = offset[1];
                  uint8_t type = offset[2];
                  uint8_t tarif = offset[3];
                  offset += 4;
                  if (type == 8) {
                    uint64_t data = ((uint64_t)offset[0] << 56) +
                                  ((uint64_t)offset[1] << 48) +
                                  ((uint64_t)offset[2] << 40) +
                                  ((uint64_t)offset[3] << 32) +
                                  ((uint64_t)offset[4] << 24) +
                                  ((uint64_t)offset[5] << 16) +
                                  ((uint64_t)offset[6] << 8) +
                                  offset[7];
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
                    uint32_t data = (offset[0] << 24) +
                    (offset[1] << 16) +
                    (offset[2] << 8) +
                    offset[3];
                    offset += 4;
                    switch (index) {
                    case 1:
                      // 1.4.0 Total grid power in dW - unused
                      break;
                    case 2:
                      // 2.4.0 Total feed-in power in dW - unused
                      break;
                    case 14:
                     // Not sure why this doesn't work here...
                     for(int i=0;i<=2;i++) {
                       PhasePower[i].frequency = data * 0.001;
                     }
                     break;
                    case 21:
                      PhasePower[0].power = data * 0.1;
                      PhasePower[0].frequency = 50; // workaround
                      break;
                    case 22:
                      PhasePower[0].power -= data * 0.1;
                      break;
                    case 29:
                      PhasePower[0].apparentPower = data * 0.1;
                      break;
                    case 30:
                      PhasePower[0].apparentPower -= data * 0.1;
                      break;
                    case 31:
                      PhasePower[0].current = data * 0.001;
                      break;
                    case 32:
                      PhasePower[0].voltage = data * 0.001;
                      break;
                    case 33:
                      PhasePower[0].powerFactor = data * 0.001;
                      break;
                    case 41:
                      PhasePower[1].power = data * 0.1;
                      PhasePower[1].frequency = 50; // workaround
                      break;
                    case 42:
                      PhasePower[1].power -= data * 0.1;
                      break;
                    case 49:
                      PhasePower[1].apparentPower = data * 0.1;
                      break;
                    case 50:
                      PhasePower[1].apparentPower -= data * 0.1;
                      break;
                    case 51:
                      PhasePower[1].current = data * 0.001;
                      break;
                    case 52:
                      PhasePower[1].voltage = data * 0.001;
                      break;
                    case 53:
                      PhasePower[1].powerFactor = data * 0.001;
                      break;
                    case 61:
                      PhasePower[2].power = data * 0.1;
                      PhasePower[2].frequency = 50; // workaround
                      break;
                    case 62:
                      PhasePower[2].power -= data * 0.1;
                      break;
                    case 69:
                      PhasePower[2].apparentPower = data * 0.1;
                      break;
                    case 70:
                      PhasePower[2].apparentPower -= data * 0.1;
                      break;
                    case 71:
                      PhasePower[2].current = data * 0.001;
                      break;
                    case 72:
                      PhasePower[2].voltage = data * 0.001;
                      break;
                    case 73:
                      PhasePower[2].powerFactor = data * 0.001;
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
      setEnergyData(energyIn,energyOut);
    }
  }
}

void queryHTTP() {
  JsonDocument json;
  DEBUG_SERIAL.println("Querying HTTP source");
  http.begin(wifi_client, mqtt_server);
  http.GET();
  deserializeJson(json, http.getStream());
  if (strcmp(power_path, "") == 0) {
    DEBUG_SERIAL.println("no JSONPath for power data provided");
  } else {
    double power = resolveJsonPath(json, power_path);
    setPowerData(power);
    double energyIn = resolveJsonPath(json, energy_in_path);
    double energyOut = resolveJsonPath(json, energy_out_path);
    setEnergyData(energyIn, energyOut);
  }
  http.end();
}

void WifiManagerSetup() {
  // Set Shelly ID to ESP's MAC address by default
  uint8_t mac[6];
  WiFi.macAddress(mac);
  sprintf (shelly_mac, "%02x%02x%02x%02x%02x%02x", mac [0], mac [1], mac [2], mac [3], mac [4], mac [5]);

  preferences.begin("e2s_config", false);
  strcpy(input_type, preferences.getString("input_type", input_type).c_str());
  strcpy(mqtt_server, preferences.getString("mqtt_server", mqtt_server).c_str());
  strcpy(mqtt_port, preferences.getString("mqtt_port", mqtt_port).c_str());
  strcpy(mqtt_topic, preferences.getString("mqtt_topic", mqtt_topic).c_str());
  strcpy(mqtt_user, preferences.getString("mqtt_user", mqtt_user).c_str());
  strcpy(mqtt_passwd, preferences.getString("mqtt_passwd", mqtt_passwd).c_str());
  strcpy(power_path, preferences.getString("power_path", power_path).c_str());
  strcpy(energy_in_path, preferences.getString("energy_in_path", energy_in_path).c_str());
  strcpy(energy_out_path, preferences.getString("energy_out_path", energy_out_path).c_str());
  strcpy(query_period, preferences.getString("query_period", query_period).c_str());
  strcpy(shelly_mac, preferences.getString("shelly_mac", shelly_mac).c_str());
  
  WiFiManagerParameter custom_input_type("type", "\"MQTT\" for MQTT, \"HTTP\" for generic HTTP, \"SMA\" for SMA EM/HM Multicast or \"SHRDZM\" for SHRDZM UDP data", input_type, 40);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server IP or query url for generic HTTP", mqtt_server, 80);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqtt_topic, 60);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT user (optional)", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_passwd("passwd", "MQTT password (optional)", mqtt_passwd, 20);
  WiFiManagerParameter custom_power_path("power_path", "Power JSON path (e.g. \"ENERGY.Power\") for MQTT and generic HTTP (optional)", power_path, 60);
  WiFiManagerParameter custom_energy_in_path("energy_in_path", "Energy from grid JSON path (e.g. \"ENERGY.Grid\") for MQTT and generic HTTP (optional)", energy_in_path, 60);
  WiFiManagerParameter custom_energy_out_path("energy_out_path", "Energy to grid JSON path (e.g. \"ENERGY.FeedIn\") for MQTT and generic HTTP (optional)", energy_out_path, 60);
  WiFiManagerParameter custom_query_period("query_period", "query period for generic HTTP", query_period, 10);
  WiFiManagerParameter custom_shelly_mac("mac", "Shelly ID (12 char hexadecimal)", shelly_mac, 13);

  WiFiManager wifiManager;
  if(!DEBUG) {
    wifiManager.setDebugOutput(false);
  }

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_input_type);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_passwd);
  wifiManager.addParameter(&custom_power_path);
  wifiManager.addParameter(&custom_energy_in_path);
  wifiManager.addParameter(&custom_energy_out_path);
  wifiManager.addParameter(&custom_query_period);
  wifiManager.addParameter(&custom_shelly_mac);

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
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_passwd, custom_mqtt_passwd.getValue());
  strcpy(power_path, custom_power_path.getValue());
  strcpy(energy_in_path, custom_energy_in_path.getValue());
  strcpy(energy_out_path, custom_energy_out_path.getValue());
  strcpy(query_period, custom_query_period.getValue());
  strcpy(shelly_mac, custom_shelly_mac.getValue());
  

  DEBUG_SERIAL.println("The values in the preferences are: ");
  DEBUG_SERIAL.println("\tinput_type : " + String(input_type));
  DEBUG_SERIAL.println("\tmqtt_server : " + String(mqtt_server));
  DEBUG_SERIAL.println("\tmqtt_port : " + String(mqtt_port));
  DEBUG_SERIAL.println("\tmqtt_topic : " + String(mqtt_topic));
  DEBUG_SERIAL.println("\tmqtt_user : " + String(mqtt_user));
  DEBUG_SERIAL.println("\tmqtt_passwd : " + String(mqtt_passwd));
  DEBUG_SERIAL.println("\tpower_path : " + String(power_path));
  DEBUG_SERIAL.println("\tenergy_in_path : " + String(energy_in_path));
  DEBUG_SERIAL.println("\tenergy_out_path : " + String(energy_out_path));
  DEBUG_SERIAL.println("\tquery_period : " + String(query_period));
  DEBUG_SERIAL.println("\tshelly_mac : " + String(shelly_mac));

  if(strcmp(input_type, "SMA") == 0) {
    dataSMA = true;
    DEBUG_SERIAL.println("Enabling SMA Multicast data input");
  } else if (strcmp(input_type, "SHRDZM") == 0) {
    dataSHRDZM = true;
    DEBUG_SERIAL.println("Enabling SHRDZM UDP data input");
  } else if (strcmp(input_type, "HTTP") == 0) {
    dataHTTP = true;
    DEBUG_SERIAL.println("Enabling generic HTTP data input");
  } else {
    dataMQTT = true;
    DEBUG_SERIAL.println("Enabling MQTT data input");
  }

  if (shouldSaveConfig) {
    DEBUG_SERIAL.println("saving config");
    preferences.putString("input_type", input_type);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putString("mqtt_port", mqtt_port);
    preferences.putString("mqtt_topic", mqtt_topic);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_passwd", mqtt_passwd);
    preferences.putString("power_path", power_path);
    preferences.putString("energy_in_path", energy_in_path);
    preferences.putString("energy_out_path", energy_out_path);
    preferences.putString("query_period", query_period);
    preferences.putString("shelly_mac", shelly_mac);
    wifiManager.reboot();
  }
  DEBUG_SERIAL.println("local ip");
  DEBUG_SERIAL.println(WiFi.localIP());

}

void setup(void) {
  DEBUG_SERIAL.begin(115200);
  WifiManagerSetup();

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

  server.on("/rpc", HTTP_POST, [](AsyncWebServerRequest *request) {
    GetDeviceInfo();
    request->send(200, "application/json", serJsonResponse);
  });

  webSocket.onEvent(webSocketEvent);
  server.addHandler(&webSocket);
  server.begin();

  // Set up RPC over UDP for Marstek users
  UdpRPC.begin(1010);

  // Set up MQTT
  if(dataMQTT) {
    mqtt_client.setServer(mqtt_server, String(mqtt_port).toInt());
    mqtt_client.setCallback(mqtt_callback);
  }

  // Set Up Multicast for SMA Energy Meter
  if(dataSMA) {
    Udp.begin(multicastPort);
    #ifdef ESP8266
      Udp.beginMulticast(WiFi.localIP(), multicastIP, multicastPort);
    #else
      Udp.beginMulticast(multicastIP, multicastPort);
    #endif
  }

  // Set Up UDP for SHRDZM smart meter interface
  if(dataSHRDZM) {
    Udp.begin(multicastPort);
  }

  // Set Up HTTP query
  if(dataHTTP) {
    period = atol(query_period);
    startMillis = millis();
    http.useHTTP10(true);
  }

  // Set up mDNS responder
  strcat(shelly_name,shelly_mac);
  if (!MDNS.begin(shelly_name)) {
    DEBUG_SERIAL.println("Error setting up MDNS responder!");
  }

  #ifdef ESP32
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("shelly", "tcp", 80);
    mdns_txt_item_t serviceTxtData[4] = {
      {"fw_id","20241011-114455/1.4.4-g6d2a586"},
      {"arch","esp8266"},
      {"id",shelly_name},
      {"gen","2"}
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
      MDNS.addServiceTxt(hMDNSService, "fw_id", "20241011-114455/1.4.4-g6d2a586");
      MDNS.addServiceTxt(hMDNSService, "arch", "esp8266");
      MDNS.addServiceTxt(hMDNSService, "id", shelly_name);
      MDNS.addServiceTxt(hMDNSService, "gen", "2");
    }
    if (hMDNSService2) {
      MDNS.setServiceName(hMDNSService2, shelly_name);
      MDNS.addServiceTxt(hMDNSService2, "fw_id", "20241011-114455/1.4.4-g6d2a586");
      MDNS.addServiceTxt(hMDNSService2, "arch", "esp8266");
      MDNS.addServiceTxt(hMDNSService2, "id", shelly_name);
      MDNS.addServiceTxt(hMDNSService2, "gen", "2");
    }
  #endif
  DEBUG_SERIAL.println("mDNS responder started");
}

void loop(void) {
  #ifndef ESP32
    MDNS.update();
  #endif
  parseUdpRPC();
  if(shouldResetConfig) {
    WiFi.disconnect(true);
    delay(1000);
    ESP.restart();  
  }
  if(dataMQTT) {
    if (!mqtt_client.connected()) {
      mqtt_reconnect();
    }
    mqtt_client.loop();
  }
  if(dataSMA) {
    parseSMA();
  }
  if(dataSHRDZM) {
    parseSHRDZM();
  
  }
  if(dataHTTP) {
    currentMillis = millis();
    if (currentMillis - startMillis >= period) {
      queryHTTP();
      startMillis = currentMillis;
    }
  }
}