This a Shelly Pro 3EM Emulator running on ESP8266 (and hopefully soon ESP32) using various input sources for power data. This can be used for zero feed-in with Hoymiles MS-A2 (and in the near future Marstek Venus).

Kudos to sdeigms excellent work at https://github.com/sdeigm/uni-meter which made this port easily possible.
SMA Multicast code is based on https://www.mikrocontroller.net/topic/559607

After flashing and power up, it opens a hotspot named "Energy2Shelly" running WifiManager for intial WiFi Setup.

On the captive portal you can currently enter:
- MQTT Server IP, port and topic; power values on the MQTT topic can be either a raw number or at a configurable JSON field using a JSON Path-style syntax, e.g. "energy.data" for {"energy":{"data":mypowervalue}}.
- "SMA" to use SMA Energy Meter or Home Manager multicast data
- "SHRDZM" to use SHRDZM smart meter interface UDP unicast data; please enable UDP broadcasts to the IP of the ESP and port 9522 within SHRDZM

The Shelly ID defaults to the ESP's MAC address, you may change this if you want to substitute an existing uni-meter configuration without reconnecting the battery to a new shelly device.

You can check the current power data at http://IP-address/status, if you want to reset you Wifi-Configuration or power meter configuration go to http://IP-address/reset.