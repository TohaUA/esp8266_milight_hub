#include <GroupStatePersistence.h>
#include <FS.h>
#include "ProjectFS.h"

#ifdef ESP8266
    static const char FILE_PREFIX[] = "group_states/";
#elif defined(ESP32)
    static const char FILE_PREFIX[] = "/group_states/";
#endif

void GroupStatePersistence::get(const BulbId &id, GroupState& state) {
  char path[30];
  memset(path, 0, 30);
  buildFilename(id, path);

  if (ProjectFS.exists(path)) {
    File f = ProjectFS.open(path, "r");
    state.load(f);
    f.close();
  }
}

void GroupStatePersistence::set(const BulbId &id, const GroupState& state) {
  char path[30];
  memset(path, 0, 30);
  buildFilename(id, path);

  char tmpPath[35];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);

  File f = ProjectFS.open(tmpPath, "w");
  if (!f) {
    Serial.printf("Failed to open state temp file: %s\n", tmpPath);
    return;
  }
  state.dump(f);
  f.close();

  ProjectFS.remove(path);
  ProjectFS.rename(tmpPath, path);
}

void GroupStatePersistence::clear(const BulbId &id) {
  char path[30];
  buildFilename(id, path);

  if (ProjectFS.exists(path)) {
    ProjectFS.remove(path);
  }
}

char* GroupStatePersistence::buildFilename(const BulbId &id, char *buffer) {
  uint32_t compactId = id.getCompactId();
  return buffer + sprintf(buffer, "%s%x", FILE_PREFIX, compactId);
}
