
#pragma once

#include "autoAdjust.hpp"

#include "fpga_code/cryptonight.hpp"
#include "jconf.hpp"
#include "xmrstak/misc/console.hpp"
#include "xmrstak/misc/configEditor.hpp"
#include "xmrstak/params.hpp"

#include <vector>
#include <cstdio>
#include <sstream>
#include <string>


namespace xmrstak
{
namespace fpga
{

class autoAdjust
{
public:

	autoAdjust()
	{

	}

	/** print the adjusted values if needed
	 *
	 * Routine exit the application and print the adjusted values if needed else
	 * nothing is happened.
	 */
	bool printConfig()
	{
		int deviceCount = 0;
		if (fpga_get_devicecount(&deviceCount) == 0)
			return false;
		// evaluate config parameter for if auto adjustment is needed
		for (int i = 0; i < deviceCount; i++)
		{
			fpga_ctx ctx;

			ctx.device_id = i;
			// -1 trigger auto adjustment
			ctx.device_comport = -1;
			ctx.device_threads = -1;

			if (fpga_get_deviceinfo(&ctx) == 0)
				fpgaCtxVec.push_back(ctx);
			else
				printer::inst()->print_msg(L0, "WARNING: FPGA setup failed for device %d.\n", i);

		}

		generateThreadConfig();
		return true;

	}

private:

	void generateThreadConfig()
	{
		// load the template of the backend config into a char variable
		const char *tpl =
			#include "./config.tpl"
		;

		configEditor configTpl{};
		configTpl.set( std::string(tpl) );

		std::string conf;
		
		for (auto& ctx : fpgaCtxVec)
		{
			conf += std::string("  { \"comport\" : ") + std::to_string(ctx.device_comport) + ",\n" +
				"    \"threads\" : " + std::to_string(ctx.device_threads) + ",\n" +
				"  },\n";
		}

		configTpl.replace("FPGACONFIG",conf);
		configTpl.write(params::inst().configFileFPGA);
		printer::inst()->print_msg(L0, "FPGA: Device configuration stored in file '%s'", params::inst().configFileFPGA.c_str());
	}

	std::vector<fpga_ctx> fpgaCtxVec;
};

} // namespace fpga
} // namespace xmrstak
