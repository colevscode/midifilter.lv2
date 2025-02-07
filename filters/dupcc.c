MFD_FILTER(dupcc)

#ifdef MX_TTF

	mflt:dupcc
	TTF_DEFAULTDEF("MIDI CC Duplicator", "MIDI CC Dup")
	, TTF_IPORT(0, "channel", "Filter Channel", 0, 16, 0,
			PORTENUMZ("Any")
			DOC_CHANF)
	, TTF_IPORTTOGGLE(1, "boost", "Enable Boost", 0)
	, TTF_IPORT(2, "dst1_cc_offset", "Dest 1 CC Offset", -1, 127, 1,
			lv2:portProperty lv2:integer;
			rdfs:comment "CC offset from source for destination 1 (-1 to disable)")
	, TTF_IPORT(3, "dst1_gain_offset", "Dest 1 Gain CC Offset", -1, 127, 100,
			lv2:portProperty lv2:integer;
			rdfs:comment "CC offset from source for destination 1 gain control (-1 to disable). CC value 63 = unity gain, 127 = 2x gain")
	, TTF_IPORT(4, "dst2_cc_offset", "Dest 2 CC Offset", -1, 127, 2,
			lv2:portProperty lv2:integer;
			rdfs:comment "CC offset from source for destination 2 (-1 to disable)")
	, TTF_IPORT(5, "dst2_gain_offset", "Dest 2 Gain CC Offset", -1, 127, 101,
			lv2:portProperty lv2:integer;
			rdfs:comment "CC offset from source for destination 2 gain control (-1 to disable). CC value 63 = unity gain, 127 = 2x gain")
	, TTF_IPORT(6, "map1_src", "Map 1 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(7, "map2_src", "Map 2 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(8, "map3_src", "Map 3 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(9, "map4_src", "Map 4 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(10, "map5_src", "Map 5 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(11, "map6_src", "Map 6 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(12, "map7_src", "Map 7 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(13, "map8_src", "Map 8 Source CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	; rdfs:comment "Map up to 8 MIDI CC sources to two destinations with CC offsets. For each source CC, destinations are at (source + dst{1,2}_cc_offset) and gain for each destination is controlled by (source + dst{1,2}_gain_offset). Set source CC to -1 to disable mapping. When boost is enabled, gain CCs can boost up to 2x. When disabled, max gain is 1x."
	.

#elif defined MX_CODE

#define DUPCC_MAXPORTS 14  // 1 channel + 1 boost + 4 offsets + 8 source CCs

static void filter_init_dupcc(MidiFilter* self) { 
    for (int i = 0; i < 8; i++) {
        self->memI[i] = -1;        // Last source CC value for this mapping
        self->memF[i*2] = 1.0;     // Gain for dest1
        self->memF[i*2 + 1] = 1.0; // Gain for dest2
    }
}

static inline int clamp_midi_value(float value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return (int)value;
}

static void
filter_midi_dupcc(MidiFilter* self,
        uint32_t tme,
        const uint8_t* const buffer,
        uint32_t size)
{
    const uint8_t chs = midi_limit_chn(floorf(*self->cfg[0]) - 1);
    const uint8_t chn = buffer[0] & 0x0f;
    const uint8_t mst = buffer[0] & 0xf0;

    // Pass through all non-CC messages immediately
    if (mst != MIDI_CONTROLCHANGE) {
        forge_midimessage(self, tme, buffer, size);
        return;
    }

    if (size != 3) return;  // Invalid CC message

    const uint8_t cc_num = buffer[1];
    const uint8_t cc_val = buffer[2];
    
    // Check channel match
    const bool channel_matches = (floorf(*self->cfg[0]) == 0) || (chs == chn);
    
    if (channel_matches) {
        bool handled = false;

        // Get global offsets and settings
        const bool boost_enabled = (*self->cfg[1] > 0);
        const int dst1_cc_offset = (int)floorf(*self->cfg[2]);  // Allow -1
        const int dst1_gain_offset = (int)floorf(*self->cfg[3]); // Allow -1
        const int dst2_cc_offset = (int)floorf(*self->cfg[4]);  // Allow -1
        const int dst2_gain_offset = (int)floorf(*self->cfg[5]); // Allow -1

        // Process each mapping
        for (int map = 0; map < 8; map++) {
            const int src_idx = 6 + map;  // Start at 6 (after channel, boost, and offsets)
            
            if (src_idx >= DUPCC_MAXPORTS) break;
            
            const int src_cc = (int)floorf(*self->cfg[src_idx]);  // Allow -1
            
            if (src_cc < 0) continue;

            // Check for gain CC messages
            for (int dst = 0; dst < 2; dst++) {
                const int gain_idx = map*2 + dst;  // Updated for 2 destinations
                if (gain_idx < 0 || gain_idx >= 16) {  // 8 mappings * 2 destinations = 16 slots
                    continue;
                }
                
                // Only check gain CC if both destination and gain are enabled
                if (dst1_gain_offset >= 0 && dst == 0) {
                    const uint8_t gain_cc = (src_cc + dst1_gain_offset) & 0x7F;
                    if (cc_num == gain_cc) {
                        // Store the new gain value (0-127 -> 0.0-1.0 or 0.0-2.0 with boost)
                        self->memF[gain_idx] = boost_enabled ? (cc_val / 63.0f) : (cc_val / 127.0f);
                        
                        // Debug print
                        fprintf(stderr, "Map %d Dst %d: Storing gain value %.2f\n", 
                                map + 1, dst + 1, self->memF[gain_idx]);
                        
                        // Send updated value if we have a stored source value
                        if (self->memI[map] >= 0) {
                            uint8_t buf[3];
                            buf[0] = MIDI_CONTROLCHANGE | chn;
                            buf[1] = (src_cc + dst1_cc_offset) & 0x7F;
                            float gain_val = self->memI[map] * self->memF[gain_idx];
                            buf[2] = clamp_midi_value(gain_val);
                            forge_midimessage(self, tme, buf, 3);
                        }
                        handled = true;
                    }
                }
                if (dst2_gain_offset >= 0 && dst == 1) {
                    const uint8_t gain_cc = (src_cc + dst2_gain_offset) & 0x7F;
                    if (cc_num == gain_cc) {
                        // Store the new gain value (0-127 -> 0.0-1.0 or 0.0-2.0 with boost)
                        self->memF[gain_idx] = boost_enabled ? (cc_val / 63.0f) : (cc_val / 127.0f);
                        
                        // Debug print
                        fprintf(stderr, "Map %d Dst %d: Storing gain value %.2f\n", 
                                map + 1, dst + 1, self->memF[gain_idx]);
                        
                        // Send updated value if we have a stored source value
                        if (self->memI[map] >= 0) {
                            uint8_t buf[3];
                            buf[0] = MIDI_CONTROLCHANGE | chn;
                            buf[1] = (src_cc + dst2_cc_offset) & 0x7F;
                            float gain_val = self->memI[map] * self->memF[gain_idx];
                            buf[2] = clamp_midi_value(gain_val);
                            forge_midimessage(self, tme, buf, 3);
                        }
                        handled = true;
                    }
                }
            }
            
            // Check if this is our source CC
            if (cc_num == src_cc) {
                handled = true;
                self->memI[map] = cc_val;

                // Always pass through source CC
                forge_midimessage(self, tme, buffer, size);

                // Process each destination
                uint8_t buf[3];
                buf[0] = MIDI_CONTROLCHANGE | chn;

                for (int dst = 0; dst < 2; dst++) {
                    const int dst_offset = (dst == 0) ? dst1_cc_offset : dst2_cc_offset;
                    if (dst_offset < 0) continue;  // Skip disabled destinations
                    
                    const uint8_t dst_cc = (src_cc + dst_offset) & 0x7F;
                    buf[1] = dst_cc;
                    
                    // Verify index is in bounds
                    const int gain_idx = map*2 + dst;
                    if (gain_idx < 0 || gain_idx >= 16) {
                        fprintf(stderr, "Warning: Invalid gain index %d (map=%d, dst=%d)\n", 
                                gain_idx, map, dst);
                        continue;
                    }
                    
                    float gain_val = cc_val * self->memF[gain_idx];
                    // Debug print
                    fprintf(stderr, "Map %d Dst %d: Applying gain %.2f to value %d\n", 
                            map + 1, dst + 1, self->memF[gain_idx], cc_val);
                    buf[2] = clamp_midi_value(gain_val);
                    forge_midimessage(self, tme, buf, 3);
                }
            }
        }

        // Pass through if not a source or gain CC
        if (!handled) {
            forge_midimessage(self, tme, buffer, size);
        }
    } else {
        // Pass through other channel messages
        forge_midimessage(self, tme, buffer, size);
    }
}

#endif 