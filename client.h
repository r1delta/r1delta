#pragma once

#include <MinHook.h>
#include "predictionerror.h"
#include "bitbuf.h"
#include "logging.h"

enum class TextMsgPrintType_t {
	HUD_PRINTNOTIFY = 1,
	HUD_PRINTCONSOLE,
	HUD_PRINTTALK,
	HUD_PRINTCENTER
};

void InitClient();