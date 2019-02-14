#include "cryptonight.hpp"
#include <Windows.h>

#define FPGA_ERROR_SET 1
#define FPGA_DEBUG_SET 1

#ifdef FPGA_ERROR_SET
#define FPGA_ERROR(format, ...) printer::inst()->print_msg(L0, format, __VA_ARGS__)
#else //FPGA_ERROR_SET
#endif //FPGA_ERROR_SET

#ifdef FPGA_DEBUG_SET
#define FPGA_DEBUG(level, format, ...) if(level <= FPGA_DEBUG_SET) printer::inst()->print_msg(L1, format, __VA_ARGS__)
#define FPGA_DEBUG_CTX(level, ctx) if(level <= FPGA_DEBUG_SET) FPGA_DEBUG(level, "%s: ctx p: %d t: %d h: %p", __FUNCTION__, (ctx).device_com_port, (ctx).device_threads, (ctx).device_com_handle)
#define FPGA_DEBUG_BYTE_ARR(level, str, size, arr) \
	if(level <= FPGA_DEBUG_SET) { \
		char buf[1024]; \
		size_t bpos; \
		snprintf(buf, sizeof(buf), "%s: %s ", __FUNCTION__, str); \
		bpos = strlen(buf); \
		for (size_t i = 0; i < (size); ++i) \
		{ \
			snprintf(buf + bpos, sizeof(buf) - bpos, "%02X", (unsigned int)(arr)[i]); \
			bpos += 2; \
		} \
		FPGA_DEBUG(level, "%s", buf); \
	}

#define FPGA_DEBUG_TIMER_RESULT(level, res, fcn, ...) \
	if(level <= FPGA_DEBUG_SET) { \
		LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds; \
		LARGE_INTEGER Frequency; \
		QueryPerformanceFrequency(&Frequency); \
		QueryPerformanceCounter(&StartingTime); \
		res = fcn(__VA_ARGS__); \
		QueryPerformanceCounter(&EndingTime); \
		ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart; \
		ElapsedMicroseconds.QuadPart *= 1000000; \
		ElapsedMicroseconds.QuadPart /= Frequency.QuadPart; \
		FPGA_DEBUG(level, "%s: Took %I64ius to complete.", #fcn, ElapsedMicroseconds.QuadPart); \
    } \
	else { \
		res = fcn(__VA_ARGS__); \
	}
#define FPGA_DEBUG_MSG_HEADER(level, msg) if(level <= FPGA_DEBUG_SET) FPGA_DEBUG(level, "%s: msg header id: %d cnt: %d sz: %d", __FUNCTION__, ((MSG_HEADER*)(msg))->message, ((MSG_HEADER*)(msg))->counter, ((MSG_HEADER*)(msg))->size)
#else //FPGA_DEBUG_SET
#define FPGA_DEBUG(format, ...)
#define FPGA_DEBUG_CTX(ctx)
#define FPGA_DEBUG_BYTE_ARR(str, size, arr)
#define FPGA_DEBUG_TIMER_RESULT(res, fcn, ...) res = fcn(__VA_ARGS__);
#define FPGA_DEBUG_MSG_HEADER(msg)
#endif //FPGA_DEBUG_SET

#define FPGA_MAX_COM_NAME_LEN 32
#define FPGA_HASH_DATA_AREA 136
#define FPGA_TEMP_DATA_AREA 144
#define FPGA_MAX_OUTPUT_MSG_SIZE 64

#pragma pack(push, 1)
typedef struct {
	BYTE header[4];
	BYTE message;
	BYTE counter;
	WORD size;
} MSG_HEADER;

typedef struct {
	MSG_HEADER header;
	uint64_t state[25];
	uint64_t workTarget;
	BYTE tail[4];
} MSG_IN_01;

typedef struct {
	MSG_HEADER header;
	uint8_t reset;
	BYTE tail[4];
} MSG_IN_03;

typedef struct {
	MSG_HEADER header;
	BYTE tail[4];
} MSG_IN_13;

typedef struct {
	MSG_HEADER header;
	uint32_t nonce;
	uint8_t hash[32];
	BYTE tail[4];
} MSG_OUT_11;

typedef struct {
	MSG_HEADER header;
	uint8_t reset;
	BYTE tail[4];
} MSG_OUT_13;
#pragma pack(pop)

uint32_t _swap_bytes32(uint32_t val);
uint64_t _swap_bytes64(uint64_t val);

void _init_message_in_01(MSG_IN_01* pMsg, BYTE counter, uint64_t state[25], uint64_t workTarget);
void _init_message_in_03(MSG_IN_03* pMsg, BYTE counter, bool bReset);
void _init_message_in_13(MSG_IN_13* pMsg, BYTE counter);

fpga_error _send_message(HANDLE hComPort, DWORD size, BYTE* msg);
fpga_error _get_message(HANDLE hComPort, size_t* size, BYTE* msg, uint32_t timeout);
fpga_error _reset(fpga_ctx *ctx, bool bReset);
fpga_error _get_reset(fpga_ctx *ctx, bool* pReset);
