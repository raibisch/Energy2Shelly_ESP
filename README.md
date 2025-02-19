This a first release of a Shelly Pro 3EM Emulator running on ESP8266 using MQTT as input source for power data. This can be used for zero feed-in with Hoymiles MS-A2 and Marstek Venus.

Kudos to sdeigms excellent work at https://github.com/sdeigm/uni-meter which made this port easily possible.
SMA Multicast code is based on https://www.mikrocontroller.net/topic/559607

After flashing and power up, it opens a hotspot named "Energy2Shelly" running WifiManager for intial WiFi Setup.

On the captive portal you can also enter MQTT Server IP, port and topic or enter "SMA" to use SMA Energy Meter or Home Manager multicast power data.

Power values on the MQTT topic can be either a raw number or at a configurable JSON field using a JSON Path-style syntax, e.g. "energy.data" for {"energy":{"data":mypowervalue}}.

The Shelly ID defaults to the ESP's MAC address, you may change this if you want to subtitute an existing uni-meter configuration without reconnecting the battery to a new shelly device.