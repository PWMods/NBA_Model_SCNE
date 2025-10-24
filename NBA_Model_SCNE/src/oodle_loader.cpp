#include "oodle_loader.h"
#include <cstdio>

bool OodleLoader::initialize(const char* dllPath) {
    if (m_hModule) return true;

    printf("\n[Oodle] Attempting to load: %s", dllPath);

    m_hModule = LoadLibraryA(dllPath);
    if (!m_hModule) {
        DWORD error = GetLastError();
        printf("\n[Oodle] Failed to load %s (Error: %lu)", dllPath, error);
        return false;
    }

    // Load both compress and decompress functions
    m_decompressFunc = (OodleLZ_Decompress_Func)GetProcAddress(m_hModule, "OodleLZ_Decompress");
    m_compressFunc = (OodleLZ_Compress_Func)GetProcAddress(m_hModule, "OodleLZ_Compress");

    if (!m_decompressFunc || !m_compressFunc) {
        printf("\n[Oodle] Failed to find Oodle functions");
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        m_decompressFunc = nullptr;
        m_compressFunc = nullptr;
        return false;
    }

    printf("\n[Oodle] Successfully loaded Oodle compression/decompression library");
    return true;
}

void OodleLoader::shutdown() {
    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        m_decompressFunc = nullptr;
        m_compressFunc = nullptr;
    }
}

int64_t OodleLoader::decompress(const uint8_t* compBuf, size_t compBufSize,
    uint8_t* decompBuf, size_t decompBufSize) {
    if (!m_decompressFunc) return -1;

    return m_decompressFunc(
        compBuf, static_cast<int64_t>(compBufSize),
        decompBuf, static_cast<int64_t>(decompBufSize),
        1, 1, 0,  // fuzzSafe, checkCRC, verbosity
        nullptr, 0,  // decBufBase, decBufSize
        nullptr, nullptr,  // callback
        nullptr, 0,  // decoder memory
        3   // threadPhase
    );
}

int64_t OodleLoader::compress(const uint8_t* buffer, size_t bufferSize,
    uint8_t* outputBuffer, size_t outputBufferSize,
    int format, int level) {
    if (!m_compressFunc) return -1;

    // Correct call based on Siren example
    return m_compressFunc(
        format,
        buffer, static_cast<int64_t>(bufferSize),
        outputBuffer, level,  // Just the compression level
        nullptr, nullptr, nullptr,
        nullptr, 0
    );
}

size_t OodleLoader::compressBound(size_t bufferSize) {
    // Calculate maximum possible compressed size
    return bufferSize + 274 * ((bufferSize + 0x3FFFF) / 0x40000);
}