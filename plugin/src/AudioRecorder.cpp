#include "AudioRecorder.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <unordered_map>

AudioRecorder::AudioRecorder() : juce::Thread("TONE3000AudioRecorder") {
  // WAV, AIFF and (when enabled in the JUCE config) FLAC / Ogg Vorbis.
  // Registering FLAC/Ogg a second time here would trip JUCE's "same format
  // twice" assertion in debug builds.
  formatManager.registerBasicFormats();
}

AudioRecorder::~AudioRecorder() {
  releaseResources();
  stopThread(3000);
}

juce::File AudioRecorder::getDefaultRecordingsFolder() {
  const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                          .getChildFile("TONE3000")
                          .getChildFile("Recordings");
  folder.createDirectory();
  return folder;
}

void AudioRecorder::prepareToPlay(double sampleRate, int numChannels) {
  const juce::ScopedLock cl(controlLock);

  const double newRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  const int newChannels = numChannels > 0 ? numChannels : 2;

  // Hosts re-run prepareToPlay freely (latency changes, transport, block
  // size). With the same rate and channel count the ring buffer still fits,
  // so an active recording just keeps going instead of being cut off.
  const bool sameFormat = std::abs(newRate - currentSampleRate) < 0.01 &&
                          newChannels == currentNumChannels && ringBuffer.getNumSamples() > 0;
  if (sameFormat)
    return;

  // Rate/channels changed: the open file can't follow, finalise it.
  stopRecording();

  const juce::ScopedLock wl(writerLock);
  currentSampleRate = newRate;
  currentNumChannels = newChannels;

  const int bufferSize = static_cast<int>(currentSampleRate * kRingBufferSeconds);
  ringBuffer.setSize(currentNumChannels, bufferSize);
  ringBuffer.clear();
  writeScratch.setSize(currentNumChannels, kWriteChunk);
  fifo.setTotalSize(bufferSize);
  fifo.reset();
}

void AudioRecorder::releaseResources() {
  stopRecording();
}

void AudioRecorder::pushBlock(const juce::AudioBuffer<float>& buffer) {
  // Announce the push before checking `active`: stopRecording() clears
  // `active` and then waits for pushesInFlight to drain, so once it proceeds
  // no push can still be writing into the FIFO.
  pushesInFlight.fetch_add(1, std::memory_order_seq_cst);

  if (active.load(std::memory_order_acquire) && !paused.load(std::memory_order_relaxed)) {
    const int numSamples = buffer.getNumSamples();
    const int numChans = buffer.getNumChannels();
    const int ringChans = ringBuffer.getNumChannels();

    if (numSamples > 0 && numChans > 0 && ringChans > 0) {
      int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
      fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

      for (int c = 0; c < ringChans; ++c) {
        // Mono source into a stereo file: duplicate the last channel.
        const int srcChan = std::min(c, numChans - 1);
        if (size1 > 0)
          ringBuffer.copyFrom(c, start1, buffer, srcChan, 0, size1);
        if (size2 > 0)
          ringBuffer.copyFrom(c, start2, buffer, srcChan, size1, size2);
      }

      const int written = size1 + size2;
      fifo.finishedWrite(written);
      samplesCaptured.fetch_add(written, std::memory_order_relaxed);
      if (written < numSamples)
        samplesDropped.fetch_add(numSamples - written, std::memory_order_relaxed);
      // No samplesReadyEvent.signal() here: WaitableEvent takes a mutex, which
      // has no place on the audio thread. The writer polls every 20 ms, far
      // inside the ring buffer's 5 s headroom.
    }
  }

  pushesInFlight.fetch_sub(1, std::memory_order_seq_cst);
}

bool AudioRecorder::startRecording(const juce::File& file,
                                   const juce::String& formatExtension,
                                   int bitDepth,
                                   const juce::StringPairArray& metadata) {
  const juce::ScopedLock cl(controlLock);
  stopRecording();

  if (ringBuffer.getNumSamples() <= 0) {
    juce::Logger::writeToLog("[AudioRecorder] Error: not prepared (no audio device running).");
    return false;
  }

  file.getParentDirectory().createDirectory();

  std::unordered_map<juce::String, juce::String> meta;
  for (const auto& key : metadata.getAllKeys())
    meta[key] = metadata[key];

  auto tryFormat = [&](juce::AudioFormat* format, juce::File& outFile)
      -> std::unique_ptr<juce::AudioFormatWriter> {
    if (format == nullptr)
      return {};

    // The writers reject unsupported depths; pick the nearest one they take
    // (e.g. Ogg only allows 32, FLAC 16/24).
    const auto depths = format->getPossibleBitDepths();
    int depth = bitDepth;
    if (!depths.isEmpty() && !depths.contains(depth))
      depth = depths.getLast();

    // Compressed formats expose a quality ladder; index 0 is "lowest/fastest".
    // Prefer a mid/default rung (FLAC's documented default is 5; Ogg 5 ≈ 160 kbps).
    int qualityIndex = 0;
    const auto qualities = format->getQualityOptions();
    if (!qualities.isEmpty())
      qualityIndex = std::min(5, qualities.size() - 1);

    // Keep the extension honest when we fell back to another format, and
    // never append to / clobber an existing file (FileOutputStream appends).
    const auto ext = format->getFileExtensions().isEmpty() ? juce::String(".wav")
                                                           : format->getFileExtensions()[0];
    outFile = file.withFileExtension(ext).getNonexistentSibling(false);

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(outFile);
    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen()) {
      juce::Logger::writeToLog("[AudioRecorder] Error opening file stream: " + outFile.getFullPathName());
      return {};
    }

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(currentSampleRate)
                             .withNumChannels(currentNumChannels)
                             .withBitsPerSample(depth)
                             .withQualityOptionIndex(qualityIndex)
                             .withMetadataValues(meta);

    auto w = format->createWriterFor(stream, options);
    if (w == nullptr) {
      stream.reset();  // still ours on failure: close it before deleting
      outFile.deleteFile();
    }
    return w;
  };

  juce::File outFile;
  auto newWriter = tryFormat(formatManager.findFormatForFileExtension(formatExtension), outFile);
  if (newWriter == nullptr && !formatExtension.equalsIgnoreCase("wav")) {
    // e.g. "mp3": JUCE can read it but has no encoder.
    juce::Logger::writeToLog("[AudioRecorder] Format '" + formatExtension + "' not writable, falling back to WAV.");
    newWriter = tryFormat(formatManager.findFormatForFileExtension("wav"), outFile);
  }

  if (newWriter == nullptr) {
    juce::Logger::writeToLog("[AudioRecorder] Error creating AudioFormatWriter.");
    return false;
  }

  {
    const juce::ScopedLock wl(writerLock);
    writer = std::move(newWriter);
    targetFile = outFile;
    // No push is in flight (active is false and stopRecording drained them),
    // so resetting the FIFO here can't race the audio thread.
    fifo.reset();
    samplesCaptured.store(0, std::memory_order_relaxed);
    samplesWritten.store(0, std::memory_order_relaxed);
    samplesDropped.store(0, std::memory_order_relaxed);
    captureSampleRate.store(currentSampleRate, std::memory_order_relaxed);
  }

  paused.store(false, std::memory_order_relaxed);
  active.store(true, std::memory_order_release);

  if (!startThread(juce::Thread::Priority::normal)) {
    active.store(false, std::memory_order_release);
    while (pushesInFlight.load() > 0)
      juce::Thread::yield();
    {
      const juce::ScopedLock wl(writerLock);
      writer.reset();
      targetFile = {};
    }
    outFile.deleteFile();
    juce::Logger::writeToLog("[AudioRecorder] Error: could not start the writer thread.");
    return false;
  }

  juce::Logger::writeToLog("[AudioRecorder] Started recording to: " + outFile.getFullPathName());
  return true;
}

void AudioRecorder::stopRecording() {
  const juce::ScopedLock cl(controlLock);

  if (!active.exchange(false)) {
    return;
  }

  // Wait out a push the audio thread may be in the middle of (microseconds).
  while (pushesInFlight.load() > 0)
    juce::Thread::yield();

  signalThreadShouldExit();
  samplesReadyEvent.signal();
  stopThread(3000);

  const juce::ScopedLock wl(writerLock);

  // Flush any remaining samples in FIFO to disk
  if (writer) {
    drainFifo(INT_MAX);
    writer->flush();
    writer.reset();  // finalises the WAV header
    juce::Logger::writeToLog("[AudioRecorder] Stopped recording. Total samples: " +
                             juce::String(samplesWritten.load()) +
                             ", dropped: " + juce::String(samplesDropped.load()));
  }

  paused.store(false, std::memory_order_relaxed);
}

void AudioRecorder::setPaused(bool shouldBePaused) {
  paused.store(shouldBePaused, std::memory_order_relaxed);
}

double AudioRecorder::getRecordedDurationSeconds() const {
  const double rate = captureSampleRate.load(std::memory_order_relaxed);
  if (rate <= 0.0)
    return 0.0;
  return static_cast<double>(samplesCaptured.load(std::memory_order_relaxed)) / rate;
}

juce::int64 AudioRecorder::getRecordedBytes() const {
  const auto file = getCurrentFile();
  if (file.existsAsFile())
    return file.getSize();
  return 0;
}

juce::File AudioRecorder::getCurrentFile() const {
  const juce::ScopedLock cl(controlLock);
  return targetFile;
}

void AudioRecorder::drainFifo(int maxSamples) {
  if (writer == nullptr)
    return;

  int remaining = maxSamples;
  while (remaining > 0) {
    const int toRead = std::min({fifo.getNumReady(), kWriteChunk, remaining});
    if (toRead <= 0)
      break;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(toRead, start1, size1, start2, size2);

    for (int c = 0; c < currentNumChannels; ++c) {
      if (size1 > 0)
        writeScratch.copyFrom(c, 0, ringBuffer, c, start1, size1);
      if (size2 > 0)
        writeScratch.copyFrom(c, size1, ringBuffer, c, start2, size2);
    }

    const int got = size1 + size2;
    fifo.finishedRead(got);

    writer->writeFromAudioSampleBuffer(writeScratch, 0, got);
    samplesWritten.fetch_add(got, std::memory_order_relaxed);
    remaining -= got;
  }
}

void AudioRecorder::run() {
  while (!threadShouldExit()) {
    samplesReadyEvent.wait(20);

    if (threadShouldExit())
      break;

    const juce::ScopedLock sl(writerLock);
    drainFifo(INT_MAX);
  }
}

std::vector<AudioRecorder::RecordingFileInfo> AudioRecorder::getSavedRecordings() {
  std::vector<RecordingFileInfo> result;
  const juce::File folder = getDefaultRecordingsFolder();

  juce::Array<juce::File> files;
  folder.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.mp3;*.flac;*.ogg");

  // Newest first by modification time (names stop being chronological once
  // the user renames a recording).
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
    return a.getLastModificationTime() > b.getLastModificationTime();
  });

  juce::AudioFormatManager readers;
  readers.registerBasicFormats();

  for (const auto& f : files) {
    RecordingFileInfo info;
    info.fileName = f.getFileName();
    info.filePath = f.getFullPathName();
    info.fileSizeBytes = f.getSize();
    const auto creationTime = f.getCreationTime();
    info.creationTimeISO = juce::String::formatted("%04d-%02d-%02d %02d:%02d:%02d",
                                                   creationTime.getYear(),
                                                   creationTime.getMonth() + 1,
                                                   creationTime.getDayOfMonth(),
                                                   creationTime.getHours(),
                                                   creationTime.getMinutes(),
                                                   creationTime.getSeconds());

    std::unique_ptr<juce::AudioFormatReader> reader(readers.createReaderFor(f));
    if (reader != nullptr && reader->sampleRate > 0.0) {
      info.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      info.presetName = reader->metadataValues.getValue("Preset", "");
    }

    result.push_back(info);
  }

  return result;
}

bool AudioRecorder::deleteRecordingFile(const juce::String& filePath) {
  const juce::File f(filePath);
  // Only ever delete inside the recordings folder, whatever path comes in.
  if (!f.existsAsFile() || !f.isAChildOf(getDefaultRecordingsFolder()))
    return false;
  return f.deleteFile();
}

bool AudioRecorder::renameRecordingFile(const juce::String& oldPath, const juce::String& newName) {
  const juce::File oldFile(oldPath);
  if (!oldFile.existsAsFile() || !oldFile.isAChildOf(getDefaultRecordingsFolder()))
    return false;

  // Strips path separators and other illegal characters, so the new name
  // can't escape the folder ("../x").
  juce::String sanitized = juce::File::createLegalFileName(newName.trim());
  if (sanitized.isEmpty())
    return false;

  if (!juce::File(sanitized).hasFileExtension(oldFile.getFileExtension()))
    sanitized += oldFile.getFileExtension();

  const juce::File newFile = oldFile.getParentDirectory().getChildFile(sanitized);
  if (newFile == oldFile)
    return true;
  if (newFile.exists())
    return false;  // never overwrite another recording
  return oldFile.moveFileTo(newFile);
}
