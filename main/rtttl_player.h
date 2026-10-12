#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Lightweight, asynchronous RTTTL playback for short UI/game effects. */
esp_err_t rtttl_player_start(void);
/* Cancels playback; the worker remains idle so codec locks are never abandoned. */
void rtttl_player_stop(void);
/* Starts immediately and replaces an older queued/playing effect. */
esp_err_t rtttl_player_play(const char *song);
/* Lets microphone-driven features suppress speaker feedback. */
bool rtttl_player_is_playing(void);

/* Looping playback for background music. A looped song re-queues itself after
   finishing, but any newer play()/play_loop() request (an SFX, for instance)
   permanently supersedes it -- the caller re-arms the loop when idle. */
esp_err_t rtttl_player_play_loop(const char *song);
