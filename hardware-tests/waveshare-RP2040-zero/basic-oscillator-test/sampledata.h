#define NUM_SAMPLES 53

struct SampleData {
    uint8_t index;
    const int16_t* data;
    int frames;
    int size;
    const char* name;
    bool basic_shape;
};

SampleData wavetable[NUM_SAMPLES] = {
    { 0, _init_sine, 1, 256, "_init_sine", false },
    { 1, _ramp, 1, 256, "_ramp", false },
    { 2, _saw, 1, 256, "_saw", false },
    { 3, _square, 1, 256, "_square", false },
    { 4, _tri, 1, 256, "_tri", false },
    { 5, cpx_cyborg, 32, 8192, "cpx_cyborg", false },
    { 6, cpx_freya, 32, 8192, "cpx_freya", false },
    { 7, cpx_growl_I, 32, 8192, "cpx_growl_I", false },
    { 8, cpx_mt_I, 32, 8192, "cpx_mt_I", false },
    { 9, cpx_mt_II, 32, 8192, "cpx_mt_II", false },
    { 10, cpx_mt_III, 32, 8192, "cpx_mt_III", false },
    { 11, cpx_tooth, 32, 8192, "cpx_tooth", false },
    { 12, cpx_turb_I, 32, 8192, "cpx_turb_I", false },
    { 13, cpx_wave_I, 32, 8192, "cpx_wave_I", false },
    { 14, cpx_wave_II, 32, 8192, "cpx_wave_II", false },
    { 15, cpx_wave_III, 64, 16384, "cpx_wave_III", false },
    { 16, dark_bass_I, 32, 8192, "dark_bass_I", false },
    { 17, dark_flute, 32, 8192, "dark_flute", false },
    { 18, dark_fm_I, 32, 8192, "dark_fm_I", false },
    { 19, dark_harm_I, 32, 8192, "dark_harm_I", false },
    { 20, dark_harm_II, 32, 8192, "dark_harm_II", false },
    { 21, dark_magneto, 16, 4096, "dark_magneto", false },
    { 22, dark_pluck, 31, 7936, "dark_pluck", false },
    { 23, dark_robot, 16, 4096, "dark_robot", false },
    { 24, dark_sentinel, 16, 4096, "dark_sentinel", false },
    { 25, dark_tachyon, 32, 8192, "dark_tachyon", false },
    { 26, filter_ramp_LP, 22, 5632, "filter_ramp_LP", false },
    { 27, filter_saw_BP, 32, 8192, "filter_saw_BP", false },
    { 28, filter_saw_HP, 32, 8192, "filter_saw_HP", false },
    { 29, filter_saw_LP, 32, 8192, "filter_saw_LP", false },
    { 30, filter_saw_PK, 32, 8192, "filter_saw_PK", false },
    { 31, filter_saw_PK_II, 32, 8192, "filter_saw_PK_II", false },
    { 32, filter_sq_BP, 32, 8192, "filter_sq_BP", false },
    { 33, filter_sq_HP, 32, 8192, "filter_sq_HP", false },
    { 34, filter_sq_LP, 32, 8192, "filter_sq_LP", false },
    { 35, pwm_pulse_I, 32, 8192, "pwm_pulse_I", false },
    { 36, sync_pulse_R, 150, 38400, "sync_pulse_R", false },
    { 37, sync_saw_R, 154, 39424, "sync_saw_R", false },
    { 38, sync_sine_R, 168, 43008, "sync_sine_R", false },
    { 39, sync_tri_R, 149, 38144, "sync_tri_R", false },
    { 40, vowl_beast_I, 32, 8192, "vowl_beast_I", false },
    { 41, vowl_bent, 32, 8192, "vowl_bent", false },
    { 42, vowl_choir_I, 32, 8192, "vowl_choir_I", false },
    { 43, vowl_choir_II, 32, 8192, "vowl_choir_II", false },
    { 44, vowl_hive, 43, 11008, "vowl_hive", false },
    { 45, vowl_hyena, 32, 8192, "vowl_hyena", false },
    { 46, wrd_artillery, 32, 8192, "wrd_artillery", false },
    { 47, wrd_combine, 32, 8192, "wrd_combine", false },
    { 48, wrd_glassed, 32, 8192, "wrd_glassed", false },
    { 49, wrd_maser, 32, 8192, "wrd_maser", false },
    { 50, wrd_neuron, 32, 8192, "wrd_neuron", false },
    { 51, wrd_poison, 32, 8192, "wrd_poison", false },
    { 52, wrd_wyrm, 32, 8192, "wrd_wyrm", false },
};
