#include "cryptonight.hpp"

extern "C" {

	/** get device count
	 *
	 * @param deviceCount[out] fpga device count
	 * @return error code: 0 == error is occurred, 1 == no error
	 */
	int fpga_get_devicecount(int* deviceCount)
	{
		*deviceCount = 1;
		return 1;
	}

	int fpga_get_deviceinfo(fpga_ctx *ctx)
	{
		ctx->device_id = 0;
		ctx->device_comport = 0;
		ctx->device_threads = 1;

		return 0;
	}
}
