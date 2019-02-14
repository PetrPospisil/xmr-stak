#pragma once
#include <stdlib.h>
#include <string>
#include <vector>
#include "xmrstak/params.hpp"

namespace xmrstak
{
namespace fpga
{

class jconf
{
public:
	static jconf* inst()
	{
		if (oInst == nullptr) oInst = new jconf;
		return oInst;
	};

	bool parse_config(const char* sFilename = params::inst().configFileFPGA.c_str());

	struct test_cfg {
		std::string operation;
		std::vector<uint8_t> data;
		uint32_t timeout;
	};

	struct thd_cfg {
		uint32_t comport;
		uint32_t threads;
		uint32_t iCpuAff;
	};

	size_t GetFPGADeviceCount();

	bool GetFPGADeviceConfig(size_t id, thd_cfg &cfg);

	size_t GetFPGADeviceTestConfigCount(size_t id);

	bool GetFPGADeviceTestConfig(size_t id, size_t id_test, test_cfg& test_cfg);

	bool NeedsAutoconf();

private:
	jconf();
	static jconf* oInst;

	struct opaque_private;
	opaque_private* prv;

};

} // namespace fpga
} // namespace xmrstak
