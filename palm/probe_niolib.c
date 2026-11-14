/* FI H2H NL's side of the counters: none. The app links fujinet-nio-lib's
 * libniolib-client.a, and every exchange runs inside the installed NioLib
 * (its own code resource), where no probe can reach; the report says the
 * link time is not split. Notes carry NioLib's last error and result. */
#include <PalmOS.h>

#include "NioLibClient.h"
#include "h2h.h"

LinkCounters gLink;

void probe_last(Char *out, UInt16 size)
{
    Char buf[32];

    StrPrintF(buf, "nl err %u res %u", NioLibClientLastError(), NioLibClientLastResult());
    StrNCopy(out, buf, size - 1);
    out[size - 1] = '\0';
}

void probe_log(const Char *line)
{
    (void)line;
}
