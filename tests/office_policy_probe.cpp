// Test-only replacement engine: inspect the actual private profile passed by the app.
#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
namespace fs = std::filesystem;
int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring url; fs::path output, source;
        for (int i = 1; i < argc; ++i) {
            std::wstring argument = argv[i];
            if (argument.starts_with(L"-env:UserInstallation=")) url = argument.substr(22);
            if (argument == L"--outdir" && i + 1 < argc) output = argv[++i];
            if (i == argc - 1) source = argv[i];
        }
        wchar_t path[32768]{}; DWORD size = 32768;
        if (url.empty() || FAILED(PathCreateFromUrlW(url.c_str(), path, &size, 0))) throw std::runtime_error("private profile URL missing");
        std::ifstream stream(fs::path(path) / L"user/registrymodifications.xcu", std::ios::binary);
        std::string xml{std::istreambuf_iterator<char>(stream), {}};
        const auto start = xml.find("<item oor:path=\"/org.openoffice.Office.Common/Misc\">");
        const auto end = xml.find("</item>", start);
        const auto item = start == std::string::npos ? std::string{} : xml.substr(start, end - start);
        if (item.find("<prop oor:name=\"UseOpenCL\" oor:op=\"fuse\"><value>false</value></prop>") == std::string::npos)
            throw std::runtime_error("private Office profile must disable OpenCL startup benchmarking");
        if (xml.find("MacroSecurityLevel\" oor:op=\"fuse\"><value>3</value>") == std::string::npos ||
            xml.find("DisableMacrosExecution\" oor:op=\"fuse\"><value>true</value>") == std::string::npos)
            throw std::runtime_error("macro isolation was lost");
        if (!fs::is_directory(output) || !fs::is_regular_file(source)) throw std::runtime_error("private input/output missing");
        std::ofstream result(output / (source.stem().wstring() + L".pdf"), std::ios::binary);
        result << "%PDF-policy-probe-only\n";
        return result ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what(); return 42; }
}
