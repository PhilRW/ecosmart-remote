# Working with ESPHome

Place the `ecosmart.h` file in your `esphome/` configuration directory, then you can include these sections in your device YAML config:

```yaml
esphome:
  includes:
    - ecosmart.h
  libraries:
    - IRremoteESP8266

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
```

> NOTE: The pins that connect the NodeMCU board with the EcoSmart control board are currently hard-coded in the `[ecosmart.h](ecosmart.h)` file.
