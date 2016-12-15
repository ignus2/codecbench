/**
 * codecbench - Video For Windows codec benchmarking tool.
 *
 * Copyright (C) 2016 Balazs OROSZI
 *
 * This file is part of codecbench.
 *
 * codecbench is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * codecbench is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with codecbench.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>
#include <vfw.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <stdexcept>
#include <vector>
#include <fstream>
#include <map>

struct Timer
{
	Timer()
	{
		QueryPerformanceFrequency(&freq);
		sumCounts = numSamples = 0;
	}

	void begin()
	{
		QueryPerformanceCounter(&startCount);
	}

	void end()
	{
		QueryPerformanceCounter(&endCount);
		sumCounts += endCount.QuadPart - startCount.QuadPart;
		++numSamples;
	}

	int64_t sumTimeUs()
	{
		return 1000000 * sumCounts / freq.QuadPart; // freq is counts / sec
	}

	LARGE_INTEGER freq, startCount, endCount;
	int64_t sumCounts, numSamples;
};

class ArgvParser
{
public:
	ArgvParser(int argc, char* argv[])
	{
		char* lastarg = 0;
		for (int i = 1; i < argc; ++i)
		{
			if (argv[i][0] == '-')
			{
				lastarg = argv[i];
				argmap[lastarg] = 0;
			}
			else if (lastarg) // param of an arg
			{
				argmap[lastarg] = argv[i];
				lastarg = 0;
			}
			else // global (switchless) ordered arg
			{
				arglist.push_back(argv[i]);
			}
		}
	}

	bool hasArg(const std::string& arg, bool allowParam = false)
	{
		ArgMap::const_iterator it = argmap.find(arg);
		if (it == argmap.end())
			return false;
		else if (!allowParam && it->second)
			throw std::runtime_error("ERROR: '" + arg + "' does not accept arguments");
		return true;
	}

	/// Throws an exception if arg was given on the command line but without value
	/// Returns defVal if arg was not given at all
	const char* getArg(const std::string& arg, const char* defVal) const
	{
		ArgMap::const_iterator it = argmap.find(arg);
		if (it == argmap.end())
			return defVal;
		if (!it->second)
			throw std::runtime_error("ERROR: Missing argument for option '" + arg + "'");
		return it->second;
	}

	typedef std::map<std::string, char*> ArgMap;
	typedef std::vector<char*> ArgList;
	ArgMap argmap;
	ArgList arglist;
};

void printfcc(char* buf, DWORD fcc, int bpp)
{
	if (isprint(fcc & 0xFF) && isprint((fcc >> 8) & 0xFF) && isprint((fcc >> 16) & 0xFF) && isprint((fcc >> 24) & 0xFF))
	{
		*(DWORD*)(buf) = fcc;
		buf[4] = '\0';
	}
	else if (fcc == 0)
	{
		sprintf(buf, "RGB%d", bpp);
	}
	else
	{
		sprintf(buf, "0x%08lX", fcc);
	}
}

void PrintBitmapInfo(BITMAPINFOHEADER* biFormat)
{
	char fccstr[32];
	printfcc(fccstr, biFormat->biCompression, biFormat->biBitCount);
	printf("%ld x %ld [%s] %d bpp", biFormat->biWidth, biFormat->biHeight, fccstr, biFormat->biBitCount);
}

template <int ALIGN, typename T>
T align_to(T val)
{
	return (T)((uintptr_t(val) + ALIGN - 1) / ALIGN * ALIGN);
}

void GetDecompFormat(const char* format, int width, int height, BITMAPINFOHEADER* biFormatOut)
{
	memset(biFormatOut, 0, sizeof(BITMAPINFOHEADER));
	biFormatOut->biSize = sizeof(BITMAPINFOHEADER);
	biFormatOut->biWidth = width;
	biFormatOut->biHeight = height;
	biFormatOut->biPlanes = 1;

	if (strcmp(format, "RGB24") == 0 || strcmp(format, "bgr24") == 0) {
		biFormatOut->biBitCount = 24;
		biFormatOut->biCompression = BI_RGB;
		biFormatOut->biSizeImage = align_to<4>(biFormatOut->biWidth * 3) * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "RGB32") == 0 || strcmp(format, "bgr32") == 0) {
		biFormatOut->biBitCount = 32;
		biFormatOut->biCompression = BI_RGB;
		biFormatOut->biSizeImage = biFormatOut->biWidth * 4 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "BGRA") == 0) {
		biFormatOut->biBitCount = 32;
		biFormatOut->biCompression = mmioFOURCC('B','G','R','A');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 4 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "AYUV") == 0) {
		biFormatOut->biBitCount = 32;
		biFormatOut->biCompression = mmioFOURCC('A','Y','U','V');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 4 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "YUY2") == 0) {
		biFormatOut->biBitCount = 16;
		biFormatOut->biCompression = mmioFOURCC('Y','U','Y','2');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 2 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "UYVY") == 0) {
		biFormatOut->biBitCount = 16;
		biFormatOut->biCompression = mmioFOURCC('U','Y','V','Y');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 2 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "YV12") == 0) {
		biFormatOut->biBitCount = 12;
		biFormatOut->biCompression = mmioFOURCC('Y','V','1','2');
		biFormatOut->biSizeImage = biFormatOut->biWidth * abs(biFormatOut->biHeight) * 3 / 2;
	}
	else if (strcmp(format, "YV24") == 0) {
		biFormatOut->biBitCount = 24;
		biFormatOut->biCompression = mmioFOURCC('Y','V','2','4');
		biFormatOut->biSizeImage = biFormatOut->biWidth * abs(biFormatOut->biHeight) * 3;
	}
	else if (strcmp(format, "Y8") == 0) {
		biFormatOut->biBitCount = 8;
		biFormatOut->biCompression = mmioFOURCC('Y','8',' ',' ');
		biFormatOut->biSizeImage = biFormatOut->biWidth * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "b64a") == 0) {
		biFormatOut->biBitCount = 64;
		biFormatOut->biCompression = mmioFOURCC('b','6','4','a');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 8 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "b48r") == 0) {
		biFormatOut->biBitCount = 48;
		biFormatOut->biCompression = mmioFOURCC('b','4','8','r');
		biFormatOut->biSizeImage = biFormatOut->biWidth * 6 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "v210") == 0) {
		biFormatOut->biBitCount = 20;
		biFormatOut->biCompression = mmioFOURCC('v','2','1','0');
		biFormatOut->biSizeImage = (biFormatOut->biWidth + 47) / 48 * 128 * abs(biFormatOut->biHeight);
	}
	else if (strcmp(format, "r210") == 0) {
		biFormatOut->biBitCount = 30;
		biFormatOut->biCompression = mmioFOURCC('r','2','1','0');
		biFormatOut->biSizeImage = (biFormatOut->biWidth + 63) / 64 * 256 * abs(biFormatOut->biHeight);
	}
	else {
		throw std::runtime_error(std::string("Invalid requested decompressed format: ") + format);
	}
}

/////////////////////////////////////
template <typename T>
T readVar(std::istream& input)
{
	T var;
	input.read((char*) &var, sizeof(var));
	return var;
}

template <typename T>
void writeVar(std::ostream& output, T var)
{
	output.write((char*) &var, sizeof(var));
}

/////////////////////////////////////
class BitmapInfoHeader
{
public:
	BitmapInfoHeader()
	{
		resize(sizeof(BITMAPINFOHEADER));
	}

	BitmapInfoHeader& operator=(const BITMAPINFOHEADER* bih)
	{
		if (!bih)
			return *this;

		resize(bih->biSize);
		memcpy(&buf[0], bih, bih->biSize);
		return *this;
	}

	operator BITMAPINFOHEADER*()
	{
		return (BITMAPINFOHEADER*)&buf[0];
	}

	void resize(size_t size)
	{
		buf.resize(size);
	}

private:
	std::vector<char> buf;
};

/////////////////////////////////////
class VideoReader
{
public:
	void openRaw(const char* infile, const char* format, int width, int height);

	void open(const char* infile);

	bool readFrame();

	void rewind()
	{
		m_inFile.clear();
		m_inFile.seekg(m_headerSize, m_inFile.beg);
	}

	char* frameData()
	{
		return &m_frameBuf[0];
	}

	uint32_t frameSize()
	{
		return m_frameSize;
	}

	BITMAPINFOHEADER* getFormat()
	{
		return (BITMAPINFOHEADER*)m_biFormat;
	}

private:
	std::vector<char> m_frameBuf;
	uint32_t m_frameSize;
	std::ifstream m_inFile;
	BitmapInfoHeader m_biFormat;
	uint32_t m_headerSize;
	bool m_raw;
};

void VideoReader::openRaw(const char* infile, const char* format, int width, int height)
{
	GetDecompFormat(format, width, height, (BITMAPINFOHEADER*)m_biFormat);

	m_inFile.open(infile, std::ios::binary);
	if (!m_inFile)
	{
		throw std::runtime_error(std::string("ERROR: Failed to open file: ") + infile);
	}
	m_raw = true;
	m_headerSize = 0;
}

void VideoReader::open(const char* infile)
{
	m_inFile.open(infile, std::ios::binary);
	if (!m_inFile)
	{
		throw std::runtime_error(std::string("ERROR: Failed to open file: ") + infile);
	}
	m_raw = false;

	// Read magic
	uint32_t magic = readVar<uint32_t>(m_inFile);
	if (magic != 0xABCDEF01)
	{
		throw std::runtime_error("ERROR: Invalid file magic");
	}

	// Read format block
	uint32_t formatSize = readVar<uint32_t>(m_inFile);
	m_biFormat.resize(formatSize);
	m_inFile.read((char*)(BITMAPINFOHEADER*)m_biFormat, formatSize);

	m_headerSize = 8 + formatSize;
}

bool VideoReader::readFrame()
{
	if (m_inFile.eof())
	{
		return false;
	}

	if (m_raw)
	{
		m_frameSize = getFormat()->biSizeImage;
	}
	else
	{
		m_frameSize = readVar<uint32_t>(m_inFile);
	}

	if (m_frameBuf.size() < m_frameSize)
	{
		m_frameBuf.resize(m_frameSize);
	}
	m_inFile.read(&m_frameBuf[0], m_frameSize);
	return (bool) m_inFile;
}

/////////////////////////////////////
class VideoWriter
{
public:
	/// If biFormat is NULL the file is written as raw
	void open(const char* outfile, BITMAPINFOHEADER* biFormat = NULL);

	void writeFrame(const void* data, uint32_t frameSize);

private:
	std::ofstream m_outFile;
	bool m_raw;
};

void VideoWriter::open(const char* outfile, BITMAPINFOHEADER* biFormat)
{
	m_outFile.open(outfile, std::ios::binary);
	if (!m_outFile)
	{
		throw std::runtime_error(std::string("ERROR: Failed to open file: ") + outfile);
	}

	if (!biFormat)
	{
		m_raw = true;
	}
	else
	{
		m_raw = false;

		// Write magic
		writeVar<uint32_t>(m_outFile, 0xABCDEF01);

		// Write format block
		writeVar<uint32_t>(m_outFile, biFormat->biSize);
		m_outFile.write((char*)biFormat, biFormat->biSize);
	}
}

void VideoWriter::writeFrame(const void* data, uint32_t frameSize)
{
	if (!m_raw)
	{
		writeVar<uint32_t>(m_outFile, frameSize);
	}
	m_outFile.write((char*)data, frameSize);
}

/////////////////////////////////////
class Decompressor
{
public:
	Decompressor()
		: m_hic(0)
		, m_decompressing(false)
	{}

	~Decompressor()
	{
		if (m_hic)
		{
			if (m_decompressing)
				ICDecompressEnd(m_hic);
			ICClose(m_hic);
		}
		m_hic = 0;
	}

	void init(BITMAPINFOHEADER* biFormatIn, BITMAPINFOHEADER* biFormatOut = NULL, int width = 0, int height = 0);

	void decompressFrame(const void* data, uint32_t dataSize);

	char* frameData()
	{
		return &m_frameBuf[0];
	}

	BITMAPINFOHEADER* getOutputFormat()
	{
		return (BITMAPINFOHEADER*)m_biFormatOut;
	}

private:
	HIC m_hic;
	bool m_decompressing;
	std::vector<char> m_frameBuf;
	BitmapInfoHeader m_biFormatIn;
	BitmapInfoHeader m_biFormatOut;
};

void Decompressor::init(BITMAPINFOHEADER* biFormatIn, BITMAPINFOHEADER* biFormatOut, int width, int height)
{
	m_decompressing = false;

	// Locate decompressor
	m_hic = ICLocate(ICTYPE_VIDEO, biFormatIn->biCompression, biFormatIn, NULL, ICMODE_DECOMPRESS);
	if (!m_hic)
	{
		throw std::runtime_error("ERROR: Could not find appropriate decompressor!\n");
	}

	ICINFO icinfo = {};
	icinfo.dwSize = sizeof(icinfo);
	ICGetInfo(m_hic, &icinfo, sizeof(icinfo));
	wprintf(L"INFO: Decompressor        : '%ls' - '%ls'\n", icinfo.szName, icinfo.szDescription);

	// Determine/request decompressed format
	if (biFormatOut)
	{
		// Try to decompress to the requested format
		LRESULT result = ICDecompressQuery(m_hic, biFormatIn, biFormatOut);
		if (result != ICERR_OK)
		{
			throw std::runtime_error("ERROR: The decompressor cannot decompress to the requested format/size\n");
		}
		m_biFormatOut = biFormatOut;
	}
	else
	{
		// Ask the decompressor about decompressed format
		DWORD decompFormatSize = ICDecompressGetFormatSize(m_hic, biFormatIn);
		if (!decompFormatSize || (LONG)decompFormatSize < 0)
		{
			throw std::runtime_error("ICDecompressGetFormatSize() failed\n");
		}
		m_biFormatOut.resize(decompFormatSize);
		LRESULT result = ICDecompressGetFormat(m_hic, biFormatIn, (BITMAPINFOHEADER*)m_biFormatOut);
		if (result != ICERR_OK)
		{
			throw std::runtime_error("ICDecompressGetFormat() failed\n");
		}
	}

	// Determine/request decompressed size
	// Do separately, as it is for both the above cases
	if (width || height )
	{
		if (width)  ((BITMAPINFOHEADER*)m_biFormatOut)->biWidth  = width;
		if (height) ((BITMAPINFOHEADER*)m_biFormatOut)->biHeight = height;
		LRESULT result = ICDecompressQuery(m_hic, biFormatIn, (BITMAPINFOHEADER*)m_biFormatOut);
		if (result != ICERR_OK)
		{
			throw std::runtime_error("ERROR: The decompressor cannot decompress to the specified size\n");
		}
	}

	// Initialize decompressor
	LRESULT result = ICDecompressBegin(m_hic, biFormatIn, (BITMAPINFOHEADER*)m_biFormatOut);
	if (result != ICERR_OK)
	{
		throw std::runtime_error("ICDecompressBegin() failed\n");
	}
	m_biFormatIn = biFormatIn;
	m_decompressing = true;
	m_frameBuf.resize(((BITMAPINFOHEADER*)m_biFormatOut)->biSizeImage);
}

void Decompressor::decompressFrame(const void* data, uint32_t dataSize)
{
	((BITMAPINFOHEADER*)m_biFormatIn)->biSizeImage = dataSize;
	ICDecompress(m_hic, 0, (BITMAPINFOHEADER*)m_biFormatIn, (LPVOID) data, (BITMAPINFOHEADER*)m_biFormatOut, &m_frameBuf[0]);
}

/////////////////////////////////////
class Compressor
{
public:
	Compressor()
		: m_compvars()
		, m_compressing(false)
	{}

	~Compressor()
	{
		if (m_compvars.hic)
		{
			if (m_compressing)
				ICSeqCompressFrameEnd(&m_compvars);
			ICClose(m_compvars.hic);
		}
	}

	/// Return false means "no-compression" was selected
	bool init(BITMAPINFOHEADER* biFormatIn);

	void compressFrame(const void* data);

	char* frameData() const
	{
		return (char*) m_frameData;
	}

	uint32_t frameSize() const
	{
		return m_frameSize;
	}

	BITMAPINFOHEADER* getOutputFormat()
	{
		return (BITMAPINFOHEADER*)m_biFormatOut;
	}

private:
	COMPVARS m_compvars;
	bool m_compressing;
	LPVOID m_frameData;
	LONG m_frameSize;
	BitmapInfoHeader m_biFormatOut;
};

bool Compressor::init(BITMAPINFOHEADER* biFormatIn)
{
	m_compressing = false;
	m_compvars.cbSize = sizeof(m_compvars);

	// Choose compressor
	if (!ICCompressorChoose(GetDesktopWindow(), 0, biFormatIn, NULL, &m_compvars, NULL))
	{
		throw std::runtime_error("Compressor selection was canceled!\n");
	}

	if (!m_compvars.hic)
	{
		return false;
	}
	else
	{
		ICINFO icinfo = {};
		icinfo.dwSize = sizeof(icinfo);
		ICGetInfo(m_compvars.hic, &icinfo, sizeof(icinfo));
		wprintf(L"INFO: Compressor          : '%ls' - '%ls'\n", icinfo.szName, icinfo.szDescription);

		// Initialize compressor
		BOOL res = ICSeqCompressFrameStart(&m_compvars, (LPBITMAPINFO) biFormatIn);
		if (!res)
		{
			throw std::runtime_error("ERROR: ICSeqCompressFrameStart() failed\n");
		}

		m_compressing = true;
		m_biFormatOut = (BITMAPINFOHEADER*) m_compvars.lpbiOut;
	}

	return true;
}

void Compressor::compressFrame(const void* data)
{
	BOOL fKey = 1;
	m_frameSize = ((BITMAPINFOHEADER*) m_compvars.lpbiIn)->biSizeImage;
	m_frameData = ICSeqCompressFrame(&m_compvars, 0, (LPVOID) data, &fKey, &m_frameSize);
	//printf("Key: %2d, Size: %ld, Size Out: %ld\n", fKey, m_frameSize, ((BITMAPINFOHEADER*) m_compvars.lpbiOut)->biSizeImage);
}

/////////////////////////////////////
class CodecBench
{
public:
	void init(int argc, char* argv[]);

	void run();

private:
	void initArguments(int argc, char* argv[]);

	void initInput();

	void initOutput();

	bool         m_rawin, m_rawout, m_decompress, m_compress;
	const char  *m_infile, *m_outfile, *m_decompFormat;
	int          m_decompWidth, m_decompHeight, m_framesToProcess, m_loopCount;
	VideoReader  m_videoReader;
	VideoWriter  m_videoWriter;
	Decompressor m_decompressor;
	Compressor   m_compressor;
	BitmapInfoHeader m_formatDecompressed;
	BitmapInfoHeader m_formatCompressed;

	// ---

	static bool s_stop;

	static void sighandler(int signo)
	{
		s_stop = true;
	}
};

bool CodecBench::s_stop = false;

void CodecBench::init(int argc, char* argv[])
{
	signal(SIGINT, sighandler);

	initArguments(argc, argv);
	initInput();
	initOutput();
}

void CodecBench::initArguments(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Usage: %s -i <infile> -o [outfile] [options]\n", argv[0]);
		printf("Options:\n");
		printf("  -i [infile]  Input file. Required.\n");
		printf("  -o [outfile] Output file. Optional. If not given, the output is discarded.\n");
		printf("  -nd          Do not decompress input (send read input directly to compressor).\n");
		printf("  -nc          Do not compress. Useful for benchmarking a decoder.\n");
		printf("  -rawin       Input is raw. -nd is turned on automatically. -f, -w and -h must be specified.\n");
		printf("  -rawout      Output is raw. -nc is turned on automatically.\n");
		printf("  -f [format]  Request the decompressor to decode <infile> as [format]. Valid formats are:\n");
		printf("               RGB24 (bgr24), RGB32 (bgr32), BGRA, AYUV, YUY2, UYVY, YV12, YV24, Y8, b64a, b48r, v210, r210\n");
		printf("               If not given, the decompressor specifies the format.\n");
		printf("               For -rawin: specifies raw video format.\n");
		printf("  -w [width]   Request the decompressor to decode <infile> to given width.\n");
		printf("               If not given, the decompressor specifies the width.\n");
		printf("               For -rawin: specifies raw video width.\n");
		printf("  -h [height]  Request the decompressor to decode <infile> to given height.\n");
		printf("               Negative value is possible, which will request top-to-bottom RGB (RGB only).\n");
		printf("               If not given, the decompressor specifies the height.\n");
		printf("               For -rawin: specifies raw video height.\n");
		printf("  -frames [n]  Process only the first [n] frames (0: all).\n");
		printf("  -loop [n]    Loop the process [n] times (default: 1).\n");
		throw std::runtime_error("");
	}

	// Parse command line
	ArgvParser parser(argc, argv);
	m_rawin           = parser.hasArg("-rawin");
	m_rawout          = parser.hasArg("-rawout");
	m_decompress      = m_rawin  ? false : !parser.hasArg("-nd");
	m_compress        = m_rawout ? false : !parser.hasArg("-nc");
	m_decompFormat    = parser.getArg("-f", NULL);
	m_decompWidth     = atoi(parser.getArg("-w", "0"));
	m_decompHeight    = atoi(parser.getArg("-h", "0"));
	m_framesToProcess = atoi(parser.getArg("-frames", "0"));
	m_loopCount       = atoi(parser.getArg("-loop", "1"));
	m_infile          = parser.getArg("-i", NULL);
	m_outfile         = parser.getArg("-o", NULL);

	// Verify arguments
	if (!m_infile)
	{
		throw std::runtime_error("ERROR: No input file given (-i)!\n");
	}

	if (m_rawin) // raw input: format must be given
	{
		if (!m_decompFormat || !m_decompWidth || !m_decompHeight)
		{
			throw std::runtime_error("ERROR: -f, -w, -h must be given for raw inputs\n");
		}
	}
	else if (!m_decompress) // non-raw no-decompress: take format from file
	{
		if (m_decompFormat)
		{
			printf("WARNING: ignoring -f option because -nd option was given\n");
			m_decompFormat = NULL;
		}
		if (m_decompWidth)
		{
			printf("WARNING: ignoring -w option because -nd option was given\n");
			m_decompWidth = 0;
		}
		if (m_decompHeight)
		{
			printf("WARNING: ignoring -h option because -nd option was given\n");
			m_decompHeight = 0;
		}
	}
}

void CodecBench::initInput()
{
	// Open input video stream
	if (m_rawin)
	{
		m_videoReader.openRaw(m_infile, m_decompFormat, m_decompWidth, m_decompHeight);
	}
	else
	{
		m_videoReader.open(m_infile);
	}
	printf("INFO: Input file          : %s%s\n", m_rawin ? "[RAW] " : "", m_infile);

	printf("INFO: Input format        : ");
	PrintBitmapInfo(m_videoReader.getFormat());
	printf("\n");

	// Prepare decompressor if needed
	if (m_decompress)
	{
		BITMAPINFOHEADER biFormatDecomp = {};
		if (m_decompFormat)
		{
			GetDecompFormat(m_decompFormat, m_videoReader.getFormat()->biWidth, m_videoReader.getFormat()->biHeight, &biFormatDecomp);
		}
		m_decompressor.init(m_videoReader.getFormat(), m_decompFormat ? &biFormatDecomp : NULL, m_decompWidth, m_decompHeight);
		m_formatDecompressed = m_decompressor.getOutputFormat();

		printf("INFO: Decompressed format : ");
		PrintBitmapInfo((BITMAPINFOHEADER*)m_formatDecompressed);
		printf("\n");
	}
	else
	{
		printf("INFO: Decompressor        : -\n");
		m_formatDecompressed = m_videoReader.getFormat();
	}
}

void CodecBench::initOutput()
{
	// Prepare compressor if needed
	if (m_compress)
	{
		m_compress = m_compressor.init(m_formatDecompressed);
		if (m_compress)
		{
			m_formatCompressed = m_compressor.getOutputFormat();
		}
	}

	if (!m_compress)
	{
		printf("INFO: Compressor          : -\n");
		m_formatCompressed = m_formatDecompressed;
	}

	printf("INFO: Output format       : ");
	PrintBitmapInfo((BITMAPINFOHEADER*)m_formatCompressed);
	printf("\n");

	// Initialize output file if needed
	if (m_outfile)
	{
		// Open file
		m_videoWriter.open(m_outfile, m_rawout ? NULL : (BITMAPINFOHEADER*)m_formatCompressed);
	}
	printf("INFO: Output file         : %s%s\n", m_outfile && m_rawout ? "[RAW] " : "", m_outfile ? m_outfile : "-");
}

void CodecBench::run()
{
	Timer decompTimer, compTimer;
	int numFrames = 0, currentFrameNum = 0;
	uint64_t sumInputSize = 0LL, sumOutputSize = 0LL, sumRawSize = 0LL;

	printf("\n");
	int loop = 0, ncharsPrev = 0;
	while (!s_stop && loop < m_loopCount)
	{
		if (!m_videoReader.readFrame() || (m_framesToProcess && currentFrameNum >= m_framesToProcess))
		{
			currentFrameNum = 0;
			++loop;
			if (loop < m_loopCount)
				m_videoReader.rewind();
			continue;
		}

		++currentFrameNum;
		++numFrames;
		char* currData = m_videoReader.frameData();
		uint32_t currDataSize = m_videoReader.frameSize();

		sumInputSize += currDataSize;

		// Decompress if needed
		if (m_decompress)
		{
			decompTimer.begin();
			m_decompressor.decompressFrame(currData, currDataSize);
			decompTimer.end();
			currData = m_decompressor.frameData();
			currDataSize = m_decompressor.getOutputFormat()->biSizeImage;
		}

		sumRawSize += currDataSize;

		// Compress if needed
		if (m_compress)
		{
			compTimer.begin();
			m_compressor.compressFrame(currData);
			compTimer.end();
			currData = m_compressor.frameData();
			currDataSize = m_compressor.frameSize();
		}

		sumOutputSize += currDataSize;

		// Write output if needed
		if (m_outfile)
		{
			m_videoWriter.writeFrame(currData, currDataSize);
		}

		int nchars = 0;
		nchars += printf("\rF: %d", numFrames);
		if (m_decompress)
		{
			double decompFPS   = 1000000.0 * numFrames / decompTimer.sumTimeUs();
			double decompMiBps = 1000000.0 * sumRawSize / 1024.0 / 1024.0 / decompTimer.sumTimeUs();
			double decompRatio = (double) sumRawSize / sumInputSize;
			nchars += printf(" | Decompress: %.1f fps (%.1f MiB/s) (ratio: %.2f)", decompFPS, decompMiBps, decompRatio);
		}
		if (m_compress)
		{
			double compFPS     = 1000000.0 * numFrames / compTimer.sumTimeUs();
			double compMiBps   = 1000000.0 * sumRawSize / 1024.0 / 1024.0 / compTimer.sumTimeUs();
			double compRatio   = (double) sumRawSize / sumOutputSize;
			nchars += printf(" | Compress: %.1f fps (%.1f MiB/s) (ratio: %.2f)", compFPS, compMiBps, compRatio);
		}
		int padding = ncharsPrev - nchars;
		if (padding > 0) printf("%*s", padding, "");
		fflush(stdout);
		ncharsPrev = nchars;
	}
	printf("\n");
}

/////////////////////////////////////
int main(int argc, char* argv[])
{
	try
	{
		CodecBench cb;
		cb.init(argc, argv);
		cb.run();
	}
	catch (std::exception& e)
	{
		printf("%s\n", e.what());
		return 1;
	}

	return 0;
}
