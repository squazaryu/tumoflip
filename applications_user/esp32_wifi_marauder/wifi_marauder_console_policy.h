#pragma once
#include <stdbool.h>
#include <string.h>

// Use the mission-specific command. ESP firmware 1.17 also stops Recon from stopscan.
static inline const char* wifi_marauder_console_stop_command(const char* command) {
    if(command && (!strcmp(command, "recon status") || !strcmp(command, "recon stop") ||
                   !strcmp(command, "protocolinfo") || !strcmp(command, "backupstatus") ||
                   !strcmp(command, "backupspiffs"))) return NULL;
    return command && (!strcmp(command, "recon wifi") || !strcmp(command, "recon ble")) ?
               "recon stop\n" :
               "stopscan\n";
}

static inline bool wifi_marauder_console_can_mark_poi(const char* command) {
    return command && !strncmp(command, "wardrive", 8) &&
           (command[8] == '\0' || command[8] == ' ');
}
