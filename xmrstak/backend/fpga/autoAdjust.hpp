
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
			conf += std::string("  { \"comport\" : ") + std::to_string(ctx.device_com_port) + ",\n" +
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
