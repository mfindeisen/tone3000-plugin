import React, { useState, useEffect } from 'react';
import type { IAudioBackend, RecordingState } from '../types/IAudioBackend';

interface RecorderWidgetProps {
  backend: IAudioBackend;
  onOpenRecordings: () => void;
}

export const RecorderWidget: React.FC<RecorderWidgetProps> = ({ backend, onOpenRecordings }) => {
  const [recordingState, setRecordingState] = useState<RecordingState>({
    isRecording: false,
    isPaused: false,
    durationSeconds: 0,
    fileSizeBytes: 0,
    filePath: '',
    fileName: '',
  });

  const [selectedFormat, setSelectedFormat] = useState<'wav' | 'flac' | 'ogg'>('wav');
  const [selectedBitDepth, setSelectedBitDepth] = useState<number>(24);
  const [showFormatMenu, setShowFormatMenu] = useState(false);

  // Poll recording state every 200ms when active
  useEffect(() => {
    let timer: number | null = null;

    const poll = async () => {
      try {
        const getStateFn = backend.getPluginFunction('getRecordingState');
        if (getStateFn) {
          const state = (await getStateFn()) as RecordingState;
          if (state) {
            setRecordingState(state);
          }
        }
      } catch (err) {
        console.error('Failed to poll recording state:', err);
      }
    };

    poll();
    timer = window.setInterval(poll, 200);

    return () => {
      if (timer !== null) clearInterval(timer);
    };
  }, [backend]);

  const handleToggleRecord = async () => {
    try {
      if (recordingState.isRecording) {
        const stopFn = backend.getPluginFunction('stopRecording');
        if (stopFn) await stopFn();
      } else {
        const startFn = backend.getPluginFunction('startRecording');
        if (startFn) await startFn(selectedFormat, selectedBitDepth);
      }
    } catch (err) {
      console.error('Error toggling recording:', err);
    }
  };

  const handleTogglePause = async () => {
    try {
      const pauseFn = backend.getPluginFunction('setRecordingPaused');
      if (pauseFn) await pauseFn(!recordingState.isPaused);
    } catch (err) {
      console.error('Error setting pause state:', err);
    }
  };

  const formatTimer = (seconds: number) => {
    const totalSec = Math.floor(seconds);
    const hrs = Math.floor(totalSec / 3600);
    const mins = Math.floor((totalSec % 3600) / 60);
    const secs = totalSec % 60;

    const pad = (n: number) => n.toString().padStart(2, '0');
    if (hrs > 0) {
      return `${pad(hrs)}:${pad(mins)}:${pad(secs)}`;
    }
    return `${pad(mins)}:${pad(secs)}`;
  };

  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '8rem',
        backgroundColor: '#121214',
        border: '1rem solid #28282c',
        borderRadius: '20rem',
        padding: '4rem 12rem',
        position: 'relative',
      }}
    >
      {/* Record button */}
      <button
        onClick={handleToggleRecord}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          width: '28rem',
          height: '28rem',
          borderRadius: '50%',
          border: 'none',
          backgroundColor: recordingState.isRecording ? '#ff3b30' : '#2c2c30',
          color: '#ffffff',
          cursor: 'pointer',
          boxShadow: recordingState.isRecording ? '0 0 10rem rgba(255, 59, 48, 0.6)' : 'none',
          transition: 'all 0.2s ease',
        }}
        title={recordingState.isRecording ? 'Stop Recording' : 'Start Output Recording'}
      >
        {recordingState.isRecording ? (
          <div
            style={{
              width: '10rem',
              height: '10rem',
              backgroundColor: '#ffffff',
              borderRadius: '2rem',
            }}
          />
        ) : (
          <div
            style={{
              width: '12rem',
              height: '12rem',
              backgroundColor: '#ff3b30',
              borderRadius: '50%',
            }}
          />
        )}
      </button>

      {/* Timer / Format Display */}
      {recordingState.isRecording ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: '8rem' }}>
          <span
            style={{
              fontFamily: 'monospace',
              fontSize: '13rem',
              fontWeight: 600,
              color: recordingState.isPaused ? '#e5c07b' : '#ff453a',
              animation: recordingState.isPaused ? 'none' : 'pulse 1.5s infinite',
            }}
          >
            {formatTimer(recordingState.durationSeconds)}
          </span>
          <button
            onClick={handleTogglePause}
            style={{
              background: 'none',
              border: 'none',
              color: '#8e8e93',
              cursor: 'pointer',
              fontSize: '11rem',
              padding: '2rem 4rem',
            }}
            title={recordingState.isPaused ? 'Resume' : 'Pause'}
          >
            {recordingState.isPaused ? '▶' : '❚❚'}
          </button>
        </div>
      ) : (
        <div style={{ position: 'relative' }}>
          <button
            onClick={() => setShowFormatMenu(!showFormatMenu)}
            style={{
              background: 'none',
              border: 'none',
              color: '#8e8e93',
              fontSize: '12rem',
              fontWeight: 500,
              cursor: 'pointer',
              padding: '2rem 4rem',
            }}
          >
            {selectedFormat.toUpperCase()} ({selectedBitDepth}-bit) ▾
          </button>

          {showFormatMenu && (
            <div
              style={{
                position: 'absolute',
                top: '32rem',
                left: '0',
                backgroundColor: '#1c1c1e',
                border: '1rem solid #3a3a3c',
                borderRadius: '8rem',
                padding: '6rem 0',
                zIndex: 1000,
                boxShadow: '0 8rem 24rem rgba(0,0,0,0.5)',
                minWidth: '130rem',
              }}
            >
              <div
                style={{
                  padding: '4rem 12rem',
                  fontSize: '10rem',
                  color: '#636366',
                  textTransform: 'uppercase',
                  fontWeight: 700,
                }}
              >
                Format Choice
              </div>
              {[
                { fmt: 'wav', bits: 24, label: 'WAV 24-bit (Mastering)' },
                { fmt: 'wav', bits: 16, label: 'WAV 16-bit (CD Quality)' },
                { fmt: 'flac', bits: 24, label: 'FLAC (Lossless)' },
                { fmt: 'ogg', bits: 16, label: 'Ogg Vorbis (Compressed)' },
              ].map((opt) => (
                <button
                  key={`${opt.fmt}-${opt.bits}`}
                  onClick={() => {
                    setSelectedFormat(opt.fmt as 'wav' | 'flac' | 'ogg');
                    setSelectedBitDepth(opt.bits);
                    setShowFormatMenu(false);
                  }}
                  style={{
                    display: 'block',
                    width: '100%',
                    textAlign: 'left',
                    padding: '6rem 12rem',
                    background: 'none',
                    border: 'none',
                    color:
                      selectedFormat === opt.fmt && selectedBitDepth === opt.bits
                        ? '#007aff'
                        : '#ffffff',
                    fontSize: '12rem',
                    cursor: 'pointer',
                  }}
                >
                  {opt.label}
                </button>
              ))}
            </div>
          )}
        </div>
      )}

      {/* View Library Button */}
      <button
        onClick={onOpenRecordings}
        style={{
          background: 'none',
          border: 'none',
          color: '#636366',
          cursor: 'pointer',
          fontSize: '14rem',
          padding: '2rem 4rem',
          display: 'flex',
          alignItems: 'center',
        }}
        title="View Recordings Folder & Files"
      >
        📂
      </button>
    </div>
  );
};
