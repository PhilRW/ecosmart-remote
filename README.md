# ecosmart-remote

This project is based largely on the work performed by [Ryan Griggs](https://github.com/ryangriggs/EcoSmartLib) to [decode](https://electronics.stackexchange.com/questions/233374/reverse-engineering-asynchronous-serial-protocol-for-ecosmart-tankless-water-hea) the EcoSmart remote control interface protocol.

EcoSmart has long since stopped selling [remote controls](https://www.amazon.com/Ecosmart-US-ECO-RC-Control/dp/B005R41ZAS) for their [electric tankless water heaters](https://www.ecosmartus.com/), but that doesn't mean that the remote protocol doesn't work anymore. In fact, it still works and with a little patience and ingenuity you can incorporate the EcoSmart tankless water heater into your [home automation system](https://www.home-assistant.io/).

## Decoding the protocol

The protocol as so far determined is as such:

seq.              | µs duration | mark or space | note
------------------|-------------|---------------|------
1                 | 7000        | mark          | header/sync
2                 | 4000        | space         | header/sync
3,5,7...81        | 2400 / 720  | mark          | data bit 1 / 0
4,6,8...80        | 840         | space         | data space
82 (if repeating) | 2700        | space         | repeat space

Repeat 1 - 82 if repeating, otherwise go through 81 and stop.

The data is always 40 bits (5 bytes) in length and might look something like this:

```
00001111 00111100 00011000 01101010 00101001
```

Or, in hex bytes:

```
0F 3C 18 6A 29
```

### Byte 1
The first byte is `0x0F` and is unknown at this time.

### Byte 2

The second byte has been observed to be `0x3C` and is unknown at this time. It may change depending on operation mode of the heater, such as when it is limited to 105°F. Further investigation is needed.

### Byte 3

The third byte appears to contain information about the state of the heater.
So far, three bits have been decoded.

*  Bit 4 is the on/off state of the heater.
   ```
   00001000
       ^
   ```

*  Bit 5 is if the display is in °C (or `0` if using °F).
   ```
   00010000
      ^
   ```

*  Bit 6 is if the flow sensor detects flow.
   ```
   00100000
     ^
   ```

It is currently unknown what, if anything, the other bits might represent.

#### Examples

`00000000` (or `0x00`) means the device is off, flow is off, and
the display is in °F.

`00001000` (or `0x08`) means the device is on, flow is off, and
the display is in °F.

`00101000` (or `0x28`) means the device is on, flow is on, and
the display is in °F.

`00010000` (or `0x10`) means the device is off, flow is off, and
the display is in °C.

`00111000` (or `0x38`) means the device is on, flow is on, and
the display is in °C.

### Byte 4

The fourth byte is the temperature in °F. For example, `01101010` (or `0x6A`) is 106°F.

### Byte 5

The fifth byte is the temperature in °C. For example, `00101001` (or `0x29`) is 41°C.



## Configuration

### Warning

If you are connecting directly to the [control board](https://www.amazon.com/Ecosmart-CB-QC-MEDLRG-Control/dp/B00Z0Z6HMU), **BE VERY CAREFUL** about not shorting any of the pins. The processor on the control board **is not well protected** and will more than likely be damaged by _even the slightest mishap_. I **strongly advise you** to disconnect all power to the heater, ground yourself, and be very cautious when connecting an Arduino or NodeMCU device to the heater's control board. Obviously, EcoSmart will probably not honor your warranty if you break it. Fortunately the control board is only about $50 at the moment.

### Pinout

The connector labled "CN4" on the sub-board with the display LED and the control knob has pins as follows:

```
[LED1]	

Label:
E123995
94V-0
SMT18-36A
S.V: 1


o <--- ground
o <--- +12V
o <--- data in
o <--- data out
[CN4]


[SW1]
```

~~Since I am using a [NodeMCU](https://www.amazon.com/HiLetgo-Internet-Development-Wireless-Micropython/dp/B010N1SPRK) device, it is able to be powered directly from the +12V pin to the <u>VIN</u> pin.~~ After noticing some flakiness I have decided to power the NodeMCU from a separate USB power supply (but grounds must still be connected). Connect ground to ground, connect the EcoSmart's <u>data in</u> to the `OUTPUT_PIN` (D6 by default) and the EcoSmart's <u>data out</u> pin to the `RECV_PIN` (D2 by default).

### Software

Program the correct wifi SSID, password, and MQTT broker settings before building and uploading to your remote device. I used [CLion and platformio](http://docs.platformio.org/en/latest/ide/clion.html) to develop, build, and upload the software, but use whatever works for you. I have written this software to be compatible with [Home Assistant's MQTT Climate device](https://www.home-assistant.io/components/climate.mqtt/) with the following example configuration:

```yaml
climate:
  - platform: mqtt
    name: Tankless WH
    min_temp: 27
    max_temp: 60
    modes:
      - 'off'
      - 'heat'
    mode_command_topic: "ecosmart/mode/set"
    mode_state_topic: "ecosmart/mode"
    temperature_command_topic: "ecosmart/temperature/set"
    temperature_state_topic: "ecosmart/temperature"
```

