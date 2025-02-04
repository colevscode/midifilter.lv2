MFD_FILTER(dupcc)

#ifdef MX_TTF

	mflt:dupcc
	TTF_DEFAULTDEF("MIDI CC Duplicator", "MIDI CC Dup")
	, TTF_IPORT(0, "channel", "Filter Channel", 0, 16, 0,
			PORTENUMZ("Any")
			DOC_CHANF)
	, TTF_IPORTTOGGLE(1, "filter_other", "Filter Other CC Messages", 0)
	, TTF_IPORTTOGGLE(2, "passthrough", "Pass Through Original", 1)
	, TTF_IPORT(3, "src_cc",  "Source CC Number", 0, 127, 0,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(4, "dst1_cc", "Destination 1 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(5, "dst1_att", "Destination 1 Attenuation CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(6, "dst2_cc", "Destination 2 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(7, "dst2_att", "Destination 2 Attenuation CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(8, "dst3_cc", "Destination 3 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(9, "dst3_att", "Destination 3 Attenuation CC", -1, 127, -1,
			lv2:portProperty lv2:integer)
	; rdfs:comment "Duplicate MIDI CC messages to up to three different CC numbers with attenuation controlled by CC messages. Set destination CC or attenuation CC to -1 to disable. When passthrough is disabled, the original CC message will not be forwarded."
	.

#elif defined MX_CODE

static void filter_init_dupcc(MidiFilter* self) { 
    self->memI[0] = -1;  // Last source CC value, -1 means no value received yet
    self->memF[0] = 1.0; // Last att value for dst1
    self->memF[1] = 1.0; // Last att value for dst2
    self->memF[2] = 1.0; // Last att value for dst3
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

    // If filter_other is disabled, pass through all non-CC messages immediately
    if (mst != MIDI_CONTROLCHANGE) {
        if (!(*self->cfg[1] > 0)) {  // if not filtering other messages
            forge_midimessage(self, tme, buffer, size);
        }
        return;
    }

    // At this point, we know it's a CC message
    if (size != 3) {
        return;  // Invalid CC message
    }

    const uint8_t cc_num = buffer[1];
    const uint8_t cc_val = buffer[2];
    
    // Check channel match
    const bool channel_matches = (floorf(*self->cfg[0]) == 0) || (chs == chn);
    
    if (channel_matches) {
        // Check for attenuation CC messages
        for (int i = 0; i < 3; i++) {
            int att_cc = floorf(*self->cfg[5 + i*2]);  // att CC numbers at indices 5,7,9
            if (att_cc >= 0 && cc_num == att_cc) {
                // Store the new attenuation value
                self->memF[i] = cc_val / 127.0;
                
                // If we have a stored source value and active destination, send updated value
                uint8_t dst_cc = floorf(*self->cfg[4 + i*2]);
                if (dst_cc >= 0 && self->memI[0] >= 0) {
                    uint8_t buf[3];
                    buf[0] = MIDI_CONTROLCHANGE | chn;
                    buf[1] = dst_cc;
                    float att_val = self->memI[0] * self->memF[i];
                    buf[2] = clamp_midi_value(att_val);
                    forge_midimessage(self, tme, buf, 3);
                }
            }
        }
        
        // Check if this is our source CC
        if (cc_num == floorf(*self->cfg[3])) {
            // Store the source CC value
            self->memI[0] = cc_val;

            // Pass through the original message if enabled
            if (*self->cfg[2] > 0) {  // passthrough
                forge_midimessage(self, tme, buffer, size);
            }

            // Process each destination
            uint8_t buf[3];
            buf[0] = MIDI_CONTROLCHANGE | chn;

            for (int i = 0; i < 3; i++) {
                uint8_t dst_cc = floorf(*self->cfg[4 + i*2]);  // CC numbers at indices 4,6,8
                if (dst_cc >= 0) {
                    buf[1] = dst_cc;
                    float att_val = cc_val * self->memF[i];  // Always use stored attenuation
                    buf[2] = clamp_midi_value(att_val);
                    forge_midimessage(self, tme, buf, 3);
                }
            }
        } else if (!(*self->cfg[1] > 0)) {  // if not filtering other messages
            // Pass through other CC messages
            forge_midimessage(self, tme, buffer, size);
        }
    } else if (!(*self->cfg[1] > 0)) {  // if not filtering other messages
        // Pass through other CC messages on other channels
        forge_midimessage(self, tme, buffer, size);
    }
}

#endif 