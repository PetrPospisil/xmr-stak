#include "cryptonight.hpp"
#include "xmrstak/misc/console.hpp"
#include <vector>
#include <string.h>

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

extern "C" {
	uint32_t _swap_bytes32(uint32_t val)
	{
		return ((((val) & 0xff000000ull) >> 24) |
			(((val) & 0x00ff0000ull) >> 8) |
			(((val) & 0x0000ff00ull) << 8) |
			(((val) & 0x000000ffull) << 24));
	}

	uint64_t _swap_bytes64(uint64_t val)
	{
		return ((((val) & 0xff00000000000000ull) >> 56) |
			(((val) & 0x00ff000000000000ull) >> 40) |
			(((val) & 0x0000ff0000000000ull) >> 24) |
			(((val) & 0x000000ff00000000ull) >> 8) |
			(((val) & 0x00000000ff000000ull) << 8) |
			(((val) & 0x0000000000ff0000ull) << 24) |
			(((val) & 0x000000000000ff00ull) << 40) |
			(((val) & 0x00000000000000ffull) << 56));
	}

	BYTE lpHeader[] =
	{
		//HEADER: deadbeef
		0xde, 0xad, 0xbe, 0xef
	};

	BYTE lpTail[] =
	{
		//TAIL: b16b00b5
		0xb1, 0x6b, 0x00, 0xb5
	};
	
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
		uint32_t nonce;
		uint8_t hash[32];
		BYTE tail[4];
	} MSG_OUT_11;
	#pragma pack(pop)

	void _init_message_in_01(MSG_IN_01* pMsg, BYTE counter, uint64_t state[25], uint64_t workTarget)
	{
		pMsg->header.header[0] = lpHeader[0];
		pMsg->header.header[1] = lpHeader[1];
		pMsg->header.header[2] = lpHeader[2];
		pMsg->header.header[3] = lpHeader[3];

		pMsg->header.message = 0x01;
		pMsg->header.counter = counter;
		pMsg->header.size = sizeof(pMsg->state) + sizeof(pMsg->workTarget);
		memcpy(pMsg->state, state, sizeof(pMsg->state));
		pMsg->workTarget = workTarget;

		pMsg->tail[0] = lpTail[0];
		pMsg->tail[1] = lpTail[1];
		pMsg->tail[2] = lpTail[2];
		pMsg->tail[3] = lpTail[3];
	}

	void _init_message_in_03(MSG_IN_03* pMsg, BYTE counter, bool bReset)
	{
		pMsg->header.header[0] = lpHeader[0];
		pMsg->header.header[1] = lpHeader[1];
		pMsg->header.header[2] = lpHeader[2];
		pMsg->header.header[3] = lpHeader[3];

		pMsg->header.message = 0x03;
		pMsg->header.counter = counter;
		pMsg->header.size = 1;

		pMsg->reset = (uint8_t)bReset;

		pMsg->tail[0] = lpTail[0];
		pMsg->tail[1] = lpTail[1];
		pMsg->tail[2] = lpTail[2];
		pMsg->tail[3] = lpTail[3];
	}

	fpga_error _send_message(HANDLE hComPort, DWORD size, BYTE* msg)
	{
		FPGA_DEBUG(20, "%s: h: 0x%p", __FUNCTION__, hComPort);
		FPGA_DEBUG_BYTE_ARR(1, "msg:", size, msg);

		if (hComPort == NULL)
		{
			FPGA_ERROR("%s: hComPort null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		DWORD  dNoOFBytestoWrite;              // No of bytes to write into the port
		DWORD  dNoOfBytesWritten = 0;          // No of bytes written to the port

		dNoOFBytestoWrite = size; // Calculating the no of bytes to write into the port

		BOOL Status;
		Status = WriteFile(
			hComPort,            // Handle to the Serialport
			msg,           // Data to be written to the port 
			dNoOFBytestoWrite,   // No of bytes to write into the port
			&dNoOfBytesWritten,  // No of bytes written to the port
			NULL);

		if (Status == FALSE)
		{
			FPGA_ERROR("%s: Writing to Serial Port failed with last error: %u.", __FUNCTION__, GetLastError());
			return fpga_e_PortWriteFailed;
		}

		FPGA_DEBUG(20, "%s: OK", __FUNCTION__);
		return fpga_OK;
	}

	/** get device count
	 *
	 * @param deviceComPort[in] device COM port
	 * @param ctx[out] device context
	 * @return fpga_error
	 */
	fpga_error fpga_get_deviceinfo(int deviceComPort, fpga_ctx *ctx)
	{
		FPGA_DEBUG(1, "%s: p: %d", __FUNCTION__, deviceComPort);

		if (ctx == NULL)
		{
			FPGA_ERROR("%s: ctx null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		char strPort[FPGA_MAX_COM_NAME_LEN] = { 0 };
		snprintf(strPort, sizeof(strPort)-1, "COM%d", deviceComPort);

		DWORD dwSize = 0;
		LPCOMMCONFIG lpCC = (LPCOMMCONFIG) new BYTE[1];
		BOOL ret = GetDefaultCommConfigA(strPort, lpCC, &dwSize);
		if (!dwSize)
		{
			FPGA_ERROR("%s: GetDefaultCommConfig failed with last error: %u. Will try UNC variant.", __FUNCTION__, GetLastError());
			snprintf(strPort, sizeof(strPort) - 1, "\\\\.\\COM%d", deviceComPort);

			ret = GetDefaultCommConfigA(strPort, lpCC, &dwSize);
			if (!dwSize)
			{
				FPGA_ERROR("%s: GetDefaultCommConfig failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortNotConnected;
			}
		}
		delete[] lpCC;

		lpCC = (LPCOMMCONFIG) new BYTE[dwSize];
		ret = GetDefaultCommConfigA(strPort, lpCC, &dwSize);
		delete[] lpCC;

		if (!ret)
		{
			FPGA_ERROR("%s: GetDefaultCommConfig %u failed with last error: %u.", __FUNCTION__, dwSize, GetLastError());
			return fpga_e_PortNotConnected;
		}

		ctx->device_com_port = deviceComPort;
		ctx->device_threads = -1;
		ctx->device_com_handle = INVALID_HANDLE_VALUE;
		ctx->msg_counter = 0;

		FPGA_DEBUG_CTX(1, *ctx);
		FPGA_DEBUG(1, "%s: OK", __FUNCTION__);
		return fpga_OK;
	}

	fpga_error cryptonight_fpga_open(fpga_ctx *ctx)
	{
		FPGA_DEBUG(1, "%s", __FUNCTION__);

		if (ctx == NULL)
		{
			FPGA_ERROR("%s: ctx null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		FPGA_DEBUG_CTX(1, *ctx);

		char strPort[FPGA_MAX_COM_NAME_LEN] = { 0 };
		snprintf(strPort, sizeof(strPort) - 1, "\\\\.\\COM%d", ctx->device_com_port);

		ctx->device_com_handle = CreateFileA(
			strPort,
			GENERIC_READ | GENERIC_WRITE, // Read/Write Access
			0,                            // No Sharing, ports cant be shared
			NULL,                         // No Security
			OPEN_EXISTING,                // Open existing port only
			0,                            // Non Overlapped I/O
			NULL);                        // Null for Comm Devices

		if (ctx->device_com_handle == INVALID_HANDLE_VALUE)
		{
			FPGA_ERROR("%s: CreateFileA failed with last error: %u.", __FUNCTION__, GetLastError());
			return fpga_e_UnableToOpenPort;
		}

		DCB dcbSerialParams = { 0 };                         // Initializing DCB structure
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

		BOOL Status = GetCommState(ctx->device_com_handle, &dcbSerialParams);      //retreives  the current settings
		if (Status == FALSE) 
		{
			FPGA_ERROR("%s: GetCommState failed with last error: %u.", __FUNCTION__, GetLastError());
			CloseHandle(ctx->device_com_handle);
			ctx->device_com_handle = INVALID_HANDLE_VALUE;
			return fpga_e_UnableToGetCommState;
		}

		dcbSerialParams.BaudRate = 460800;		// Setting BaudRate = 9600
		dcbSerialParams.ByteSize = 8;			// Setting ByteSize = 8
		dcbSerialParams.StopBits = ONESTOPBIT;	// Setting StopBits = 1
		dcbSerialParams.Parity = NOPARITY;		// Setting Parity = None 

		Status = SetCommState(ctx->device_com_handle, &dcbSerialParams);  //Configuring the port according to settings in DCB 
		if (Status == FALSE)
		{
			FPGA_ERROR("%s: SetCommState failed with last error: %u.", __FUNCTION__, GetLastError());
			CloseHandle(ctx->device_com_handle);
			ctx->device_com_handle = INVALID_HANDLE_VALUE;
			return fpga_e_UnableToSetCommState;
		}

		COMMTIMEOUTS timeouts = { 0 };
		/*Status = GetCommTimeouts(ctx->device_com_handle, &timeouts);
		if (Status == FALSE)
		{
			FPGA_ERROR("%s: GetCommTimeouts failed with last error: %u.", __FUNCTION__, GetLastError());
			CloseHandle(ctx->device_com_handle);
			ctx->device_com_handle = INVALID_HANDLE_VALUE;
			return fpga_e_UnableToSetCommTimeouts;
		}*/

		timeouts.ReadIntervalTimeout = 100;
		timeouts.ReadTotalTimeoutConstant = 100;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		timeouts.WriteTotalTimeoutConstant = 0;
		timeouts.WriteTotalTimeoutMultiplier = 0;

		Status = SetCommTimeouts(ctx->device_com_handle, &timeouts);
		if (Status == FALSE)
		{
			FPGA_ERROR("%s: SetCommTimeouts failed with last error: %u.", __FUNCTION__, GetLastError());
			CloseHandle(ctx->device_com_handle);
			ctx->device_com_handle = INVALID_HANDLE_VALUE;
			return fpga_e_UnableToSetCommTimeouts;
		}

		FPGA_DEBUG(1, "%s: GetCommTimeouts set: r: it: %u ttc: %u ttm: %u w: ttc: %u ttm: %u", __FUNCTION__,
			timeouts.ReadIntervalTimeout,
			timeouts.ReadTotalTimeoutConstant,
			timeouts.ReadTotalTimeoutMultiplier,
			timeouts.WriteTotalTimeoutConstant,
			timeouts.WriteTotalTimeoutMultiplier
		);

		Status = SetCommMask(ctx->device_com_handle, EV_RXCHAR); //Configure Windows to Monitor the serial device for Character Reception
		if (Status == FALSE)
		{
			FPGA_ERROR("%s: SetCommMask failed with last error: %u.", __FUNCTION__, GetLastError());
			CloseHandle(ctx->device_com_handle);
			ctx->device_com_handle = INVALID_HANDLE_VALUE;
			return fpga_e_UnableToSetCommMask;
		}

		ctx->msg_counter = (BYTE)(time(NULL) % 256);

		FPGA_DEBUG(1, "%s: h: 0x%p OK", __FUNCTION__, ctx->device_com_handle);
		return fpga_OK;
	}

	fpga_error _reset(fpga_ctx *ctx, bool bReset)
	{
		FPGA_DEBUG(20, "%s r: %u", __FUNCTION__, (uint32_t)bReset);
		if (ctx == NULL)
		{
			FPGA_ERROR("%s: ctx null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		MSG_IN_03 msg;
		_init_message_in_03(&msg, ++ctx->msg_counter, bReset);

		fpga_error res;
		FPGA_DEBUG_TIMER_RESULT(2, res, _send_message, ctx->device_com_handle, (DWORD)sizeof(msg), (BYTE*)&msg);
		if (FPGA_FAILED(res))
		{
			FPGA_ERROR("%s: _send_message failed with last error: %d.", __FUNCTION__, res);
			return res;
		}

		return fpga_OK;
	}

	fpga_error cryptonight_fpga_set_data(fpga_ctx *ctx, xmrstak_algo algo, const void* input, size_t len, uint64_t workTarget)
	{
		FPGA_DEBUG(1, "%s: algorithm: %u", __FUNCTION__, algo);
		if (ctx == NULL)
		{
			FPGA_ERROR("%s: ctx null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (input == NULL)
		{
			FPGA_ERROR("%s: input null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (len == 0)
		{
			FPGA_ERROR("%s: len zero", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (len + 1 >= FPGA_TEMP_DATA_AREA)
		{
			FPGA_ERROR("%s: data too large %u >= %u", __FUNCTION__, (DWORD)len + 1, FPGA_TEMP_DATA_AREA);
			return fpga_e_InvalidArgument;
		}

		fpga_error res;

		//reset FPGA

		//deactivate
		FPGA_DEBUG_TIMER_RESULT(1, res, _reset, ctx, true);
		if (FPGA_FAILED(res))
		{
			FPGA_ERROR("%s: _reset true failed with last error: %d.", __FUNCTION__, res);
			return res;
		}

		//activate
		FPGA_DEBUG_TIMER_RESULT(1, res, _reset, ctx, false);
		if (FPGA_FAILED(res))
		{
			FPGA_ERROR("%s: _reset false failed with last error: %d.", __FUNCTION__, res);
			return res;
		}

		/*BYTE a[] = { 
			0x07, 0x07, 0xb8, 0xae, 0xb6, 0xda, 0x05, 0xd3, 0x57, 0x79, 0x5f, 0x7a, 0x28, 0xbd, 0x2a, 0x26, 0x85, 0x1c, 0x60, 0xe6, 0xf1, 0x47, 
			0x70, 0xcb, 0xbe, 0xa3, 0xb1, 0xfb, 0x4f, 0x63, 0x2c, 0xc8, 0x7f, 0xc7, 0x77, 0xa9, 0x5d, 0xa3, 0xde, 0x1a, 0x02, 0x00, 0xd7, 0x11, 
			0x4c, 0x84, 0xc4, 0x0b, 0x07, 0xcd, 0x40, 0xfc, 0xa7, 0x08, 0x38, 0x34, 0x15, 0xaa, 0xeb, 0xd3, 0x3f, 0xa2, 0x74, 0xb3, 0xa1, 0xcc, 
			0x99, 0x15, 0xb4, 0x25, 0x4d, 0xb6, 0x92, 0x1f, 0x3f, 0x11 
			};

		input = a;
		workTarget = 0xFF00000000000000;*/

		/*BYTE a[] = {
			 0x00, 0xce, 0xef, 0x08, 0x5b, 0x67, 0x24, 0x17, 0x3f, 0x1d, 0xc5, 0x52, 0x98, 0xaf, 0x32, 0xb8, 0x56, 0xbd, 0x0d, 0xd0, 0x3d, 0xe6, 
			 0x70, 0x61, 0x55, 0x1f, 0x91, 0xd9, 0x95, 0xd3, 0xa3, 0x7b, 0xe2, 0x80, 0xc2, 0xb2, 0x67, 0x1e, 0x96, 0xC8, 0x01, 0x00, 0x00, 0x00, 
			 0x00, 0x09, 0x11, 0xff, 0xd3, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xec, 0x48, 0xe0, 0x9a, 0x72, 0x26, 0xce, 0x90, 0xf3, 0x23, 
			 0xf0, 0x1d, 0xa0, 0xcf, 0x06, 0x0e, 0xaa, 0x84, 0x09, 0xcc
			};

		input = a;
		workTarget = 0x000d1b71758e2196;*/

		FPGA_DEBUG(1, "%s: input size: %u.", __FUNCTION__, (DWORD)len);
		FPGA_DEBUG_BYTE_ARR(1, "input data:", len, (BYTE*)input);
		FPGA_DEBUG(1, "%s: input worktarget: 0x%I64x.", __FUNCTION__, workTarget);

		uint64_t st[25] = {};
		uint8_t temp[FPGA_TEMP_DATA_AREA];

		memcpy(temp, input, len);
		temp[len] = 0x01;
		memset(temp + len + 1, 0, FPGA_HASH_DATA_AREA - len - 1);
		temp[FPGA_HASH_DATA_AREA - 1] |= 0x80;

		for (size_t i = 0; i < FPGA_HASH_DATA_AREA / sizeof(uint64_t); i++)
			st[i] ^= ((uint64_t *)temp)[i];

		for (size_t i = 0; i < sizeof(st) / sizeof(uint64_t); ++i)
			st[i] = _swap_bytes64(st[i]);

		MSG_IN_01 msg;
		_init_message_in_01(&msg, ++ctx->msg_counter, st, _swap_bytes64(workTarget));

		FPGA_DEBUG_TIMER_RESULT(2, res, _send_message, ctx->device_com_handle, (DWORD)sizeof(msg), (BYTE*)&msg);
		if (FPGA_FAILED(res))
		{
			FPGA_ERROR("%s: _send_message failed with last error: %d.", __FUNCTION__, res);
			return res;
		}

		FPGA_DEBUG(1, "%s: OK", __FUNCTION__);
		return res;
	}

	fpga_error _get_message(HANDLE hComPort, size_t* size, BYTE* msg, uint32_t timeout)
	{
		FPGA_DEBUG(20, "%s: h: 0x%p max size: %u timeout: %u", __FUNCTION__, hComPort, (DWORD)*size, timeout);
		if (hComPort == NULL)
		{
			FPGA_ERROR("%s: hComPort null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (size == NULL)
		{
			FPGA_ERROR("%s: size null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (*size == 0)
		{
			FPGA_ERROR("%s: size 0", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		if (msg == 0)
		{
			FPGA_ERROR("%s: msg null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		BOOL Status; // Status of the various operations 
		size_t dwMessageIndex;

		DWORD dwStartTimestamp = GetTickCount();
		while (1)
		{
			DWORD NoBytesRead;
			dwMessageIndex = 0;

			DWORD dwCurrentTimestamp = GetTickCount();
			if (dwCurrentTimestamp - dwStartTimestamp > timeout)
			{
				//timeout has passed
				FPGA_DEBUG(20, "%s: timeout %u.", __FUNCTION__, timeout);
				return fpga_e_Timeout;
			}

			//read header
			DWORD dwHeaderIndex = 0;
			bool bSkip = false;
			while (1)
			{
				BYTE  TempChar;

				Status = ReadFile(hComPort, &TempChar, sizeof(TempChar), &NoBytesRead, NULL);
				if (Status == FALSE)
				{
					FPGA_ERROR("%s: ReadFile header failed with last error: %u.", __FUNCTION__, GetLastError());
					return fpga_e_PortReadFailed;
				}

				//fpga did not return any result
				if (NoBytesRead != sizeof(TempChar))
				{
					//we w8 1ms
					Sleep(1);
					bSkip = true;
					break;
				}

				//fpga data do not match header...
				if (TempChar != lpHeader[dwHeaderIndex])
				{
					FPGA_ERROR("%s: Warning! Header mismatch... skipped data %02x[%u] != %02x.", __FUNCTION__, lpHeader[dwHeaderIndex], dwHeaderIndex, TempChar);
					bSkip = true;
					break;
				}

				dwHeaderIndex++;
				msg[dwMessageIndex++] = TempChar;
				if (dwHeaderIndex == (DWORD)sizeof(lpHeader))
					break;
			}

			if (bSkip)
				continue;

			//read MSG command
			Status = ReadFile(hComPort, msg + dwMessageIndex, sizeof(BYTE), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(BYTE))
			{
				FPGA_ERROR("%s: ReadFile msg id failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortReadFailed;
			}

			BYTE* pMsgCommand = (BYTE*)(msg + dwMessageIndex);
			dwMessageIndex += sizeof(BYTE);

			//read MSG counter
			Status = ReadFile(hComPort, msg + dwMessageIndex, sizeof(BYTE), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(BYTE))
			{
				FPGA_ERROR("%s: ReadFile msg counter failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortReadFailed;
			}

			BYTE* pMsgCounter = (BYTE*)(msg + dwMessageIndex);
			dwMessageIndex += sizeof(BYTE);

			//read MSG size
			Status = ReadFile(hComPort, msg + dwMessageIndex, sizeof(WORD), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(WORD))
			{
				FPGA_ERROR("%s: ReadFile msg size failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortReadFailed;
			}

			WORD* pMsgSize = (WORD*)(msg + dwMessageIndex);
			dwMessageIndex += sizeof(WORD);

			if (*pMsgSize >= *size - dwMessageIndex - sizeof(lpTail))
			{
				FPGA_ERROR("%s: Message too big %u.", __FUNCTION__, *pMsgSize);
				return fpga_e_BufferTooSmall;
			}

			//read data
			Status = ReadFile(hComPort, msg + dwMessageIndex, *pMsgSize, &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != *pMsgSize)
			{
				FPGA_ERROR("%s: ReadFile msg failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortReadFailed;
			}

			dwMessageIndex += *pMsgSize;

			//read tail
			Status = ReadFile(hComPort, msg + dwMessageIndex, sizeof(lpTail), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(lpTail))
			{
				FPGA_ERROR("%s: ReadFile tail failed with last error: %u.", __FUNCTION__, GetLastError());
				return fpga_e_PortReadFailed;
			}

			if (memcmp(msg + dwMessageIndex, lpTail, sizeof(lpTail)) != 0)
			{
				FPGA_ERROR("%s: Warning! Tail mismatch... skipped data", __FUNCTION__);
				continue;
			}

			dwMessageIndex += sizeof(lpTail);

			break;
		}

		*size = dwMessageIndex;
		FPGA_DEBUG_BYTE_ARR(1, "msg:", *size, msg);
		FPGA_DEBUG_MSG_HEADER(2, msg);
		FPGA_DEBUG(20, "%s: OK", __FUNCTION__);
		return fpga_OK;
	}

	fpga_error cryptonight_fpga_hash(fpga_ctx *ctx, uint32_t* piNonce, uint8_t hash[32])
	{
		FPGA_DEBUG(20, "%s", __FUNCTION__);
		if (ctx == NULL)
		{
			FPGA_ERROR("%s: ctx null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}
		if (piNonce == NULL)
		{
			FPGA_ERROR("%s: piNonce null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}
		if (hash == NULL)
		{
			FPGA_ERROR("%s: hash null", __FUNCTION__);
			return fpga_e_InvalidArgument;
		}

		BYTE msg[FPGA_MAX_OUTPUT_MSG_SIZE];
		while (1)
		{
			fpga_error res;
			size_t size = sizeof(msg);
			FPGA_DEBUG_TIMER_RESULT(2, res, _get_message, ctx->device_com_handle, &size, msg, 1000);
			if (res == fpga_e_Timeout)
			{
				FPGA_DEBUG(20, "%s: Waited for ~1s. No output yet.", __FUNCTION__);
				return res;
			}
			else if (FPGA_FAILED(res))
			{
				FPGA_ERROR("%s: _get_message failed with error: %d.", __FUNCTION__, res);
				return res;
			}

			MSG_HEADER* pHeader = (MSG_HEADER*)msg;
			if (pHeader->message != 17)
			{
				FPGA_ERROR("%s: Warning! Msg id %u != 17 mismatch... skipped", __FUNCTION__, pHeader->message);
				continue;
			}

			if (pHeader->counter != ctx->msg_counter)
			{
				FPGA_ERROR("%s: Warning! Msg counter 0x%X != 0x%X ctx mismatch... skipped", __FUNCTION__, pHeader->counter, ctx->msg_counter);
				continue;
			}

			if (pHeader->size != 36)
			{
				FPGA_ERROR("%s: Warning! Msg size %u != 36 mismatch... skipped", __FUNCTION__, pHeader->size);
				continue;
			}

			break;
		}

		MSG_OUT_11* pMessage = (MSG_OUT_11*)msg;
		
		//copy hash
		memcpy(hash, pMessage->hash, sizeof(pMessage->hash));

		//copy nonce
		*piNonce = pMessage->nonce;

		FPGA_DEBUG_BYTE_ARR(1, "output hash:", sizeof(pMessage->hash), hash);
		FPGA_DEBUG(1, "%s: output nonce: 0x%x.", __FUNCTION__, *piNonce);

		FPGA_DEBUG(20, "%s: OK", __FUNCTION__);
		return fpga_OK;
	}

	void cryptonight_fpga_close(fpga_ctx *ctx)
	{
		FPGA_DEBUG(1, "%s", __FUNCTION__);

		if (ctx->device_com_handle != INVALID_HANDLE_VALUE)
		{
			//stop FPGA
			_reset(ctx, true);

			CloseHandle(ctx->device_com_handle);
		}

		FPGA_DEBUG(1, "%s: OK", __FUNCTION__);
	}
}
