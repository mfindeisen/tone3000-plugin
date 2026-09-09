#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

class AudioRecorder : public juce::Thread {
public:
  AudioRecorder();
  ~AudioRecorder() override;

  /** Initialize sample rate and channel count from processBlock/prepareToPlay. */
  void prepareToPlay(double sampleRate, int numChannels);

  /** Release resources and ensure any active recording is safely stopped. */
  void releaseResources();

  /**
   * Real-Time Audio Thread entry point.
   * Lock-free, non-allocating push of audio samples into internal ring buffer.
   */
  void pushBlock(const juce::AudioBuffer<float>& buffer);

  /**
   * Start recording to a file with specified format ("wav" or "mp3"), bit depth,
   * and optional metadata tags.
   */
  bool startRecording(const juce::File& file,
                      const juce::String& formatExtension = "wav",
                      int bitDepth = 24,
                      const juce::StringPairArray& metadata = {});

  /** Stop recording and flush remaining samples to disk. */
  void stopRecording();

  /** Pause or resume recording. */
  void setPaused(bool shouldBePaused);
  bool isPaused() const { return paused.load(std::memory_order_relaxed); }

  /** Returns true if currently recording (active). */
  bool isRecording() const { return active.load(std::memory_order_relaxed); }

  /** Get elapsed recording duration in seconds. */
  double getRecordedDurationSeconds() const;

  /** Get total recorded bytes written to disk. */
  juce::int64 getRecordedBytes() const;

  /** Get current recording file path. */
  juce::File getCurrentFile() const;

  /** Get default recordings directory (~/Documents/TONE3000/Recordings/). */
  static juce::File getDefaultRecordingsFolder();

  /** Structure representing a saved recording file info. */
  struct RecordingFileInfo {
    juce::String fileName;
    juce::String filePath;
    juce::int64 fileSizeBytes{0};
    double durationSeconds{0.0};
    juce::String creationTimeISO;
    juce::String presetName;
  };

  /** List all recordings saved in the default recordings directory. */
  static std::vector<RecordingFileInfo> getSavedRecordings();

  /** Delete a recording file by path. */
  static bool deleteRecordingFile(const juce::String& filePath);

  /** Rename a recording file. */
  static bool renameRecordingFile(const juce::String& oldPath, const juce::String& newName);

private:
  void run() override;

  juce::AudioFormatManager formatManager;
  std::unique_ptr<juce::AudioFormatWriter> writer;
  std::unique_ptr<juce::FileOutputStream> fileStream;

  double currentSampleRate{44100.0};
  int currentNumChannels{2};

  // Lock-free ring buffer for RT thread safety
  static constexpr int kRingBufferSeconds = 5;
  juce::AbstractFifo fifo{1};
  juce::AudioBuffer<float> ringBuffer;

  std::atomic<bool> active{false};
  std::atomic<bool> paused{false};
  std::atomic<juce::int64> samplesWritten{0};

  juce::WaitableEvent samplesReadyEvent;
  juce::CriticalSection writerLock;
  juce::File targetFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};
