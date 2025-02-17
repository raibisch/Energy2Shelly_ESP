This a first release of a Shelly Pro 3EM Emulator running on ESP8266 using MQTT as input source for power data. This can be used for zero feed-in with Hoymiles MS-A2 and Marstek Venus.

Kudos to sdeigms excellent work at https://github.com/sdeigm/uni-meter which made this port easily possible.

It uses WifiManager for intial WiFi Setup, SMA Multicast based on https://www.mikrocontroller.net/topic/559607

On the captive portal you can enter MQTT Server IP, port, topic or enter "SMA" to use SMA EnergyMeter or HomeManager Power Data.
MQTT input  power data is currently expected in JSON-Format: {"ENERGY":{"Power":xyz}}.

Please don't forget to change the default Shelly ID to a random 12 digit hexadecimal number. A future version will default to the ESP's MAC address.