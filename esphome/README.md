# Working with ESPHome

Place the [`ecosmart.h`](ecosmart.h) file in your `esphome/` configuration directory, then you could use something like this for your esphome device YAML config:

```yaml
esphome:
  name: Whatever you want
  platform: ESP8266
  board: nodemcuv2
  includes:
    - ecosmart.h
  libraries:
    - IRremoteESP8266

packages:
  wifi: !include .common_wifi.yaml  # or however you want to configure wifi

logger:

status_led:
  pin:
    number: D0
    inverted: True

custom_component:
  - id: ecosmart
    lambda: |-
      auto ecosmart = new EcoSmart();
      App.register_component(ecosmart);
      return {ecosmart};

climate:
  - platform: custom
    lambda: return {get_ecosmart(ecosmart)->climate};
    climates:
      - name: EcoSmart Tankless Water Heater

binary_sensor:
  - platform: custom
    lambda: return {get_ecosmart(ecosmart)->flow_sensor};
    binary_sensors:
      - name: EcoSmart Tankless Water Heater Flow
        device_class: moving
        
sensor:
  - platform: wifi_signal
    name: EcoSmart Tankless Water Heater WiFi
```

> NOTE: The pins that connect the NodeMCU board with the EcoSmart control board are currently hard-coded in the [`ecosmart.h`](ecosmart.h) file.
