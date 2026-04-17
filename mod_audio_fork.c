#include "mod_audio_fork.h"
#include "lws_glue.h"

/* Forward declaration for API function to ensure scope visibility */
SWITCH_STANDARD_API(fork_function);

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_audio_fork_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_audio_fork_load);
SWITCH_MODULE_DEFINITION(mod_audio_fork, mod_audio_fork_load, mod_audio_fork_shutdown, NULL);

static void responseHandler(switch_core_session_t* session, const char * eventName, char * json) {
    switch_event_t *event;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    if (json) switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "responseHandler: %s payload: %s.\n", eventName, json);
    switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName);
    switch_channel_event_set_data(channel, event);
    if (json) switch_event_add_body(event, "%s", json);
    switch_event_fire(&event);
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    switch (type) {
        case SWITCH_ABC_TYPE_INIT: break;
        case SWITCH_ABC_TYPE_CLOSE:
            {
                private_t* tech_pvt = (private_t *) user_data;
                fork_session_cleanup(session, tech_pvt->bugname, NULL, 1);
            }
            break;
        case SWITCH_ABC_TYPE_READ:
            return fork_frame(session, bug);
        default: break;
    }
    return SWITCH_TRUE;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, char* host, unsigned int port, char* path, int sampling, int sslFlags, char* bugname, char* metadata)
{
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug;
    switch_status_t status;
    switch_codec_t* read_codec;
    void *pUserData = NULL;
    int channels = (flags & SMBF_STEREO) ? 2 : 1;

    if (switch_channel_get_private(channel, bugname)) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "mod_audio_fork: bug %s already attached!\n", bugname);
        return SWITCH_STATUS_FALSE;
    }

    read_codec = switch_core_session_get_read_codec(session);
    if (SWITCH_STATUS_FALSE == fork_session_init(session, responseHandler, read_codec->implementation->actual_samples_per_second, host, port, path, sampling, sslFlags, channels, bugname, metadata, &pUserData)) {
        return SWITCH_STATUS_FALSE;
    }

    if ((status = switch_core_media_bug_add(session, bugname, NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
        return status;
    }

    switch_channel_set_private(channel, bugname, bug);
    return fork_session_connect(&pUserData);
}

static switch_status_t do_stop(switch_core_session_t *session, char* bugname, char* text)
{
    return fork_session_cleanup(session, bugname, text, 0);
}

static switch_status_t send_text(switch_core_session_t *session, char* bugname, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    /* Cast for C++ compatibility */
    switch_media_bug_t *bug = (switch_media_bug_t *) switch_channel_get_private(channel, bugname);

    if (bug) {
        return fork_session_send_text(session, bugname, text);
    }
    return SWITCH_STATUS_FALSE;
}

/* API implementation */
SWITCH_STANDARD_API(fork_function)
{
    /* ... existing fork_function logic ... (this part is long but you already have it working) */
    /* Ensure start_capture, do_stop, etc., are called within this function */
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_audio_fork_load)
{
    switch_api_interface_t *api_interface;
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

    /* Register events */
    switch_event_reserve_subclass(EVENT_TRANSCRIPTION);
    switch_event_reserve_subclass(EVENT_PLAY_AUDIO);
    /* ... rest of event registrations ... */

    SWITCH_ADD_API(api_interface, "uuid_audio_fork", "audio_fork API", fork_function, FORK_API_SYNTAX);
    
    return fork_init();
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_audio_fork_shutdown)
{
    return fork_cleanup();
}
