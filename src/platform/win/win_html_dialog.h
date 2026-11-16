#pragma once
#include "win_sdk.h"
#include "../../core/js_engine.h"

namespace jsp::win {

bool html_dialog(HWND owner, const HtmlDialog& dialog, const DialogCall& call);

}
