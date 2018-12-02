#pragma once

#include <stdint.h>
#include <string>

#include "xmrstak/jconf.hpp"
#include "xmrstak/backend/cryptonight.hpp"

typedef struct {
	int device_id;
	int device_comport;
	int device_threads;
} fpga_ctx;

extern "C" {

	/** get device count
	 *
	 * @param deviceCount[out] fpga device count
	 * @return error code: 0 == error is occurred, 1 == no error
	 */
	int fpga_get_devicecount(int* deviceCount);
	int fpga_get_deviceinfo(fpga_ctx *ctx);
}
