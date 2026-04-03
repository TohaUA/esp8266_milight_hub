//
// Created by chris on 9/18/2023.
//

#ifndef ESP8266_MILIGHT_HUB_BACKUPMANAGER_H
#define ESP8266_MILIGHT_HUB_BACKUPMANAGER_H

#include <Settings.h>

class BackupManager {
public:
    enum class RestoreStatus {
        OK,
        INVALID_JSON,
        MISSING_SETTINGS,
        INVALID_SETTINGS
    };

    static void createBackup(const Settings& settings, Print& stream);
    static RestoreStatus restoreBackup(Settings& settings, Stream& stream);
};


#endif //ESP8266_MILIGHT_HUB_BACKUPMANAGER_H
