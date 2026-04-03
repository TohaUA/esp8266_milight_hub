//
// Created by chris on 9/18/2023.
//

#include <BackupManager.h>
#include <ProjectFS.h>
#include <DebugSerial.h>

void BackupManager::createBackup(const Settings& settings, Print& stream) {
  // Write the wrapper opening and settings key
  stream.print(F("{\"settings\":"));

  // Serialize live in-memory settings directly (single allocation, no file I/O)
  settings.serialize(stream);

  // Append aliases as a sibling key inside the settings object.
  // Since serialize() already wrote a complete JSON object ending with '}',
  // we need to rewind that closing brace and append the aliases field.
  // Instead, we close the settings object and add aliases at the top level.

  // Actually, group_id_aliases is already part of Settings::patch/serialize
  // if present. But aliases are stored in a separate binary file and not
  // included in serialize(). We need to append them.
  // The simplest correct approach: close the wrapper and add aliases separately.

  stream.print(F(",\"aliases\":"));

  // Build just the aliases JSON
  JsonDocument aliasDoc;
  JsonObject aliasesObj = aliasDoc.to<JsonObject>();
  for (const auto& entry : settings.groupIdAliases) {
    JsonArray bulbProps = aliasesObj[entry.first].to<JsonArray>();
    const BulbId& bulbId = entry.second.bulbId;
    bulbProps.add(MiLightRemoteTypeHelpers::remoteTypeToString(bulbId.deviceType));
    bulbProps.add(bulbId.deviceId);
    bulbProps.add(bulbId.groupId);
  }
  serializeJson(aliasDoc, stream);

  stream.print('}');
}

BackupManager::RestoreStatus BackupManager::restoreBackup(Settings& settings, Stream& stream) {
  JsonDocument doc;
  auto error = deserializeJson(doc, stream);

  if (error) {
    DebugSerial.printf("ERROR: failed to parse backup JSON: %s\n", error.c_str());
    return RestoreStatus::INVALID_JSON;
  }

  if (!doc[F("settings")].is<JsonObject>()) {
    DebugSerial.println(F("ERROR: backup missing 'settings' key"));
    return RestoreStatus::MISSING_SETTINGS;
  }

  // Reset settings to defaults and apply from backup
  settings = Settings();

  JsonObject settingsObj = doc[F("settings")].as<JsonObject>();

  // Merge aliases into settings so patch() handles them via group_id_aliases
  if (doc[F("aliases")].is<JsonObject>()) {
    JsonObject aliases = doc[F("aliases")].as<JsonObject>();
    settingsObj[F("group_id_aliases")] = aliases;
  }

  String patchError = settings.patch(settingsObj);
  if (patchError.length() > 0) {
    DebugSerial.printf("ERROR: invalid settings in backup: %s\n", patchError.c_str());
    return RestoreStatus::INVALID_SETTINGS;
  }
  settings.save();

  // Reload to ensure aliases file is synced
  Settings::load(settings);

  return RestoreStatus::OK;
}
