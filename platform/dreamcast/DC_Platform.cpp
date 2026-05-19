// =============================================================================
// DC_Platform.cpp
// Dreamcast Platform Abstraction Layer Implementation
// =============================================================================

#include "PAL.h"
#include <dc/cdrom.h>
#include <dc/sound/stream.h>
#include <kos/dbglog.h>

namespace Engine::PAL {

// ---------------------------------------------------------------------------
// DC_Audio Implementation
// ---------------------------------------------------------------------------
class DC_AudioImpl final : public AudioInterface {
public:
    DC_AudioImpl() = default;
    ~DC_AudioImpl() override = default;

    bool init() override {
        snd_stream_init();
        m_isPlaying = false;
        m_startSector = 0;
        m_endSector = 0;
        return true;
    }

    void shutdown() override {
        stop();
        snd_stream_shutdown();
    }

    bool play(uint8_t songId) override {
        uint8_t targetTrack = songId + 2;
        CDROM_TOC toc;
        if (cdrom_read_toc(&toc, 0) != 0) {
            dbglog(DBG_ERROR, "[DREAMSURF] CDDA Error: Failed to read TOC.\n");
            return false;
        }

        uint8_t firstTrack = CDROM_TOC_FIRST(toc);
        uint8_t lastTrack  = CDROM_TOC_LAST(toc);
        if (targetTrack < firstTrack || targetTrack > lastTrack) return false;

        m_startSector = CDROM_TOC_LBA(toc.entry[targetTrack - 1]);
        m_endSector   = CDROM_TOC_LBA(toc.entry[targetTrack]);

        if (cdrom_cdda_play(targetTrack, targetTrack, 0, CDDA_TRACKS) != 0) return false;

        m_isPlaying = true;
        return true;
    }

    void setPaused(bool p) override {
        if (p && m_isPlaying) cdrom_cdda_pause();
        else if (!p && m_isPlaying) cdrom_cdda_resume();
    }

    void stop() override {
        if (m_isPlaying) {
            cdrom_spin_down();
            m_isPlaying = false;
        }
    }

    FP16 getTrackProgress() override {
        if (!m_isPlaying) return 0;
        int driveStatus = 0;
        int currentTrackLBA = 0;

        if (cdrom_get_status(&driveStatus, &currentTrackLBA) != 0) return m_lastProgress;

        if (driveStatus != CD_STATUS_PLAYING && driveStatus != CD_STATUS_PAUSED) {
            m_isPlaying = false;
            return 0xFFFF;
        }

        if (currentTrackLBA < m_startSector) return 0;
        if (currentTrackLBA >= m_endSector) return 0xFFFF;

        uint32_t totalTrackSectors = m_endSector - m_startSector;
        uint32_t sectorsElapsed = currentTrackLBA - m_startSector;

        if (totalTrackSectors == 0) return 0;

        uint32_t prog = (sectorsElapsed * 65535u) / totalTrackSectors;
        m_lastProgress = static_cast<FP16>(prog & 0xFFFFu);
        return m_lastProgress;
    }

    uint8_t getEnergyLevel() override { return 128; }

private:
    bool     m_isPlaying    = false;
    uint32_t m_startSector  = 0;
    uint32_t m_endSector    = 0;
    FP16     m_lastProgress = 0;
};

// ---------------------------------------------------------------------------
// DC_Clock Implementation
// ---------------------------------------------------------------------------
class DC_Clock final : public ClockInterface {
public:
    explicit DC_Clock(AudioInterface* audio) : m_audio(audio) {}
    FP16 getCurrentTime() override { return m_audio->getTrackProgress(); }
private:
    AudioInterface* m_audio;
};

// ---------------------------------------------------------------------------
// FACTORY IMPLEMENTATION
// ---------------------------------------------------------------------------
PlatformBundle createPlatform() {
    PlatformBundle bundle;
    
    // Note: Assuming these concrete classes are also defined in this file
    // or headers available to this translation unit.
    auto* audio = new DC_AudioImpl();
    audio->init();
    
    bundle.audio = audio;
    bundle.clock = new DC_Clock(audio);
    
    // Instantiate your other interfaces (Graphics, Input) here
    // bundle.graphics = new DC_GraphicsImpl();
    // bundle.input = new DC_InputImpl();
    
    return bundle;
}

void destroyPlatform(PlatformBundle& bundle) {
    if (bundle.audio) { bundle.audio->shutdown(); delete bundle.audio; }
    delete bundle.clock;
    // delete bundle.graphics;
    // delete bundle.input;
}

} // namespace Engine::PAL
