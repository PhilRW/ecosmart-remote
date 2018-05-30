/*
  EcoSmart tankless electric water heater remote interface using ESP8266 and MQTT.
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ecosmart_remote.h>


// WIFI and MQTT setup
const char *ssid = "your-wifi-SSID";
const char *password = "your-wifi-password";
const char *mqtt_server = "your.mqtt.server";
const char *mqtt_username = "";
const char *mqtt_password = "";
const int mqtt_port = 1883;


// OTA upgrading & MQTT client ID
#define SENSORNAME "ecosmart" //change this to whatever you want to call your device
#define OTA_PASSWORD "your-OTA-password"
uint16_t OTAport = 8266;


// MQTT config
const char *mode_command_topic = "ecosmart/mode/set";
const char *mode_state_topic = "ecosmart/mode";
const char *temperature_command_topic = "ecosmart/temperature/set";
const char *temperature_state_topic = "ecosmart/temperature";
const char *flow_state_topic = "ecosmart/flow";

const char *on_mode = "on";
const char *off_mode = "off";
const char *flow_on = "ON";
const char *flow_off = "OFF";


// should set to true if using Celsius or false if using Fahrenheit (in Home Assistant)
bool use_c = true;


// IR library
#define MIN_UNKNOWN_SIZE       12
#define CAPTURE_BUFFER_SIZE  8192
#define TIMEOUT               15U  // Suits most messages, while not swallowing many repeats.


#define INITIAL_COMMAND       0x0F3C186929 // When this device restarts, it should have an initial state (105/41)


// instantiate objects and variables
WiFiClient espClient;
PubSubClient client(espClient);
//EcoSmart ecoSmart;
IRrecv irrecv(RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);
decode_results results;

bool stateOn = false;
bool stateFlow = false;

int codeType = -1; // The type of code
unsigned long codeValue; // The code value if not raw
unsigned int rawCodes[RAWBUF]; // The durations if raw
int codeLen; // The length of the code


uint64_t cmd;


void setup_wifi() {

    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}


float ftoc(float temp_f) {
    auto temp = (temp_f - 32) / 1.8;
    return static_cast<float>(temp);
}


float ctof(float temp_c) {
    auto temp = (temp_c * 1.8) + 32;
    return static_cast<float>(temp);
}


byte getTempF() {
    auto temp_f = static_cast<byte>((cmd >> ECOSMART_TEMP_F_SHIFT) & 0xFF);
    return temp_f;
}


byte getTempC() {
    auto temp_c = static_cast<byte>((cmd >> ECOSMART_TEMP_C_SHIFT) & 0xFF);
    return temp_c;
}

void setTempF(byte temp_f) {
    cmd &= ~(0xFF << ECOSMART_TEMP_F_SHIFT);
    cmd |= temp_f << ECOSMART_TEMP_F_SHIFT;
}

void setTempC(byte temp_c) {
    cmd &= ~(0xFF << ECOSMART_TEMP_C_SHIFT);
    cmd |= temp_c << ECOSMART_TEMP_C_SHIFT;

}

void updateState() {

    stateOn = ((cmd >> ECOSMART_ON_BIT_SHIFT) & 1U) == 1;
    use_c = ((cmd >> ECOSMART_C_BIT_SHIFT) & 1U) == 1;
    stateFlow = ((cmd >> ECOSMART_FLOW_BIT_SHIFT) & 1U) == 1;

}

void sendState() {
    updateState();

    Serial.println("sending state update via MQTT");

    int temp = lroundf(use_c ? getTempC() : getTempF());
    char t[12];
    sprintf(t, "%i", temp);

    client.publish(mode_state_topic, (stateOn) ? on_mode : off_mode);
    client.publish(flow_state_topic, (stateFlow) ? flow_on : flow_off);
    client.publish(temperature_state_topic, t);
}


void sendCommand() {
    Serial.print("writing command: ");
    serialPrintUint64(cmd, HEX);
    Serial.println();
    sendEcoSmart(cmd, 40, RPT_CODES);
}


void callback(char *topic, byte *payload, int length) {
    Serial.print("message received: [");
    Serial.print(topic);
    Serial.print("] ");

    char message[length + 1];
    for (int i = 0; i < length; i++) {
        message[i] = (char) payload[i];
    }
    message[length] = '\0';
    Serial.println(message);


    if (strcmp(topic, mode_command_topic) == 0) {

        if (strcmp(message, on_mode) == 0) {
            cmd |= 1ULL << ECOSMART_ON_BIT_SHIFT;
            sendCommand();
            stateOn = true;

        } else if (strcmp(message, off_mode) == 0) {
            cmd &= ~(1ULL << ECOSMART_ON_BIT_SHIFT);
            sendCommand();
            stateOn = false;
        }

    } else if (strcmp(topic, temperature_command_topic) == 0) {

        float temp_f = strtof(message, nullptr);
        float temp_c = ftoc(temp_f);

        if (use_c) {
            temp_c = strtof(message, nullptr);
            temp_f = ctof(temp_c);
        }

        if (temp_f < 80) {
            temp_f = 80;
            temp_c = ftoc(temp_f);
        } else if (temp_f > 140) {
            temp_f = 140;
            temp_c = ftoc(temp_f);
        }


        setTempF(roundf(temp_f));
        setTempC(roundf(temp_c));

        sendCommand();

    }

    sendState();
}


void setup() {
    Serial.begin(115200);

    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);

    //OTA SETUP
    ArduinoOTA.setPort(OTAport);
    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(SENSORNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        Serial.println("Starting");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();

#if DECODE_HASH
    // Ignore messages with less than minimum on or off pulses.
    irrecv.setUnknownThreshold(MIN_UNKNOWN_SIZE);
#endif  // DECODE_HASH
    irrecv.enableIRIn();  // Start the receiver

    pinMode(OUTPUT_PIN, OUTPUT);

    Serial.println("Ready");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // set the initial state of the command
    uint64_t init = INITIAL_COMMAND;
    if (use_c) {
        init |= 1ULL << ECOSMART_C_BIT_SHIFT;
    } else {
        init &= ~(1ULL << ECOSMART_C_BIT_SHIFT);
    }
    cmd = init;
}


void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(SENSORNAME, mqtt_username, mqtt_password)) {
            Serial.println("connected");
            client.subscribe(mode_command_topic);
            client.subscribe(temperature_command_topic);
            sendState();
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}


void processData(uint64_t data) {

    cmd = data;

    updateState();

    Serial.print("data (hex) : ");
    Serial.println(uint64ToString(results.value, HEX));
    Serial.print("data (bin) : ");
    Serial.println(uint64ToString(results.value, BIN));


    Serial.print("stateOn    : ");
    Serial.println(stateOn);

    Serial.print("use_c      : ");
    Serial.println(use_c);

    Serial.print("stateFlow  : ");
    Serial.println(stateFlow);

    Serial.print("temp_f     : ");
    Serial.println(getTempF());

    Serial.print("temp_c     : ");
    Serial.println(getTempC());

    sendState();

}

void loop() {

    if (!client.connected()) {
        reconnect();
    }

    if (WiFi.status() != WL_CONNECTED) {
        delay(1);
        Serial.print("WIFI disconnected. Attempting reconnection.");
        setup_wifi();
        return;
    }

    client.loop();

    ArduinoOTA.handle();


    if (irrecv.decode(&results)) {

        // Blank line between entries
        Serial.println("Attempting EcoSmart decode");
        if (results.decode_type == UNKNOWN && decodeEcoSmart(&results)) {
            Serial.println();
            Serial.println("*** EcoSmart data found ***");
            processData(results.value);
            Serial.println();

        } else {
            Serial.println("EcoSmart decode FAILED");

            Serial.println();

// Display a crude timestamp.
            uint32_t now = millis();
            Serial.printf("Timestamp : %06u.%03u\n", now / 1000, now % 1000);
            if (results.overflow) {
                Serial.printf("WARNING: IR code is too big for buffer (>= %d). "
                              "This result shouldn't be trusted until this is resolved. "
                              "Edit & increase CAPTURE_BUFFER_SIZE.\n",
                              CAPTURE_BUFFER_SIZE);
            }

// Display the basic output of what we found.
            Serial.print(resultToHumanReadableBasic(&results));
            yield();  // Feed the WDT as the text output can take a while to print.

// Display the library version the message was captured with.
            Serial.print("Library   : v");
            Serial.println(_IRREMOTEESP8266_VERSION_);
            Serial.println();

// Output RAW timing info of the result.
            Serial.println(resultToTimingInfo(&results));
            yield();  // Feed the WDT (again)

// Output the results as source code
            Serial.println(resultToSourceCode(&results));
            yield();  // Feed the WDT (again)

        }
    }

}

