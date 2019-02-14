#pragma once

#include <stdint.h>
#include <string>

#include "xmrstak/jconf.hpp"
#include "xmrstak/backend/cryptonight.hpp"
#include <Windows.h>

typedef struct {
	int device_com_port;
	int device_threads;

	HANDLE device_com_handle;
	BYTE msg_counter;
	DWORD timeout;
} fpga_ctx;

typedef enum {
	fpga_OK = 0,
	fpga_e_InvalidArgument = -1,
	fpga_e_PortNotConnected = -2,
	fpga_e_UnableToOpenPort = -3,
	fpga_e_UnableToGetCommState = -4,
	fpga_e_UnableToSetCommState = -5,
	fpga_e_UnableToSetCommTimeouts = -6,
	fpga_e_UnableToSetCommMask = -7,
	fpga_e_PortWriteFailed = -8,
	fpga_e_Timeout = -9,
	fpga_e_PortReadFailed = -10,
	fpga_e_BufferTooSmall = -11,
} fpga_error;

#define FPGA_SUCCEEDED(x) ((x) >= fpga_OK)
#define FPGA_FAILED(x) ((x) < fpga_OK)

#define FPGA_TIMEOUT 1000

extern "C" {

	/** get device count
	 *
	 * @param deviceComPort[in] device COM port
	 * @param ctx[out] device context
	 * @return fpga_error
	 */
	fpga_error fpga_get_deviceinfo(int deviceComPort, fpga_ctx *ctx);

	fpga_error cryptonight_fpga_open(fpga_ctx *ctx);
	fpga_error cryptonight_fpga_set_data(fpga_ctx *ctx, xmrstak_algo algo, const void* input, size_t len, uint64_t workTarget);
	fpga_error cryptonight_fpga_hash(fpga_ctx *ctx, uint32_t* piNonce, uint8_t bHashOut[32]);
	fpga_error cryptonight_fpga_reset(fpga_ctx* ctx);
	void cryptonight_fpga_close(fpga_ctx *ctx);
}
