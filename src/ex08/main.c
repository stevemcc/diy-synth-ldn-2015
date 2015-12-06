#include "ex08/main.h"

#define NUM_USER_FNS 2

AppState appState = APP_IDLE;
__IO PlaybackState playbackState = PLAYBACK_IDLE;

USBH_HandleTypeDef hUSBHost; /* USB Host handle - DON'T change name*/
USBAppState usbAppState = USBH_USER_FS_INIT;

uint8_t midiReceiveBuffer[MIDI_BUF_SIZE];
uint8_t audioBuffer[AUDIO_BUFFER_SIZE];
__IO DMABufferState bufferState = BUFFER_OFFSET_NONE;
__IO uint32_t canReceive = 0;

static void usbUserProcess(USBH_HandleTypeDef *pHost, uint8_t vId);
static void midiApplication(void);
static void processMidiPackets(void);
static void initAudio(void);
static void initSequencer(void);
static void resumePlayback(void);
static void rewindPlayback(void);
static void pausePlayback(void);
static void stopPlayback(void);
static void playNote(Synth* synth, SeqTrack *track, int8_t note,
		uint32_t tick);
static void trackOscRect(SeqTrack *track, SynthVoice *voice, float freq, uint32_t tick);
static void trackOscWavetable1(SeqTrack *track, SynthVoice *voice, float freq, uint32_t tick);

static void updateAudioBuffer(Synth *synth);
static void updateTempo(uint32_t ticks);
static void changeTrackUserFn(int32_t id);

static Synth synth;
static SeqTrack* tracks[2];
static tinymt32_t rng;

static int8_t notes1[] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int8_t notes2[] = { -1, -1, -1, -1, -1, -1, -1, -1 };

static SeqTrackUserFn userFns[] = { trackOscRect, trackOscWavetable1 };
__IO static int32_t userFnID = 0;

int main(void) {
	HAL_Init();

	led_all_init();
	SystemClock_Config();

	tinymt32_init(&rng, 0xdeadbeef);

	BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_EXTI);

	USBH_Init(&hUSBHost, usbUserProcess, 0);
	USBH_RegisterClass(&hUSBHost, USBH_MIDI_CLASS);
	USBH_Start(&hUSBHost);
	while (1) {
		midiApplication();
		USBH_Process(&hUSBHost);
	}
}

void usbUserProcess(USBH_HandleTypeDef *usbHost, uint8_t eventID) {
	switch (eventID) {
	case HOST_USER_SELECT_CONFIGURATION:
		break;
	case HOST_USER_DISCONNECTION:
		appState = APP_DISCONNECT;
		BSP_LED_Off(LED_GREEN);
		BSP_LED_Off(LED_BLUE);
		break;
	case HOST_USER_CLASS_ACTIVE:
		appState = APP_READY;
		BSP_LED_On(LED_GREEN);
		BSP_LED_Off(LED_BLUE);
		BSP_LED_Off(LED_RED);
		break;
	case HOST_USER_CONNECTION:
		appState = APP_START;
		BSP_LED_On(LED_BLUE);
		break;
	default:
		break;
	}
}

void midiApplication(void) {
	switch (appState) {
	case APP_READY:
		USBH_MIDI_Receive(&hUSBHost, midiReceiveBuffer, MIDI_BUF_SIZE);
		initAudio();
		pausePlayback();
		initSequencer();
		appState = APP_RUNNING;
		break;
	case APP_RUNNING:
		if ((playbackState == PLAYBACK_PLAY)
				|| (playbackState == PLAYBACK_RESUME)) {
			uint32_t tick = HAL_GetTick();
			updateAllTracks(&synth, tracks, 2, tick);
			updateAudioBuffer(&synth);
		}
		if (canReceive) {
			canReceive = 0;
			USBH_MIDI_Receive(&hUSBHost, midiReceiveBuffer, MIDI_BUF_SIZE);
		}
		break;
	case APP_DISCONNECT:
		appState = APP_IDLE;
		playbackState = PLAYBACK_IDLE;
		stopPlayback();
		USBH_MIDI_Stop(&hUSBHost);
		break;
	default:
		break;
	}
}

void processMidiPackets() {
	uint8_t *ptr = midiReceiveBuffer;
	//midi_package_t packet;
	uint16_t numPackets = USBH_MIDI_GetLastReceivedDataSize(&hUSBHost) >> 2;
	if (numPackets != 0) {
		while (numPackets--) {
			uint32_t cin_cable = *ptr++;
			uint32_t type = *ptr++;
			uint32_t subtype = *ptr++;
			uint32_t val = *ptr++;

			if ((type & 0xf0) == 0xb0) {
				switch (subtype) {
				case MIDI_CC_BT_STOP:
					BSP_LED_Off(LED_ORANGE);
					pausePlayback();
					break;
				case MIDI_CC_BT_PLAY:
					BSP_LED_On(LED_ORANGE);
					resumePlayback();
					break;
				case MIDI_CC_BT_REWIND:
					rewindPlayback();
					break;
				case MIDI_CC_BT_LEFT:
					changeTrackUserFn(-1);
					break;
				case MIDI_CC_BT_RIGHT:
					changeTrackUserFn(1);
					break;
				case MIDI_CC_SLIDER1:
					tracks[0]->cutoff = 240.f + ((float)val)/127.0f*4890.0f;
					break;
				case MIDI_CC_SLIDER2:
					tracks[1]->cutoff = 240.0f + ((float)val)/127.0f*4890.0f;
					break;
				case MIDI_CC_KNOB1:
					tracks[0]->resonance = 0.95f * (float)(val/127.0f);
					break;

				case MIDI_CC_KNOB2:
					tracks[1]->resonance = 0.95f * (float)val/127.0f;
					break;

				case MIDI_CC_KNOB3:
					tracks[0]->damping = 0.05+0.9 * (float)val/127.0f;
					tracks[1]->damping = 0.05+0.9 * (float)val/127.0f;
					break;

				default:
					if (subtype >= MIDI_CC_BT_S1 && subtype <= MIDI_CC_BT_S8) {
						int8_t note = tracks[0]->notes[subtype - MIDI_CC_BT_S1];
						tracks[0]->notes[subtype - MIDI_CC_BT_S1] = (
								(note == -1) ?
										(tinymt32_generate_uint32(&rng) % 48) :
										-1);
					}
					if (subtype >= MIDI_CC_BT_M1 && subtype <= MIDI_CC_BT_M8) {
						int8_t note = tracks[1]->notes[subtype - MIDI_CC_BT_M1];
						tracks[1]->notes[subtype - MIDI_CC_BT_M1] = (
								(note == -1) ?
										(tinymt32_generate_uint32(&rng) % 24) :
										-1);
					}
					break;
				}
			}
		}
	}
}

void updateTempo(uint32_t ticks) {
	tracks[1]->ticks = (uint32_t) (tracks[1]->tempoScale * ticks);
	if (tracks[1]->ticks < 150) {
		tracks[1]->ticks = 150;
	}
}

void changeTrackUserFn(int32_t id) {
	userFnID = (userFnID + id) % NUM_USER_FNS;
	if (userFnID < 0) {
		userFnID = NUM_USER_FNS - 1;
	}
	tracks[0]->userFn = userFns[userFnID];
	tracks[1]->userFn = userFns[userFnID];
}

void initAudio(void) {
	if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_AUTO, 85, SAMPLERATE) != 0) {
		Error_Handler();
	}
	synth_init(&synth);
	synth_bus_init(&(synth.bus[0]),
			(int16_t*) malloc(sizeof(int16_t) * DELAY_LENGTH),
			DELAY_LENGTH, 2);
	synth_osc_init(&(synth.lfoEnvMod), synth_osc_sin_dc, 0, 0, 0, 1.0f);
	BSP_AUDIO_OUT_Play((uint16_t*) &audioBuffer[0], AUDIO_BUFFER_SIZE);
}

void initSequencer(void) {
	tracks[0] = initTrack((SeqTrack*) malloc(sizeof(SeqTrack)), playNote,
			notes1, 8, 250, 1.0f);
	tracks[1] = initTrack((SeqTrack*) malloc(sizeof(SeqTrack)), playNote,
			notes2, 8, 250, 2.0f);
	changeTrackUserFn(0);
}

void resumePlayback(void) {
	rewindPlayback();
	playbackState = PLAYBACK_RESUME;
	BSP_AUDIO_OUT_Resume();
}

void rewindPlayback(void) {
	tracks[0]->currNote = 0;
	tracks[1]->currNote = 0;
}

void pausePlayback(void) {
	playbackState = PLAYBACK_PAUSE;
	BSP_AUDIO_OUT_Pause();
}

void stopPlayback(void) {
	playbackState = PLAYBACK_IDLE;
	BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
}

void updateAudioBuffer(Synth *synth) {
	if (bufferState == BUFFER_OFFSET_HALF) {
		int16_t *ptr = (int16_t*) &audioBuffer[0];
		synth_render_slice(synth, ptr, AUDIO_BUFFER_SIZE >> 3);
		bufferState = BUFFER_OFFSET_NONE;
	}

	if (bufferState == BUFFER_OFFSET_FULL) {
		int16_t *ptr = (int16_t*) &audioBuffer[AUDIO_BUFFER_SIZE >> 1];
		synth_render_slice(synth, ptr, AUDIO_BUFFER_SIZE >> 3);
		bufferState = BUFFER_OFFSET_NONE;
	}
}

void playNote(Synth* synth, SeqTrack *track, int8_t note, uint32_t tick) {
	float freq = notes[note] * powf(1.02, (float) track->pitchBend);
	SynthVoice *voice = synth_new_voice(synth);
	(&voice->filter[0])->type=IIR_LP;
	(&voice->filter[1])->type=IIR_LP;
	synth_set_iir_coeff(&voice->filter[0], track->cutoff, track->resonance, track->damping);
	synth_set_iir_coeff(&voice->filter[1], track->cutoff, track->resonance, track->damping);
	synth_adsr_init(&(voice->env), 0.25f, 0.000025f, 0.005f, 1.0f, 0.95f);
	synth_osc_init(&(voice->lfoPitch), synth_osc_sin, FREQ_TO_RAD(5.0f), 0.0f,
			10.0f, 0.0f);
	track->userFn(track, voice, freq, tick);
}

void trackOscRect(SeqTrack *track, SynthVoice *voice, float freq, uint32_t tick) {
	synth_osc_init(&(voice->osc[0]), synth_osc_rect, 0.10f, 0.0f, freq, 0.0f);
	synth_osc_init(&(voice->osc[1]), synth_osc_rect, 0.10f, 0.0f, freq, 0.0f);
}

void trackOscWavetable1(SeqTrack *track, SynthVoice *voice, float freq, uint32_t tick) {
	synth_osc_init(&(voice->osc[0]), synth_osc_wtable_morph, 0.10f, 0.0f, freq, 0.0f);
	synth_osc_set_wavetables(&(voice->osc[0]), wtable_sin_exp2, wtable_sin_pow2);
	synth_osc_init(&(voice->osc[1]), synth_osc_wtable_morph, 0.10f, 0.0f, freq * 0.5f, 0.0f);
	synth_osc_set_wavetables(&(voice->osc[1]), wtable_sin_exp2, wtable_super_saw);
}

void USBH_MIDI_ReceiveCallback(USBH_HandleTypeDef *phost) {
	BSP_LED_Toggle(LED_ORANGE);
	processMidiPackets();
	canReceive = 1;
	//USBH_MIDI_Receive(&hUSBHost, midiReceiveBuffer, MIDI_BUF_SIZE);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void) {
	bufferState = BUFFER_OFFSET_HALF;
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void) {
	bufferState = BUFFER_OFFSET_FULL;
	BSP_AUDIO_OUT_ChangeBuffer((uint16_t*) &audioBuffer[0],
	AUDIO_BUFFER_SIZE >> 1);
}

void BSP_AUDIO_OUT_Error_CallBack(void) {
	BSP_LED_On(LED_RED);
	while (1) {
	}
}
