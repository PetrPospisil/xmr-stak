#include "cryptonight_fpga_internal.hpp"
#include "xmrstak/misc/console.hpp"
#include <vector>
#include <string.h>

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

void _init_message_in_13(MSG_IN_13* pMsg, BYTE counter)
{
	pMsg->header.header[0] = lpHeader[0];
	pMsg->header.header[1] = lpHeader[1];
	pMsg->header.header[2] = lpHeader[2];
	pMsg->header.header[3] = lpHeader[3];

	pMsg->header.message = 0x13;
	pMsg->header.counter = counter;
	pMsg->header.size = 0;

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

	FPGA_DEBUG(20, "%s: OK", __FUNCTION__);
	return fpga_OK;
}

fpga_error _get_reset(fpga_ctx *ctx, bool* pReset)
{
	FPGA_DEBUG(20, "%s", __FUNCTION__);
	if (ctx == NULL)
	{
		FPGA_ERROR("%s: ctx null", __FUNCTION__);
		return fpga_e_InvalidArgument;
	}

	if (pReset == NULL)
	{
		FPGA_ERROR("%s: pReset null", __FUNCTION__);
		return fpga_e_InvalidArgument;
	}

	MSG_IN_13 msgin;
	_init_message_in_13(&msgin, ++ctx->msg_counter);

	fpga_error res;
	FPGA_DEBUG_TIMER_RESULT(2, res, _send_message, ctx->device_com_handle, (DWORD)sizeof(msgin), (BYTE*)&msgin);
	if (FPGA_FAILED(res))
	{
		FPGA_ERROR("%s: _send_message failed with last error: %d.", __FUNCTION__, res);
		return res;
	}

	BYTE msgout[FPGA_MAX_OUTPUT_MSG_SIZE];
	while (1)
	{
		fpga_error res;
		size_t size = sizeof(msgout);
		FPGA_DEBUG_TIMER_RESULT(2, res, _get_message, ctx->device_com_handle, &size, msgout, ctx->timeout);
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

		MSG_HEADER* pHeader = (MSG_HEADER*)msgout;
		if (pHeader->message != 0x13)
		{
			FPGA_ERROR("%s: Warning! Msg id %u != 0x13 mismatch... skipped", __FUNCTION__, pHeader->message);
			continue;
		}

		if (pHeader->counter != ctx->msg_counter)
		{
			FPGA_ERROR("%s: Warning! Msg counter 0x%X != 0x%X ctx mismatch... skipped", __FUNCTION__, pHeader->counter, ctx->msg_counter);
			continue;
		}

		if (pHeader->size != 1)
		{
			FPGA_ERROR("%s: Warning! Msg size %u != 1 mismatch... skipped", __FUNCTION__, pHeader->size);
			continue;
		}

		break;
	}

	MSG_OUT_13* pMessage = (MSG_OUT_13*)msgout;

	*pReset = pMessage->reset ? true : false;

	FPGA_DEBUG(20, "%s: OK r:%d", __FUNCTION__, (uint32_t)pMessage->reset);
	return fpga_OK;
}
