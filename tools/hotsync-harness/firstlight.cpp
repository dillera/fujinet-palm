// Minimal serial HotSync: handshake, identify the device, list RAM databases.
#include "DlpClient.h"
#include "HostLinks.h"
#include "PadpTransport.h"

#include <cstdio>

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/cu.usbserial-1440";
    SerialLink link(dev);
    if (!link.is_open())
        return 1;
    link.trace = getenv("TRACE") != nullptr;
    PadpTransport padp(link);
    fprintf(stderr, "Waiting on %s -- press HotSync on the cradle...\n", dev);
    while (padp.accept().is_error())
        ;
    fprintf(stderr, "Connected at %u baud\n", padp.baud_rate());

    DlpClient dlp(padp);
    DlpSysInfo sys;
    DlpError e = dlp.read_sys_info(sys);
    printf("ReadSysInfo: %s rom=%08x dlp=%u.%u maxrec=%u\n", dlp_error_name(e), sys.rom_version,
           sys.dlp_major, sys.dlp_minor, sys.max_record_size);
    DlpUserInfo user;
    e = dlp.read_user_info(user);
    printf("ReadUserInfo: %s user='%s' id=%u lastpc=%08x last=%04u-%02u-%02u\n", dlp_error_name(e),
           user.user_name.c_str(), user.user_id, user.last_sync_pc, user.last_sync.year,
           user.last_sync.month, user.last_sync.day);

    dlp.open_conduit();
    uint16_t start = 0;
    int total = 0;
    for (;;)
    {
        std::vector<DlpDbInfo> batch;
        uint16_t next;
        e = dlp.read_db_list(DLP_LIST_RAM | DLP_LIST_MULTIPLE, start, batch, next);
        if (e != DlpError::NONE)
            break;
        for (auto &db : batch)
            printf("  %-32s %s/%s v%u flags=%04x\n", db.name.c_str(), db.type, db.creator,
                   db.version, db.flags);
        total += batch.size();
        start = next;
    }
    printf("%d RAM databases (list ended: %s)\n", total, dlp_error_name(e));
    dlp.add_sync_log_entry("FujiNet HotSync first light\n");
    e = dlp.end_of_sync();
    printf("EndOfSync: %s\n", dlp_error_name(e));
    return 0;
}
