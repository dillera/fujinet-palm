/* H2H's probe on fujinet-nio-lib's palmos-serial transport. The library
 * is used as built; the linker wraps four of its functions (palm/Makefile):
 *
 *   --wrap=fn_transport_exchange --wrap=fn_transport_exchange_buffers
 *       the bounds of one request/reply exchange
 *   --wrap=fn_channel_write_byte --wrap=fn_channel_read_byte
 *       every byte on the cradle line, which splits the exchange into
 *       send (bytes handed to SrmSend), wait (until the first reply byte) and
 *       recv (the rest of the reply, SLIP-decoded and checked)
 *
 * A second request inside one exchange (the library's wake-up GetInfo after
 * 30 s idle, or a resend after a timeout) is counted in gLink.resends. Only
 * phase changes read the clock, so the probe costs a few microseconds per
 * exchange and a counter per byte. */
#include <PalmOS.h>
#include <stdint.h>

#include "h2h.h"

uint8_t __real_fn_transport_exchange(void);
uint8_t __real_fn_transport_exchange_buffers(const uint8_t *request, uint16_t requestLength,
                                             uint8_t *response, uint16_t responseCapacity,
                                             uint16_t *responseLength);
uint8_t __real_fn_channel_write_byte(uint8_t value, uint16_t timeoutMs);
uint8_t __real_fn_channel_read_byte(uint8_t *value, uint16_t timeoutMs);

LinkCounters gLink;

/* The serial probe only counts: its notes are the library's errors alone. */
void probe_last(Char *out, UInt16 size)
{
    if (size)
        out[0] = '\0';
}

void probe_log(const Char *line)
{
    (void)line;
}

enum { PHASE_IDLE, PHASE_SEND, PHASE_WAIT, PHASE_RECV };

static UInt8 gDepth, gPhase;
static UInt16 gWrote;      /* bytes written in this exchange */
static UInt32 gStart, gMark;

/* Charges the time since gMark to the phase that is ending. */
static void Charge(UInt32 now)
{
    switch (gPhase) {
    case PHASE_SEND: gLink.usSend += now - gMark; break;
    case PHASE_WAIT: gLink.usWait += now - gMark; break;
    case PHASE_RECV: gLink.usRecv += now - gMark; break;
    }
    gMark = now;
}

static void Begin(void)
{
    gStart = gMark = clk_us();
    gPhase = PHASE_SEND;
    gWrote = 0;
    gLink.exchanges++;
}

static void End(uint8_t result)
{
    UInt32 now = clk_us();

    Charge(now);
    gLink.usLink += now - gStart;
    if (result != 0)
        gLink.failures++;
    gPhase = PHASE_IDLE;
}

uint8_t __wrap_fn_transport_exchange(void)
{
    uint8_t r;

    if (gDepth++) {
        r = __real_fn_transport_exchange();
    } else {
        Begin();
        r = __real_fn_transport_exchange();
        End(r);
    }
    gDepth--;
    return r;
}

uint8_t __wrap_fn_transport_exchange_buffers(const uint8_t *request, uint16_t requestLength,
                                             uint8_t *response, uint16_t responseCapacity,
                                             uint16_t *responseLength)
{
    uint8_t r;

    if (gDepth++) {
        r = __real_fn_transport_exchange_buffers(request, requestLength, response,
                                                 responseCapacity, responseLength);
    } else {
        Begin();
        r = __real_fn_transport_exchange_buffers(request, requestLength, response,
                                                 responseCapacity, responseLength);
        End(r);
    }
    gDepth--;
    return r;
}

uint8_t __wrap_fn_channel_write_byte(uint8_t value, uint16_t timeoutMs)
{
    if (gPhase == PHASE_WAIT || gPhase == PHASE_RECV) {
        Charge(clk_us());
        gPhase = PHASE_SEND;
        gWrote = 0;
        gLink.resends++;
    }
    gWrote++;
    gLink.bytesOut++;
    return __real_fn_channel_write_byte(value, timeoutMs);
}

uint8_t __wrap_fn_channel_read_byte(uint8_t *value, uint16_t timeoutMs)
{
    uint8_t r;

    if (gPhase == PHASE_SEND && gWrote) {
        Charge(clk_us());
        gPhase = PHASE_WAIT;
    }
    r = __real_fn_channel_read_byte(value, timeoutMs);
    if (r) {
        gLink.bytesIn++;
        if (gPhase == PHASE_WAIT) {
            Charge(clk_us());
            gPhase = PHASE_RECV;
        }
    }
    return r;
}
