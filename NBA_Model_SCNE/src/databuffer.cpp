#include <databuffer.h>
#include <nbamodel.h>
#include <scenefile.h>
#include <common.h>
#include <gzip/decompress.hpp>
#include <gzip/utils.hpp>
#include <bin_codec.h>
#include <filesystem>

CDataBuffer::CDataBuffer()
	:
	CDataStream(),
	m_index(0),  // Changed from NULL to 0 - default to stream 0
	m_size(NULL)
{
}

int CDataBuffer::getStreamIdx()
{
	return m_index;
}

void CDataBuffer::setStride(int val)
{
	m_stride = val;
}

void CDataBuffer::setOffset(int val)
{
	m_offset = val;
}

std::string CDataBuffer::getEncoding()
{
	return common::splitString(m_format, '_').front();
}

std::string CDataBuffer::getType()
{
	auto type = common::splitString(m_format, '_').back();
	::common::str_to_lower(type);
	return type;
}

std::string CDataBuffer::getFormat() {
	return m_format;
}

int CDataBuffer::getStride()
{
	if (m_stride > 0 || m_format.empty())
		return m_stride;
	// Manually calculate stride length if non available
	auto encoding = getEncoding();
	auto type = getType();
	BinaryCodec codec(encoding, type);
	m_stride = codec.size(1);
	return m_stride;
}

int CDataBuffer::getDataOffset()
{
	return m_offset;
}

void CDataBuffer::parse(JSON& json)
{
	for (JSON::iterator it = json.begin(); it != json.end(); ++it)
	{
		auto key = common::chash(it.key());
		auto value = it.value();
		switch (key)
		{
		case enPropertyTag::FORMAT:
			m_format = value;
			break;
		case enPropertyTag::STREAM:
			m_index = value;
			break;
		case enPropertyTag::OFFSET:
			translate = { it.value()[0], it.value()[1], it.value()[2], it.value()[3] };
			break;
		case enPropertyTag::SCALE:
			scale = { it.value()[0], it.value()[1], it.value()[2], it.value()[3] };
			break;
		case enPropertyTag::SIZE_:
			m_size = value;
			break;
		case enPropertyTag::BINARY:
			m_path = value;
			break;
		case enPropertyTag::BYTE_OFFSET:
			this->setOffset(value);
			break;
		case enPropertyTag::STRIDE:
			this->setStride(value);
			break;
		default:
			break;
		};
	}
}

void CDataBuffer::readFileData(char*& data, size_t& file_size)
{
	m_binaryPath = this->findBinaryFile();
	data = common::readFile(m_binaryPath, &file_size);
	/* Missing model data - must throw exception */
	if (m_binaryPath.empty() || !data) {
		printf("\n[CDataBuffer] Invalid scene - inaccessible data file: %s\n", m_path.c_str());
		throw std::runtime_error("Invalid data buffer.");
	}
}

void CDataBuffer::loadFileData(char* src, const size_t& size)
{
	std::string encoding = getEncoding();
	std::string type = getType();

	// Validate memory buffer size with target length
	BinaryCodec codec(encoding, type);
	size_t items = m_size / getStride();
	size_t dataSize = codec.size(items);

	// Debug output
	printf("\n[CDataBuffer::loadFileData]");
	printf("\n  - Format: %s", m_format.c_str());
	printf("\n  - Encoding: %s, Type: %s", encoding.c_str(), type.c_str());
	printf("\n  - Stream: %d", m_index);
	printf("\n  - Stride: %d, Offset: %d", getStride(), m_offset);
	printf("\n  - Items: %zu, DataSize needed: %zu, Buffer size: %zu", items, dataSize, size);

	// load data elements from binary
	if (dataSize <= size) {
		codec.decode(src, items, data, m_offset, m_stride);
		printf("\n  - Successfully decoded %zu floats", data.size());
	}
	else {
		printf("\n  - ERROR: DataSize (%zu) > Buffer (%zu)!", dataSize, size);
	}
}

void CDataBuffer::loadBinary()
{
	if (m_format.empty() || !m_size || m_path.empty())
		return;
	size_t size(NULL);
	char* binary;
	// Process file binary
	this->readFileData(binary, size);
	this->loadFileData(binary, size);
	// free binary
	delete[] binary;
}

std::vector<uint8_t>
CDataBuffer::getBinary()
{
	std::vector<uint8_t> data;
	size_t size(NULL);
	char* binary;
	if (m_format.empty() || !m_size || m_path.empty())
		return data;
	// Process file binary
	this->readFileData(binary, size);
	// Copy data to target vector
	data.resize(size);
	memcpy(data.data(), binary, size);
	// free allocated binary
	delete[] binary;
	return data;
}

void CDataBuffer::updateSceneReference(const std::string& newPath)
{
	// For now, just inform the user to manually update the SCNE file
	// In the future, we can automate this by modifying the JSON
	std::string filename = std::filesystem::path(newPath).filename().string();

	printf("\n[CDataBuffer] ========================================");
	printf("\n[CDataBuffer] IMPORTANT: Update your .scne file:");
	printf("\n[CDataBuffer] ========================================");
	printf("\n[CDataBuffer] 1. Find the reference to the old .gz file");
	printf("\n[CDataBuffer] 2. Change \"Binary\": \"...\" to: \"%s\"", filename.c_str());
	printf("\n[CDataBuffer] 3. Change \"CompressionMethod\": 33 to: 0");
	printf("\n[CDataBuffer] 4. (Or remove \"CompressionMethod\" entirely)");
	printf("\n[CDataBuffer] ========================================\n");
}

bool CDataBuffer::saveBinary(char* data, const size_t size)
{
	printf("\n[CDataBuffer] ========================================");
	printf("\n[CDataBuffer] saveBinary() CALLED!");
	printf("\n[CDataBuffer] ========================================");

	// Check all preconditions
	printf("\n[CDataBuffer] Checking preconditions...");
	printf("\n  - data pointer: %s", data ? "OK" : "NULL!");
	printf("\n  - m_binaryPath: '%s' (%s)", m_binaryPath.c_str(), m_binaryPath.empty() ? "EMPTY!" : "OK");
	printf("\n  - m_path: '%s' (%s)", m_path.c_str(), m_path.empty() ? "EMPTY!" : "OK");
	printf("\n  - size: %zu bytes", size);

	if (!data || m_binaryPath.empty() || m_path.empty())
	{
		printf("\n[CDataBuffer] ERROR: Precondition failed - returning false!");
		printf("\n[CDataBuffer] ========================================\n");
		return false;
	}

	printf("\n[CDataBuffer] All preconditions passed!");

	// Create backup of original file
	printf("\n[CDataBuffer] Creating backup...");
	common::createFileBackup(m_binaryPath.c_str());
	printf("\n[CDataBuffer] Backup attempted for: %s", m_binaryPath.c_str());

	// Determine output path - change .gz to .bin for uncompressed
	std::string outputPath = m_binaryPath;
	bool wasCompressed = common::containsSubstring(outputPath, ".gz");

	if (wasCompressed) {
		common::replaceSubString(outputPath, ".gz", ".bin");
		printf("\n[CDataBuffer] Changed extension: .gz -> .bin (uncompressed)");
	}

	printf("\n[CDataBuffer] Final output path: '%s'", outputPath.c_str());
	printf("\n[CDataBuffer] Data size: %zu bytes (uncompressed)", size);

	// Write uncompressed data
	printf("\n[CDataBuffer] Calling writeDataToFile()...");
	bool success = writeDataToFile(outputPath, data, size);

	if (success) {
		printf("\n[CDataBuffer] writeDataToFile() returned TRUE!");
		printf("\n[CDataBuffer] Successfully saved uncompressed data!");

		// If we changed the extension, inform user to update SCNE
		if (wasCompressed) {
			updateSceneReference(outputPath);
		}
	}
	else {
		printf("\n[CDataBuffer] writeDataToFile() returned FALSE!");
		printf("\n[CDataBuffer] ERROR: Failed to write file!");
	}

	printf("\n[CDataBuffer] ========================================\n");

	return success;
}