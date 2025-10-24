#include <datastream.h>
#include <bin_codec.h>
#include <common.h>
#include <fstream>
#include <filesystem>
#include <gzip/utils.hpp>
#include <gzip/decompress.hpp>
#include "oodle_loader.h"

CDataStream::CDataStream()
	:
	m_offset(NULL),
	m_stride(NULL)
{
}

bool CDataStream::writeDataToFile(const std::string& filePath, const std::string& data)
{
	std::ofstream outFile(filePath, std::ios::binary);
	if (!outFile) return false;
	// write stream contents to file
	outFile.write(data.c_str(), data.size());
	// check write success...
	if (!outFile) return false;
	outFile.close();
	return true;
}

bool CDataStream::writeDataToFile(const std::string& filePath, const char* data, const size_t size)
{
	std::ofstream outFile(filePath, std::ios::binary);
	if (!outFile) return false;
	// write stream contents to file
	outFile.write(data, size);
	// check write success...
	if (!outFile) return false;
	outFile.close();
	return true;
}

bool CDataStream::decompressGzFile(const std::string& filePath, std::string& targetPath)
{
	size_t size;
	char* data = common::readFile(filePath, &size);
	if (!data) return false;

	// Check for VCZ-33 format (NBA 2K26 Oodle compression)
	// VCZ-33 has signature: 0x1F 0x8B 0x21 (first 3 bytes)
	if (size >= 16) {
		uint8_t* p = (uint8_t*)data;
		if (p[0] == 0x1F && p[1] == 0x8B && p[2] == 0x21) {
			printf("\n[CDataStream] Detected VCZ-33 (Oodle compression)");

			// Initialize Oodle loader
			OodleLoader& oodle = OodleLoader::getInstance();
			if (!oodle.isLoaded()) {
				printf("\n[CDataStream] Loading Oodle DLL...");

				// Try loading from current directory first
				if (!oodle.initialize("oo2core_9_win64.dll")) {
					// Try loading from exe directory
					char dllPath[MAX_PATH];
					HMODULE hm = NULL;

					if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						(LPCSTR)&OodleLoader::getInstance, &hm) != 0)
					{
						if (GetModuleFileNameA(hm, dllPath, sizeof(dllPath)) != 0)
						{
							char* lastSlash = strrchr(dllPath, '\\');
							if (lastSlash) {
								lastSlash[1] = '\0';
								strcat_s(dllPath, sizeof(dllPath), "oo2core_9_win64.dll");
								printf("\n[CDataStream] Trying: %s", dllPath);
								oodle.initialize(dllPath);
							}
						}
					}
				}
			}

			if (oodle.isLoaded()) {
				// Read uncompressed size from VCZ-33 header
				// Header format: [4 bytes magic] [4 bytes uncompressed size] [4 bytes compressed size] [4 bytes reserved]
				uint32_t* header = (uint32_t*)data;
				uint32_t uncompSize = header[1];
				uint32_t compSize = header[2];

				printf("\n[VCZ-33] Uncompressed size: %u bytes", uncompSize);
				printf("\n[VCZ-33] Compressed size: %u bytes", compSize);

				// Allocate buffer for decompressed data
				std::vector<uint8_t> decompressed(uncompSize);

				// Decompress (skip 16-byte header)
				int64_t result = oodle.decompress(
					(uint8_t*)data + 16, size - 16,
					decompressed.data(), uncompSize
				);

				if (result > 0) {
					printf("\n[VCZ-33] Successfully decompressed: %lld bytes", result);
					common::replaceSubString(targetPath, ".gz", ".bin");
					writeDataToFile(targetPath, (const char*)decompressed.data(), result);
					delete[] data;
					return true;
				}
				else {
					printf("\n[ERROR] Oodle decompression failed! Error code: %lld", result);
				}
			}
			else {
				printf("\n[ERROR] Oodle DLL not loaded - cannot decompress VCZ-33!");
				printf("\n[INFO] Please place oo2core_9_win64.dll next to the executable");
			}

			delete[] data;
			return false;
		}
	}

	// Standard gzip decompression (NBA 2K25 and earlier)
	if (gzip::is_compressed(data, size))
	{
		printf("\n[CDataStream] Decompressing standard .gz file...");
		auto decompressed_data = gzip::decompress(data, size);
		common::replaceSubString(targetPath, ".gz", ".bin");
		writeDataToFile(targetPath, decompressed_data);
	}
	// todo: handle already decompressed .gz files ... 
	delete[] data;
	return true;
}

std::string CDataStream::findBinaryFile()
{
	std::string targetName = std::filesystem::path(m_path).filename().string();
	bool isCompressed = common::containsSubstring(m_path, ".gz");
	if (isCompressed)
	{
		auto compressedPath = common::findFileInDirectory(WORKING_DIR, targetName);
		if (!compressedPath.empty())
		{
			std::string outPath = compressedPath;
			return (decompressGzFile(compressedPath, outPath)) ? outPath : "";
		}
		else
		{
			// file not in dir - default search to decompressed .bin file ...
			common::replaceSubString(targetName, ".gz", ".bin");
		}
	}
	return common::findFileInDirectory(WORKING_DIR, targetName);
}