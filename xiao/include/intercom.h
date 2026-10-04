#pragma once

#include "esp_http_server.h"

// Push-to-talk intercom over a WebSocket at ws://<xiao>/audio.
//
//   XIAO -> browser: binary frames, 16 kHz 16-bit LE mono PCM from the PDM mic
//   browser -> XIAO: text "talk:1" / "talk:0", then binary frames in the same
//                    format, played on the MAX98357A
//
// Half duplex: the mic is not sent while the homeowner talks or the chime
// plays, so the speaker never feeds back into the homeowner's browser.
// One client at a time - the newest connection takes over.

// Registers /audio on server and starts the mic and speaker tasks. The mic
// and speaker drivers must already be installed (initMic / initSpeaker).
void intercomBegin(httpd_handle_t server, bool micOk, bool speakerOk);

// Use as httpd_config_t.close_fn on the server passed to intercomBegin().
void intercomOnClose(httpd_handle_t hd, int sockfd);

// Plays the doorbell chime from the speaker task (never blocks the caller).
void intercomRequestChime();

bool intercomTalking();
bool intercomClientConnected();
