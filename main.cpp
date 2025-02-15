// Energy2Shelly_ESP v0.1

#include <FS.h>                   
#include <WiFiManager.h>
#ifdef ESP32
  #include <SPIFFS.h>
#endif
#include <Arduino.h>
#include <ESPAsyncTCP.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <WebSockets4WebServer.h>

#define DEBUG false // set to false for no DEBUG output
#define DEBUG_SERIAL if(DEBUG)Serial

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_topic[40] = "tele/meter/SENSOR";
char shelly_mac[13] = "1a2b3c4d5e6f";
char shelly_name[26] = "shellypro3em-";

int rpcId = 1;
char rpcUser[20] = "user_1";

//flag for saving WifiManager data
bool shouldSaveConfig = false;

//flag for data source MQTT
//bool dataMQTT = false;

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

MDNSResponder::hMDNSService hMDNSService = 0; // handle of the http service in the MDNS responder
MDNSResponder::hMDNSService hMDNSService2 = 0; // handle of the shelly service in the MDNS responder
MDNSResponder::hMDNSServiceQuery hMDNSServiceQuery = 0; // handle of the http service query
MDNSResponder::hMDNSServiceQuery hMDNSServiceQuery2 = 0; // handle of the shelly service query

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
ESP8266WebServer server(80);
WebSockets4WebServer webSocket;

double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
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

void MDNSServiceQueryCallback(MDNSResponder::MDNSServiceInfo serviceInfo, MDNSResponder::AnswerType answerType, bool p_bSetContent) {
  String answerInfo;
  switch (answerType) {
    case MDNSResponder::AnswerType::ServiceDomain: answerInfo = "ServiceDomain " + String(serviceInfo.serviceDomain()); break;
    case MDNSResponder::AnswerType::HostDomainAndPort: answerInfo = "HostDomainAndPort " + String(serviceInfo.hostDomain()) + ":" + String(serviceInfo.hostPort()); break;
    case MDNSResponder::AnswerType::IP4Address:
      answerInfo = "IP4Address ";
      for (IPAddress ip : serviceInfo.IP4Adresses()) { answerInfo += "- " + ip.toString(); };
      break;
    case MDNSResponder::AnswerType::Txt:
      answerInfo = "TXT " + String(serviceInfo.strKeyValue());
      for (auto kv : serviceInfo.keyValues()) { answerInfo += "\nkv : " + String(kv.first) + " : " + String(kv.second); }
      break;
    default: answerInfo = "Unknown Answertype";
  }
  DEBUG_SERIAL.printf("Answer %s %s\n", answerInfo.c_str(), p_bSetContent ? "Modified" : "Deleted");
}

void ShellyGetDeviceInfoHttp() {
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

  String wsResponseJson;
  serializeJson(jsonResponse,wsResponseJson);
  server.send(200,"application/json", wsResponseJson);
}

void ShellyGetDeviceInfoResponse(uint8_t num){
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

  String wsResponseJson;
  serializeJson(jsonResponse,wsResponseJson);
  webSocket.sendTXT(num, wsResponseJson);
  DEBUG_SERIAL.println(wsResponseJson);
}

void ShellyEMGetStatus(uint8_t num){
  JsonDocument jsonResponse;

  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  jsonResponse["dst"] = rpcUser;
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
  jsonResponse["result"]["total_current"] = (PhasePower[0].power + PhasePower[1].power + PhasePower[2].power) / 230;
  jsonResponse["result"]["total_act_power"] = PhasePower[0].power + PhasePower[1].power + PhasePower[2].power;
  jsonResponse["result"]["total_aprt_power"] = PhasePower[0].apparentPower + PhasePower[1].apparentPower + PhasePower[2].apparentPower;
  
  String wsResponseJson;
  serializeJson(jsonResponse,wsResponseJson);
  webSocket.sendTXT(num, wsResponseJson);
  DEBUG_SERIAL.println(wsResponseJson);
}

void ShellyEMDataGetStatus(uint8_t num) {
  JsonDocument jsonResponse;
  jsonResponse["id"] = rpcId;
  jsonResponse["src"] = shelly_name;
  jsonResponse["dst"] = rpcUser;
  jsonResponse["result"]["id"] = 0;
  jsonResponse["result"]["a_total_act_energy"] = PhaseEnergy[0].consumption;
  jsonResponse["result"]["a_total_act_ret_energy"] = PhaseEnergy[0].gridfeedin;
  jsonResponse["result"]["b_total_act_energy"] = PhaseEnergy[1].consumption;
  jsonResponse["result"]["b_total_act_ret_energy"] = PhaseEnergy[1].gridfeedin;
  jsonResponse["result"]["c_total_act_energy"] = PhaseEnergy[2].consumption;
  jsonResponse["result"]["c_total_act_ret_energy"] = PhaseEnergy[2].gridfeedin;
  jsonResponse["result"]["total_act"] = PhaseEnergy[0].consumption + PhaseEnergy[1].consumption + PhaseEnergy[2].consumption;
  jsonResponse["result"]["total_act_ret"] = PhaseEnergy[0].gridfeedin + PhaseEnergy[1].gridfeedin + PhaseEnergy[2].gridfeedin;

  String wsResponseJson;
  serializeJson(jsonResponse,wsResponseJson);
  webSocket.sendTXT(num, wsResponseJson);
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  JsonDocument json;
  switch(type) {
      case WStype_DISCONNECTED:
          DEBUG_SERIAL.printf("[%u] Websocket: disconnected!\n", num);
          break;
      case WStype_CONNECTED:
          {
              IPAddress ip = webSocket.remoteIP(num);
              DEBUG_SERIAL.printf("[%u] Websocket: connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
          }
          break;
      case WStype_TEXT:
          DEBUG_SERIAL.printf("[%u]Websocket: get Text: %s\n", num, payload);
          deserializeJson(json,payload);
          rpcId = json["id"];
          if (json["method"] == "Shelly.GetDeviceInfo") {
            DEBUG_SERIAL.println("Starting GetDeviceInfoResponse");
            ShellyGetDeviceInfoResponse(num);
          } else if(json["method"] == "EM.GetStatus") {
            strcpy(rpcUser,json["src"]);
            ShellyEMGetStatus(num);
          } else if(json["method"] == "EMData.GetStatus") {
            strcpy(rpcUser,json["src"]);
            ShellyEMDataGetStatus(num);
          }
          else {
            DEBUG_SERIAL.printf("[%u] Websocket: unknown request: %s\n", num, payload);
          }
          break;
      case WStype_BIN:
          DEBUG_SERIAL.printf("[%u] Websocket: unknown request length: %d\n", num, length);
          hexdump(payload, length);
          break;
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  JsonDocument json;
  deserializeJson(json, payload, length);
  setPowerData(json["ENERGY"]["Power"].as<double>());
  setEnergyData(json["ENERGY"]["TotalIn"].as<double>(), json["ENERGY"]["TotalOut"].as<double>());
}

void mqtt_reconnect() {
  while (!mqtt_client.connected()) {
    DEBUG_SERIAL.print("Attempting MQTT connection...");
    String clientId = "Energy2Shelly-" ;
    clientId += shelly_mac;
    if (mqtt_client.connect(clientId.c_str())) {
      DEBUG_SERIAL.println("connected");
      mqtt_client.publish(mqtt_topic, "Energy2Shelly online");
      mqtt_client.subscribe(mqtt_topic);
    } else {
      DEBUG_SERIAL.print("failed, rc=");
      DEBUG_SERIAL.print(mqtt_client.state());
      DEBUG_SERIAL.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void WifiManagerSetup() {
  JsonDocument json;
  DEBUG_SERIAL.println("mounting FS...");

  if (SPIFFS.begin()) {
    DEBUG_SERIAL.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      DEBUG_SERIAL.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_SERIAL.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
          DEBUG_SERIAL.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);
          strcpy(shelly_mac, json["shelly_mac"]);
        } else {
          DEBUG_SERIAL.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    DEBUG_SERIAL.println("failed to mount FS");
  }
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqtt_topic, 40);
  WiFiManagerParameter custom_shelly_mac("mac", "Shelly ID", shelly_mac, 13);

  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_shelly_mac);

  if (!wifiManager.autoConnect()) {
    DEBUG_SERIAL.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  DEBUG_SERIAL.println("connected");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(shelly_mac, custom_shelly_mac.getValue());
  
  DEBUG_SERIAL.println("The values in the file are: ");
  DEBUG_SERIAL.println("\tmqtt_server : " + String(mqtt_server));
  DEBUG_SERIAL.println("\tmqtt_port : " + String(mqtt_port));
  DEBUG_SERIAL.println("\tmqtt_topic : " + String(mqtt_topic));
  DEBUG_SERIAL.println("\tshelly_mac : " + String(shelly_mac));

  mqtt_client.setServer(mqtt_server, String(mqtt_port).toInt());
  mqtt_client.setCallback(mqtt_callback);

  if (shouldSaveConfig) {
    DEBUG_SERIAL.println("saving config");
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_topic"] = mqtt_topic;
    json["shelly_mac"] = shelly_mac;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_SERIAL.println("failed to open config file for writing");
    }
    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
  }
  DEBUG_SERIAL.println("local ip");
  DEBUG_SERIAL.println(WiFi.localIP());
}

void setup(void) {
  DEBUG_SERIAL.begin(115200);
  WifiManagerSetup();

  server.on("/", []() {
    server.send(200, "text/plain", "This is the Energy2Shelly for ESP converter!\r\n");
  });
  server.on("/rpc", ShellyGetDeviceInfoHttp);
  server.addHook(webSocket.hookForWebserver("/rpc", webSocketEvent));
  server.begin();

  // Set up mDNS responder
  strcat(shelly_name,shelly_mac);
  if (!MDNS.begin(shelly_name)) {
    DEBUG_SERIAL.println("Error setting up MDNS responder!");
  }
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
  if (!hMDNSServiceQuery) {
          hMDNSServiceQuery = MDNS.installServiceQuery("http", "tcp", MDNSServiceQueryCallback);
          if (hMDNSServiceQuery) {
            DEBUG_SERIAL.printf("MDNSProbeResultCallback: Service query for 'http.tcp' services installed.\n");
          } else {
            DEBUG_SERIAL.printf("MDNSProbeResultCallback: FAILED to install service query for 'http.tcp' services!\n");
          }
        }
  if (!hMDNSServiceQuery2) {
          hMDNSServiceQuery2 = MDNS.installServiceQuery("shelly", "tcp", MDNSServiceQueryCallback);
          if (hMDNSServiceQuery2) {
            DEBUG_SERIAL.printf("MDNSProbeResultCallback: Service query for 'shelly.tcp' services installed.\n");
          } else {
            DEBUG_SERIAL.printf("MDNSProbeResultCallback: FAILED to install service query for 'shelly.tcp' services!\n");
          }
        }
  
  DEBUG_SERIAL.println("mDNS responder started");
}

void loop(void) {
  server.handleClient();
  webSocket.loop();
    MDNS.update();
    if (!mqtt_client.connected()) {
      mqtt_reconnect();
    }
    mqtt_client.loop();
}
