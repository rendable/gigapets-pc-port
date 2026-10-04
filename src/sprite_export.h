// Sprite extractor: exports sprites visible on screen as PNGs (F7/F8).
#pragma once

#include "base.h"

void export_visible_sprite_clusters(const std::string& out_dir, std::set<std::string>& exported_clusters);
void sprite_export_handle_hotkeys();
