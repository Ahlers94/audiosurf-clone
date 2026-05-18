// ---------------------------------------------------------------------------
// DC_Audio (AICA Direct CDDA Stream Implementation)
// ---------------------------------------------------------------------------
class DC_AudioImpl final : public AudioInterface
{
public:
    DC_AudioImpl() = default;
    ~DC_AudioImpl() override = default;

    bool init() override     
    { 
        // Spin up the core audio system hardware components
        snd_stream_init(); 
        m_isPlaying = false;
        m_startSector = 0;
        m_endSector = 0;
        return true; 
    }
    
    void shutdown() override 
    { 
        stop();
        snd_stream_shutdown(); 
    }

    bool play(uint8_t songId) override
    {
        // Convert zero-indexed song election directly to physical audio track index bounds.
        // Assume data track resides on Track 1; Audio tracks begin on Track 2.
        uint8_t targetTrack = songId + 2; 
        
        // Grab the physical Table of Contents structure from the GD-ROM disc sub-system
        CDROM_TOC toc;
        if (cdrom_read_toc(&toc, 0) != 0) {
            dbglog(DBG_ERROR, "[DREAMSURF] CDDA Error: Failed to read disc Table of Contents.\n");
            return false;
        }

        // Verify the requested track target is bounded correctly inside the TOC parameters
        uint8_t firstTrack = CDROM_TOC_FIRST(toc);
        uint8_t lastTrack  = CDROM_TOC_LAST(toc);
        if (targetTrack < firstTrack || targetTrack > lastTrack) {
            dbglog(DBG_ERROR, "[DREAMSURF] CDDA Target Track %d out of bounds (%d - %d).\n", 
                   targetTrack, firstTrack, lastTrack);
            return false;
        }

        // Extrapolate raw physical starting and ending LBA sector markers 
        m_startSector = CDROM_TOC_LBA(toc.entry[targetTrack - 1]);
        m_endSector   = CDROM_TOC_LBA(toc.entry[targetTrack]); // Beginning of next track boundary

        // Execute raw low-level CDDA play macro: non-blocking loop commands passed to secondary sub-CPU
        if (cdrom_cdda_play(targetTrack, targetTrack, 0, CDDA_TRACKS) != 0) {
            dbglog(DBG_ERROR, "[DREAMSURF] CDDA Drive Hardware Allocation Refused.\n");
            return false;
        }

        m_isPlaying = true;
        return true;
    }

    void setPaused(bool p) override 
    { 
        if (p && m_isPlaying) {
            cdrom_cdda_pause();
        } else if (!p && m_isPlaying) {
            cdrom_cdda_resume();
        }
    }
    
    void stop() override            
    { 
        if (m_isPlaying) {
            cdrom_spin_down(); // Safely parks the drive laser array
            m_isPlaying = false;
        }
    }

    FP16 getTrackProgress() override
    {
        if (!m_isPlaying) return 0;

        // Query the optical hardware drive status via non-blocking sub-system calls
        int driveStatus = 0;
        int currentTrackLBA = 0;
        
        // Read low-level laser subcode tracking positions straight from the GD-ROM controller
        if (cdrom_get_status(&driveStatus, &currentTrackLBA) != 0) {
            return m_lastProgress; // Fallback smoothly to cached metrics on bus failure timeouts
        }

        // Detect if the disc mechanism has naturally wrapped past track constraints or stopped
        if (driveStatus != CD_STATUS_PLAYING && driveStatus != CD_STATUS_PAUSED) {
            m_isPlaying = false;
            return 0xFFFF; // Signal timeline retirement threshold immediately
        }

        // Safety clamp: Ensure the returned address is bounded correctly within our active frame space
        if (currentTrackLBA < m_startSector) return 0;
        if (currentTrackLBA >= m_endSector)  return 0xFFFF;

        // Compute normalized timeline position relative to physical sector lengths
        uint32_t totalTrackSectors = m_endSector - m_startSector;
        uint32_t sectorsElapsed    = currentTrackLBA - m_startSector;

        if (totalTrackSectors == 0) return 0;

        // Map sector counts flawlessly onto the 0x0000 -> 0xFFFF Fixed-Point range scale
        uint32_t prog = (sectorsElapsed * 65535u) / totalTrackSectors;
        m_lastProgress = static_cast<FP16>(prog & 0xFFFFu);

        return m_lastProgress;
    }

    uint8_t getEnergyLevel() override 
    { 
        // Returns median default if unattached to custom hardware fast-Fourier analysis drivers
        return 128; 
    }

private:
    bool     m_isPlaying   = false;
    uint32_t m_startSector = 0;
    uint32_t m_endSector   = 0;
    FP16     m_lastProgress = 0;
};
