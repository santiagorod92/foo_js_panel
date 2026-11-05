#pragma once
#include "../fb2k.h"

namespace jsp::ui {

bool copy_tracks(metadb_handle_list_cref tracks);
bool clipboard_has_tracks();
bool clipboard_tracks(metadb_handle_list_ref out);

}
