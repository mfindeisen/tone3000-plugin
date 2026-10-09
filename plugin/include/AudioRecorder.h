#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

// Records the processor output to disk.
//
// Threading:
//  - pushBlock() is the only call made from the audio thread. It is lock-free
//    and non-allocating: it copies into a ring buffer. It does not signal the
//    writer (WaitableEvent takes a mutex); the writer polls every 20 ms.
//  - A background thread (run()) drains the ring buffer into the file.
//  - prepareToPlay/startRecording/stopRecording/setPaused and the getters are
//    control calls (message thread or the host's prepare thread). They are
//    serialised by controlLock and never touch the audio thread's locks.
class AudioRecorder : public juce::Thread {
public:
  AudioRecorder();
  ~AudioRecorder() override;

  /** Initialize sample rate and channel count (call from prepareToPlay). An
      active recording keeps running when the format is unchanged; otherwise it
      is finalised, because the file can't change rate/channels midway. */
  void prepareToPlay(double sampleRate, int numChannels);

  /** Release resources and ensure any active recording is safely stopped. */
  void releaseResources();

  /**
   * Real-Time Audio Thread entry point.
   * Lock-free, non-allocating push of audio samples into internal ring buffer.
   */
  void pushBlock(const juce::AudioBuffer<float>& buffer);

  /**
   * Start recording to a file with specified format ("wav", "flac" or "ogg"),
   * bit depth, and optional metadata tags. Unwritable formats / unsupported
   * settings fall back to WAV. Returns false if nothing could be started.
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
  bool isRecording() const { return active.load(std::memory_order_acquire); }

  /** Elapsed recording duration in seconds (captured audio, pauses excluded). */
  double getRecordedDurationSeconds() const;

  /** Current size of the recording file on disk. */
  juce::int64 getRecordedBytes() const;

  /** Samples dropped because the disk writer fell behind (ring buffer full). */
  juce::int64 getDroppedSamples() const { return samplesDropped.load(std::memory_order_relaxed); }

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

  /** List all recordings saved in the default recordings directory (newest first). */
  static std::vector<RecordingFileInfo> getSavedRecordings();

  /** Delete a recording file by path (only inside the recordings folder). */
  static bool deleteRecordingFile(const juce::String& filePath);

  /** Rename a recording file (stays in its folder, never overwrites). */
  static bool renameRecordingFile(const juce::String& oldPath, const juce::String& newName);

private:
  void run() override;

  // Drains up to maxSamples from the FIFO into the writer. Caller holds writerLock.
  void drainFifo(int maxSamples);

  juce::AudioFormatManager formatManager;
  std::unique_ptr<juce::AudioFormatWriter> writer;

  double currentSampleRate{44100.0};
  int currentNumChannels{2};

  // Lock-free ring buffer for RT thread safety
  static constexpr int kRingBufferSeconds = 5;
  static constexpr int kWriteChunk = 4096;
  juce::AbstractFifo fifo{1};
  juce::AudioBuffer<float> ringBuffer;
  juce::AudioBuffer<float> writeScratch;  // disk-thread scratch, sized in prepareToPlay

  std::atomic<bool> active{false};
  std::atomic<bool> paused{false};
  std::atomic<int> pushesInFlight{0};
  std::atomic<juce::int64> samplesCaptured{0};
  std::atomic<juce::int64> samplesWritten{0};
  std::atomic<juce::int64> samplesDropped{0};
  // Rate used for samplesCaptured → seconds. Snapshotted at startRecording so
  // a later prepareToPlay rate change cannot skew the duration of this take.
  std::atomic<double> captureSampleRate{44100.0};

  juce::WaitableEvent samplesReadyEvent;
  juce::CriticalSection controlLock;  // start/stop/prepare, targetFile
  juce::CriticalSection writerLock;   // writer + FIFO read side
  juce::File targetFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};
