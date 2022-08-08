#include "build.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

const std::string_view InputData = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Proin egestas, erat at efficitur gravida, augue diam accumsan risus, nec sodales velit dui et quam. Nunc lacus augue, vestibulum et commodo ut, fermentum et est. Ut dignissim ante ac venenatis porttitor. Quisque mattis eu leo non posuere. Maecenas volutpat orci eget urna pellentesque congue. Vivamus molestie, massa a vulputate convallis, quam dui scelerisque tortor, at maximus ipsum purus non arcu. Nam sagittis vel velit ac scelerisque.";

static voidpf ZlibAlloc(voidpf, uInt items, uInt size)
{
	return malloc(size * items);
}

static void ZlibFree(voidpf, voidpf address)
{
	free(address);
}

static bool CompressZlib(const void* data, uint32_t dataSize, std::vector<uint8_t>& output)
{
	z_stream zstr;
	memset(&zstr, 0, sizeof(zstr));

	zstr.next_in = (Bytef*)data;
	zstr.avail_in = dataSize;
	zstr.next_out = static_cast<Bytef*>(output.data());
	zstr.avail_out = (uint32_t)output.size();
	zstr.zalloc = &ZlibAlloc;
	zstr.zfree = &ZlibFree;

	// start compression
	auto initRet = deflateInit(&zstr, Z_BEST_COMPRESSION);
	if (initRet != Z_OK)
		return false;

	// single step DEFLATE
	auto result = deflate(&zstr, Z_FINISH);
	deflateEnd(&zstr);

	// check results
	if (result != Z_STREAM_END)
		return false; // possible buffer was to small

	output.resize(zstr.total_out);
	return true;
}

static bool DecompressZlib(const void* data, uint32_t dataSize, std::vector<uint8_t>& output)
{
	z_stream zstr;
	memset(&zstr, 0, sizeof(zstr));

	zstr.next_in = (Bytef*)data;
	zstr.avail_in = dataSize;
	zstr.zalloc = &ZlibAlloc;
	zstr.zfree = &ZlibFree;

	// initialize
	auto initRet = inflateInit(&zstr);
	if (initRet != Z_OK)
		return false;

	zstr.next_out = (Bytef*)output.data();
	zstr.avail_out = (uint32_t)output.size();

	auto result = inflate(&zstr, Z_NO_FLUSH);
	inflateEnd(&zstr);

	if (result != Z_STREAM_END)
		return false;

	output.resize(zstr.total_out);
	return true;
}

static std::string BufferToString(const std::vector<uint8_t>& data)
{
	std::string ret;
	ret.resize(data.size());
	memcpy(ret.data(), data.data(), data.size());
	return ret;
}

int main()
{
	std::vector<uint8_t> compressed;
	compressed.resize(2 * InputData.size());

	if (!CompressZlib(InputData.data(), (uint32_t)InputData.size(), compressed))
		return -1;

	std::vector<uint8_t> decompressed;
	decompressed.resize(2 * InputData.size());
	if (!DecompressZlib(compressed.data(), (uint32_t)compressed.size(), decompressed))
		return -2;

	const auto decompressedStr = BufferToString(decompressed);
	if (decompressedStr != InputData)
		return -3;
	
	return 0;
}