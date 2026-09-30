// Host HotSync server built from the firmware's lib/hotsync sources.
//   hotsync serial <device> <root> [none|flagged|all]
//   hotsync net <root> [backup]         NetSync on TCP 14238
//   hotsync emu <root> [backup]         serial-over-TCP on 6416 (POSE, CloudpilotEmu)
#include "DirStorage.h"
#include "HostLinks.h"
#include "HotSyncSession.h"
#include "NetSyncTransport.h"
#include "PadpTransport.h"

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static HotSyncOptions make_options(const char *backup)
{
    HotSyncOptions options;
    options.pc_id = 0x464A4E54; // "FJNT"
    options.new_user_id = static_cast<uint32_t>(time(nullptr));
    std::string b = backup ? backup : "flagged";
    options.backup = b == "none" ? HotSyncBackup::NONE
                   : b == "all"  ? HotSyncBackup::ALL
                                 : HotSyncBackup::FLAGGED;
    time_t t = time(nullptr);
    tm local;
    localtime_r(&t, &local);
    options.now.year = local.tm_year + 1900;
    options.now.month = local.tm_mon + 1;
    options.now.day = local.tm_mday;
    options.now.hour = local.tm_hour;
    options.now.minute = local.tm_min;
    options.now.second = local.tm_sec;
    return options;
}

static void report(const HotSyncReport &r)
{
    printf("user='%s' installed=%d (failed %d) backed_up=%d (failed %d) result=%s\n",
           r.user_name.c_str(), r.installed, r.install_failures, r.backed_up, r.backup_failures,
           dlp_error_name(r.error));
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: hotsync serial <dev> <root> [backup] | net|emu <root> [backup]\n");
        return 2;
    }
    std::string mode = argv[1];
    bool trace = getenv("TRACE") != nullptr;

    if (mode == "serial")
    {
        SerialLink link(argv[2]);
        if (!link.is_open() || argc < 4)
            return 1;
        link.trace = trace;
        DirStorage storage(argv[3]);
        PadpTransport padp(link);
        fprintf(stderr, "Waiting on %s -- press HotSync...\n", argv[2]);
        while (padp.accept().is_error())
            ;
        report(HotSyncSession(padp, storage, make_options(argc > 4 ? argv[4] : nullptr)).run());
        return 0;
    }

    int port = mode == "net" ? NETSYNC_PORT : 6416;
    int server = listen_tcp(port);
    if (server < 0)
        return 1;
    DirStorage storage(argv[2]);
    fprintf(stderr, "Listening on TCP %d\n", port);
    int fd = accept(server, nullptr, nullptr);
    SocketLink link(fd);
    link.trace = trace;
    std::unique_ptr<DlpTransport> transport;
    if (mode == "net")
        transport = std::make_unique<NetSyncTransport>(link);
    else
        transport = std::make_unique<PadpTransport>(link);
    if (transport->accept().is_error())
    {
        fprintf(stderr, "handshake failed\n");
        return 1;
    }
    report(HotSyncSession(*transport, storage, make_options(argc > 3 ? argv[3] : nullptr)).run());
    close(server);
    return 0;
}
