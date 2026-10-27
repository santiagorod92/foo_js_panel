#include "../fb2k.h"

#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef JSP_VERSION
#define JSP_VERSION "0.0.0-dev"
#endif

DECLARE_COMPONENT_VERSION(
    "JS Panel",
    JSP_VERSION,
    "JavaScript panels for foobar2000 v2 on Windows and macOS, on the QuickJS engine,\n"
    "with a Spider Monkey Panel compatible API.\n"
    "https://github.com/santiagorod92/foo_js_panel\n");
