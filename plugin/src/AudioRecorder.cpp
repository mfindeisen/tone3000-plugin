#include "AudioRecorder.h"
#include <algorithm>

AudioRecorder::AudioRecorder() : juce::Thread("TONE3000AudioRecorder") {
  formatManager.registerBasicFormats();
  formatManager.registerFormat(new juce::FlacAudioFormat(), true);
  formatManager.registerFormat(new juce::OggVorbisAudioFormat(), true);
}

AudioRecorder::~AudioRecorder() {
  releaseResources();
}

juce::File AudioRecorder::getDefaultRecordingsFolder() {
  const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                          .getChildFile("TONE3000")
                          .getChildFile("Recordings");
  folder.createDirectory();
  return folder;
}

void AudioRecorder::prepareToPlay(double sampleRate, int numChannels) {
  stopRecording();

  const juce::ScopedLock sl(writerLock);
  currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  currentNumChannels = numChannels > 0 ? numChannels : 2;

  const int bufferSize = static_cast<int>(currentSampleRate * kRingBufferSeconds);
  ringBuffer.setSize(currentNumChannels, bufferSize);
  fifo.setTotalSize(bufferSize);
  fifo.reset();
}

void AudioRecorder::releaseResources() {
  stopRecording();
}

void AudioRecorder::pushBlock(const juce::AudioBuffer<float>& buffer) {
  if (!active.load(std::memory_order_relaxed) || paused.load(std::memory_order_relaxed))
    return;

  const int numSamples = buffer.getNumSamples();
  const int numChans = buffer.getNumChannels();

  if (numSamples <= 0 || numChans <= 0)
    return;

  int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
  fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

  if (size1 > 0) {
    for (int c = 0; c < currentNumChannels; ++c) {
      const int srcChan = std::min(c, numChans - 1);
      ringBuffer.copyFrom(c, start1, buffer.getReadPointer(srcChan), size1);
    }
  }

  if (size2 > 0) {
    for (int c = 0; c < currentNumChannels; ++c) {
      const int srcChan = std::min(c, numChans - 1);
      ringBuffer.copyFrom(c, start2, buffer.getReadPointer(srcChan, size1), size2);
    }
  }

  fifo.finishedWrite(size1 + size2);
  samplesReadyEvent.signal();
}

bool AudioRecorder::startRecording(const juce::File& file,
                                    const juce::String& formatExtension,
                                    int bitDepth,
                                    const juce::StringPairArray& metadata) {
  stopRecording();

  const juce::ScopedLock sl(writerLock);

  targetFile = file;
  targetFile.getParentDirectory().createDirectory();

  juce::AudioFormat* format = formatManager.findFormatForFileExtension(formatExtension);
  if (!format) {
    format = formatManager.findFormatForFileExtension("wav");
  }

  if (!format) {
    juce::Logger::writeToLog("[AudioRecorder] Error: No suitable AudioFormat found.");
    return false;
  }

  targetFile.deleteFile();
  auto stream = std::make_unique<juce::FileOutputStream>(targetFile);

  if (stream->failedToOpen()) {
    juce::Logger::writeToLog("[AudioRecorder] Error opening file stream: " + targetFile.getFullPathName());
    return false;
  }

  juce::AudioFormatWriter* rawWriter = format->createWriterFor(
      stream.get(), currentSampleRate, static_cast<unsigned int>(currentNumChannels), bitDepth, metadata, 0);

  if (!rawWriter) {
    juce::Logger::writeToLog("[AudioRecorder] Error creating AudioFormatWriter.");
    return false;
  }

  // Stream is now owned by rawWriter
  stream.release();
  writer.reset(rawWriter);

  samplesWritten.store(0, std::memory_order_relaxed);
  fifo.reset();
  paused.store(false, std::memory_order_relaxed);
  active.store(true, std::memory_order_release);

  startThread(juce::Thread::Priority::normal);
  juce::Logger::writeToLog("[AudioRecorder] Started recording to: " + targetFile.getFullPathName());
  return true;
}

void AudioRecorder::stopRecording() {
  if (!active.exchange(false)) {
    return;
  }

  samplesReadyEvent.signal();
  stopThread(3000);

  const juce::ScopedLock sl(writerLock);

  // Flush any remaining samples in FIFO to disk
  if (writer) {
    int numReady = fifo.getNumReady();
    if (numReady > 0) {
      int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
      fifo.prepareToRead(numReady, start1, size1, start2, size2);

      juce::AudioBuffer<float> tempBuf(currentNumChannels, numReady);

      if (size1 > 0) {
        for (int c = 0; c < currentNumChannels; ++c)
          tempBuf.copyFrom(c, 0, ringBuffer.getReadPointer(c, start1), size1);
      }
      if (size2 > 0) {
        for (int c = 0; c < currentNumChannels; ++c)
          tempBuf.copyFrom(c, size1, ringBuffer.getReadPointer(c, start2), size2);
      }

      fifo.finishedRead(size1 + size2);
      writer->writeFromAudioSampleBuffer(tempBuf, 0, numReady);
      samplesWritten.fetch_add(numReady);
    }

    writer->flush();
    writer.reset(); // Safely closes WAV header
    juce::Logger::writeToLog("[AudioRecorder] Stopped recording. Total samples: " +
                             juce::String(samplesWritten.load()));
  }
}

void AudioRecorder::setPaused(bool shouldBePaused) {
  paused.store(shouldBePaused, std::memory_order_relaxed);
}

double AudioRecorder::getRecordedDurationSeconds() const {
  if (currentSampleRate <= 0.0)
    return 0.0;
  return static_cast<double>(samplesWritten.load(std::memory_order_relaxed)) / currentSampleRate;
}

juce::int64 AudioRecorder::getRecordedBytes() const {
  if (targetFile.existsAsFile())
    return targetFile.getSize();
  return 0;
}

juce::File AudioRecorder::getCurrentFile() const {
  return targetFile;
}

void AudioRecorder::run() {
  while (!threadShouldExit()) {
    samplesReadyEvent.wait(100);

    if (threadShouldExit())
      break;

    const juce::ScopedLock sl(writerLock);
    if (!writer || !active.load(std::memory_order_relaxed))
      continue;

    int numReady = fifo.getNumReady();
    // Process in chunks of up to 4096 samples
    while (numReady > 0 && !threadShouldExit()) {
      int numToRead = std::min(numReady, 4096);
      int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
      fifo.prepareToRead(numToRead, start1, size1, start2, size2);

      juce::AudioBuffer<float> tempBuf(currentNumChannels, numToRead);

      if (size1 > 0) {
        for (int c = 0; c < currentNumChannels; ++c)
          tempBuf.copyFrom(c, 0, ringBuffer.getReadPointer(c, start1), size1);
      }
      if (size2 > 0) {
        for (int c = 0; c < currentNumChannels; ++c)
          tempBuf.copyFrom(c, size1, ringBuffer.getReadPointer(c, start2), size2);
      }

      fifo.finishedRead(size1 + size2);

      writer->writeFromAudioSampleBuffer(tempBuf, 0, numToRead);
      samplesWritten.fetch_add(numToRead, std::memory_order_relaxed);

      numReady = fifo.getNumReady();
    }
  }
}

std::vector<AudioRecorder::RecordingFileInfo> AudioRecorder::getSavedRecordings() {
  std::vector<RecordingFileInfo> result;
  const juce::File folder = getDefaultRecordingsFolder();

  juce::Array<juce::File> files;
  folder.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.mp3;*.flac;*.ogg");

  // Sort descending by creation / modification time
  files.sort();

  for (int i = files.size() - 1; i >= 0; --i) {
    const auto& f = files[i];
    RecordingFileInfo info;
    info.fileName = f.getFileName();
    info.filePath = f.getFullPathName();
    info.fileSizeBytes = f.getSize();
    auto creationTime = f.getCreationTime();
    info.creationTimeISO = juce::String::formatted("%04d-%02d-%02d %02d:%02d:%02d",
                                                    creationTime.getYear(),
                                                    creationTime.getMonth() + 1,
                                                    creationTime.getDayOfMonth(),
                                                    creationTime.getHours(),
                                                    creationTime.getMinutes(),
                                                    creationTime.getSeconds());

    // Estimate duration if wav header can be read
    juce::AudioFormatManager tempMgr;
    tempMgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(tempMgr.createReaderFor(f));
    if (reader) {
      info.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      info.presetName = reader->metadataValues.getValue("Preset", "");
    } else {
      info.durationSeconds = 0.0;
    }

    result.push_back(info);
  }

  return result;
}

bool AudioRecorder::deleteRecordingFile(const juce::String& filePath) {
  juce::File f(filePath);
  if (f.existsAsFile())
    return f.deleteFile();
  return false;
}

bool AudioRecorder::renameRecordingFile(const juce::String& oldPath, const juce::String& newName) {
  juce::File oldFile(oldPath);
  if (!oldFile.existsAsFile())
    return false;

  juce::String sanitized = newName.trim();
  if (!sanitized.endsWithIgnoreCase(".wav") && !sanitized.endsWithIgnoreCase(".mp3")) {
    sanitized += "." + oldFile.getFileExtension();
  }

  juce::File newFile = oldFile.getParentDirectory().getChildFile(sanitized);
  return oldFile.moveFileTo(newFile);
}
