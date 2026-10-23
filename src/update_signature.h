#pragma once
#include "updates.h"
namespace shiftwheel {
UpdateRelease parse_signed_update_manifest(const std::string& text,const std::string& public_key_hex,const std::string& key_id,const std::wstring& minimum_version);
bool update_signature_configured();
}
