#pragma once
#include <windows.h>
#include <cstdint>

class OodleLoader {
public:
    static OodleLoader& getInstance() {
        static OodleLoader instance;
        return instance;
    }

    bool initialize(const char* dllPath = "oo2core_9_win64.dll");
    void shutdown();

    int64_t decompress(const uint8_t* compBuf, size_t compBufSize,
        uint8_t* decompBuf, size_t decompBufSize);

    int64_t compress(const uint8_t* buffer, size_t bufferSize,
        uint8_t* outputBuffer, size_t outputBufferSize,
        int format = 13, int level = 4); // Format 13 = Kraken, Level 4 = Normal

    size_t compressBound(size_t bufferSize);

    bool isLoaded() const { return m_hModule != nullptr; }

private:
    OodleLoader() : m_hModule(nullptr), m_decompressFunc(nullptr), m_compressFunc(nullptr) {}
    ~OodleLoader() { shutdown(); }

    HMODULE m_hModule;

    // Oodle function signatures for 64-bit
    typedef int64_t(*OodleLZ_Decompress_Func)(
        const uint8_t* compBuf, int64_t compBufSize,
        uint8_t* rawBuf, int64_t rawLen,
        int fuzzSafe, int checkCRC, int verbosity,
        uint8_t* decBufBase, int64_t decBufSize,
        void* fpCallback, void* callbackUserData,
        void* decoderMemory, int64_t decoderMemorySize,
        int threadPhase
        );

    typedef int64_t(*OodleLZ_Compress_Func)(
        int format,
        const uint8_t* buffer, int64_t bufferSize,
        uint8_t* outputBuffer, int level,  // level is int, not int64_t
        void* opts, void* dictionaryBase, void* lrm,
        void* scratchMem, int64_t scratchSize
        );

    OodleLZ_Decompress_Func m_decompressFunc;
    OodleLZ_Compress_Func m_compressFunc;
};