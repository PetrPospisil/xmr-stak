#pragma once

#include <stdint.h>
#include <string>

#include "xmrstak/jconf.hpp"
#include "xmrstak/backend/cryptonight.hpp"
#include <Windows.h>

typedef struct {
	int device_id;
	int device_comport;
	int device_threads;

	HANDLE hComm;
} fpga_ctx;

extern "C" {

	/** get device count
	 *
	 * @param deviceCount[out] fpga device count
	 * @return error code: 0 == error is occurred, 1 == no error
	 */
	int fpga_get_devicecount(int* deviceCount);
	int fpga_get_deviceinfo(fpga_ctx *ctx);

	int cryptonight_fpga_open(fpga_ctx *ctx);
	void cryptonight_fpga_set_data(fpga_ctx *ctx, xmrstak_algo algo, const void* input, size_t len);
	void cryptonight_fpga_hash(fpga_ctx *ctx, void* output);
	void cryptonight_fpga_close(fpga_ctx *ctx);
}
