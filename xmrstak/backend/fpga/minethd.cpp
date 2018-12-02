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

minethd::minethd(miner_work& pWork, size_t iNo, int64_t affinity)
{
	this->backendType = iBackend::CPU;
	oWork = pWork;
	bQuit = 0;
	iThreadNo = (uint8_t)iNo;
	iJobNo = 0;
	this->affinity = affinity;

	std::unique_lock<std::mutex> lck(thd_aff_set);
	std::future<void> order_guard = order_fix.get_future();

	oWorkThd = std::thread(&minethd::work_main, this);

	order_guard.wait();

	if (affinity >= 0) //-1 means no affinity
		if (!thd_setaffinity(oWorkThd.native_handle(), affinity))
			printer::inst()->print_msg(L1, "WARNING setting affinity failed.");
}

cryptonight_ctx* minethd::minethd_alloc_ctx()
{
	cryptonight_ctx* ctx;
	alloc_msg msg = { 0 };

	switch (::jconf::inst()->GetSlowMemSetting())
	{
	case ::jconf::never_use:
		ctx = cryptonight_alloc_ctx(1, 1, &msg);
		if (ctx == NULL)
			printer::inst()->print_msg(L0, "MEMORY ALLOC FAILED: %s", msg.warning);
		return ctx;

	case ::jconf::no_mlck:
		ctx = cryptonight_alloc_ctx(1, 0, &msg);
		if (ctx == NULL)
			printer::inst()->print_msg(L0, "MEMORY ALLOC FAILED: %s", msg.warning);
		return ctx;

	case ::jconf::print_warning:
		ctx = cryptonight_alloc_ctx(1, 1, &msg);
		if (msg.warning != NULL)
			printer::inst()->print_msg(L0, "MEMORY ALLOC FAILED: %s", msg.warning);
		if (ctx == NULL)
			ctx = cryptonight_alloc_ctx(0, 0, NULL);
		return ctx;

	case ::jconf::always_use:
		return cryptonight_alloc_ctx(0, 0, NULL);

	case ::jconf::unknown_value:
		return NULL; //Shut up compiler
	}

	return nullptr; //Should never happen
}

bool minethd::self_test()
{
	alloc_msg msg = { 0 };
	size_t res;
	bool fatal = false;

	switch (::jconf::inst()->GetSlowMemSetting())
	{
	case ::jconf::never_use:
		res = cryptonight_init(1, 1, &msg);
		fatal = true;
		break;

	case ::jconf::no_mlck:
		res = cryptonight_init(1, 0, &msg);
		fatal = true;
		break;

	case ::jconf::print_warning:
		res = cryptonight_init(1, 1, &msg);
		break;

	case ::jconf::always_use:
		res = cryptonight_init(0, 0, &msg);
		break;

	case ::jconf::unknown_value:
	default:
		return false; //Shut up compiler
	}

	if (msg.warning != nullptr)
		printer::inst()->print_msg(L0, "MEMORY INIT ERROR: %s", msg.warning);

	if (res == 0 && fatal)
		return false;

	cryptonight_ctx *ctx[1] = { 0 };
	if ((*ctx = minethd_alloc_ctx()) == nullptr)
	{
		printer::inst()->print_msg(L0, "ERROR: miner was not able to allocate memory.");
		return false;
	}

	bool bResult = true;

	unsigned char out[32];
	cn_hash_fun hashf;
	cn_hash_fun hashf_multi;

	xmrstak_algo algo = xmrstak_algo::invalid_algo;

	for (int algo_idx = 0; algo_idx < 2; ++algo_idx)
	{
		if (algo_idx == 0)
			algo = ::jconf::inst()->GetCurrentCoinSelection().GetDescription(1).GetMiningAlgo();
		else
			algo = ::jconf::inst()->GetCurrentCoinSelection().GetDescription(1).GetMiningAlgoRoot();

		if (algo == cryptonight)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight);
			hashf("This is a test", 14, out, ctx);
			bResult = bResult && memcmp(out, "\xa0\x84\xf0\x1d\x14\x37\xa0\x9c\x69\x85\x40\x1b\x60\xd4\x35\x54\xae\x10\x58\x02\xc5\xf5\xd8\xa9\xb3\x25\x36\x49\xc0\xbe\x66\x05", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight);
			hashf("This is a test", 14, out, ctx);
			bResult = bResult && memcmp(out, "\xa0\x84\xf0\x1d\x14\x37\xa0\x9c\x69\x85\x40\x1b\x60\xd4\x35\x54\xae\x10\x58\x02\xc5\xf5\xd8\xa9\xb3\x25\x36\x49\xc0\xbe\x66\x05", 32) == 0;
		}
		else if (algo == cryptonight_lite)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_lite);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\x5a\x24\xa0\x29\xde\x1c\x39\x3f\x3d\x52\x7a\x2f\x9b\x39\xdc\x3d\xb3\xbc\x87\x11\x8b\x84\x52\x9b\x9f\x0\x88\x49\x25\x4b\x5\xce", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_lite);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\x5a\x24\xa0\x29\xde\x1c\x39\x3f\x3d\x52\x7a\x2f\x9b\x39\xdc\x3d\xb3\xbc\x87\x11\x8b\x84\x52\x9b\x9f\x0\x88\x49\x25\x4b\x5\xce", 32) == 0;
		}
		else if (algo == cryptonight_monero)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_monero);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\x1\x57\xc5\xee\x18\x8b\xbe\xc8\x97\x52\x85\xa3\x6\x4e\xe9\x20\x65\x21\x76\x72\xfd\x69\xa1\xae\xbd\x7\x66\xc7\xb5\x6e\xe0\xbd", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_monero);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\x1\x57\xc5\xee\x18\x8b\xbe\xc8\x97\x52\x85\xa3\x6\x4e\xe9\x20\x65\x21\x76\x72\xfd\x69\xa1\xae\xbd\x7\x66\xc7\xb5\x6e\xe0\xbd", 32) == 0;
		}
		else if (algo == cryptonight_monero_v8)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_monero_v8);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = memcmp(out, "\x35\x3f\xdc\x06\x8f\xd4\x7b\x03\xc0\x4b\x94\x31\xe0\x05\xe0\x0b\x68\xc2\x16\x8a\x3c\xc7\x33\x5c\x8b\x9b\x30\x81\x56\x59\x1a\x4f", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_monero_v8);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult &= memcmp(out, "\x35\x3f\xdc\x06\x8f\xd4\x7b\x03\xc0\x4b\x94\x31\xe0\x05\xe0\x0b\x68\xc2\x16\x8a\x3c\xc7\x33\x5c\x8b\x9b\x30\x81\x56\x59\x1a\x4f", 32) == 0;
		}
		else if (algo == cryptonight_aeon)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_aeon);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xfc\xa1\x7d\x44\x37\x70\x9b\x4a\x3b\xd7\x1e\xf3\xed\x21\xb4\x17\xca\x93\xdc\x86\x79\xce\x81\xdf\xd3\xcb\xdd\xa\x22\xd7\x58\xba", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_aeon);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xfc\xa1\x7d\x44\x37\x70\x9b\x4a\x3b\xd7\x1e\xf3\xed\x21\xb4\x17\xca\x93\xdc\x86\x79\xce\x81\xdf\xd3\xcb\xdd\xa\x22\xd7\x58\xba", 32) == 0;
		}
		else if (algo == cryptonight_ipbc)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_ipbc);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xbc\xe7\x48\xaf\xc5\x31\xff\xc9\x33\x7f\xcf\x51\x1b\xe3\x20\xa3\xaa\x8d\x4\x55\xf9\x14\x2a\x61\xe8\x38\xdf\xdc\x3b\x28\x3e\x0xb0", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_ipbc);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xbc\xe7\x48\xaf\xc5\x31\xff\xc9\x33\x7f\xcf\x51\x1b\xe3\x20\xa3\xaa\x8d\x4\x55\xf9\x14\x2a\x61\xe8\x38\xdf\xdc\x3b\x28\x3e\x0", 32) == 0;
		}
		else if (algo == cryptonight_stellite)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_stellite);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xb9\x9d\x6c\xee\x50\x3c\x6f\xa6\x3f\x30\x69\x24\x4a\x0\x9f\xe4\xd4\x69\x3f\x68\x92\xa4\x5c\xc2\x51\xae\x46\x87\x7c\x6b\x98\xae", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_stellite);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xb9\x9d\x6c\xee\x50\x3c\x6f\xa6\x3f\x30\x69\x24\x4a\x0\x9f\xe4\xd4\x69\x3f\x68\x92\xa4\x5c\xc2\x51\xae\x46\x87\x7c\x6b\x98\xae", 32) == 0;
		}
		else if (algo == cryptonight_masari)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_masari);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xbf\x5f\xd\xf3\x5a\x65\x7c\x89\xb0\x41\xcf\xf0\xd\x46\x6a\xb6\x30\xf9\x77\x7f\xd9\xc6\x3\xd7\x3b\xd8\xf1\xb5\x4b\x49\xed\x28", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_masari);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xbf\x5f\xd\xf3\x5a\x65\x7c\x89\xb0\x41\xcf\xf0\xd\x46\x6a\xb6\x30\xf9\x77\x7f\xd9\xc6\x3\xd7\x3b\xd8\xf1\xb5\x4b\x49\xed\x28", 32) == 0;
		}
		else if (algo == cryptonight_heavy)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_heavy);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xf9\x44\x97\xce\xb4\xf0\xd9\x84\xb\x9b\xfc\x45\x94\x74\x55\x25\xcf\x26\x83\x16\x4f\xc\xf8\x2d\xf5\xf\x25\xff\x45\x28\x2e\x85", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_heavy);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xf9\x44\x97\xce\xb4\xf0\xd9\x84\xb\x9b\xfc\x45\x94\x74\x55\x25\xcf\x26\x83\x16\x4f\xc\xf8\x2d\xf5\xf\x25\xff\x45\x28\x2e\x85", 32) == 0;
		}
		else if (algo == cryptonight_haven)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_haven);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xc7\xd4\x52\x9\x2b\x48\xa5\xaf\xae\x11\xaf\x40\x9a\x87\xe5\x88\xf0\x29\x35\xa3\x68\xd\xe3\x6b\xce\x43\xf6\xc8\xdf\xd3\xe3\x9", 32) == 0;

			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), true, xmrstak_algo::cryptonight_haven);
			hashf("This is a test This is a test This is a test", 44, out, ctx);
			bResult = bResult && memcmp(out, "\xc7\xd4\x52\x9\x2b\x48\xa5\xaf\xae\x11\xaf\x40\x9a\x87\xe5\x88\xf0\x29\x35\xa3\x68\xd\xe3\x6b\xce\x43\xf6\xc8\xdf\xd3\xe3\x9", 32) == 0;
		}
		else if (algo == cryptonight_bittube2)
		{
			hashf = func_selector(::jconf::inst()->HaveHardwareAes(), false, xmrstak_algo::cryptonight_bittube2);

			hashf("\x38\x27\x4c\x97\xc4\x5a\x17\x2c\xfc\x97\x67\x98\x70\x42\x2e\x3a\x1a\xb0\x78\x49\x60\xc6\x05\x14\xd8\x16\x27\x14\x15\xc3\x06\xee\x3a\x3e\xd1\xa7\x7e\x31\xf6\xa8\x85\xc3\xcb\xff\x01\x02\x03\x04", 48, out, ctx);
			bResult = bResult && memcmp(out, "\x18\x2c\x30\x41\x93\x1a\x14\x73\xc6\xbf\x7e\x77\xfe\xb5\x17\x9b\xa8\xbe\xa9\x68\xba\x9e\xe1\xe8\x24\x1a\x12\x7a\xac\x81\xb4\x24", 32) == 0;

			hashf("\x04\x04\xb4\x94\xce\xd9\x05\x18\xe7\x25\x5d\x01\x28\x63\xde\x8a\x4d\x27\x72\xb1\xff\x78\x8c\xd0\x56\x20\x38\x98\x3e\xd6\x8c\x94\xea\x00\xfe\x43\x66\x68\x83\x00\x00\x00\x00\x18\x7c\x2e\x0f\x66\xf5\x6b\xb9\xef\x67\xed\x35\x14\x5c\x69\xd4\x69\x0d\x1f\x98\x22\x44\x01\x2b\xea\x69\x6e\xe8\xb3\x3c\x42\x12\x01", 76, out, ctx);
			bResult = bResult && memcmp(out, "\x7f\xbe\xb9\x92\x76\x87\x5a\x3c\x43\xc2\xbe\x5a\x73\x36\x06\xb5\xdc\x79\xcc\x9c\xf3\x7c\x43\x3e\xb4\x18\x56\x17\xfb\x9b\xc9\x36", 32) == 0;

			hashf("\x85\x19\xe0\x39\x17\x2b\x0d\x70\xe5\xca\x7b\x33\x83\xd6\xb3\x16\x73\x15\xa4\x22\x74\x7b\x73\xf0\x19\xcf\x95\x28\xf0\xfd\xe3\x41\xfd\x0f\x2a\x63\x03\x0b\xa6\x45\x05\x25\xcf\x6d\xe3\x18\x37\x66\x9a\xf6\xf1\xdf\x81\x31\xfa\xf5\x0a\xaa\xb8\xd3\xa7\x40\x55\x89", 64, out, ctx);
			bResult = bResult && memcmp(out, "\x90\xdc\x65\x53\x8d\xb0\x00\xea\xa2\x52\xcd\xd4\x1c\x17\x7a\x64\xfe\xff\x95\x36\xe7\x71\x68\x35\xd4\xcf\x5c\x73\x56\xb1\x2f\xcd", 32) == 0;
		}

		if (!bResult)
			printer::inst()->print_msg(L0,
				"Cryptonight hash self-test failed. This might be caused by bad compiler optimizations.");
	}

	cryptonight_free_ctx(*ctx);

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

		minethd* thd = new minethd(pWork, i + threadOffset, cfg.iCpuAff);
		pvThreads->push_back(thd);
	}

	return pvThreads;
}

minethd::cn_hash_fun minethd::func_selector(bool bHaveAes, bool bNoPrefetch, xmrstak_algo algo)
{
	// We have two independent flag bits in the functions
	// therefore we will build a binary digit and select the
	// function as a two digit binary

	uint8_t algv;
	switch (algo)
	{
	case cryptonight:
		algv = 2;
		break;
	case cryptonight_lite:
		algv = 1;
		break;
	case cryptonight_monero:
		algv = 0;
		break;
	case cryptonight_heavy:
		algv = 3;
		break;
	case cryptonight_aeon:
		algv = 4;
		break;
	case cryptonight_ipbc:
		algv = 5;
		break;
	case cryptonight_stellite:
		algv = 6;
		break;
	case cryptonight_masari:
		algv = 7;
		break;
	case cryptonight_haven:
		algv = 8;
		break;
	case cryptonight_bittube2:
		algv = 9;
		break;
	case cryptonight_monero_v8:
		algv = 10;
		break;
	default:
		algv = 2;
		break;
	}

	static const cn_hash_fun func_table[] = {
		Cryptonight_hash<1>::template hash<cryptonight_monero, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_monero, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_monero, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_monero, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_lite, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_lite, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_lite, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_lite, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_heavy, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_heavy, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_heavy, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_heavy, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_aeon, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_aeon, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_aeon, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_aeon, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_ipbc, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_ipbc, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_ipbc, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_ipbc, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_stellite, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_stellite, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_stellite, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_stellite, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_masari, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_masari, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_masari, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_masari, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_haven, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_haven, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_haven, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_haven, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_bittube2, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_bittube2, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_bittube2, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_bittube2, true, true>,

		Cryptonight_hash<1>::template hash<cryptonight_monero_v8, false, false>,
		Cryptonight_hash<1>::template hash<cryptonight_monero_v8, true, false>,
		Cryptonight_hash<1>::template hash<cryptonight_monero_v8, false, true>,
		Cryptonight_hash<1>::template hash<cryptonight_monero_v8, true, true>
	};

	std::bitset<2> digit;
	digit.set(0, !bHaveAes);
	digit.set(1, !bNoPrefetch);

	auto selected_function = func_table[algv << 2 | digit.to_ulong()];

	return selected_function;
}

void minethd::prep_work(uint8_t *bWorkBlob, uint32_t *piNonce)
{
	memcpy(bWorkBlob, oWork.bWorkBlob, oWork.iWorkSize);
}

void minethd::work_main()
{
	if (affinity >= 0) //-1 means no affinity
		bindMemoryToNUMANode(affinity);

	order_fix.set_value();
	std::unique_lock<std::mutex> lck(thd_aff_set);
	lck.release();
	std::this_thread::yield();

	cryptonight_ctx *ctx;
	uint64_t iCount = 0;
	uint64_t *piHashVal;
	uint32_t *piNonce;
	uint8_t bHashOut[32];
	uint8_t bWorkBlob[sizeof(miner_work::bWorkBlob)];
	uint32_t iNonce;
	job_result res;

	ctx = minethd_alloc_ctx();
	if (ctx == nullptr)
	{
		printer::inst()->print_msg(L0, "ERROR: miner was not able to allocate memory.");
		win_exit(1);
	}
	piHashVal = (uint64_t*)(bHashOut + 24);
	piNonce = (uint32_t*)(bWorkBlob + 39);

	if (!oWork.bStall)
		prep_work(bWorkBlob, piNonce);

	globalStates::inst().iConsumeCnt++;

	// start with root algorithm and switch later if fork version is reached
	auto miner_algo = ::jconf::inst()->GetCurrentCoinSelection().GetDescription(1).GetMiningAlgoRoot();
	cn_hash_fun hash_fun_multi = func_selector(::jconf::inst()->HaveHardwareAes(), false, miner_algo);
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
				hash_fun_multi = func_selector(::jconf::inst()->HaveHardwareAes(), false, miner_algo);
			}
			else
			{
				miner_algo = coinDesc.GetMiningAlgoRoot();
				hash_fun_multi = func_selector(::jconf::inst()->HaveHardwareAes(), false, miner_algo);
			}
			lastPoolId = oWork.iPoolId;
			version = new_version;
		}

		while (globalStates::inst().iGlobalJobNo.load(std::memory_order_relaxed) == iJobNo)
		{
			if ((iCount++ & 0x7) == 0)  //Store stats every 8*N hashes
			{
				uint64_t iStamp = get_timestamp_ms();
				iHashCount.store(iCount, std::memory_order_relaxed);
				iTimestamp.store(iStamp, std::memory_order_relaxed);
			}

			nonce_ctr -= 1;
			if (nonce_ctr <= 0)
			{
				globalStates::inst().calc_start_nonce(iNonce, oWork.bNiceHash, nonce_chunk);
				nonce_ctr = nonce_chunk;
				// check if the job is still valid, there is a small posibility that the job is switched
				if (globalStates::inst().iGlobalJobNo.load(std::memory_order_relaxed) != iJobNo)
					break;
			}

			*piNonce = iNonce++;

			hash_fun_multi(bWorkBlob, oWork.iWorkSize, bHashOut, &ctx);


			if (*piHashVal < oWork.iTarget)
			{
				executor::inst()->push_event(
					ex_event(job_result(oWork.sJobID, iNonce - 1, bHashOut, iThreadNo, miner_algo),
						oWork.iPoolId)
				);
			}

			std::this_thread::yield();
		}

		globalStates::inst().consume_work(oWork, iJobNo);
		prep_work(bWorkBlob, piNonce);
	}

	cryptonight_free_ctx(ctx);
}

} // namespace fpga
} // namespace xmrstak