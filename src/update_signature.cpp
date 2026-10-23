#include "update_signature.h"
#include "update_trust.h"
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace shiftwheel {
namespace {
[[noreturn]]void invalid(){throw std::runtime_error("Подпись канала обновлений не подтверждена или версия устарела.");}
std::vector<unsigned char> unhex(const std::string& s,size_t bytes){
 if(s.size()!=bytes*2)invalid();std::vector<unsigned char> v;v.reserve(bytes);
 auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;invalid();};
 for(size_t i=0;i<s.size();i+=2)v.push_back(static_cast<unsigned char>((digit(s[i])<<4)|digit(s[i+1])));return v;
}
struct Algorithm{BCRYPT_ALG_HANDLE h=nullptr;~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key{BCRYPT_KEY_HANDLE h=nullptr;~Key(){if(h)BCryptDestroyKey(h);}};
}
bool update_signature_configured(){return sizeof(SHIFTWHEEL_UPDATE_PUBLIC_KEY)>1;}
UpdateRelease parse_signed_update_manifest(const std::string& text,const std::string& public_hex,const std::string& id,const std::wstring& minimum){
 if(text.size()>16384||text.find('\r')!=std::string::npos||id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)invalid();
 const auto key_pos=text.rfind("\nkey_id=");if(key_pos==std::string::npos)invalid();
 const auto payload=text.substr(0,key_pos+1);const auto suffix=text.substr(key_pos+1);const auto prefix="key_id="+id+"\nsignature=";
 if(!suffix.starts_with(prefix)||suffix.size()!=prefix.size()+128+1||suffix.back()!='\n')invalid();
 auto signature=unhex(suffix.substr(prefix.size(),128),64);auto blob=unhex(public_hex,72);BCRYPT_ECCKEY_BLOB header{};memcpy(&header,blob.data(),sizeof(header));
 if(header.dwMagic!=BCRYPT_ECDSA_PUBLIC_P256_MAGIC||header.cbKey!=32)invalid();
 Algorithm alg,sha;Key key;
 if(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)<0||BCryptImportKeyPair(alg.h,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.h,blob.data(),static_cast<ULONG>(blob.size()),0)<0)invalid();
 std::string signed_data="ShiftWheel-update-v1\nkey_id="+id+"\n"+payload;std::array<unsigned char,32> digest{};
 if(BCryptOpenAlgorithmProvider(&sha.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptHash(sha.h,nullptr,0,reinterpret_cast<PUCHAR>(signed_data.data()),static_cast<ULONG>(signed_data.size()),digest.data(),static_cast<ULONG>(digest.size()))<0||BCryptVerifySignature(key.h,nullptr,digest.data(),static_cast<ULONG>(digest.size()),signature.data(),static_cast<ULONG>(signature.size()),0)<0)invalid();
 auto release=parse_update_manifest(payload);if(newer_version(minimum,release.version))invalid();return release;
}
}
