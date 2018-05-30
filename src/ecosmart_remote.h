//
// Created by Philip Rosenberg-Watt on 2018/5/21.
//

#ifndef ECOSMART_NODEMCU_ECOSMART_REMOTE_H
#define ECOSMART_NODEMCU_ECOSMART_REMOTE_H


#include <algorithm>
#include "IRrecv.h"
#include "IRsend.h"
#include "IRtimer.h"
#include "IRutils.h"


#define OUTPUT_PIN             12 // D6 on NodeMCU
#define RECV_PIN                4 // D2 on NodeMCU


#define ECOSMART_HDR_MARK           7000U
#define ECOSMART_HDR_SPACE          4000U
#define ECOSMART_BIT_MARK_HIGH      2400U
#define ECOSMART_BIT_MARK_LOW        720U
#define ECOSMART_BIT_SPACE           840U
#define ECOSMART_RPT_SPACE          2700U

#define ECOSMART_ON_BIT_SHIFT       19
#define ECOSMART_C_BIT_SHIFT        20
#define ECOSMART_FLOW_BIT_SHIFT     21
#define ECOSMART_TEMP_F_SHIFT       8
#define ECOSMART_TEMP_C_SHIFT       0

#define RPT_CODES                   0 // number of times to repeat sending the code (0 for no repeats)


#define DECODE_ECOSMART    true
#define SEND_ECOSMART      true


#if SEND_ECOSMART


void mark(unsigned int duration) {
    digitalWrite(OUTPUT_PIN, HIGH);
    delayMicroseconds(duration);
    digitalWrite(OUTPUT_PIN, LOW);

}

void space(unsigned int duration) {
    delayMicroseconds(duration);
}

// Send an EcoSmart packet.
//
// Args:
//   data: The data we want to send. MSB first.
//   nbits: The number of bits of data to send. (Typically 40)
//   repeat: The nr. of times the message should be sent.
//
// Status:  BETA / Should be working.
void sendEcoSmart(uint64_t data, uint16_t nbits, uint16_t repeat) {

    for (uint16_t r = 0; r <= repeat; r++) {
        // Header
        mark(ECOSMART_HDR_MARK);
        space(ECOSMART_HDR_SPACE);
        for (int32_t i = nbits; i > 0; i--) {
            switch ((data >> (i - 1)) & 1UL) {
                case 0:
                    mark(ECOSMART_BIT_MARK_LOW);
                    DPRINT(i);
                    DPRINTLN(": 0");
                    break;
                case 1:
                    mark(ECOSMART_BIT_MARK_HIGH);
                    DPRINT(i);
                    DPRINTLN(": 1");
                    break;
            }
            space(ECOSMART_BIT_SPACE);
        }

        // wait this long between repeats
        space(ECOSMART_RPT_SPACE - ECOSMART_BIT_SPACE);
    }
}

#endif


#if DECODE_ECOSMART
// Decode an EcoSmart packet (41 bits) if possible.
// Places successful decode information in the results pointer.
// Args:
//   results: Ptr to the data to decode and where to store the decode result.
// Returns:
//   boolean: True if it can decode it, false if it can't.
//
// Status:  BETA / Should be working.
//
// Ref:
//   https://electronics.stackexchange.com/questions/233374/reverse-engineering-asynchronous-serial-protocol-for-ecosmart-tankless-water-hea
bool decodeEcoSmart(decode_results *results) {
    uint64_t data = 0;
    uint16_t offset = OFFSET_START;

    if (results->rawlen < 82) {
        return false;  // Not enough entries to be EcoSmart.
    }

    // Calc the maximum size in bits the message can be or that we can accept.
    int maxBitSize = std::min((uint16_t) (results->rawlen / 2) - 1,
                              (uint16_t) sizeof(data) * 8);

    // Header decode
    if (!IRrecv::matchMark(results->rawbuf[offset], ECOSMART_HDR_MARK)) {
        DPRINTLN("FALSE due to not matchMark on ECOSMART_HDR_MARK");
        return false;
    }

    if (!IRrecv::matchSpace(results->rawbuf[++offset], ECOSMART_HDR_SPACE)) {
        DPRINTLN("FALSE due to not matchMark on ECOSMART_HDR_SPACE");
        return false;
    }

    // Data decode
    uint16_t actualBits;
    for (actualBits = 0; actualBits < maxBitSize; actualBits++) {
        data <<= 1;

        offset++;
        if (IRrecv::matchMark(results->rawbuf[offset], ECOSMART_BIT_MARK_LOW)) {
            DPRINT("offset = ");
            DPRINTLN(offset);
            DPRINTLN("data += 0");
            data += 0;
        } else if (IRrecv::matchMark(results->rawbuf[offset], ECOSMART_BIT_MARK_HIGH)) {
            DPRINT("offset = ");
            DPRINTLN(offset);
            DPRINTLN("data += 1");
            data += 1;
        } else {
            return false;
        }

        offset++;
        if (actualBits + 1 >= 40 && offset + 1 >= results->rawlen) {
            DPRINTLN("Breaking due to 40 bits found...");
            actualBits++;
            break;
        } else if (IRrecv::matchSpace(results->rawbuf[offset], ECOSMART_RPT_SPACE)) {
            DPRINTLN("Resetting due to repeat...");
            data = 0;
            actualBits = 0;
            if (!IRrecv::matchMark(results->rawbuf[offset + 1], ECOSMART_HDR_MARK)) {
                DPRINTLN("FALSE due to not matchMark on ECOSMART_HDR_MARK after ECOSMART_RPT_SPACE");
                return false;
            }
            if (!IRrecv::matchSpace(results->rawbuf[offset + 2], ECOSMART_HDR_SPACE)) {
                DPRINTLN("FALSE due to not matchMark on ECOSMART_HDR_SPACE after ECOSMART_RPT_SPACE");
                return false;
            }
            offset += 2;
        } else if (!IRrecv::matchSpace(results->rawbuf[offset], ECOSMART_BIT_SPACE)) {
            DPRINTLN("FALSE due to not matchSpace on ECOSMART_BIT_SPACE after mark");
            return false;
        }
        DPRINT("offset = ");
        DPRINTLN(offset);
        DPRINT("data so far: ");
        DPRINTLN(uint64ToString(data, BIN));
#ifdef DEBUG
        yield();
#endif

    }

    // Success
    results->value = data;
    results->decode_type = ECOSMART;
    results->bits = actualBits;
    results->address = 0;
    results->command = 0;
    return true;
}

#endif


#endif //ECOSMART_NODEMCU_ECOSMART_REMOTE_H
