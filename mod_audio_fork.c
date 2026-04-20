/* * mod_audio_fork.c -- Freeswitch module for forking audio to remote server over websockets
 */
#include "mod_audio_fork.h"
#include "lws_glue.h"

/* Forward declaration for API function */
SWITCH_STANDARD_API(fork_function);

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_audio_fork_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_audio_fork_load);

/* Module Definition */
SWITCH_MODULE_DEFINITION(mod_audio_fork, mod_audio_fork_load, mod_audio_fork_shutdown, NULL);

static void responseHandler(switch_core_session_t* session, const char * eventName, char * json) {
    switch_event_t *event;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    
    if (json) switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "responseHandler: %s payload: %s.\n", eventName, json);
    
    if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName) == SWITCH_STATUS_SUCCESS) {
        switch_channel_event_set_data(channel, event);
        if (json) switch_event_add_body(event, "%s", json);
        switch_event_fire(&event);
    }
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    switch (type) {
        case SWITCH_ABC_TYPE_INIT: break;
        case SWITCH_ABC_TYPE_CLOSE:
            {
                private_t* tech_pvt = (private_t *) user_data;
                if (tech_pvt) {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Got CLOSE for bug %s\n", tech_pvt->bugname);
                    fork_session_cleanup(session, tech_pvt->bugname, NULL, 1);
                }
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

    if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "mod_audio_fork: channel must reach pre-answer before start!\n");
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

static switch_status_t do_pauseresume(switch_core_session_t *session, char* bugname, int pause)
{
    return fork_session_pauseresume(session, bugname, pause);
}

static switch_status_t send_text(switch_core_session_t *session, char* bugname, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t *) switch_channel_get_private(channel, bugname);
    if (bug) return fork_session_send_text(session, bugname, text);
    return SWITCH_STATUS_FALSE;
}

SWITCH_STANDARD_API(fork_function)
{
    char *mycmd = NULL;
    char *argv[10] = { 0 };
    int argc = 0;
    switch_status_t status = SWITCH_STATUS_FALSE;
    char *bugname = MY_BUG_NAME;
    switch_core_session_t *lsession = NULL; /* Declared at top to avoid 'goto' errors */

    if (!zstr(cmd) && (mycmd = strdup(cmd))) {
        argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
    }

    if (zstr(cmd) || argc < 2) {
        /* USAGE is defined in your .h file */
        stream->write_function(stream, "-USAGE: %s\n", FORK_API_SYNTAX);
        goto done;
    }

    if (!(lsession = switch_core_session_locate(argv[0]))) {
        stream->write_function(stream, "-ERR Operation Failed: session not found\n");
        goto done;
    }

    /* Command Routing */
    if (!strcasecmp(argv[1], "start") && argc >= 4) {
        char host[MAX_WS_URL_LEN], path[MAX_PATH_LEN];
        unsigned int port;
        int sslFlags, sampling = 8000;
        switch_media_bug_flag_t flags = SMBF_READ_STREAM;
        char *metadata = NULL;

        /* Metadata/Bugname logic */
        if (argc > 6) { 
            bugname = argv[5]; 
            metadata = argv[6]; 
        } else if (argc > 5) { 
            if (argv[5][0] == '{' || argv[5][0] == '[') metadata = argv[5];
            else bugname = argv[5];
        }

        if (!strcmp(argv[3], "mixed")) flags |= SMBF_WRITE_STREAM;
        else if (!strcmp(argv[3], "stereo")) flags |= (SMBF_WRITE_STREAM | SMBF_STEREO);

        sampling = (!strcmp(argv[4], "16k")) ? 16000 : (!strcmp(argv[4], "8k")) ? 8000 : atoi(argv[4]);

        if (parse_ws_uri(switch_core_session_get_channel(lsession), argv[2], host, path, &port, &sslFlags)) {
            status = start_capture(lsession, flags, host, port, path, sampling, sslFlags, bugname, metadata);
        }
    } 
    else if (!strcasecmp(argv[1], "stop")) {
        char *text = NULL;
        if (argc > 3) {
            bugname = argv[2];
            text = argv[3];
        } else if (argc > 2) {
            if (argv[2][0] == '{' || argv[2][0] == '[') text = argv[2];
            else bugname = argv[2];
        }
        status = do_stop(lsession, bugname, text);
    }
    else if (!strcasecmp(argv[1], "send_text")) {
        char *text = (argc > 3) ? argv[3] : argv[2];
        if (argc > 3) bugname = argv[2];
        status = send_text(lsession, bugname, text);
    }
    else if (!strcasecmp(argv[1], "pause")) status = do_pauseresume(lsession, (argc > 2 ? argv[2] : bugname), 1);
    else if (!strcasecmp(argv[1], "resume")) status = do_pauseresume(lsession, (argc > 2 ? argv[2] : bugname), 0);
    else if (!strcasecmp(argv[1], "graceful-shutdown")) status = do_graceful_shutdown(lsession, (argc > 2 ? argv[2] : bugname));

    switch_core_session_rwunlock(lsession);

    if (status == SWITCH_STATUS_SUCCESS) stream->write_function(stream, "+OK Success\n");
    else stream->write_function(stream, "-ERR Operation Failed\n");

done:
    switch_safe_free(mycmd);
    return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_LOAD_FUNCTION(mod_audio_fork_load)
{
    switch_api_interface_t *api_interface;
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

    switch_event_reserve_subclass(EVENT_TRANSCRIPTION);
    switch_event_reserve_subclass(EVENT_PLAY_AUDIO);
    switch_event_reserve_subclass(EVENT_ERROR);

    SWITCH_ADD_API(api_interface, "uuid_audio_fork", "audio_fork API", fork_function, FORK_API_SYNTAX);
    return fork_init();
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_audio_fork_shutdown)
{
    switch_event_free_subclass(EVENT_TRANSCRIPTION);
    switch_event_free_subclass(EVENT_PLAY_AUDIO);
    return fork_cleanup();
}
