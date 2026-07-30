#pragma once

#include <Arduino.h>
#include <ir_Midea.h>

#include "Climate/PortaSplitState.h"
#include "Settings/Settings.h"

enum class IrCommandType : uint8_t { None, Climate, FollowMe, Test };

class PortaSplitIrController {
 public:
  ~PortaSplitIrController();
  void begin(const SettingsData &settings);
  bool canSend(uint32_t now) const;
  bool sendClimate(const PortaSplitState &state);
  bool sendFollowMe(const PortaSplitState &state, float temperature);
  uint32_t lastSendMs() const { return _lastSendMs; }
  uint32_t lastFollowMeMs() const { return _lastFollowMeMs; }
  uint64_t lastRaw() const { return _lastRaw; }
  IrCommandType lastType() const { return _lastType; }

 private:
  IRMideaAC *_midea = nullptr;
  const SettingsData *_settings = nullptr;
  uint32_t _lastSendMs = 0;
  uint32_t _lastFollowMeMs = 0;
  uint64_t _lastRaw = 0;
  IrCommandType _lastType = IrCommandType::None;
  PortaSplitState _lastApplied{};
  bool _hasLastApplied = false;
  void applyClimate(const PortaSplitState &state);
  void logState(const char *type, const PortaSplitState &state, float sensorTemperature);
};
