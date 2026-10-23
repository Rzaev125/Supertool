#include "update_signature.h"
#include <bcrypt.h>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace shiftwheel;
void check(bool b){if(!b)throw std::runtime_error("signature contract");}
std::string hex(const std::vector<unsigned char>& v){std::string s;for(auto c:v){s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;}
template<class F>void reject(F f){bool b=false;try{f();}catch(...){b=true;}check(b);}
int main(int argc,char** argv){try{
 if(argc==5){std::ifstream f(argv[1],std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});auto r=parse_signed_update_manifest(data,argv[2],argv[3],std::wstring(argv[4],argv[4]+strlen(argv[4])));std::wcout<<L"PASS signed manifest "<<r.version;return 0;}
 BCRYPT_ALG_HANDLE alg=nullptr,sha=nullptr;BCRYPT_KEY_HANDLE key=nullptr;
 check(BCryptOpenAlgorithmProvider(&alg,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0);check(BCryptGenerateKeyPair(alg,&key,256,0)>=0);check(BCryptFinalizeKeyPair(key,0)>=0);
 ULONG size=0;check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&size,0)>=0);std::vector<unsigned char> pub(size);check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,pub.data(),size,&size,0)>=0);
 const std::string payload="version=0.8.0\nurl=https://example.com/setup.exe\nsize=3\nsha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\nnotes=Queue recovery\n";
 std::string signed_data="ShiftWheel-update-v1\nkey_id=test\n"+payload;std::vector<unsigned char> digest(32),sig(64);
 check(BCryptOpenAlgorithmProvider(&sha,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);check(BCryptHash(sha,nullptr,0,reinterpret_cast<PUCHAR>(signed_data.data()),static_cast<ULONG>(signed_data.size()),digest.data(),32)>=0);
 check(BCryptSignHash(key,nullptr,digest.data(),32,sig.data(),64,&size,0)>=0);
 auto envelope=payload+"key_id=test\nsignature="+hex(sig)+"\n";auto r=parse_signed_update_manifest(envelope,hex(pub),"test",L"0.7.0");check(r.version==L"0.8.0");
 auto tampered=envelope;tampered[8]='9';reject([&]{parse_signed_update_manifest(tampered,hex(pub),"test",L"0.7.0");});
 reject([&]{parse_signed_update_manifest(envelope,hex(pub),"wrong",L"0.7.0");});reject([&]{parse_signed_update_manifest(payload,hex(pub),"test",L"0.7.0");});
 reject([&]{parse_signed_update_manifest(envelope,hex(pub),"test",L"0.9.0");});auto wrong=pub;wrong.back()^=1;reject([&]{parse_signed_update_manifest(envelope,hex(wrong),"test",L"0.7.0");});
 BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(alg,0);BCryptCloseAlgorithmProvider(sha,0);
 std::cout<<"PASS ECDSA manifest, tamper, wrong key/id, unsigned and rollback refusal\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
