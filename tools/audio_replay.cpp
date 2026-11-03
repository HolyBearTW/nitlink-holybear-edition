#include "audio/audio_trace.h"
#include <fstream>
#include <iostream>

namespace {
int ReplayFile(int argc, const std::filesystem::path& path) {
    if (argc != 2) {
        std::cerr << "Usage: nitlink_audio_replay trace.csv\n"
                     "Exit 0: complete matching replay; 1: invalid trace or mismatches; 2: incomplete trace.\n";
        return 1;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) { std::cerr << "Could not open audio trace.\n"; return 1; }
    NitLink::AudioTraceSnapshot trace; std::string error;
    if (!NitLink::ReadAudioTraceCsv(input, trace, error)) { std::cerr << error << '\n'; return 1; }
    const auto report = NitLink::ReplayAudioTrace(trace);
    NitLink::PrintAudioReplayReport(std::cout, report);
    return report.mismatches ? 1 : report.complete ? 0 : 2;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return ReplayFile(argc, argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{}); }
#else
int main(int argc, char** argv) { return ReplayFile(argc, argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{}); }
#endif
