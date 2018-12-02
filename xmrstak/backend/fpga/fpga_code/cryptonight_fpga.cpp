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

	int cryptonight_fpga_open(fpga_ctx *ctx)
	{
		return 1;
	}

	void cryptonight_fpga_hash(fpga_ctx *ctx, xmrstak_algo algo, const void* input, size_t len, void* output)
	{

	}

	void cryptonight_fpga_close(fpga_ctx *ctx)
	{

	}
}
