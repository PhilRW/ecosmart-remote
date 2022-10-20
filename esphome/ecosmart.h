#include "esphome.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>

#define get_ecosmart(constructor) static_cast<EcoSmart *>(const_cast<custom_component::CustomComponentConstructor *>(&constructor)->get_component(0))

const uint8_t ECOSMART_TEMP_MIN = 27; // Celsius
const uint8_t ECOSMART_TEMP_MAX = 60; // Celsius

#define OUTPUT_PIN 12 // D6 on NodeMCU
#define RECV_PIN 4    // D2 on NodeMCU

#define ECOSMART_HDR_MARK 7000U
#define ECOSMART_HDR_SPACE 4000U
#define ECOSMART_BIT_MARK_HIGH 2400U
#define ECOSMART_BIT_MARK_LOW 720U
#define ECOSMART_BIT_SPACE 840U
#define ECOSMART_RPT_SPACE 2700U

#define ECOSMART_ON_BIT_SHIFT 19
#define ECOSMART_C_BIT_SHIFT 20
#define ECOSMART_FLOW_BIT_SHIFT 21
#define ECOSMART_TEMP_F_SHIFT 8
#define ECOSMART_TEMP_C_SHIFT 0

#define RPT_CODES 5 // number of times to repeat sending the code (0 for no repeats)

// IR library
#define OFFSET_START 1U
#define MIN_UNKNOWN_SIZE 12
#define CAPTURE_BUFFER_SIZE 1024
#define TIMEOUT 15U // Suits most messages, while not swallowing many repeats.
#define RAWBUF 100U

int codeType = -1;             // The type of code
unsigned long codeValue;       // The code value if not raw
unsigned int rawCodes[RAWBUF]; // The durations if raw
int codeLen;                   // The length of the code

#define INITIAL_COMMAND 0x0F3C186929 // When this device restarts, it should have an initial state (105/41)

static const char *TAG = "ecosmart";

bool use_c = true;
bool stateOn = false;
bool stateFlow = false;
uint64_t cmd;

IRrecv irrecv(RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);
// IRrecv irrecv(RECV_PIN);
decode_results results;

class EcoSmartClimate : public Component, public Climate

{
public:
  void setup() override
  {
    ESP_LOGV(TAG, "setup()");

    pinMode(OUTPUT_PIN, OUTPUT);
    uint64_t init = INITIAL_COMMAND;
    if (use_c)
    {
      init |= 1ULL << ECOSMART_C_BIT_SHIFT;
    }
    else
    {
      init &= ~(1ULL << ECOSMART_C_BIT_SHIFT);
    }
    cmd = init;
    auto restore = this->restore_state_();
    if (restore.has_value())
    {
      restore->apply(this);
    }
    else
    {
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->current_temperature = NAN;
    }
    if (isnan(this->target_temperature))
    {
      this->target_temperature = ECOSMART_TEMP_MIN;
    }
    this->publish_state();
  }

  ClimateTraits traits() override
  {
    ESP_LOGV(TAG, "traits()");

    auto traits = climate::ClimateTraits();
    traits.set_supports_current_temperature(false);
    traits.set_supported_modes({
        climate::CLIMATE_MODE_OFF,
        climate::CLIMATE_MODE_HEAT,
    });
    traits.set_supports_two_point_target_temperature(false);
    traits.set_visual_min_temperature(ECOSMART_TEMP_MIN);
    traits.set_visual_max_temperature(ECOSMART_TEMP_MAX);
    traits.set_visual_temperature_step(1);
    return traits;
  }

  void control(const ClimateCall &call) override
  {
    ESP_LOGV(TAG, "control()");

    if (call.get_mode().has_value())
    {
      // User requested mode change
      ClimateMode mode = *call.get_mode();

      switch (mode)
      {
      case climate::CLIMATE_MODE_OFF:
        cmd &= ~(1ULL << ECOSMART_ON_BIT_SHIFT);
        break;
      case climate::CLIMATE_MODE_HEAT:
        cmd |= 1ULL << ECOSMART_ON_BIT_SHIFT;
        break;
      default:
        ESP_LOGE(TAG, "Climate mode not supported: %s", climate_mode_to_string(this->mode));
        break;
      }
      // Send mode to hardware
      // ...
      this->mode = mode;
      sendCommand();

      // Publish updated state
      //      this->mode = mode;
      //      this->publish_state();
    }
    if (call.get_target_temperature().has_value())
    {
      // User requested target temperature change
      float temp_c = clamp<float>(*call.get_target_temperature(), ECOSMART_TEMP_MIN, ECOSMART_TEMP_MAX);
      float temp_f = ctof(temp_c);
      setTempF(roundf(temp_f));
      setTempC(roundf(temp_c));
      this->target_temperature = temp_c;
      sendCommand();
      // ...
    }
  }

  void mark(unsigned int duration)
  {
    digitalWrite(OUTPUT_PIN, HIGH);
    delayMicroseconds(duration);
    digitalWrite(OUTPUT_PIN, LOW);
  }

  void space(unsigned int duration)
  {
    delayMicroseconds(duration);
  }

  void sendEcoSmart(uint64_t data, uint16_t nbits, uint16_t repeat)
  {

    for (uint16_t r = 0; r <= repeat; r++)
    {
      // Header
      mark(ECOSMART_HDR_MARK);
      space(ECOSMART_HDR_SPACE);
      for (int32_t i = nbits; i > 0; i--)
      {
        switch ((data >> (i - 1)) & 1UL)
        {
        case 0:
          mark(ECOSMART_BIT_MARK_LOW);
          break;
        case 1:
          mark(ECOSMART_BIT_MARK_HIGH);
          break;
        }
        space(ECOSMART_BIT_SPACE);
      }

      // wait this long between repeats
      space(ECOSMART_RPT_SPACE - ECOSMART_BIT_SPACE);
    }
  }

  float ftoc(float temp_f)
  {
    ESP_LOGV(TAG, "ftoc()");

    auto temp = (temp_f - 32) / 1.8;
    return static_cast<float>(temp);
  }

  float ctof(float temp_c)
  {
    ESP_LOGV(TAG, "ctof()");

    auto temp = (temp_c * 1.8) + 32;
    return static_cast<float>(temp);
  }

  void setTempF(byte temp_f)
  {
    ESP_LOGV(TAG, "setTempF()");

    cmd &= ~(0xFF << ECOSMART_TEMP_F_SHIFT);
    cmd |= temp_f << ECOSMART_TEMP_F_SHIFT;
  }

  void setTempC(byte temp_c)
  {
    ESP_LOGV(TAG, "setTempC()");

    cmd &= ~(0xFF << ECOSMART_TEMP_C_SHIFT);
    cmd |= temp_c << ECOSMART_TEMP_C_SHIFT;
  }

  void sendCommand()
  {
    ESP_LOGV(TAG, "Sending command: 0x0F%08X", cmd);

    sendEcoSmart(cmd, 40, RPT_CODES);
    this->publish_state();
  }
};

class EcoSmart : public Component, CustomAPIDevice
{
public:
  BinarySensor *flow_sensor = new BinarySensor("Flow Sensor");
  EcoSmartClimate *climate = new EcoSmartClimate();
  EcoSmart() {}

  // float get_setup_priority() const { return setup_priority::HARDWARE; }

  void setup() override
  {
    ESP_LOGV(TAG, "setup()");

    irrecv.enableIRIn();
    climate->setup();
  }

  void loop() override
  {
    if (irrecv.decode(&results))
    {

      // Blank line between entries
      ESP_LOGV(TAG, "Attempting EcoSmart decode");
      if (results.decode_type == UNKNOWN && decodeEcoSmart(&results))
      {
        ESP_LOGV(TAG, "*** EcoSmart data found ***");
        processData(results.value);
      }
      else
      {
        ESP_LOGV(TAG, "EcoSmart decode FAILED");

        if (results.overflow)
        {
          ESP_LOGW(TAG, "IR code is too big for buffer (>= %d). "
                        "This result shouldn't be trusted until this is resolved. "
                        "Edit & increase CAPTURE_BUFFER_SIZE.\n",
                   CAPTURE_BUFFER_SIZE);
        }

        // Display the basic output of what we found.
        ESP_LOGVV(TAG, "%s", resultToHumanReadableBasic(&results).c_str());
        yield(); // Feed the WDT as the text output can take a while to print.

        // Display the library version the message was captured with.
        ESP_LOGVV(TAG, "Library version: %s", _IRREMOTEESP8266_VERSION_);

        // Output RAW timing info of the result.
        ESP_LOGVV(TAG, "%s", resultToTimingInfo(&results).c_str());
        yield(); // Feed the WDT (again)

        // Output the results as source code
        ESP_LOGVV(TAG, "%s", resultToSourceCode(&results).c_str());
        yield(); // Feed the WDT (again)
      }
    }
  };

  bool decodeEcoSmart(decode_results *results)
  {
    ESP_LOGV(TAG, "decodeEcoSmart()");

    uint64_t data = 0;
    uint16_t offset = OFFSET_START;

    if (results->rawlen < 82)
    {
      return false; // Not enough entries to be EcoSmart.
    }

    // Calc the maximum size in bits the message can be or that we can accept.
    int maxBitSize = std::min((uint16_t)(results->rawlen / 2) - 1,
                              (uint16_t)sizeof(data) * 8);

    // Header decode
    if (!irrecv.matchMark(results->rawbuf[offset], ECOSMART_HDR_MARK))
    {
      ESP_LOGV(TAG, "FALSE due to not matchMark on ECOSMART_HDR_MARK");
      return false;
    }

    if (!irrecv.matchSpace(results->rawbuf[++offset], ECOSMART_HDR_SPACE))
    {
      ESP_LOGV(TAG, "FALSE due to not matchMark on ECOSMART_HDR_SPACE");
      return false;
    }

    // Data decode
    uint16_t actualBits;
    for (actualBits = 0; actualBits < maxBitSize; actualBits++)
    {
      data <<= 1;

      offset++;
      if (irrecv.matchMark(results->rawbuf[offset], ECOSMART_BIT_MARK_LOW))
      {
        ESP_LOGVV(TAG, "offset = %i, data += 0", offset);
        data += 0;
      }
      else if (irrecv.matchMark(results->rawbuf[offset], ECOSMART_BIT_MARK_HIGH))
      {
        ESP_LOGVV(TAG, "offset = %i, data += 1", offset);
        data += 1;
      }
      else
      {
        return false;
      }

      offset++;
      if (actualBits + 1 >= 40 && offset + 1 >= results->rawlen)
      {
        ESP_LOGVV(TAG, "Breaking due to 40 bits found...");
        actualBits++;
        break;
      }
      else if (irrecv.matchSpace(results->rawbuf[offset], ECOSMART_RPT_SPACE))
      {
        ESP_LOGVV(TAG, "Resetting due to repeat...");
        data = 0;
        actualBits = 0;
        if (!irrecv.matchMark(results->rawbuf[offset + 1], ECOSMART_HDR_MARK))
        {
          ESP_LOGV(TAG, "FALSE due to not matchMark on ECOSMART_HDR_MARK after ECOSMART_RPT_SPACE");
          return false;
        }
        if (!irrecv.matchSpace(results->rawbuf[offset + 2], ECOSMART_HDR_SPACE))
        {
          ESP_LOGV(TAG, "FALSE due to not matchMark on ECOSMART_HDR_SPACE after ECOSMART_RPT_SPACE");
          return false;
        }
        offset += 2;
      }
      else if (!irrecv.matchSpace(results->rawbuf[offset], ECOSMART_BIT_SPACE))
      {
        ESP_LOGV(TAG, "FALSE due to not matchSpace on ECOSMART_BIT_SPACE after mark");
        return false;
      }
    }

    // Success
    results->value = data;
    // results->decode_type = UNKNOWN;
    results->bits = actualBits;
    results->address = 0;
    results->command = 0;
    return true;
  }

  void processData(uint64_t data)
  {
    ESP_LOGV(TAG, "processData()");

    cmd = data;

    stateOn = ((cmd >> ECOSMART_ON_BIT_SHIFT) & 1U) == 1;
    use_c = ((cmd >> ECOSMART_C_BIT_SHIFT) & 1U) == 1;
    stateFlow = ((cmd >> ECOSMART_FLOW_BIT_SHIFT) & 1U) == 1;

    climate->target_temperature = lroundf(use_c ? getTempC() : getTempF());
    climate->mode = stateOn ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;
    climate->publish_state();

    flow_sensor->publish_state(stateFlow);
  }

  byte getTempF()
  {
    ESP_LOGV(TAG, "getTempF()");

    auto temp_f = static_cast<byte>((cmd >> ECOSMART_TEMP_F_SHIFT) & 0xFF);
    return temp_f;
  }

  byte getTempC()
  {
    ESP_LOGV(TAG, "getTempC()");

    auto temp_c = static_cast<byte>((cmd >> ECOSMART_TEMP_C_SHIFT) & 0xFF);
    return temp_c;
  }
};

