import React, { useState, useEffect } from 'react';
import type { IAudioBackend, RecordingFileInfo } from '../types/IAudioBackend';

interface RecordingsModalProps {
  backend: IAudioBackend;
  isOpen: boolean;
  onClose: () => void;
}

export const RecordingsModal: React.FC<RecordingsModalProps> = ({ backend, isOpen, onClose }) => {
  const [recordings, setRecordings] = useState<RecordingFileInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [editingFile, setEditingFile] = useState<string | null>(null);
  const [newName, setNewName] = useState('');

  const fetchRecordings = async () => {
    setLoading(true);
    try {
      const fn = backend.getPluginFunction('getSavedRecordings');
      if (fn) {
        const res = (await fn()) as RecordingFileInfo[];
        if (Array.isArray(res)) {
          setRecordings(res);
        }
      }
    } catch (err) {
      console.error('Failed to fetch recordings:', err);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    if (isOpen) {
      fetchRecordings();
    }
  }, [isOpen]);

  const handleOpenFolder = async () => {
    try {
      const fn = backend.getPluginFunction('openRecordingsFolder');
      if (fn) await fn();
    } catch (err) {
      console.error('Error opening folder:', err);
    }
  };

  const handleDelete = async (filePath: string) => {
    try {
      const fn = backend.getPluginFunction('deleteRecordingFile');
      if (fn) {
        await fn(filePath);
        fetchRecordings();
      }
    } catch (err) {
      console.error('Error deleting file:', err);
    }
  };

  const handleStartRename = (file: RecordingFileInfo) => {
    setEditingFile(file.filePath);
    setNewName(file.fileName);
  };

  const handleSaveRename = async (oldPath: string) => {
    if (!newName.trim()) return;
    try {
      const fn = backend.getPluginFunction('renameRecordingFile');
      if (fn) {
        await fn(oldPath, newName);
        setEditingFile(null);
        fetchRecordings();
      }
    } catch (err) {
      console.error('Error renaming file:', err);
    }
  };

  const formatSize = (bytes: number) => {
    if (bytes < 1024 * 1024) {
      return `${(bytes / 1024).toFixed(1)} KB`;
    }
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  };

  const formatDuration = (seconds: number) => {
    if (!seconds || seconds <= 0) return '0s';
    const m = Math.floor(seconds / 60);
    const s = Math.floor(seconds % 60);
    return m > 0 ? `${m}m ${s}s` : `${s}s`;
  };

  if (!isOpen) return null;

  return (
    <div
      style={{
        position: 'fixed',
        top: 0,
        left: 0,
        right: 0,
        bottom: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.75)',
        backdropFilter: 'blur(8px)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        zIndex: 9999,
      }}
    >
      <div
        style={{
          width: '560rem',
          maxHeight: '80vh',
          backgroundColor: '#18181a',
          border: '1rem solid #2c2c2e',
          borderRadius: '16rem',
          display: 'flex',
          flexDirection: 'column',
          boxShadow: '0 20rem 50rem rgba(0,0,0,0.8)',
          overflow: 'hidden',
        }}
      >
        {/* Header */}
        <div
          style={{
            padding: '16rem 20rem',
            borderBottom: '1rem solid #2c2c2e',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: '10rem' }}>
            <span style={{ fontSize: '18rem' }}>🎙️</span>
            <h2 style={{ margin: 0, fontSize: '16rem', fontWeight: 600, color: '#ffffff' }}>
              Recorded Audio Files
            </h2>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: '12rem' }}>
            <button
              onClick={handleOpenFolder}
              style={{
                backgroundColor: '#2c2c2e',
                color: '#34c759',
                border: 'none',
                borderRadius: '8rem',
                padding: '6rem 12rem',
                fontSize: '12rem',
                fontWeight: 600,
                cursor: 'pointer',
              }}
            >
              📁 Open Folder in Explorer
            </button>
            <button
              onClick={onClose}
              style={{
                background: 'none',
                border: 'none',
                color: '#8e8e93',
                fontSize: '20rem',
                cursor: 'pointer',
                lineHeight: 1,
              }}
            >
              ✕
            </button>
          </div>
        </div>

        {/* List Content */}
        <div style={{ padding: '16rem 20rem', overflowY: 'auto', flex: 1 }}>
          {loading ? (
            <div style={{ padding: '30rem', textAlign: 'center', color: '#8e8e93' }}>
              Loading recordings...
            </div>
          ) : recordings.length === 0 ? (
            <div style={{ padding: '40rem 20rem', textAlign: 'center', color: '#8e8e93' }}>
              <p style={{ fontSize: '14rem', margin: 0 }}>No recordings found yet.</p>
              <p style={{ fontSize: '12rem', marginTop: '6rem', color: '#636366' }}>
                Click the red record button in the header bar to capture your output sound.
              </p>
            </div>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '10rem' }}>
              {recordings.map((file) => (
                <div
                  key={file.filePath}
                  style={{
                    backgroundColor: '#222225',
                    border: '1rem solid #2e2e32',
                    borderRadius: '10rem',
                    padding: '12rem 14rem',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                  }}
                >
                  <div style={{ flex: 1, marginRight: '12rem' }}>
                    {editingFile === file.filePath ? (
                      <div style={{ display: 'flex', gap: '8rem' }}>
                        <input
                          type="text"
                          value={newName}
                          onChange={(e) => setNewName(e.target.value)}
                          style={{
                            backgroundColor: '#141416',
                            border: '1rem solid #007aff',
                            color: '#ffffff',
                            borderRadius: '6rem',
                            padding: '4rem 8rem',
                            fontSize: '13rem',
                            flex: 1,
                          }}
                        />
                        <button
                          onClick={() => handleSaveRename(file.filePath)}
                          style={{
                            backgroundColor: '#007aff',
                            color: '#ffffff',
                            border: 'none',
                            borderRadius: '6rem',
                            padding: '4rem 10rem',
                            fontSize: '12rem',
                            cursor: 'pointer',
                          }}
                        >
                          Save
                        </button>
                      </div>
                    ) : (
                      <div>
                        <div
                          style={{
                            fontWeight: 600,
                            color: '#ffffff',
                            fontSize: '14rem',
                            marginBottom: '4rem',
                            wordBreak: 'break-all',
                          }}
                        >
                          {file.fileName}
                        </div>
                        <div style={{ fontSize: '11rem', color: '#8e8e93' }}>
                          Duration: {formatDuration(file.durationSeconds)} • Size:{' '}
                          {formatSize(file.fileSizeBytes)}
                        </div>
                      </div>
                    )}
                  </div>

                  {/* Actions */}
                  <div style={{ display: 'flex', alignItems: 'center', gap: '8rem' }}>
                    <button
                      onClick={() => handleStartRename(file)}
                      style={{
                        background: 'none',
                        border: '1rem solid #3a3a3c',
                        color: '#a1a1a6',
                        borderRadius: '6rem',
                        padding: '4rem 8rem',
                        fontSize: '11rem',
                        cursor: 'pointer',
                      }}
                      title="Rename"
                    >
                      ✏️ Rename
                    </button>
                    <button
                      onClick={() => handleDelete(file.filePath)}
                      style={{
                        background: 'none',
                        border: '1rem solid #5c2020',
                        color: '#ff453a',
                        borderRadius: '6rem',
                        padding: '4rem 8rem',
                        fontSize: '11rem',
                        cursor: 'pointer',
                      }}
                      title="Delete"
                    >
                      🗑️ Delete
                    </button>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
