#ifndef WAKE_WORD_ENGINE_H
#define WAKE_WORD_ENGINE_H

#include <Arduino.h>
#include <functional>

class WakeWordEngine {
public:
  static WakeWordEngine& getInstance() {
    static WakeWordEngine instance;
    return instance;
  }

  void setWakeCallback(std::function<void()> cb) { _onWake = cb; }

  bool begin() { return false; } // Fail immediately

  void resume() {}
  void pause() {}

  bool isReady() const { return false; }
  bool isListening() const { return false; }

  const char* wakeWord() const { return "你好小智"; }

private:
  WakeWordEngine() {}
  WakeWordEngine(const WakeWordEngine&) = delete;
  WakeWordEngine& operator=(const WakeWordEngine&) = delete;

  std::function<void()> _onWake = nullptr;
};

#endif // WAKE_WORD_ENGINE_H
