#include <windows.h>
#include <filesystem>
#include <fstream>
int main(){wchar_t root[32768]{};if(!GetEnvironmentVariableW(L"SHIFTWHEEL_UPDATE_TEST_ROOT",root,32768))return 2;std::ofstream(std::filesystem::path(root)/L"installed.txt")<<"ok";return 0;}