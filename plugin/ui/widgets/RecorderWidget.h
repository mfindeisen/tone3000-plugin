#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../backend/Backend.h"
#include "Clickable.h"

namespace t3k::ui {

class RecorderWidget : public juce::Component, private juce::Timer {
public:
  explicit RecorderWidget(Backend& backend);
  ~RecorderWidget() override;

  void resized() override;
  void paint(juce::Graphics& g) override;

private:
  void timerCallback() override;
  void toggleRecording();
  void updateState();

  class RecordButton : public Clickable {
  public:
    RecordButton() : Clickable({}) {}
    void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
    bool isRecording = false;
  };

  Backend& backend_;
  RecordButton recordButton_;
  juce::Label timeLabel_;
  bool isRecording_ = false;
};

}  // namespace t3k::ui
