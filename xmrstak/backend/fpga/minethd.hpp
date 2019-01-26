#pragma once

#include "xmrstak/jconf.hpp"
#include "jconf.hpp"
#include "fpga_code/cryptonight.hpp"

#include "xmrstak/backend/cpu/minethd.hpp"
#include "xmrstak/backend/iBackend.hpp"
#include "xmrstak/misc/environment.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <future>


namespace xmrstak
{
namespace fpga
{

	class minethd : public iBackend
	{
	public:
		static std::vector<iBackend*>* thread_starter(uint32_t threadOffset, miner_work& pWork);

		typedef void(*cn_hash_fun)(const void*, size_t, void*, cryptonight_ctx**);

		static bool thd_setaffinity(std::thread::native_handle_type h, uint64_t cpu_id);
	private:

		minethd(miner_work& pWork, size_t iNo, const jconf::thd_cfg& cfg);

		bool self_test();

		void prep_work(uint8_t *bWorkBlob, uint32_t *piNonce);

		void work_main();

		uint64_t iJobNo;

		miner_work oWork;

		std::promise<void> order_fix;
		std::mutex thd_aff_set;

		std::thread oWorkThd;
		int64_t affinity;

		fpga_ctx ctx;
		jconf::thd_cfg cfg;

		bool bQuit;
	};

} // namespace fpga
} // namespace xmrstak
