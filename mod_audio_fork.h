#ifndef __MOD_FORK_H__
#define __MOD_FORK_H__

#include <switch.h>
#include <libwebsockets.h>
#include <speex/speex_resampler.h>
#include <unistd.h>

#define MY_BUG_NAME "audio_fork"
#define MAX_BUG_LEN (64)
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_METADATA_LEN (8192)

/* API Syntax */
#define FORK_API_SYNTAX "<uuid> [start | stop | send_text | pause | resume | graceful-shutdown ] [wss-url | path] [mono | mixed | stereo] [8000 | 16000 | 24000 | 32000 | 64000] [bugname] [metadata]"

/* Event Definitions */
#define EVENT_TRANSCRIPTION   "mod_audio_fork::transcription"
#define EVENT_TRANSFER        "mod_audio_fork::transfer"
#define EVENT_PLAY_AUDIO      "mod_audio_fork::play_audio"
#define EVENT_KILL_AUDIO      "mod_audio_fork::kill_audio"
#define EVENT_DISCONNECT      "mod_audio_fork::disconnect"
#define EVENT_ERROR           "mod_audio_fork::error"
#define EVENT_CONNECT_SUCCESS "mod_audio_fork::connect"
#define EVENT_CONNECT_FAIL    "mod_audio_fork::connect_failed"
#define EVENT_BUFFER_OVERRUN  "mod_audio_fork::buffer_overrun"
#define EVENT_JSON            "mod_audio_fork::json"

struct playout {
  char *file;
  struct playout* next;
};

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, char* json);

struct private_data {
  switch_mutex_t *mutex;
  char sessionId[MAX_SESSION_ID];
  char bugname[MAX_BUG_LEN+1];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pAudioPipe;
  int ws_state;
  char host[MAX_WS_URL_LEN];
  unsigned int port;
  char path[MAX_PATH_LEN];
  int sampling;
  struct playout* playout;
  int channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int audio_paused:1;
  int graceful_shutdown:1;
  char initialMetadata[MAX_METADATA_LEN];
};

typedef struct private_data private_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Function declarations aligned with lws_glue.cpp definitions */
switch_status_t fork_init(void);
switch_status_t fork_cleanup(void);
switch_status_t fork_session_init(switch_core_session_t *session, responseHandler_t responseHandler, uint32_t samples_per_second, char *host, unsigned int port, char *path, int sampling, int sslFlags, int channels, char *bugname, char *metadata, void **pUserData);
switch_status_t fork_session_cleanup(switch_core_session_t *session, char *bugname, char *text, int stop_media_bug);
switch_status_t fork_session_connect(void **ppUserData);
switch_bool_t fork_frame(switch_core_session_t *session, switch_media_bug_t *bug);
switch_status_t fork_session_pauseresume(switch_core_session_t *session, char *bugname, int pause);
switch_status_t fork_session_graceful_shutdown(switch_core_session_t *session, char *bugname);
switch_status_t fork_session_send_text(switch_core_session_t *session, char *bugname, char *text);

/* URI Parser Helper */
int parse_ws_uri(switch_channel_t *channel, const char* url, char* host, char* path, unsigned int* port, int* sslFlags);

#ifdef __cplusplus
}
#endif

#endif
