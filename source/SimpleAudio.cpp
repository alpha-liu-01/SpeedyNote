#include "SimpleAudio.h"
#include <QFile>
#include <QDebug>
#include <QObject>
#include <QTimer>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <dsound.h>
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
#elif defined(__linux__)
    #include <alsa/asoundlib.h>
    #include <QThread>
    #include <QMutex>
    #include <QElapsedTimer>
    #include <memory>
    #include <errno.h>
#elif defined(__APPLE__)
    #include <AudioToolbox/AudioToolbox.h>
    #include <CoreAudio/CoreAudioTypes.h>
    #include <QMutex>
    #include <QElapsedTimer>
    
    // Forward declare callback for macOS AudioQueue
    static void audioQueueOutputCallback(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
#endif

#ifdef __APPLE__
// AudioQueue callback - called when a buffer has finished playing
static void audioQueueOutputCallback(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    // The buffer has finished playing, we can free it
    // Note: AudioQueueReset() already frees buffers, so we need to be careful about double-free
    // We only free if the buffer reference is still valid (not already freed by reset)
    if (inAQ && inBuffer) {
        AudioQueueFreeBuffer(inAQ, inBuffer);
    }
}
#endif

class SimpleAudio::SimpleAudioPrivate
{
public:
    SimpleAudioPrivate() : 
#ifdef __APPLE__
        volume(0.8f),  // macOS AudioQueue needs higher volume
#else
        volume(0.4f),
#endif
        audioData(nullptr), audioSize(0), 
#ifdef __APPLE__
        minimumInterval(10) {  // Very low interval for macOS rapid-fire (can interrupt itself)
#else
        minimumInterval(50) {
#endif
#ifdef _WIN32
        directSound = nullptr;
        primaryBuffer = nullptr;
        soundBuffer = nullptr;
        directSoundInitialized = false;
        memset(&waveFormat, 0, sizeof(WAVEFORMATEX));
        sampleRate = 44100;
        channels = 1;
        bitsPerSample = 16;
#elif defined(__APPLE__)
        audioQueue = nullptr;
        sampleRate = 44100;
        channels = 1;
        bitsPerSample = 16;
        lastPlayTimer.start();
#endif
    }
    ~SimpleAudioPrivate() {
        cleanup();
    }
    
    void cleanup() {
#ifdef _WIN32
        // Clean up DirectSound objects
        if (soundBuffer) {
            soundBuffer->Release();
            soundBuffer = nullptr;
        }
        if (primaryBuffer) {
            primaryBuffer->Release();
            primaryBuffer = nullptr;
        }
        if (directSound) {
            directSound->Release();
            directSound = nullptr;
        }
        directSoundInitialized = false;
        
        if (audioData) {
            delete[] audioData;
            audioData = nullptr;
            audioSize = 0;
        }
#elif defined(__linux__)
        if (audioData) {
            delete[] audioData;
            audioData = nullptr;
            audioSize = 0;
        }
#elif defined(__APPLE__)
        // Clean up AudioQueue - lock mutex to prevent race with play()
        playMutex.lock();
        if (audioQueue) {
            AudioQueueStop(audioQueue, true);     // Stop immediately
            AudioQueueDispose(audioQueue, true);  // Dispose queue and free all resources (including buffers)
            audioQueue = nullptr;
        }
        playMutex.unlock();
        
        if (audioData) {
            delete[] audioData;
            audioData = nullptr;
            audioSize = 0;
        }
#endif
    }
    
#ifdef _WIN32
    bool initializeDirectSound() {
        if (directSoundInitialized) return true;
        
        // Create DirectSound object
        HRESULT hr = DirectSoundCreate8(NULL, &directSound, NULL);
        if (FAILED(hr)) {
            qWarning() << "SimpleAudio: Failed to create DirectSound object, HRESULT:" << QString::number(hr, 16);
            return false;
        }
        
        
        // Set cooperative level - use DSSCL_NORMAL for better compatibility
        HWND hwnd = GetDesktopWindow();
        hr = directSound->SetCooperativeLevel(hwnd, DSSCL_NORMAL);
        if (FAILED(hr)) {
            qWarning() << "SimpleAudio: Failed to set DirectSound cooperative level, HRESULT:" << QString::number(hr, 16);
            // Try with DSSCL_PRIORITY as fallback
            hr = directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
            if (FAILED(hr)) {
                qWarning() << "SimpleAudio: Failed to set DirectSound cooperative level (priority), HRESULT:" << QString::number(hr, 16);
                directSound->Release();
                directSound = nullptr;
                return false;
            }
        }
        
        
        // Create primary buffer to ensure proper setup
        DSBUFFERDESC primaryDesc;
        memset(&primaryDesc, 0, sizeof(DSBUFFERDESC));
        primaryDesc.dwSize = sizeof(DSBUFFERDESC);
        primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
        primaryDesc.dwBufferBytes = 0;
        primaryDesc.lpwfxFormat = NULL;
        
        hr = directSound->CreateSoundBuffer(&primaryDesc, &primaryBuffer, NULL);
        // Continue regardless of primary buffer creation result
        directSoundInitialized = true;
        return true;
    }
    
    bool createSoundBuffer() {
        if (!directSoundInitialized || !audioData) return false;
        
        // Release existing buffer
        if (soundBuffer) {
            soundBuffer->Release();
            soundBuffer = nullptr;
        }
        
        // Set up wave format using actual WAV file format
        waveFormat.wFormatTag = WAVE_FORMAT_PCM;
        waveFormat.nChannels = channels;
        waveFormat.nSamplesPerSec = sampleRate;
        waveFormat.wBitsPerSample = bitsPerSample;
        waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
        waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
        waveFormat.cbSize = 0;
        
        
        // Create buffer description
        DSBUFFERDESC bufferDesc;
        memset(&bufferDesc, 0, sizeof(DSBUFFERDESC));
        bufferDesc.dwSize = sizeof(DSBUFFERDESC);
        bufferDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
        bufferDesc.dwBufferBytes = audioSize;
        bufferDesc.lpwfxFormat = &waveFormat;
        
        
        // Create the sound buffer
        HRESULT hr = directSound->CreateSoundBuffer(&bufferDesc, &soundBuffer, NULL);
        if (FAILED(hr)) {
            qWarning() << "SimpleAudio: Failed to create DirectSound buffer, HRESULT:" << QString::number(hr, 16);
            return false;
        }
        
        // Lock the buffer and copy audio data
        LPVOID audioPtr1, audioPtr2;
        DWORD audioBytes1, audioBytes2;
        
        hr = soundBuffer->Lock(0, audioSize, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, DSBLOCK_ENTIREBUFFER);
        if (FAILED(hr)) {
            qWarning() << "SimpleAudio: Failed to lock DirectSound buffer, HRESULT:" << QString::number(hr, 16);
            return false;
        }
        
        // Copy audio data
        memcpy(audioPtr1, audioData, audioBytes1);
        if (audioPtr2 && audioBytes2 > 0) {
            memcpy(audioPtr2, (char*)audioData + audioBytes1, audioBytes2);
        }
        
        // Unlock the buffer
        hr = soundBuffer->Unlock(audioPtr1, audioBytes1, audioPtr2, audioBytes2);
        if (FAILED(hr)) {
            qWarning() << "SimpleAudio: Failed to unlock DirectSound buffer";
            return false;
        }
        
        return true;
    }
#endif
    
    float volume;
    char* audioData;
    size_t audioSize;
    int minimumInterval; // Minimum interval between sounds in milliseconds
    
#ifdef __linux__
    QMutex playMutex; // Prevent concurrent playback on Linux
    QElapsedTimer lastPlayTimer; // Track last play time for rate limiting
    qint64 lastMutexLockTime = 0; // Track when mutex was locked for deadlock detection
#endif
    
#ifdef _WIN32
    // DirectSound objects
    LPDIRECTSOUND8 directSound;
    LPDIRECTSOUNDBUFFER primaryBuffer;
    LPDIRECTSOUNDBUFFER soundBuffer;
    WAVEFORMATEX waveFormat;
    bool directSoundInitialized;
    
    // WAV format info (needed for Windows too)
    int sampleRate;
    int channels;
    int bitsPerSample;
#endif
    
#ifdef __linux__
    // WAV format info
    int sampleRate;
    int channels;
    int bitsPerSample;
    size_t dataOffset;
#endif

#ifdef __APPLE__
    // AudioQueue objects for macOS
    AudioQueueRef audioQueue;
    QMutex playMutex; // Prevent concurrent playback
    QElapsedTimer lastPlayTimer; // Track last play time for rate limiting
    
    // WAV format info
    int sampleRate;
    int channels;
    int bitsPerSample;
#endif
};

SimpleAudio::SimpleAudio() : d(new SimpleAudioPrivate())
{
#if defined(__linux__) || defined(__APPLE__)
    d->lastPlayTimer.start();
#endif
}

SimpleAudio::~SimpleAudio()
{
    delete d;
}

bool SimpleAudio::loadWavFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "SimpleAudio: Could not open file:" << filePath;
        return false;
    }
    
    // Read the entire file
    QByteArray fileData = file.readAll();
    file.close();
    
    if (fileData.size() < 44) {
        qWarning() << "SimpleAudio: File too small to be a valid WAV file";
        return false;
    }
    
    // Basic WAV header validation
    if (fileData.left(4) != "RIFF" || fileData.mid(8, 4) != "WAVE") {
        qWarning() << "SimpleAudio: Not a valid WAV file";
        return false;
    }
    
    d->cleanup();
    
#ifdef _WIN32
    // For DirectSound, we need to parse the WAV file format and extract PCM data
    const char* data = fileData.constData();
    
    // First, find the "fmt " chunk to get format information
    size_t pos = 12;
    bool formatFound = false;
    while (pos < fileData.size() - 8 && !formatFound) {
        if (memcmp(data + pos, "fmt ", 4) == 0) {
            uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            if (chunkSize >= 16) {
                uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(data + pos + 8);
                if (audioFormat != 1) { // PCM
                    qWarning() << "SimpleAudio: Only PCM format supported, got format:" << audioFormat;
                    return false;
                }
                
                d->channels = *reinterpret_cast<const uint16_t*>(data + pos + 10);
                d->sampleRate = *reinterpret_cast<const uint32_t*>(data + pos + 12);
                d->bitsPerSample = *reinterpret_cast<const uint16_t*>(data + pos + 22);
                
                if (d->bitsPerSample != 16) {
                    qWarning() << "SimpleAudio: Only 16-bit samples supported, got:" << d->bitsPerSample;
                    return false;
                }
                formatFound = true;
            }
        }
        if (!formatFound) {
            pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
        }
    }
    
    if (!formatFound) {
        // qWarning() << "SimpleAudio: Could not find fmt chunk in WAV file";
        return false;
    }
    
    // Now find the "data" chunk to get the actual PCM audio data
    pos = 12;
    while (pos < fileData.size() - 8) {
        if (memcmp(data + pos, "data", 4) == 0) {
            uint32_t dataSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            size_t dataOffset = pos + 8;
            
            // Store only the PCM data (not the WAV header)
            d->audioSize = dataSize;
            d->audioData = new char[d->audioSize];
            memcpy(d->audioData, data + dataOffset, d->audioSize);
            
            // Initialize DirectSound and create buffer
            if (!d->initializeDirectSound()) {
                return false;
            }
            
            if (!d->createSoundBuffer()) {
                return false;
            }
            
            // Apply the current volume setting to the new buffer
            // This ensures volume is set immediately after buffer creation
            float currentVolume = d->volume;
            d->volume = -1.0f; // Force volume update
            setVolume(currentVolume);
            
            return true;
        }
        pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
    }
    
    // qWarning() << "SimpleAudio: Could not find data chunk in WAV file";
    return false;
    
#elif defined(__linux__)
    // Parse WAV header for Linux ALSA playback
    const char* data = fileData.constData();
    
    // Find the "fmt " chunk
    size_t pos = 12;
    while (pos < static_cast<size_t>(fileData.size()) - 8) {
        if (memcmp(data + pos, "fmt ", 4) == 0) {
            uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            if (chunkSize >= 16) {
                uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(data + pos + 8);
                if (audioFormat != 1) { // PCM
                    qWarning() << "SimpleAudio: Only PCM format supported";
                    return false;
                }
                
                d->channels = *reinterpret_cast<const uint16_t*>(data + pos + 10);
                d->sampleRate = *reinterpret_cast<const uint32_t*>(data + pos + 12);
                d->bitsPerSample = *reinterpret_cast<const uint16_t*>(data + pos + 22);
                
                if (d->bitsPerSample != 16) {
                    qWarning() << "SimpleAudio: Only 16-bit samples supported";
                    return false;
                }
                break;
            }
        }
        pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
    }
    
    // Find the "data" chunk
    pos = 12;
    while (pos < static_cast<size_t>(fileData.size()) - 8) {
        if (memcmp(data + pos, "data", 4) == 0) {
            uint32_t dataSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            d->dataOffset = pos + 8;
            d->audioSize = dataSize;
            d->audioData = new char[d->audioSize];
            memcpy(d->audioData, data + d->dataOffset, d->audioSize);
            return true;
        }
        pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
    }
    
    qWarning() << "SimpleAudio: Could not find data chunk in WAV file";
    return false;
    
#elif defined(__APPLE__)
    // Parse WAV header for macOS AudioQueue playback
    const char* data = fileData.constData();
    
    // Find the "fmt " chunk
    size_t pos = 12;
    while (pos < fileData.size() - 8) {
        if (memcmp(data + pos, "fmt ", 4) == 0) {
            uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            if (chunkSize >= 16) {
                uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(data + pos + 8);
                if (audioFormat != 1) { // PCM
                    qWarning() << "SimpleAudio: Only PCM format supported";
                    return false;
                }
                
                d->channels = *reinterpret_cast<const uint16_t*>(data + pos + 10);
                d->sampleRate = *reinterpret_cast<const uint32_t*>(data + pos + 12);
                d->bitsPerSample = *reinterpret_cast<const uint16_t*>(data + pos + 22);
                
                if (d->bitsPerSample != 16) {
                    qWarning() << "SimpleAudio: Only 16-bit samples supported";
                    return false;
                }
                break;
            }
        }
        pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
    }
    
    // Find the "data" chunk
    pos = 12;
    while (pos < fileData.size() - 8) {
        if (memcmp(data + pos, "data", 4) == 0) {
            uint32_t dataSize = *reinterpret_cast<const uint32_t*>(data + pos + 4);
            size_t dataOffset = pos + 8;
            d->audioSize = dataSize;
            d->audioData = new char[d->audioSize];
            memcpy(d->audioData, data + dataOffset, d->audioSize);
            
            // Initialize AudioQueue
            AudioStreamBasicDescription audioFormat;
            memset(&audioFormat, 0, sizeof(audioFormat));
            audioFormat.mSampleRate = d->sampleRate;
            audioFormat.mFormatID = kAudioFormatLinearPCM;
            audioFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
            audioFormat.mBytesPerPacket = d->channels * (d->bitsPerSample / 8);
            audioFormat.mFramesPerPacket = 1;
            audioFormat.mBytesPerFrame = d->channels * (d->bitsPerSample / 8);
            audioFormat.mChannelsPerFrame = d->channels;
            audioFormat.mBitsPerChannel = d->bitsPerSample;
            audioFormat.mReserved = 0;
            
            // Create AudioQueue with a callback function
            // Use NULL for run loop to let AudioQueue use its own dedicated thread (faster, no UI blocking)
            OSStatus status = AudioQueueNewOutput(
                &audioFormat, 
                audioQueueOutputCallback,  // Callback function for buffer cleanup
                nullptr,                   // No user data needed (callback just frees buffer)
                NULL,                      // NULL = AudioQueue uses its own high-priority thread
                NULL,                      // NULL run loop mode (not needed with NULL run loop)
                0,                         // Reserved flags
                &d->audioQueue
            );
            
            if (status != noErr) {
                qWarning() << "SimpleAudio: Failed to create AudioQueue, error:" << status;
                return false;
            }
            
            // Prime the queue for lower latency
            AudioQueuePrime(d->audioQueue, 0, NULL);
            
            return true;
        }
        pos += 8 + *reinterpret_cast<const uint32_t*>(data + pos + 4);
    }
    
    qWarning() << "SimpleAudio: Could not find data chunk in WAV file";
    return false;
    
#else
    qWarning() << "SimpleAudio: Platform not supported";
    return false;
#endif
}

void SimpleAudio::play()
{
    if (!d->audioData || d->audioSize == 0) {
        return;
    }
    
#ifdef _WIN32
    // DirectSound can handle rapid playback much better than PlaySound
    static DWORD lastPlayTime = 0;
    DWORD currentTime = GetTickCount();
    
    // Apply minimum interval rate limiting
    if (currentTime - lastPlayTime < static_cast<DWORD>(d->minimumInterval)) {
        return;
    }
    lastPlayTime = currentTime;
    
    if (!d->soundBuffer) {
        return;
    }
    
    // Check if the buffer is currently playing and stop it
    DWORD status;
    HRESULT hr = d->soundBuffer->GetStatus(&status);
    if (SUCCEEDED(hr) && (status & DSBSTATUS_PLAYING)) {
        d->soundBuffer->Stop();
    }
    
    // Reset the play position to the beginning
    hr = d->soundBuffer->SetCurrentPosition(0);
    if (FAILED(hr)) {
        return;
    }
    
    // Start playing
    d->soundBuffer->Play(0, 0, 0); // Play once, no looping
    
#elif defined(__linux__)
    // Apply rate limiting on Linux
    if (d->lastPlayTimer.isValid() && d->lastPlayTimer.elapsed() < d->minimumInterval) {
        return; // Too soon, skip this play
    }
    
    // Check if mutex has been locked for too long (> 3 seconds = likely deadlocked thread)
    // Need enough time for device recovery after volume changes (up to ~1.5s of retries + playback)
    if (d->lastMutexLockTime > 0 && (d->lastPlayTimer.elapsed() - d->lastMutexLockTime) > 3000) {
        // qWarning() << "SimpleAudio: Detected stale mutex lock (thread may have crashed), skipping until resolved";
        return; // Don't force unlock - it's not safe. Just skip until the thread completes.
    }
    
    // Try to lock the mutex (non-blocking)
    if (!d->playMutex.tryLock()) {
        return; // Another sound is already playing, skip
    }
    
    d->lastPlayTimer.restart();
    d->lastMutexLockTime = d->lastPlayTimer.elapsed();
    
    // Capture 'd' by value to ensure thread safety
    SimpleAudioPrivate* dPtr = d;
    
    // Use ALSA for Linux playback in a separate thread
    QThread* thread = QThread::create([dPtr]() {
        snd_pcm_t* pcm_handle = nullptr;
        int err = -1;
        
        // Try to open the PCM device with retry and progressive backoff
        // This handles device switching, volume changes, and temporary unavailability
        // Volume adjustments can cause PulseAudio/PipeWire to temporarily lock the device
        int retries = 8;
        for (int i = 0; i < retries; i++) {
            // Try opening the device
            err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
            if (err >= 0 && pcm_handle) {
                break; // Success
            }
            
            // Log errors for debugging (only on first few attempts to avoid spam)
            if (i < 3) {
                if (err == -ENOENT) {
                    // qDebug() << "SimpleAudio: ALSA device not found (attempt" << i+1 << ")";
                } else if (err == -EBUSY) {
                    // qDebug() << "SimpleAudio: ALSA device busy (attempt" << i+1 << ")";
                } else if (err == -EAGAIN) {
                    // qDebug() << "SimpleAudio: ALSA device temporarily unavailable (attempt" << i+1 << ")";
                } else {
                    // qDebug() << "SimpleAudio: Failed to open device:" << snd_strerror(err) << "(attempt" << i+1 << ")";
                }
            }
            
            if (i < retries - 1) {
                // Progressive backoff with longer delays for volume adjustment recovery
                // 30ms, 50ms, 70ms, 90ms, 110ms, 130ms, 150ms
                QThread::msleep(30 + (i * 20));
            }
        }
        
        if (err < 0 || !pcm_handle) {
            qWarning() << "SimpleAudio: Failed to open ALSA device after" << retries << "retries:" << snd_strerror(err);
            dPtr->lastMutexLockTime = 0; // Reset lock time
            dPtr->playMutex.unlock();
            return;
        }
        
        // Set parameters with smaller buffer size for lower latency
        snd_pcm_hw_params_t* hw_params;
        snd_pcm_hw_params_malloc(&hw_params);
        snd_pcm_hw_params_any(pcm_handle, hw_params);
        snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(pcm_handle, hw_params, dPtr->channels);
        
        unsigned int rate = dPtr->sampleRate;
        snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);
        
        // Set smaller buffer size for lower latency
        snd_pcm_uframes_t buffer_size = 1024;
        snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hw_params, &buffer_size);
        
        err = snd_pcm_hw_params(pcm_handle, hw_params);
        if (err < 0) {
            qWarning() << "SimpleAudio: Failed to set ALSA hw params:" << snd_strerror(err);
            snd_pcm_hw_params_free(hw_params);
            if (pcm_handle) {
                snd_pcm_close(pcm_handle);
            }
            dPtr->lastMutexLockTime = 0; // Reset lock time
            dPtr->playMutex.unlock();
            return;
        }
        
        snd_pcm_hw_params_free(hw_params);
        
        err = snd_pcm_prepare(pcm_handle);
        if (err < 0) {
            qWarning() << "SimpleAudio: Failed to prepare ALSA device:" << snd_strerror(err);
            if (pcm_handle) {
                snd_pcm_close(pcm_handle);
            }
            dPtr->lastMutexLockTime = 0; // Reset lock time
            dPtr->playMutex.unlock();
            return;
        }
        
        // Play the audio data
        snd_pcm_sframes_t frames = dPtr->audioSize / (dPtr->channels * 2); // 16-bit = 2 bytes per sample
        snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle, dPtr->audioData, frames);
        if (written < 0) {
            // Try to recover from various error conditions
            if (written == -EPIPE || written == -ESTRPIPE || written == -ENODEV || written == -EAGAIN) {
                if (written == -ENODEV) {
                    // qDebug() << "SimpleAudio: Device disconnected during playback, attempting recovery";
                } else if (written == -EAGAIN) {
                    // qDebug() << "SimpleAudio: Device temporarily unavailable, attempting recovery";
                }
                
                // Wait a moment if device is busy, then prepare and retry
                if (written == -EAGAIN) {
                    QThread::msleep(10);
                }
                
                snd_pcm_prepare(pcm_handle);
                written = snd_pcm_writei(pcm_handle, dPtr->audioData, frames);
            }
            if (written < 0) {
                qWarning() << "SimpleAudio: Failed to write audio data after recovery:" << snd_strerror(written);
            }
        }
        
        // Clean up ALSA device - always close, even on error
        if (pcm_handle) {
            // Use drop instead of drain for faster completion
            snd_pcm_drop(pcm_handle);
            snd_pcm_close(pcm_handle);
            pcm_handle = nullptr;
        }
        
        // Always unlock the mutex and reset lock time, even on error
        dPtr->lastMutexLockTime = 0;
        dPtr->playMutex.unlock();
    });
    
    // Make thread clean up itself when finished (prevents memory leak)
    QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
    
#elif defined(__APPLE__)
    // Apply rate limiting on macOS (prevent excessive calls)
    if (d->lastPlayTimer.isValid() && d->lastPlayTimer.elapsed() < d->minimumInterval) {
        return; // Too soon, skip this play
    }
    
    d->lastPlayTimer.restart();
    
    if (!d->audioQueue) {
        return;
    }
    
    // Lock mutex for thread-safe AudioQueue access
    // Use blocking lock briefly - we want to allow rapid-fire clicks
    d->playMutex.lock();
    
    // Stop any currently playing audio IMMEDIATELY (like Windows DirectSound)
    // This allows us to interrupt and restart, enabling rapid-fire clicks
    AudioQueueStop(d->audioQueue, true);  // true = stop immediately
    
    // AudioQueueReset() releases all enqueued buffers WITHOUT calling callbacks
    // This prevents double-free since callbacks won't fire for interrupted buffers
    AudioQueueReset(d->audioQueue);
    
    // Set volume before playback
    AudioQueueSetParameter(d->audioQueue, kAudioQueueParam_Volume, d->volume);
    
    // Allocate a new buffer for this playback
    AudioQueueBufferRef buffer = nullptr;
    OSStatus status = AudioQueueAllocateBuffer(d->audioQueue, d->audioSize, &buffer);
    if (status != noErr) {
        qWarning() << "SimpleAudio: Failed to allocate AudioQueue buffer, error:" << status;
        d->playMutex.unlock();
        return;
    }
    
    // Copy audio data to buffer
    memcpy(buffer->mAudioData, d->audioData, d->audioSize);
    buffer->mAudioDataByteSize = d->audioSize;
    
    // Enqueue the buffer - the callback will be called when playback completes normally
    status = AudioQueueEnqueueBuffer(d->audioQueue, buffer, 0, nullptr);
    if (status != noErr) {
        qWarning() << "SimpleAudio: Failed to enqueue AudioQueue buffer, error:" << status;
        AudioQueueFreeBuffer(d->audioQueue, buffer);
        d->playMutex.unlock();
        return;
    }
    
    // Start playback IMMEDIATELY
    // Use NULL to start immediately without waiting for any specific time
    status = AudioQueueStart(d->audioQueue, NULL);
    if (status != noErr) {
        qWarning() << "SimpleAudio: Failed to start AudioQueue, error:" << status;
        // CRITICAL: If start fails, stop and reset to ensure callback is not called
        // This prevents the enqueued buffer from leaking
        AudioQueueStop(d->audioQueue, true);
        AudioQueueReset(d->audioQueue);  // This will free the enqueued buffer
        d->playMutex.unlock();
        return;
    }
    
    // Unlock immediately - playback has started successfully
    // The callback will handle buffer cleanup when playback completes
    d->playMutex.unlock();
    
    // Memory safety notes:
    // 1. AudioQueueReset() frees buffers WITHOUT calling callbacks (no double-free)
    // 2. Callbacks only fire for buffers that complete normally
    // 3. If AudioQueueStart() fails, we reset to free the enqueued buffer (no leak)
    // 4. Mutex protects against race conditions during stop/reset/start sequence
#endif
}

void SimpleAudio::setVolume(float volume)
{
    d->volume = qBound(0.0f, volume, 1.0f);
    
#ifdef _WIN32
    // Apply volume to DirectSound buffer if available
    if (d->soundBuffer) {
        // DirectSound volume is in hundredths of decibels (centibels)
        // Range: DSBVOLUME_MIN (-10000) to DSBVOLUME_MAX (0)
        // 0 = full volume, -10000 = silence
        LONG dsVolume;
        if (d->volume <= 0.0f) {
            dsVolume = DSBVOLUME_MIN; // Silence
        } else {
            // Just use full volume (0) and let the system volume control it
            // This eliminates the complexity of DirectSound volume conversion
            dsVolume = DSBVOLUME_MAX; // Always full volume (0 decibels)
        }
        
        d->soundBuffer->SetVolume(dsVolume);
    }
#elif defined(__APPLE__)
    // Apply volume to AudioQueue if available
    if (d->audioQueue) {
        AudioQueueSetParameter(d->audioQueue, kAudioQueueParam_Volume, d->volume);
    }
#endif
}

void SimpleAudio::setMinimumInterval(int milliseconds)
{
    d->minimumInterval = qBound(10, milliseconds, 1000); // Between 10ms and 1000ms
}

bool SimpleAudio::isAudioAvailable()
{
#ifdef _WIN32
    return true; // Windows always has PlaySound
#elif defined(__linux__)
    // Check if ALSA is available
    snd_pcm_t* pcm_handle;
    int err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err >= 0) {
        snd_pcm_close(pcm_handle);
        return true;
    }
    return false; // Fallback to system beep will still work
#elif defined(__APPLE__)
    return true; // macOS always has CoreAudio
#else
    return false;
#endif
}
