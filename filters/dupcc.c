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
	, TTF_IPORT(4, "mix_cc", "Mix CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT(5, "mix_algo", "Mix Algorithm", 0, 2, 0,
			lv2:portProperty lv2:integer;
			lv2:portProperty lv2:enumeration;
			lv2:scalePoint [ rdfs:label "Ramp" ; rdf:value 0 ] ;
			lv2:scalePoint [ rdfs:label "Ramp50" ; rdf:value 1 ] ;
			lv2:scalePoint [ rdfs:label "Ramp10" ; rdf:value 2 ] )
	, TTF_IPORT(6, "dst1_cc", "Destination 1 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORTTOGGLE(7, "dst1_flip", "Destination 1 Flip Mix", 0)
	, TTF_IPORT(8, "dst2_cc", "Destination 2 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORTTOGGLE(9, "dst2_flip", "Destination 2 Flip Mix", 0)
	, TTF_IPORT(10, "dst3_cc", "Destination 3 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORTTOGGLE(11, "dst3_flip", "Destination 3 Flip Mix", 0)
	; rdfs:comment "Duplicate MIDI CC messages to up to three different CC numbers with mix control via CC message. Each destination can flip the mix value. Mix algorithms: Ramp (linear 0-1), Ramp50 (linear 0-1 over first 50%), Ramp10 (linear 0-1 over first 10%). Set destination CC to -1 to disable that duplication. When passthrough is disabled, the original CC message will not be forwarded."
	.

#elif defined MX_CODE

static void filter_init_dupcc(MidiFilter* self) { 
    self->memF[0] = 1.0; // Mix value (0-1)
}

static inline int clamp_midi_value(float value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return (int)value;
}

static inline float calculate_mix(float normalized_cc, int algorithm, bool flip) {
    float mix;
    switch(algorithm) {
        case 1:  // Ramp50
            if (flip) {
                mix = normalized_cc > 0.5 ? (1.0 - normalized_cc) * 2.0 : 1.0;
            } else {
                mix = normalized_cc < 0.5 ? normalized_cc * 2.0 : 1.0;
            }
            break;
        case 2:  // Ramp10
            if (flip) {
                mix = normalized_cc > 0.9 ? (1.0 - normalized_cc) * 10.0 : 1.0;
            } else {
                mix = normalized_cc < 0.1 ? normalized_cc * 10.0 : 1.0;
            }
            break;
        default: // Ramp
            mix = flip ? 1.0 - normalized_cc : normalized_cc;
    }
    return mix;
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
    const int mix_algo = floorf(*self->cfg[5]);
    
    // Check channel match
    const bool channel_matches = (floorf(*self->cfg[0]) == 0) || (chs == chn);

    // Check if this is our mix CC
    if (channel_matches && cc_num == floorf(*self->cfg[4]) && floorf(*self->cfg[4]) >= 0) {
        float normalized_cc = cc_val / 127.0;
        self->memF[0] = normalized_cc;  // Store the normalized mix value
        
        // If we have a stored source value, update all destinations
        if (self->memI[0] >= 0) {
            uint8_t buf[3];
            buf[0] = MIDI_CONTROLCHANGE | chn;
            
            for (int i = 0; i < 3; i++) {
                uint8_t dst_cc = floorf(*self->cfg[6 + i*2]);  // CC numbers at indices 6,8,10
                if (dst_cc >= 0) {
                    buf[1] = dst_cc;
                    bool flip = *self->cfg[7 + i*2] > 0;  // flip at indices 7,9,11
                    float mix = calculate_mix(normalized_cc, mix_algo, flip);
                    float val = self->memI[0] * mix;
                    buf[2] = clamp_midi_value(val);
                    forge_midimessage(self, tme, buf, 3);
                }
            }
        }
        return;
    }
    
    // Check if this is our source CC
    if (cc_num == floorf(*self->cfg[3]) && channel_matches) {
        // Store the source CC value
        self->memI[0] = cc_val;

        // Pass through the original message if enabled
        if (*self->cfg[2] > 0) {  // passthrough
            forge_midimessage(self, tme, buffer, size);
        }

        // Process each destination
        uint8_t buf[3];
        buf[0] = MIDI_CONTROLCHANGE | chn;

        float normalized_cc = self->memF[0];  // Use stored mix value
        for (int i = 0; i < 3; i++) {
            uint8_t dst_cc = floorf(*self->cfg[6 + i*2]);  // CC numbers at indices 6,8,10
            if (dst_cc >= 0) {
                buf[1] = dst_cc;
                bool flip = *self->cfg[7 + i*2] > 0;  // flip at indices 7,9,11
                float mix = (floorf(*self->cfg[4]) >= 0) ? 
                    calculate_mix(normalized_cc, mix_algo, flip) : 1.0;  // Use 1.0 if mix CC disabled
                float val = cc_val * mix;
                buf[2] = clamp_midi_value(val);
                forge_midimessage(self, tme, buf, 3);
            }
        }
    } else if (!(*self->cfg[1] > 0)) {  // if not filtering other messages
        // Pass through other CC messages
        forge_midimessage(self, tme, buffer, size);
    }
}

#endif 