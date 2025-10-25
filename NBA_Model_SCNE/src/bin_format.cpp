#include <bin_format.h>
#include <algorithm>
#include <memoryreader.h>

using namespace memreader;

static constexpr uint64_t getMaxIntValue(int bits, bool sign)
{
	return sign ? (1LL << (bits - 1)) - 1 : (1ULL << bits) - 1;
}

static float unpackValue(const float input, const int num_bits, const std::string& type)
{
	// Check if type contains "snorm" anywhere
	if (type.find("snorm") != std::string::npos)
		return (input / ::getMaxIntValue(num_bits, true));

	// Check if type contains "unorm" anywhere
	if (type.find("unorm") != std::string::npos)
		return (input / ::getMaxIntValue(num_bits, false));

	return input;
}

// ============================================
// GET CHANNELS FUNCTIONS
// ============================================

template <int Channels>
int Format_32Bit<Channels>::get_channels()
{
	return Channels;
}

template <int Channels>
int Format_16Bit<Channels>::get_channels()
{
	return Channels;
}

template <int Channels>
int Format_8Bit<Channels>::get_channels()
{
	return Channels;
}

int R10G10B10A2::get_channels()
{
	return 4;
}

int R10G10B10::get_channels()
{
	return 3;
}

int R11G11B10::get_channels()
{
	return 3;
}

int R21G21B22::get_channels()
{
	return 3;
}

// ============================================
// GET SIZE FUNCTIONS
// ============================================

template <int Channels>
int Format_32Bit<Channels>::get_size(const int items)
{
	return items * sizeof(int32_t) * Channels;
}

template <int Channels>
int Format_16Bit<Channels>::get_size(const int items)
{
	return items * sizeof(int16_t) * Channels;
}

template <int Channels>
int Format_8Bit<Channels>::get_size(const int items)
{
	return items * sizeof(int8_t) * Channels;
}

int R10G10B10A2::get_size(const int items)
{
	return items * sizeof(int32_t);
}

int R10G10B10::get_size(const int items)
{
	return items * sizeof(int32_t);
}

int R11G11B10::get_size(const int items)
{
	return items * sizeof(int32_t);
}

int R21G21B22::get_size(const int items)
{
	return items * sizeof(uint64_t);
}

// ============================================
// DECODE FUNCTIONS
// ============================================

template <int Channels>
void Format_32Bit<Channels>::decode(FMT_DT_PARAMS)
{
	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
		{
			if (type == "sint" || type == "uint") {
				target.push_back(ReadInt32(data));
			}
			else {
				target.push_back(ReadFloat(data));
			}
		}
	}
}

template <int Channels>
void Format_16Bit<Channels>::decode(FMT_DT_PARAMS)
{
	auto unpack = [type](char*& data) -> float {
		if (type == "snorm" || type == "sint")
			return ::unpackValue(ReadInt16(data), 16, type);
		else if (type == "unorm" || type == "uint")
			return ::unpackValue(ReadUInt16(data), 16, type);
		};

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
			target.push_back(unpack(data));
	}
}

template <int Channels>
void Format_8Bit<Channels>::decode(FMT_DT_PARAMS)
{
	auto unpack = [type](char*& data) -> float {
		if (type == "snorm" || type == "sint")
			return ::unpackValue(ReadInt8(data), 8, type);
		else if (type == "unorm" || type == "uint")
			return ::unpackValue(ReadUInt8(data), 8, type);
		};

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
			target.push_back(unpack(data));
	}
}

void R10G10B10A2::decode(FMT_DT_PARAMS)
{
	float r, g, b, a;
	uint32_t packedValue;

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;
		packedValue = ReadUInt32(data);

		r = (packedValue >> 0) & 0x3FF;
		g = (packedValue >> 10) & 0x3FF;
		b = (packedValue >> 20) & 0x3FF;
		a = (packedValue >> 30) & 0x3;

		target.push_back(unpackValue(r, 10, type));
		target.push_back(unpackValue(g, 10, type));
		target.push_back(unpackValue(b, 10, type));
		target.push_back(unpackValue(a, 02, type));
	}
}

void R10G10B10::decode(FMT_DT_PARAMS)
{
	float r, g, b;
	uint32_t packedValue;

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;
		packedValue = ReadUInt32(data);

		// Extract 10-bit values
		uint32_t r_raw = (packedValue >> 0) & 0x3FF;
		uint32_t g_raw = (packedValue >> 10) & 0x3FF;
		uint32_t b_raw = (packedValue >> 20) & 0x3FF;

		// ✓ Handle signed formats (SNORM/SINT) - sign-extend from 10 bits
		if (type.find("snorm") != std::string::npos || type.find("sint") != std::string::npos)
		{
			// Sign-extend: if bit 9 is set, extend with 1s
			if (r_raw & 0x200) r_raw |= 0xFFFFFC00;  // Sign extend
			if (g_raw & 0x200) g_raw |= 0xFFFFFC00;
			if (b_raw & 0x200) b_raw |= 0xFFFFFC00;

			// Reinterpret as signed
			int32_t r_signed = *reinterpret_cast<int32_t*>(&r_raw);
			int32_t g_signed = *reinterpret_cast<int32_t*>(&g_raw);
			int32_t b_signed = *reinterpret_cast<int32_t*>(&b_raw);

			r = static_cast<float>(r_signed);
			g = static_cast<float>(g_signed);
			b = static_cast<float>(b_signed);
		}
		else
		{
			// Unsigned
			r = static_cast<float>(r_raw);
			g = static_cast<float>(g_raw);
			b = static_cast<float>(b_raw);
		}

		target.push_back(unpackValue(r, 10, type));
		target.push_back(unpackValue(g, 10, type));
		target.push_back(unpackValue(b, 10, type));
	}
}

void R11G11B10::decode(FMT_DT_PARAMS)
{
	float r, g, b;
	uint32_t packedValue;

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;
		packedValue = ReadUInt32(data);

		// Extract values
		uint32_t r_raw = (packedValue >> 0) & 0x7FF;  // 11 bits
		uint32_t g_raw = (packedValue >> 11) & 0x7FF;  // 11 bits
		uint32_t b_raw = (packedValue >> 22) & 0x3FF;  // 10 bits

		// ✓ Handle signed formats
		if (type.find("snorm") != std::string::npos || type.find("sint") != std::string::npos)
		{
			// Sign-extend 11-bit and 10-bit values
			if (r_raw & 0x400) r_raw |= 0xFFFFF800;  // 11-bit sign extend
			if (g_raw & 0x400) g_raw |= 0xFFFFF800;
			if (b_raw & 0x200) b_raw |= 0xFFFFFC00;  // 10-bit sign extend

			int32_t r_signed = *reinterpret_cast<int32_t*>(&r_raw);
			int32_t g_signed = *reinterpret_cast<int32_t*>(&g_raw);
			int32_t b_signed = *reinterpret_cast<int32_t*>(&b_raw);

			r = static_cast<float>(r_signed);
			g = static_cast<float>(g_signed);
			b = static_cast<float>(b_signed);
		}
		else
		{
			r = static_cast<float>(r_raw);
			g = static_cast<float>(g_raw);
			b = static_cast<float>(b_raw);
		}

		target.push_back(unpackValue(r, 11, type));
		target.push_back(unpackValue(g, 11, type));
		target.push_back(unpackValue(b, 10, type));
	}
}

void R21G21B22::decode(FMT_DT_PARAMS)
{
	float x, y, z;
	uint64_t packedValue;

	for (int i = 0; i < size; i++)
	{
		char* data = src + (i * stride) + offset;
		packedValue = ReadUInt64(data);

		x = static_cast<float>((packedValue >> 0) & 0x1FFFFF);   // 21 bits
		y = static_cast<float>((packedValue >> 21) & 0x1FFFFF);  // 21 bits
		z = static_cast<float>((packedValue >> 42) & 0x3FFFFF);  // 22 bits

		target.push_back(unpackValue(x, 21, type));
		target.push_back(unpackValue(y, 21, type));
		target.push_back(unpackValue(z, 22, type));
	}
}

// ============================================
// UPDATE FUNCTIONS
// ============================================

template <int Channels>
void Format_32Bit<Channels>::updateData(INJ_DT_PARAMS)
{
	int numItems = size / Channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
		{
			int index = (i * Channels) + j;
			WriteFloat(stream, target[index]);
		}
	}
}

template <int Channels>
void Format_16Bit<Channels>::updateData(INJ_DT_PARAMS)
{
	int numItems = size / Channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
		{
			int index = (i * Channels) + j;
			float value = target[index];

			if (type == "snorm" || type == "sint")
			{
				int16_t pack = (type == "snorm") ? (value * ::getMaxIntValue(m_bits, true)) : value;
				WriteSInt16(stream, pack);
			}
			else if (type == "unorm" || type == "uint")
			{
				uint16_t pack = (type == "unorm") ? (value * ::getMaxIntValue(m_bits, false)) : value;
				WriteUInt16(stream, pack);
			}
		}
	}
}

template <int Channels>
void Format_8Bit<Channels>::updateData(INJ_DT_PARAMS)
{
	int numItems = size / Channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		for (int j = 0; j < Channels; j++)
		{
			int index = (i * Channels) + j;
			float value = target[index];

			if (type == "snorm" || type == "sint")
			{
				int8_t pack = (type == "snorm") ? (value * ::getMaxIntValue(m_bits, true)) : value;
				WriteSInt8(stream, pack);
			}
			else if (type == "unorm" || type == "uint")
			{
				uint8_t pack = (type == "unorm") ? (value * ::getMaxIntValue(m_bits, false)) : value;
				WriteUInt8(stream, pack);
			}
		}
	}
}

void R10G10B10A2::updateData(INJ_DT_PARAMS)
{
	int channels = 4;
	int numItems = size / channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		uint32_t packedR = (target[(i * channels) + 0]);
		uint32_t packedG = (target[(i * channels) + 1]);
		uint32_t packedB = (target[(i * channels) + 2]);
		uint32_t packedA = (target[(i * channels) + 3]);

		uint32_t packedValue = (packedR & 0x3FF)
			| ((packedG & 0x3FF) << 10)
			| ((packedB & 0x3FF) << 20)
			| ((packedA & 0x3) << 30);

		WriteUInt32(stream, packedValue);
	}
}

void R10G10B10::updateData(INJ_DT_PARAMS)
{
	int channels = 3;
	int numItems = size / channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		uint32_t packedR = (target[(i * channels) + 0]);
		uint32_t packedG = (target[(i * channels) + 1]);
		uint32_t packedB = (target[(i * channels) + 2]);

		uint32_t packedValue = (packedR & 0x3FF)
			| ((packedG & 0x3FF) << 10)
			| ((packedB & 0x3FF) << 20);

		WriteUInt32(stream, packedValue);
	}
}

void R11G11B10::updateData(INJ_DT_PARAMS)
{
	int channels = 3;
	int numItems = size / channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		uint32_t packedR = (target[(i * channels) + 0]);
		uint32_t packedG = (target[(i * channels) + 1]);
		uint32_t packedB = (target[(i * channels) + 2]);

		uint32_t packedValue = (packedR & 0x7FF)        // 11 bits
			| ((packedG & 0x7FF) << 11)  // 11 bits
			| ((packedB & 0x3FF) << 22); // 10 bits

		WriteUInt32(stream, packedValue);
	}
}

void R21G21B22::updateData(INJ_DT_PARAMS)
{
	int channels = 3;
	int numItems = size / channels;

	for (int i = 0; i < numItems; i++)
	{
		char* stream = src + (i * stride) + offset;

		float xVal = target[(i * channels) + 0];
		float yVal = target[(i * channels) + 1];
		float zVal = target[(i * channels) + 2];

		uint64_t packedX, packedY, packedZ;

		if (type == "snorm" || type == "sint")
		{
			packedX = static_cast<uint64_t>(static_cast<int64_t>(
				(type == "snorm") ? (xVal * ::getMaxIntValue(21, true)) : xVal
				)) & 0x1FFFFF;
			packedY = static_cast<uint64_t>(static_cast<int64_t>(
				(type == "snorm") ? (yVal * ::getMaxIntValue(21, true)) : yVal
				)) & 0x1FFFFF;
			packedZ = static_cast<uint64_t>(static_cast<int64_t>(
				(type == "snorm") ? (zVal * ::getMaxIntValue(22, true)) : zVal
				)) & 0x3FFFFF;
		}
		else if (type == "unorm" || type == "uint")
		{
			packedX = static_cast<uint64_t>(
				(type == "unorm") ? (xVal * ::getMaxIntValue(21, false)) : xVal
				) & 0x1FFFFF;
			packedY = static_cast<uint64_t>(
				(type == "unorm") ? (yVal * ::getMaxIntValue(21, false)) : yVal
				) & 0x1FFFFF;
			packedZ = static_cast<uint64_t>(
				(type == "unorm") ? (zVal * ::getMaxIntValue(22, false)) : zVal
				) & 0x3FFFFF;
		}
		else
		{
			packedX = static_cast<uint64_t>(xVal) & 0x1FFFFF;
			packedY = static_cast<uint64_t>(yVal) & 0x1FFFFF;
			packedZ = static_cast<uint64_t>(zVal) & 0x3FFFFF;
		}

		uint64_t packedValue = packedX | (packedY << 21) | (packedZ << 42);
		WriteUInt64(stream, packedValue);
	}
}

// ============================================
// ENCODE FUNCTIONS
// ============================================

template <int Channels>
char* Format_8Bit<Channels>::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = Channels * sizeof(int8_t);
	length = (size / Channels) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

template <int Channels>
char* Format_16Bit<Channels>::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = Channels * sizeof(int16_t);
	length = (size / Channels) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

template <int Channels>
char* Format_32Bit<Channels>::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = Channels * sizeof(int32_t);
	length = (size / Channels) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

char* R10G10B10A2::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = sizeof(int32_t);
	length = (size / get_channels()) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

char* R10G10B10::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = sizeof(int32_t);
	length = (size / get_channels()) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

char* R11G11B10::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = sizeof(int32_t);
	length = (size / get_channels()) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}

char* R21G21B22::encode(EXP_DT_PARAMS)
{
	size_t size = target.size();
	size_t stride = sizeof(uint64_t);
	length = (size / get_channels()) * stride;
	char* stream = new char[length];

	this->updateData(stream, size, target, type, 0, stride);
	return stream;
}