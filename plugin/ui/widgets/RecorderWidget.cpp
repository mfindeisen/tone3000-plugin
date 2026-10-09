#include "RecorderWidget.h"
#include "../core/Theme.h"

namespace t3k::ui {

void RecorderWidget::RecordButton::paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) {
  auto bounds = getLocalBounds().toFloat().reduced(4.0f);
  if (isRecording) {
    g.setColour(juce::Colours::red.withAlpha(isButtonDown ? 0.7f : 1.0f));
    g.fillRoundedRectangle(bounds.reduced(2.0f), 4.0f);
  } else {
    g.setColour(isMouseOverButton ? juce::Colours::white : theme::kTextSecondary);
    g.fillEllipse(bounds);
  }
}

RecorderWidget::RecorderWidget(Backend& backend) : backend_(backend) {
  recordButton_.onClick = [this] { toggleRecording(); };
  addAndMakeVisible(recordButton_);

  timeLabel_.setJustificationType(juce::Justification::centredRight);
  timeLabel_.setFont(juce::Font(14.0f));
  timeLabel_.setColour(juce::Label::textColourId, theme::kTextSecondary);
  addAndMakeVisible(timeLabel_);

  startTimerHz(2);
  updateState();
}

RecorderWidget::~RecorderWidget() {
  stopTimer();
}

void RecorderWidget::timerCallback() {
  updateState();
}

void RecorderWidget::toggleRecording() {
  if (isRecording_) {
    backend_.stopRecording();
  } else {
    backend_.startRecording("wav", 24);
  }
  updateState();
}

void RecorderWidget::updateState() {
  juce::var state = backend_.getRecordingState();
  if (!state.isObject()) return;

  isRecording_ = static_cast<bool>(state["isRecording"]);
  recordButton_.isRecording = isRecording_;
  recordButton_.repaint();
  
  if (isRecording_) {
    double secs = static_cast<double>(state["durationSeconds"]);
    int mins = static_cast<int>(secs) / 60;
    int s = static_cast<int>(secs) % 60;
    timeLabel_.setText(juce::String::formatted("%02d:%02d", mins, s), juce::dontSendNotification);
    timeLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
  } else {
    timeLabel_.setText("00:00", juce::dontSendNotification);
    timeLabel_.setColour(juce::Label::textColourId, theme::kTextSecondary);
  }
}

void RecorderWidget::paint(juce::Graphics&) {}

void RecorderWidget::resized() {
  auto bounds = getLocalBounds();
  recordButton_.setBounds(bounds.removeFromRight(bounds.getHeight()));
  timeLabel_.setBounds(bounds);
}

}  // namespace t3k::ui
