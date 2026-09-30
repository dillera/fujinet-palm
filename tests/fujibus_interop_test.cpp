// Cross-checks common/fujibus.c against the firmware's own FujiBusPacket.
// Build: make -C tests (needs ../fujinet-firmware next to this repo).
#include "bus/rs232/FujiBusPacket.h"

extern "C" {
#include "fujibus.h"
}

#include <cstdio>
#include <string>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Our request, parsed by the firmware.
static void request_round_trip(const std::vector<FbParam> &params, const std::string &payload)
{
    fn_u8 frame[FB_MAX_FRAME];
    fn_u16 n = fb_build_request(FB_DEVICE_FUJINET, 0xF7, params.data(), params.size(),
                                reinterpret_cast<const fn_u8 *>(payload.data()), payload.size(),
                                frame, sizeof(frame));
    EXPECT(n > 0);
    auto packet = FujiBusPacket::fromSerialized(ByteBuffer(frame, frame + n));
    EXPECT(packet != nullptr);
    if (!packet)
        return;
    EXPECT(static_cast<int>(packet->device()) == FB_DEVICE_FUJINET);
    EXPECT(static_cast<int>(packet->command()) == 0xF7);
    EXPECT(packet->paramCount() == params.size());
    for (size_t i = 0; i < params.size() && i < packet->paramCount(); ++i)
        EXPECT(packet->param(i) == params[i].value);
    std::string data = packet->dataAsString().value_or("");
    EXPECT(data == payload);
}

// The firmware's reply, parsed by us.
static void reply_round_trip(const std::string &payload)
{
    ByteBuffer body(payload.begin(), payload.end());
    FujiBusPacket reply(static_cast<fujiDeviceID_t>(FB_DEVICE_FUJINET),
                        static_cast<fujiCommandID_t>(FB_CMD_ACK), body);
    ByteBuffer wire = reply.serialize();
    std::vector<fn_u8> frame(wire.begin(), wire.end());
    FbReply parsed;
    EXPECT(fb_parse_reply(frame.data(), frame.size(), &parsed) == 1);
    EXPECT(parsed.device == FB_DEVICE_FUJINET);
    EXPECT(parsed.command == FB_CMD_ACK);
    EXPECT(std::string(reinterpret_cast<const char *>(parsed.data), parsed.data_len) == payload);
}

int main()
{
    request_round_trip({}, "");
    request_round_trip({{3, 1}}, std::string("/apps/\0*.prc\0", 13));
    request_round_trip({{40, 1}, {0, 1}}, "");
    request_round_trip({{0x1234, 2}}, "");
    request_round_trip({{2, 1}, {1, 1}}, "/apps/Hello.prc|/palm/install/");
    // Bytes that need SLIP escaping.
    request_round_trip({{0xC0, 1}}, std::string("\xC0\xDB\xC0", 3));

    reply_round_trip("");
    reply_round_trip(std::string(256, 'h'));
    reply_round_trip(std::string("\xC0\xDB\x7F\x7F", 4));

    fn_u8 corrupt[FB_MAX_FRAME];
    fn_u16 n = fb_build_request(FB_DEVICE_FUJINET, 0xF4, nullptr, 0, nullptr, 0, corrupt, sizeof(corrupt));
    corrupt[3] ^= 0x01;
    FbReply r;
    EXPECT(fb_parse_reply(corrupt, n, &r) == 0);

    std::printf(failures ? "%d failure(s)\n" : "fujibus: all interop checks passed\n", failures);
    return failures ? 1 : 0;
}
