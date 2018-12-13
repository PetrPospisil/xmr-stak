#include "cryptonight.hpp"
#include <vector>
#include <string.h>

extern "C"
{
	extern void(*const extra_hashes[4])(const void *, uint32_t, char *);
}

extern "C" {

	_Return_type_success_(return != false) bool IsNumeric(_In_z_ LPCSTR pszString, _In_ bool bIgnoreColon) noexcept
	{
		const size_t nLen = strlen(pszString);
		if (nLen == 0)
			return false;

		//What will be the return value from this function (assume the best)
		bool bNumeric = true;

		for (size_t i = 0; i < nLen && bNumeric; i++)
		{
			if (bIgnoreColon && (pszString[i] == ':'))
				bNumeric = true;
			else
				bNumeric = (isdigit(static_cast<int>(pszString[i])) != 0);
		}

		return bNumeric;
	}

	_Return_type_success_(return != false) bool UsingQueryDosDevice(_Inout_ std::vector<int>& ports)
	{
		//Use QueryDosDevice to look for all devices of the form COMx. Since QueryDosDevice does
			//not consistently report the required size of buffer, lets start with a reasonable buffer size
			//of 4096 characters and go from there
		int nChars = 4096;
		bool bWantStop = false;
		while (nChars && !bWantStop)
		{
			std::vector<char> devices;
			devices.resize(nChars);

			const DWORD dwChars = QueryDosDeviceA(nullptr, &(devices[0]), nChars);
			if (dwChars == 0)
			{
				const DWORD dwError = GetLastError();
				if (dwError == ERROR_INSUFFICIENT_BUFFER)
				{
					//Expand the buffer and  loop around again
					nChars *= 2;
				}
				else
					return false;
			}
			else
			{
				bWantStop = true;

				size_t i = 0;
				while (devices[i] != '\0')
				{
					//Get the current device name
					char* pszCurrentDevice = &(devices[i]);

					//If it looks like "COMX" then
					//add it to the array which will be returned
					const size_t nLen = strlen(pszCurrentDevice);
					if (nLen > 3)
					{
						if ((_strnicmp(pszCurrentDevice, "COM", 3) == 0) && IsNumeric(&(pszCurrentDevice[3]), false))
						{
							//Work out the port number
							const int nPort = atoi(&pszCurrentDevice[3]);
							ports.push_back(nPort);
						}
					}

					//Go to next device name
					i += (nLen + 1);
				}
			}
		}

		return true;
	}

	bool bQuery = false;
	std::vector<int> ports;

	/** get device count
	 *
	 * @param deviceCount[out] fpga device count
	 * @return error code: 0 == error is occurred, 1 == no error
	 */
	int fpga_get_devicecount(int* deviceCount)
	{
		*deviceCount = 0;

		if (!bQuery)
		{
			bQuery = true;
			if (!UsingQueryDosDevice(ports))
				return 0;
		}

		*deviceCount = ports.size();

		printf("device count %u\n", *deviceCount);

		return 1;
	}

	int fpga_get_deviceinfo(fpga_ctx *ctx)
	{
		if (!bQuery)
		{
			bQuery = true;
			if (!UsingQueryDosDevice(ports))
				return 0;
		}

		if (ctx->device_id != -1)
		{
			printf("device id %u\n", ctx->device_id);
			ctx->device_comport = ports[ctx->device_id];
			ctx->device_threads = 1;
			return 0;
		}
		else if (ctx->device_comport != -1)
		{
			printf("device com port %u\n", ctx->device_comport);
			for (size_t id = 0; id < ports.size(); ++id)
			{
				printf("device com port search %u\n", ports[id]);
				if (ports[id] == ctx->device_comport)
				{
					ctx->device_id = id;
					ctx->device_threads = 1;
					return 0;
				}
			}
		}

		return 1;
	}

	int cryptonight_fpga_open(fpga_ctx *ctx)
	{
		char szComPortName[MAX_PATH + 1] = {};
		BOOL Status; // Status of the various operations 

		snprintf(szComPortName, MAX_PATH, "\\\\.\\COM%u", ctx->device_comport);

		ctx->hComm = CreateFileA(szComPortName,
			GENERIC_READ | GENERIC_WRITE, // Read/Write Access
			0,                            // No Sharing, ports cant be shared
			NULL,                         // No Security
			OPEN_EXISTING,                // Open existing port only
			0,                            // Non Overlapped I/O
			NULL);                        // Null for Comm Devices

		if (ctx->hComm == INVALID_HANDLE_VALUE)
		{
			printf("\n    Error! - Port %s can't be opened\n", szComPortName);
			return 0;
		}
			
		printf("\n    Port %s Opened\n ", szComPortName);

		DCB dcbSerialParams = { 0 };                         // Initializing DCB structure
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

		Status = GetCommState(ctx->hComm, &dcbSerialParams);      //retreives  the current settings

		if (Status == FALSE)
			printf("\n    Error! in GetCommState()");

		dcbSerialParams.BaudRate = 460800;      // Setting BaudRate = 9600
		dcbSerialParams.ByteSize = 8;             // Setting ByteSize = 8
		dcbSerialParams.StopBits = ONESTOPBIT;    // Setting StopBits = 1
		dcbSerialParams.Parity = NOPARITY;        // Setting Parity = None 

		Status = SetCommState(ctx->hComm, &dcbSerialParams);  //Configuring the port according to settings in DCB 

		if (Status == FALSE)
		{
			printf("\n    Error! in Setting DCB Structure");
			CloseHandle(ctx->hComm);
			return 0;
		}
		
		//If Successfull display the contents of the DCB Structure
		printf("\n\n    Setting DCB Structure Successfull\n");
		printf("\n       Baudrate = %d", dcbSerialParams.BaudRate);
		printf("\n       ByteSize = %d", dcbSerialParams.ByteSize);
		printf("\n       StopBits = %d", dcbSerialParams.StopBits);
		printf("\n       Parity   = %d", dcbSerialParams.Parity);

		/*------------------------------------ Setting Timeouts --------------------------------------------------*/

		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 1000;
		timeouts.ReadTotalTimeoutConstant = 1000;
		timeouts.ReadTotalTimeoutMultiplier = 1;
		timeouts.WriteTotalTimeoutConstant = 50;
		timeouts.WriteTotalTimeoutMultiplier = 10;

		if (SetCommTimeouts(ctx->hComm, &timeouts) == FALSE)
		{
			printf("\n\n    Error! in Setting Time Outs");
			CloseHandle(ctx->hComm);
			return 0;
		}
			
		printf("\n\n    Setting Serial Port Timeouts Successfull");

		/*------------------------------------ Setting Receive Mask ----------------------------------------------*/

		Status = SetCommMask(ctx->hComm, EV_RXCHAR); //Configure Windows to Monitor the serial device for Character Reception

		if (Status == FALSE)
		{
			printf("\n\n    Error! in Setting CommMask");
			CloseHandle(ctx->hComm);
			return 0;
		}
			
		printf("\n\n    Setting CommMask successfull");

		/*Status = SetupComm(
			ctx->hComm,
			1024,
			1024
		);
		if (Status == FALSE)
		{
			printf("\n\n    Error! in Setting SetupComm");
		}

		printf("\n\n    Setting SetupComm successfull");*/

		return 1;
	}

	inline uint64_t swap_bytes(uint64_t val)
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

#define HASH_DATA_AREA 136
#define TEMP_DATA_AREA 144

	void cryptonight_fpga_set_data(fpga_ctx *ctx, xmrstak_algo algo, const void* input, size_t len)
	{
		BOOL Status; // Status of the various operations 

		BYTE a[] = { 
			0x07, 0x07, 0xb8, 0xae, 0xb6, 0xda, 0x05, 0xd3, 0x57, 0x79, 0x5f, 0x7a, 0x28, 0xbd, 0x2a, 0x26, 0x85, 0x1c, 0x60, 0xe6, 0xf1, 0x47, 
			0x70, 0xcb, 0xbe, 0xa3, 0xb1, 0xfb, 0x4f, 0x63, 0x2c, 0xc8, 0x7f, 0xc7, 0x77, 0xa9, 0x5d, 0xa3, 0xde, 0x1a, 0x02, 0x00, 0xd7, 0x11, 
			0x4c, 0x84, 0xc4, 0x0b, 0x07, 0xcd, 0x40, 0xfc, 0xa7, 0x08, 0x38, 0x34, 0x15, 0xaa, 0xeb, 0xd3, 0x3f, 0xa2, 0x74, 0xb3, 0xa1, 0xcc, 
			0x99, 0x15, 0xb4, 0x25, 0x4d, 0xb6, 0x92, 0x1f, 0x3f, 0x11 };

		input = a;

		if (len + 1 >= TEMP_DATA_AREA)
		{
			printf("Data too large.\n");
			return;
		}

		BYTE   lpHeader[] =
		{
			//HEADER: deadbeef01xxxx
			0xde, 0xad, 0xbe, 0xef, 0x01
		};

		BYTE   lpTail[] =
		{
			//TAIL: b16b00b5
			0xb1, 0x6b, 0x00, 0xb5
		};

		uint64_t st[25] = {};
		WORD stsize = sizeof(st);
		uint8_t temp[TEMP_DATA_AREA];

		memcpy(temp, input, len);
		temp[len] = 1;
		memset(temp + len + 1, 0, HASH_DATA_AREA - len - 1);
		temp[HASH_DATA_AREA - 1] |= 0x80;

		for (size_t i = 0; i < HASH_DATA_AREA / sizeof(uint64_t); i++)
			st[i] ^= ((uint64_t *)temp)[i];

		for (size_t i = 0; i < sizeof(st) / sizeof(uint64_t); ++i)
			st[i] = swap_bytes(st[i]);

		BYTE   lpMessage[sizeof(lpHeader) + sizeof(stsize) + sizeof(st) + sizeof(lpTail)] = {};
		size_t idx = 0;
		memcpy(lpMessage + idx, lpHeader, sizeof(lpHeader));
		idx += sizeof(lpHeader);

		memcpy(lpMessage + idx, &stsize, sizeof(WORD));
		idx += sizeof(stsize);

		memcpy(lpMessage + idx, st, stsize);
		idx += stsize;

		memcpy(lpMessage + idx, lpTail, sizeof(lpTail));
		idx += sizeof(lpTail);

		printf("sz: %u\n", len);

		for (size_t i = 0; i < idx; ++i)
		{
			printf("%02X", (unsigned int)lpMessage[i]);
		}

		printf("\n");
		
		DWORD  dNoOFBytestoWrite;              // No of bytes to write into the port
		DWORD  dNoOfBytesWritten = 0;          // No of bytes written to the port

		dNoOFBytestoWrite = idx; // Calculating the no of bytes to write into the port

		Status = WriteFile(ctx->hComm,               // Handle to the Serialport
			lpMessage,            // Data to be written to the port 
			dNoOFBytestoWrite,   // No of bytes to write into the port
			&dNoOfBytesWritten,  // No of bytes written to the port
			NULL);

		if (Status == FALSE)
		{
			printf("\n\n   Error %d in Writing to Serial Port", GetLastError());
		}
	}

	void cryptonight_fpga_hash(fpga_ctx *ctx, void* output)
	{
		//now we read data from the FPGA
		//we presume that FPGA is computing our data now

		LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
		LARGE_INTEGER Frequency;

		QueryPerformanceFrequency(&Frequency);
		QueryPerformanceCounter(&StartingTime);

		BOOL Status; // Status of the various operations 

		BYTE   lpMessage[256] = {};
		size_t dwMessageIndex = 0;

		DWORD dwStartTimestamp = GetTickCount();
		while (1)
		{
			DWORD dwCurrentTimestamp = GetTickCount();
			if (dwCurrentTimestamp - dwStartTimestamp > 1000)
			{
				//timeout has passed
				printf("    Error! Timeout\n");
				return;
			}

			dwMessageIndex = 0;
			DWORD NoBytesRead;

			BYTE   lpHeader[] =
			{
				//HEADER: deadbeef
				0xde, 0xad, 0xbe, 0xef
			};
			size_t dwHeaderIndex = 0;

			LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
			LARGE_INTEGER Frequency;

			QueryPerformanceFrequency(&Frequency);
			QueryPerformanceCounter(&StartingTime);

			//read header
			bool bSkip = false;
			while (1)
			{
				BYTE  TempChar;
				
				Status = ReadFile(ctx->hComm, &TempChar, sizeof(TempChar), &NoBytesRead, NULL);
				if (Status == FALSE || NoBytesRead != sizeof(TempChar))
				{
					printf("    Error! Header in ReadFile() %u %u %u\n", Status, NoBytesRead, GetLastError());
					return;
				}

				if (TempChar != lpHeader[dwHeaderIndex])
				{
					if(dwHeaderIndex > 1)
						printf("    Warning! Header mismatch... skipped data %02x[%u] != %02x\n", lpHeader[dwHeaderIndex], dwHeaderIndex, TempChar);
					bSkip = true;
					break;
				}

				dwHeaderIndex++;
				lpMessage[dwMessageIndex++] = TempChar;
				if (dwHeaderIndex == sizeof(lpHeader))
					break;
			}

			if (bSkip)
				continue;

			QueryPerformanceCounter(&EndingTime);
			ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;


			//
			// We now have the elapsed number of ticks, along with the
			// number of ticks-per-second. We use these values
			// to convert to the number of elapsed microseconds.
			// To guard against loss-of-precision, we convert
			// to microseconds *before* dividing by ticks-per-second.
			//

			ElapsedMicroseconds.QuadPart *= 1000000;
			ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

			printf("Timer %I64i us\n", ElapsedMicroseconds.QuadPart);

			//read MSG command
			Status = ReadFile(ctx->hComm, lpMessage + dwMessageIndex, sizeof(BYTE), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(BYTE))
			{
				printf("    Error! Command in ReadFile()\n");
				return;
			}

			BYTE* pMsgCommand = (BYTE*)(lpMessage + dwMessageIndex);
			dwMessageIndex += sizeof(BYTE);

			//read MSG size
			Status = ReadFile(ctx->hComm, lpMessage + dwMessageIndex, sizeof(WORD), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(WORD))
			{
				printf("    Error! Size in ReadFile()\n");
				return;
			}

			WORD* pMsgSize = (WORD*)(lpMessage + dwMessageIndex);
			dwMessageIndex += sizeof(WORD);

			if (*pMsgSize >= sizeof(lpMessage) - dwMessageIndex)
			{
				printf("    Error! Size too big\n");
				continue;
			}

			//read data
			Status = ReadFile(ctx->hComm, lpMessage + dwMessageIndex, *pMsgSize, &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != *pMsgSize)
			{
				printf("    Error! Data in ReadFile()\n");
				return;
			}
			
			dwMessageIndex += *pMsgSize;

			BYTE   lpTail[] =
			{
				//TAIL: b16b00b5
				0xb1, 0x6b, 0x00, 0xb5
			};

			//read tail
			Status = ReadFile(ctx->hComm, lpMessage + dwMessageIndex, sizeof(lpTail), &NoBytesRead, NULL);
			if (Status == FALSE || NoBytesRead != sizeof(lpTail))
			{
				printf("    Error! Tail in ReadFile()\n");
				return;
			}

			if (memcmp(lpMessage + dwMessageIndex, lpTail, sizeof(lpTail)) != 0)
			{
				printf("    Warning! Tail mismatch... skipped data\n");
				continue;
			}

			dwMessageIndex += sizeof(lpTail);

			printf("msg: %u %u \n", *pMsgCommand, *pMsgSize);

			//message succesfully read
			//we are interested only in the one kind of the messages here
			if (*pMsgCommand == 0x11 && *pMsgSize == 204)
				break;
		}
		
		BYTE* lpData = lpMessage + 7;
		WORD wDataSize = 204;

		printf("fpga:\n");
		for (size_t i = 0; i < 204; ++i)
		{
			printf("%02X", (unsigned int)lpData[i]);
		}

		printf("\n");

		//we must do the hashing now
		extra_hashes[lpData[0] & 3](lpData, wDataSize, (char*)output);

		printf("result:\n");
		for (size_t i = 0; i < 32; ++i)
		{
			printf("%02X", (unsigned int)((BYTE*)output)[i]);
		}
		printf("\n");

		QueryPerformanceCounter(&EndingTime);
		ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;


		//
		// We now have the elapsed number of ticks, along with the
		// number of ticks-per-second. We use these values
		// to convert to the number of elapsed microseconds.
		// To guard against loss-of-precision, we convert
		// to microseconds *before* dividing by ticks-per-second.
		//

		ElapsedMicroseconds.QuadPart *= 1000000;
		ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

		printf("Timer2 %I64i us\n", ElapsedMicroseconds.QuadPart);

		ExitProcess(0);

		return;
	}

	void cryptonight_fpga_close(fpga_ctx *ctx)
	{
		if (ctx->hComm != INVALID_HANDLE_VALUE)
		{
			//TODO: cleanup the status
			//reseting do not work yet

			CloseHandle(ctx->hComm);
		}
	}
}
