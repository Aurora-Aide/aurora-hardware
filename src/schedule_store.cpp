#include "schedule_store.h"

void ScheduleStore::applyConfig(int64_t version, std::vector<ContainerSchedules>&& containers) {
  version_ = version;
  containers_ = std::move(containers);
}

