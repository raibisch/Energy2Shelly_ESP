This a Shelly Pro 3EM Emulator running on ESP8266 (and hopefully soon ESP32) using various input sources for power data. This can be used for zero feed-in with Hoymiles MS-A2 (and in the near future Marstek Venus).

Kudos to sdeigms excellent work at https://github.com/sdeigm/uni-meter which made this port easily possible.
SMA Multicast code is based on https://www.mikrocontroller.net/topic/559607

After flashing and power up, it opens a hotspot named "Energy2Shelly" running WifiManager for intial WiFi setup and further configuration.

On the captive portal you can currently enter:
- MQTT Server IP, port and topic; power values on the MQTT topic are expected in JSON format. The are multiple fields to define the available values using a JSON Path-style syntax, e.g. "energy.data" for {"energy":{"data":mypowervalue}}. You can also select between monophase and triphase power data.

Besides MQTT you can also use the following entries in the MQTT server IP field to enable other power meter data sources
- "SMA" to use SMA Energy Meter or Home Manager UDP multicast data
- "SHRDZM" to use a SHRDZM smart meter interface (common in Austria) with UDP unicast data; please enable UDP broadcasts to the IP of the ESP and port 9522 within SHRDZM
- "HTTP" to use a generic HTTP input; enter a query URL in the second parameter field which delivers JSON data and define at least the JSON Path for total power

Here are some sample generic HTTP query paths for common devices:
- Tasmota devices: http://IP-address/cm?cmnd=status%2010
- ioBroker datapoints: http://IP-address:8082/getBulk/smartmeter.0.1-0:1_8_0__255.value,smartmeter.0.1-0:2_8_0__255.value,smartmeter.0.1-0:16_7_0__255.value/?json
- Fronius: http://IP-address/solar_api/v1/GetMeterRealtimeData.cgi?Scope=System

The Shelly ID defaults to the ESP's MAC address, you may change this if you want to substitute an existing uni-meter configuration without reconnecting the battery to a new shelly device.

You can check the current power data at http://IP-address/status, if you want to reset you Wifi-Configuration and/or reconfigure other settings go to http://IP-address/reset and reconnect to the Energy2Shelly hotspot.