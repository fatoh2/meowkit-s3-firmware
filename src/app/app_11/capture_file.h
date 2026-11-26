#pragma once
#include "../../system/sd_write_file.h"

namespace meowgotchi {
// Arduino File::flush() discards sync errors. Use the VFS directly so a full
// or removed card cannot silently count buffered records as saved.
using CaptureFile=meow_storage::WriteFile;
} // namespace meowgotchi
