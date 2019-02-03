/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  * Additional permission under GNU GPL version 3 section 7
  *
  * If you modify this Program, or any covered work, by linking or combining
  * it with OpenSSL (or a modified version of that library), containing parts
  * covered by the terms of OpenSSL License and SSLeay License, the licensors
  * of this Program grant you additional permission to convey the resulting work.
  *
  */

//#include "fpga_code/cryptonight.hpp"
#include "minethd.hpp"
#include "autoAdjust.hpp"
#include "xmrstak/misc/console.hpp"
#include "xmrstak/backend/cpu/crypto/cryptonight_aesni.h"
#include "xmrstak/backend/cpu/crypto/cryptonight.h"
#include "xmrstak/backend/cpu/minethd.hpp"
#include "xmrstak/params.hpp"
#include "xmrstak/misc/executor.hpp"
#include "xmrstak/jconf.hpp"
#include "xmrstak/misc/environment.hpp"
#include "xmrstak/backend/cpu/hwlocMemory.hpp"
#include "xmrstak/backend/cryptonight.hpp"
#include "xmrstak/misc/utility.hpp"

#include <assert.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <thread>
#include <bitset>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>

#if defined(__APPLE__)
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#define SYSCTL_CORE_COUNT   "machdep.cpu.core_count"
#elif defined(__FreeBSD__)
#include <pthread_np.h>
#endif //__APPLE__

#endif //_WIN32

namespace xmrstak
{
namespace fpga
{

#ifdef WIN32
HINSTANCE lib_handle;
#else
void *lib_handle;
#endif

bool minethd::thd_setaffinity(std::thread::native_handle_type h, uint64_t cpu_id)
{
#if defined(_WIN32)
	// we can only pin up to 64 threads
	if (cpu_id < 64)
	{
		return SetThreadAffinityMask(h, 1ULL << cpu_id) != 0;
	}
	else
	{
		printer::inst()->print_msg(L0, "WARNING: Windows supports only affinity up to 63.");
		return false;
	}
#elif defined(__APPLE__)
	thread_port_t mach_thread;
	thread_affinity_policy_data_t policy = { static_cast<integer_t>(cpu_id) };
	mach_thread = pthread_mach_thread_np(h);
	return thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, 1) == KERN_SUCCESS;
#elif defined(__FreeBSD__)
	cpuset_t mn;
	CPU_ZERO(&mn);
	CPU_SET(cpu_id, &mn);
	return pthread_setaffinity_np(h, sizeof(cpuset_t), &mn) == 0;
#elif defined(__OpenBSD__)
	printer::inst()->print_msg(L0, "WARNING: thread pinning is not supported under OPENBSD.");
	return true;
#else
	cpu_set_t mn;
	CPU_ZERO(&mn);
	CPU_SET(cpu_id, &mn);
	return pthread_setaffinity_np(h, sizeof(cpu_set_t), &mn) == 0;
#endif
}

minethd::minethd(miner_work& pWork, size_t iNo, const jconf::thd_cfg& cfg)
{
	this->backendType = iBackend::FPGA;
	oWork = pWork;
	bQuit = 0;
	iThreadNo = (uint8_t)iNo;
	iJobNo = 0;
	this->affinity = cfg.iCpuAff;
	this->cfg = cfg;

	std::unique_lock<std::mutex> lck(thd_aff_set);
	std::future<void> order_guard = order_fix.get_future();

	oWorkThd = std::thread(&minethd::work_main, this);

	order_guard.wait();

	if (affinity >= 0) //-1 means no affinity
		if (!thd_setaffinity(oWorkThd.native_handle(), affinity))
			printer::inst()->print_msg(L1, "WARNING setting affinity failed.");
}

bool minethd::self_test()
{
	//todo
	return true;

	fpga_ctx ctx;
	if (fpga_get_deviceinfo(this->cfg.comport, &ctx) != 0 || cryptonight_fpga_open(&ctx) != 1)
	{
		printer::inst()->print_msg(L0, "Setup failed for FPGA %d. Exiting.\n", (int)ctx.device_com_port);
		return false;
	}

	bool bResult = true;

	//unsigned char out[32];
	//cryptonight_fpga_set_data(&ctx, cryptonight_monero, "This is a test This is a test This is a test", 44);
	//cryptonight_fpga_hash(&ctx, out);
	//bResult = bResult && memcmp(out, "\x1\x57\xc5\xee\x18\x8b\xbe\xc8\x97\x52\x85\xa3\x6\x4e\xe9\x20\x65\x21\x76\x72\xfd\x69\xa1\xae\xbd\x7\x66\xc7\xb5\x6e\xe0\xbd", 32) == 0;

	cryptonight_fpga_close(&ctx);
	
	return bResult;
}

extern "C"
{
#ifdef WIN32
	__declspec(dllexport)
#endif
	std::vector<iBackend*>* xmrstak_start_backend(uint32_t threadOffset, miner_work& pWork, environment& env)
	{
		environment::inst(&env);
		return fpga::minethd::thread_starter(threadOffset, pWork);
	}
} // extern "C"

std::vector<iBackend*>* minethd::thread_starter(uint32_t threadOffset, miner_work& pWork)
{
	std::vector<iBackend*>* pvThreads = new std::vector<iBackend*>();

	if (!configEditor::file_exist(params::inst().configFileFPGA))
	{
		autoAdjust adjust;
		if (!adjust.printConfig())
			return pvThreads;
	}

	if (!jconf::inst()->parse_config())
	{
		win_exit();
	}


	//Launch the requested number of single and double threads, to distribute
	//load evenly we need to alternate single and double threads
	size_t i, n = jconf::inst()->GetFPGADeviceCount();
	pvThreads->reserve(n);

	jconf::thd_cfg cfg;
	for (i = 0; i < n; i++)
	{
		jconf::inst()->GetFPGADeviceConfig(i, cfg);

		if (cfg.iCpuAff >= 0)
		{
#if defined(__APPLE__)
			printer::inst()->print_msg(L1, "WARNING on macOS thread affinity is only advisory.");
#endif

			printer::inst()->print_msg(L1, "Starting comport %d thread, affinity: %d.", cfg.comport, (int)cfg.iCpuAff);
		}
		else
			printer::inst()->print_msg(L1, "Starting comport %d thread, no affinity.", cfg.comport);

		minethd* thd = new minethd(pWork, i + threadOffset, cfg);
		if (!thd->self_test())
		{
			printer::inst()->print_msg(L0, "FPGA %u self test not passed!", cfg.comport);
			win_exit();
		}

		pvThreads->push_back(thd);
	}

	return pvThreads;
}

void minethd::prep_work(uint8_t *bWorkBlob, uint32_t *piNonce)
{
	memcpy(bWorkBlob, oWork.bWorkBlob, oWork.iWorkSize);
}

void minethd::work_main()
{
	if (affinity >= 0) //-1 means no affinity
		bindMemoryToNUMANode(affinity);

	if (FPGA_FAILED(fpga_get_deviceinfo(this->cfg.comport, &ctx)) || FPGA_FAILED(cryptonight_fpga_open(&ctx)))
	{
		printer::inst()->print_msg(L0, "Setup failed for FPGA %d. Exiting.\n", (int)ctx.device_com_port);
		std::exit(0);
	}

	order_fix.set_value();
	std::unique_lock<std::mutex> lck(thd_aff_set);
	lck.release();
	std::this_thread::yield();

	uint64_t iCount = 0;
	uint64_t *piHashVal;
	uint32_t *piNonce;
	uint8_t bHashOut[32];
	uint8_t bWorkBlob[sizeof(miner_work::bWorkBlob)];
	uint32_t iNonce;
	job_result res;

	piHashVal = (uint64_t*)(bHashOut + 24);
	piNonce = (uint32_t*)(bWorkBlob + 39);

	if (!oWork.bStall)
		prep_work(bWorkBlob, piNonce);

	globalStates::inst().iConsumeCnt++;

	cryptonight_ctx* cpu_ctx;
	cpu_ctx = cpu::minethd::minethd_alloc_ctx();

	// start with root algorithm and switch later if fork version is reached
	auto miner_algo = ::jconf::inst()->GetCurrentCoinSelection().GetDescription(1).GetMiningAlgoRoot();
	cn_hash_fun hash_fun = cpu::minethd::func_selector(::jconf::inst()->HaveHardwareAes(), true /*bNoPrefetch*/, miner_algo);
	uint8_t version = 0;
	size_t lastPoolId = 0;

	while (bQuit == 0)
	{
		if (oWork.bStall)
		{
			/*	We are stalled here because the executor didn't find a job for us yet,
			either because of network latency, or a socket problem. Since we are
			raison d'etre of this software it us sensible to just wait until we have something*/

			while (globalStates::inst().iGlobalJobNo.load(std::memory_order_relaxed) == iJobNo)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

			globalStates::inst().consume_work(oWork, iJobNo);
			prep_work(bWorkBlob, piNonce);
			continue;
		}

		constexpr uint32_t nonce_chunk = 4096;
		int64_t nonce_ctr = 0;

		assert(sizeof(job_result::sJobID) == sizeof(pool_job::sJobID));

		if (oWork.bNiceHash)
			iNonce = *piNonce;

		uint8_t new_version = oWork.getVersion();
		if (new_version != version || oWork.iPoolId != lastPoolId)
		{
			coinDescription coinDesc = ::jconf::inst()->GetCurrentCoinSelection().GetDescription(oWork.iPoolId);
			if (new_version >= coinDesc.GetMiningForkVersion())
			{
				miner_algo = coinDesc.GetMiningAlgo();
				hash_fun = cpu::minethd::func_selector(::jconf::inst()->HaveHardwareAes(), true /*bNoPrefetch*/, miner_algo);
			}
			else
			{
				miner_algo = coinDesc.GetMiningAlgoRoot();
				hash_fun = cpu::minethd::func_selector(::jconf::inst()->HaveHardwareAes(), true /*bNoPrefetch*/, miner_algo);
			}
			lastPoolId = oWork.iPoolId;
			version = new_version;
		}

		if (globalStates::inst().iGlobalJobNo.load(std::memory_order_relaxed) == iJobNo)
		{
			fpga_error res = cryptonight_fpga_set_data(&ctx, miner_algo, bWorkBlob, oWork.iWorkSize, oWork.iTarget);

			while (globalStates::inst().iGlobalJobNo.load(std::memory_order_relaxed) == iJobNo)
			{
				res = cryptonight_fpga_hash(&ctx, &iNonce, bHashOut);
				if (FPGA_SUCCEEDED(res) && *piHashVal < oWork.iTarget)
				{
					//lets do full checking of the computed nonce from FPGA
					uint8_t	bWorkBlob[112];
					uint8_t	bResult[32];

					memcpy(bWorkBlob, oWork.bWorkBlob, oWork.iWorkSize);
					memset(bResult, 0, sizeof(job_result::bResult));

					*(uint32_t*)(bWorkBlob + 39) = iNonce - 1;

					hash_fun(bWorkBlob, oWork.iWorkSize, bResult, &cpu_ctx);
					if ((*((uint64_t*)(bResult + 24))) < oWork.iTarget)
						executor::inst()->push_event(
							ex_event(job_result(oWork.sJobID, iNonce - 1, bHashOut, iThreadNo, miner_algo), oWork.iPoolId)
						);
					else
						executor::inst()->push_event(
							ex_event("FPGA Invalid Result", ctx.device_com_port, oWork.iPoolId)
						);
				}

				std::this_thread::yield();
			}
		}

		globalStates::inst().consume_work(oWork, iJobNo);
		prep_work(bWorkBlob, piNonce);
	}

	cryptonight_fpga_close(&ctx);
}

} // namespace fpga
} // namespace xmrstak
