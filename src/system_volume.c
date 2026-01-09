/*
Copyright (c) 2025
	Remote Pianobar Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* Enable POSIX functions (popen, pclose, usleep) */
#define _POSIX_C_SOURCE 200809L

#include "system_volume.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* Track which backend is active */
static enum {
	BACKEND_NONE = 0,
	BACKEND_COREAUDIO,
	BACKEND_OSASCRIPT,
	BACKEND_PULSEAUDIO,
	BACKEND_PACTL,
	BACKEND_ALSA
} activeBackend = BACKEND_NONE;

/*
 * ============================================================================
 * macOS Implementation
 * ============================================================================
 */
#ifdef __APPLE__

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

static AudioDeviceID defaultOutputDevice = kAudioObjectUnknown;

/* Get the default output device */
static bool macosGetDefaultDevice(void) {
	AudioObjectPropertyAddress addr = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain
	};
	
	UInt32 size = sizeof(defaultOutputDevice);
	OSStatus status = AudioObjectGetPropertyData(
		kAudioObjectSystemObject, &addr, 0, NULL, &size, &defaultOutputDevice);
	
	return status == noErr && defaultOutputDevice != kAudioObjectUnknown;
}

/* Check if default output device changed and update if needed
 * Returns true if device changed, false otherwise */
static bool macosCheckAndRefreshDevice(void) {
	AudioObjectPropertyAddress addr = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain
	};
	
	AudioDeviceID currentDevice = kAudioObjectUnknown;
	UInt32 size = sizeof(currentDevice);
	OSStatus status = AudioObjectGetPropertyData(
		kAudioObjectSystemObject, &addr, 0, NULL, &size, &currentDevice);
	
	if (status != noErr || currentDevice == kAudioObjectUnknown) {
		return false;
	}
	
	/* Check if device changed */
	if (currentDevice != defaultOutputDevice) {
		AudioDeviceID oldDevice = defaultOutputDevice;
		defaultOutputDevice = currentDevice;
		
		/* Log device change */
		debugPrint(DEBUG_AUDIO, "System volume: Default audio device changed (0x%x -> 0x%x)\n", 
		           (unsigned int)oldDevice, (unsigned int)currentDevice);
		
		return true;
	}
	
	return false;
}

/* CoreAudio: Get volume (0.0-1.0 -> 0-100) */
static int macosCoreaudioGetVolume(void) {
	/* Check if default device changed */
	macosCheckAndRefreshDevice();
	
	if (defaultOutputDevice == kAudioObjectUnknown) {
		return -1;
	}
	
	AudioObjectPropertyAddress addr = {
		kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
		kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMain
	};
	
	Float32 volume = 0.0f;
	UInt32 size = sizeof(volume);
	
	OSStatus status = AudioObjectGetPropertyData(
		defaultOutputDevice, &addr, 0, NULL, &size, &volume);
	
	if (status != noErr) {
		return -1;
	}
	
	return (int)(volume * 100.0f + 0.5f);
}

/* CoreAudio: Set volume (0-100 -> 0.0-1.0) */
static bool macosCoreaudioSetVolume(int percent) {
	/* Check if default device changed */
	macosCheckAndRefreshDevice();
	
	if (defaultOutputDevice == kAudioObjectUnknown) {
		return false;
	}
	
	AudioObjectPropertyAddress addr = {
		kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
		kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMain
	};
	
	Float32 volume = (Float32)percent / 100.0f;
	
	OSStatus status = AudioObjectSetPropertyData(
		defaultOutputDevice, &addr, 0, NULL, sizeof(volume), &volume);
	
	/* If operation failed, device may have changed - try refreshing and retrying once */
	if (status != noErr) {
		if (macosCheckAndRefreshDevice() && defaultOutputDevice != kAudioObjectUnknown) {
			debugPrint(DEBUG_AUDIO, "System volume: Retrying set volume after device change\n");
			status = AudioObjectSetPropertyData(
				defaultOutputDevice, &addr, 0, NULL, sizeof(volume), &volume);
		}
	}
	
	return status == noErr;
}

/* osascript fallback: Get volume */
static int macosOsascriptGetVolume(void) {
	FILE *fp = popen("osascript -e 'output volume of (get volume settings)'", "r");
	if (!fp) {
		return -1;
	}
	
	int volume = -1;
	if (fscanf(fp, "%d", &volume) != 1) {
		volume = -1;
	}
	pclose(fp);
	
	return volume;
}

/* osascript fallback: Set volume */
static bool macosOsascriptSetVolume(int percent) {
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "osascript -e 'set volume output volume %d'", percent);
	return system(cmd) == 0;
}

/* macOS initialization */
static bool macosInit(void) {
	/* Try CoreAudio first */
	if (macosGetDefaultDevice()) {
		int testVol = macosCoreaudioGetVolume();
		if (testVol >= 0) {
			activeBackend = BACKEND_COREAUDIO;
			return true;
		}
	}
	
	/* Fall back to osascript */
	int testVol = macosOsascriptGetVolume();
	if (testVol >= 0) {
		activeBackend = BACKEND_OSASCRIPT;
		return true;
	}
	
	return false;
}

static int macosGetVolume(void) {
	switch (activeBackend) {
		case BACKEND_COREAUDIO:
			return macosCoreaudioGetVolume();
		case BACKEND_OSASCRIPT:
			return macosOsascriptGetVolume();
		default:
			return -1;
	}
}

static bool macosSetVolume(int percent) {
	switch (activeBackend) {
		case BACKEND_COREAUDIO:
			return macosCoreaudioSetVolume(percent);
		case BACKEND_OSASCRIPT:
			return macosOsascriptSetVolume(percent);
		default:
			return false;
	}
}

/*
 * ============================================================================
 * Linux Implementation
 * ============================================================================
 */
#elif defined(__linux__)

#include <alloca.h>
#include <alsa/asoundlib.h>

#ifdef HAVE_PULSEAUDIO
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pthread.h>

static pa_mainloop *paMainloop = NULL;
static pa_context *paContext = NULL;
static int paVolume = -1;
static bool paReady = false;
static pthread_mutex_t paMutex = PTHREAD_MUTEX_INITIALIZER;

/* PulseAudio callback for context state */
static void paContextStateCb(pa_context *c, void *userdata) {
	(void)userdata;
	pa_context_state_t state = pa_context_get_state(c);
	if (state == PA_CONTEXT_READY) {
		paReady = true;
	}
}

/* PulseAudio callback for sink info (get volume) */
static void paSinkInfoCb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	(void)c;
	(void)userdata;
	if (eol > 0 || !i) {
		return;
	}
	/* Convert from PA volume to percentage */
	paVolume = (int)((pa_cvolume_avg(&i->volume) * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
}

/* PulseAudio: Initialize connection (thread-safe) */
static bool pulseaudioInit(void) {
	pthread_mutex_lock(&paMutex);
	
	paMainloop = pa_mainloop_new();
	if (!paMainloop) {
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	pa_mainloop_api *api = pa_mainloop_get_api(paMainloop);
	paContext = pa_context_new(api, "pianobar");
	if (!paContext) {
		pa_mainloop_free(paMainloop);
		paMainloop = NULL;
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	pa_context_set_state_callback(paContext, paContextStateCb, NULL);
	
	if (pa_context_connect(paContext, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		pa_context_unref(paContext);
		pa_mainloop_free(paMainloop);
		paContext = NULL;
		paMainloop = NULL;
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	/* Wait for connection (with timeout) */
	int tries = 0;
	while (!paReady && tries < 50) {
		pa_mainloop_iterate(paMainloop, 0, NULL);
		if (pa_context_get_state(paContext) == PA_CONTEXT_FAILED ||
		    pa_context_get_state(paContext) == PA_CONTEXT_TERMINATED) {
			break;
		}
		usleep(10000); /* 10ms */
		tries++;
	}
	
	if (!paReady) {
		pa_context_disconnect(paContext);
		pa_context_unref(paContext);
		pa_mainloop_free(paMainloop);
		paContext = NULL;
		paMainloop = NULL;
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	pthread_mutex_unlock(&paMutex);
	return true;
}

/* PulseAudio: Get volume (thread-safe)
 * 
 * Lock optimization: Instead of holding paMutex for entire mainloop iteration,
 * we only hold it while accessing PA objects. The operation itself completes
 * asynchronously via callbacks.
 */
static int pulseaudioGetVolume(void) {
	pa_operation *op;
	
	pthread_mutex_lock(&paMutex);
	if (!paContext || !paReady) {
		pthread_mutex_unlock(&paMutex);
		return -1;
	}
	
	paVolume = -1;
	op = pa_context_get_sink_info_by_name(
		paContext, "@DEFAULT_SINK@", paSinkInfoCb, NULL);
	
	if (!op) {
		pthread_mutex_unlock(&paMutex);
		return -1;
	}
	
	/* Iterate mainloop while holding lock, but with shorter iterations
	 * to allow other threads access between iterations */
	int iterations = 0;
	const int MAX_ITERATIONS = 100;  /* ~1 second timeout */
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING && iterations < MAX_ITERATIONS) {
		pthread_mutex_unlock(&paMutex);
		usleep(10000);  /* 10ms - allow other threads to run */
		pthread_mutex_lock(&paMutex);
		
		if (paContext && paReady) {
			pa_mainloop_iterate(paMainloop, 0, NULL);
		} else {
			/* Context became invalid, abort */
			break;
		}
		iterations++;
	}
	
	pa_operation_unref(op);
	int result = paVolume;
	pthread_mutex_unlock(&paMutex);
	
	return result;
}

/* PulseAudio: Set volume (thread-safe)
 * 
 * Lock optimization: Release lock between mainloop iterations to allow
 * other threads (volume polling, concurrent set requests) to run.
 */
static bool pulseaudioSetVolume(int percent) {
	pa_operation *op;
	
	pthread_mutex_lock(&paMutex);
	if (!paContext || !paReady) {
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	pa_cvolume cv;
	pa_cvolume_set(&cv, 2, (pa_volume_t)(percent * PA_VOLUME_NORM / 100));
	
	op = pa_context_set_sink_volume_by_name(
		paContext, "@DEFAULT_SINK@", &cv, NULL, NULL);
	
	if (!op) {
		pthread_mutex_unlock(&paMutex);
		return false;
	}
	
	/* Iterate mainloop while releasing lock between iterations */
	int iterations = 0;
	const int MAX_ITERATIONS = 100;  /* ~1 second timeout */
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING && iterations < MAX_ITERATIONS) {
		pthread_mutex_unlock(&paMutex);
		usleep(10000);  /* 10ms - allow other threads to run */
		pthread_mutex_lock(&paMutex);
		
		if (paContext && paReady) {
			pa_mainloop_iterate(paMainloop, 0, NULL);
		} else {
			/* Context became invalid, abort */
			break;
		}
		iterations++;
	}
	
	pa_operation_unref(op);
	pthread_mutex_unlock(&paMutex);
	
	return true;
}

/* PulseAudio: Cleanup (thread-safe) */
static void pulseaudioDestroy(void) {
	pthread_mutex_lock(&paMutex);
	
	if (paContext) {
		pa_context_disconnect(paContext);
		pa_context_unref(paContext);
		paContext = NULL;
	}
	if (paMainloop) {
		pa_mainloop_free(paMainloop);
		paMainloop = NULL;
	}
	paReady = false;
	
	pthread_mutex_unlock(&paMutex);
}
#endif /* HAVE_PULSEAUDIO */

/* pactl CLI fallback: Get volume */
static int pactlGetVolume(void) {
	FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1 | tr -d '%'", "r");
	if (!fp) {
		return -1;
	}
	
	int volume = -1;
	if (fscanf(fp, "%d", &volume) != 1) {
		volume = -1;
	}
	pclose(fp);
	
	return volume;
}

/* pactl CLI fallback: Set volume */
static bool pactlSetVolume(int percent) {
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null", percent);
	return system(cmd) == 0;
}

/* ALSA native: Get volume using libasound */
static int alsaGetVolume(void) {
	snd_mixer_t *handle;
	snd_mixer_elem_t *elem;
	snd_mixer_selem_id_t *sid;
	long min, max, volume;
	int percent;
	
	if (snd_mixer_open(&handle, 0) < 0)
		return -1;
	if (snd_mixer_attach(handle, "default") < 0)
		goto error;
	if (snd_mixer_selem_register(handle, NULL, NULL) < 0)
		goto error;
	if (snd_mixer_load(handle) < 0)
		goto error;
	
	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, "Master");
	
	elem = snd_mixer_find_selem(handle, sid);
	if (!elem)
		goto error;
	
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &volume);
	
	percent = (int)(((volume - min) * 100) / (max - min));
	
	snd_mixer_close(handle);
	return percent;
	
error:
	snd_mixer_close(handle);
	return -1;
}

/* ALSA native: Set volume using libasound */
static bool alsaSetVolume(int percent) {
	snd_mixer_t *handle;
	snd_mixer_elem_t *elem;
	snd_mixer_selem_id_t *sid;
	long min, max, volume;
	
	if (snd_mixer_open(&handle, 0) < 0)
		return false;
	if (snd_mixer_attach(handle, "default") < 0)
		goto error;
	if (snd_mixer_selem_register(handle, NULL, NULL) < 0)
		goto error;
	if (snd_mixer_load(handle) < 0)
		goto error;
	
	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, "Master");
	
	elem = snd_mixer_find_selem(handle, sid);
	if (!elem)
		goto error;
	
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	volume = min + ((max - min) * percent) / 100;
	snd_mixer_selem_set_playback_volume_all(elem, volume);
	
	snd_mixer_close(handle);
	return true;
	
error:
	snd_mixer_close(handle);
	return false;
}

/* Linux initialization - try backends in order of preference */
static bool linuxInit(void) {
#ifdef HAVE_PULSEAUDIO
	/* Try libpulse first */
	if (pulseaudioInit()) {
		int testVol = pulseaudioGetVolume();
		if (testVol >= 0) {
			activeBackend = BACKEND_PULSEAUDIO;
			return true;
		}
		pulseaudioDestroy();
	}
#endif
	
	/* Try pactl CLI */
	int testVol = pactlGetVolume();
	if (testVol >= 0) {
		activeBackend = BACKEND_PACTL;
		return true;
	}
	
	/* Fall back to ALSA */
	testVol = alsaGetVolume();
	if (testVol >= 0) {
		activeBackend = BACKEND_ALSA;
		return true;
	}
	
	return false;
}

static int linuxGetVolume(void) {
	switch (activeBackend) {
#ifdef HAVE_PULSEAUDIO
		case BACKEND_PULSEAUDIO:
			return pulseaudioGetVolume();
#endif
		case BACKEND_PACTL:
			return pactlGetVolume();
		case BACKEND_ALSA:
			return alsaGetVolume();
		default:
			return -1;
	}
}

static bool linuxSetVolume(int percent) {
	switch (activeBackend) {
#ifdef HAVE_PULSEAUDIO
		case BACKEND_PULSEAUDIO:
			return pulseaudioSetVolume(percent);
#endif
		case BACKEND_PACTL:
			return pactlSetVolume(percent);
		case BACKEND_ALSA:
			return alsaSetVolume(percent);
		default:
			return false;
	}
}

static void linuxDestroy(void) {
#ifdef HAVE_PULSEAUDIO
	if (activeBackend == BACKEND_PULSEAUDIO) {
		pulseaudioDestroy();
	}
#endif
}

#endif /* __linux__ */

/*
 * ============================================================================
 * Public API Implementation
 * ============================================================================
 */

bool BarSystemVolumeInit(void) {
#ifdef __APPLE__
	return macosInit();
#elif defined(__linux__)
	return linuxInit();
#else
	/* Unsupported platform */
	return false;
#endif
}

void BarSystemVolumeDestroy(void) {
#ifdef __linux__
	linuxDestroy();
#endif
	activeBackend = BACKEND_NONE;
}

int BarSystemVolumeGet(void) {
#ifdef __APPLE__
	return macosGetVolume();
#elif defined(__linux__)
	return linuxGetVolume();
#else
	return -1;
#endif
}

bool BarSystemVolumeSet(int percent) {
	/* Clamp to valid range */
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	
#ifdef __APPLE__
	return macosSetVolume(percent);
#elif defined(__linux__)
	return linuxSetVolume(percent);
#else
	return false;
#endif
}

bool BarSystemVolumeAvailable(void) {
	return activeBackend != BACKEND_NONE;
}

bool BarSystemVolumeRefreshDevice(void) {
#ifdef __APPLE__
	/* Only CoreAudio backend needs device refresh */
	if (activeBackend == BACKEND_COREAUDIO) {
		return macosCheckAndRefreshDevice();
	}
	return false;
#else
	/* Linux backends use dynamic device resolution - no refresh needed */
	return false;
#endif
}

