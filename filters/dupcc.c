MFD_FILTER(dupcc)

#ifdef MX_TTF

	mflt:dupcc
	TTF_DEFAULTDEF("MIDI CC Duplicator", "MIDI CC Dup")
	, TTF_IPORT( 0, "src_cc",  "Source CC Number", 0, 127, 0,
			lv2:portProperty lv2:integer)
	, TTF_IPORT( 1, "dst1_cc", "Destination 1 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT( 2, "dst1_mul", "Destination 1 Multiplier", 0.0, 10.0, 1.0,)
	, TTF_IPORT( 3, "dst1_off", "Destination 1 Offset", -127.0, 127.0, 0.0,)
	, TTF_IPORT( 4, "dst2_cc", "Destination 2 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT( 5, "dst2_mul", "Destination 2 Multiplier", 0.0, 10.0, 1.0,)
	, TTF_IPORT( 6, "dst2_off", "Destination 2 Offset", -127.0, 127.0, 0.0,)
	, TTF_IPORT( 7, "dst3_cc", "Destination 3 CC Number", -1, 127, -1,
			lv2:portProperty lv2:integer)
	, TTF_IPORT( 8, "dst3_mul", "Destination 3 Multiplier", 0.0, 10.0, 1.0,)
	, TTF_IPORT( 9, "dst3_off", "Destination 3 Offset", -127.0, 127.0, 0.0,)
	; rdfs:comment "Duplicate MIDI CC messages to up to three different CC numbers with value transformation (multiply and offset). Set destination CC to -1 to disable that duplication."
	.

#elif defined MX_CODE

static void filter_init_dupcc(MidiFilter* self) { }

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
    const uint8_t chn = buffer[0] & 0x0f;
    const uint8_t mst = buffer[0] & 0xf0;

    // Pass through the original message
    forge_midimessage(self, tme, buffer, size);

    // Only process CC messages
    if (size != 3 || mst != MIDI_CONTROLCHANGE) {
        return;
    }

    const uint8_t src_cc = buffer[1];
    const uint8_t cc_val = buffer[2];
    
    // Check if this is our source CC
    if (src_cc != floorf(*self->cfg[0])) {
        return;
    }

    // Process each destination
    uint8_t buf[3];
    buf[0] = MIDI_CONTROLCHANGE | chn;
    float transformed_val;

    // Process all destinations
    for (int i = 0; i < 3; i++) {
        uint8_t dst_cc = floorf(*self->cfg[1 + i*3]);  // CC numbers are at indices 1,4,7
        if (dst_cc >= 0) {  // Only process if destination CC is not -1
            buf[1] = dst_cc;
            transformed_val = cc_val * (*self->cfg[2 + i*3]) + (*self->cfg[3 + i*3]);  // mul at 2,5,8; off at 3,6,9
            buf[2] = clamp_midi_value(transformed_val);
            if (buf[1] != src_cc) {
                forge_midimessage(self, tme, buf, 3);
            }
        }
    }
}

#endif 