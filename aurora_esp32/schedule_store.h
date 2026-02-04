#pragma once

#include <Arduino.h>
#include <vector>

struct ScheduleEntry {
  int id = -1;
  uint8_t day_of_week = 0;  // 0 = Monday per backend contract
  uint8_t hour = 0;
  uint8_t minute = 0;
  bool repeat = true;
};

struct ContainerSchedules {
  int slot_number = -1;
  String pill_name;
  std::vector<ScheduleEntry> schedules;
};

class ScheduleStore {
 public:
  ScheduleStore() = default;

  void applyConfig(int64_t version, std::vector<ContainerSchedules>&& containers);

  int64_t version() const { return version_; }
  const std::vector<ContainerSchedules>& containers() const { return containers_; }

 private:
  int64_t version_ = 0;
  std::vector<ContainerSchedules> containers_;
};

