/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include "ocr.h"
extern ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]);
}

#include "ocr_tbb.h"
#include <fstream>
#include "ocr_log.h"
#include <thread>

struct barrier_task : public ocr_tbb::tasking::task_type
{
	ocr_tbb::tasking::task_type* execute()
	{
		//std::cout << "Barrier reached, shutting down" << std::endl;
#if(PUBLISH_METRICS)
		ocr_tbb::runtime::get().metrics.publish(true);
#endif
		return 0;
	}
};

std::vector<char> arg_data;
ocr_tbb::memory::manager ocr_tbb::memory::manager::the_;


struct spawn_main : public ocr_tbb::tasking::task_type
{
	ocr_tbb::tasking::task_type* execute()
	{
		ocr_tbb::edt_template fake_template(0, 0, 0, 0);
		ocr_tbb::edt fake(&fake_template, 0, 0, 0, 0, 0, NULL_HINT, 0);
		ocr_tbb::runtime::get().set_my_edt(&fake);
		ocr_tbb::runtime::get().barrier = ocr_tbb::tasking::task_factory<barrier_task>::continuation(this);
		ocr_tbb::runtime::get().barrier->set_ref_count(1);
		ocrGuid_t edt_template, edt, args;
		void* args_ptr;
		ocrDbCreate(&args, &args_ptr, arg_data.size(), 0, NULL_HINT, NO_ALLOC);
		memcpy(args_ptr, &arg_data.front(), arg_data.size());
		ocrEdtTemplateCreate(&edt_template, mainEdt, 0, 1);
		ocrEdtCreate(&edt, edt_template, 0, 0, 1, &args, 0, NULL_HINT, 0);//we provide all dependency values, so the task gets spawned immediately!
		ocr_tbb::runtime::get().set_my_edt(0);
		return 0;
	}
};

void killer()
{
	std::this_thread::sleep_for(std::chrono::seconds(1));
	exit(1);
}

int main(int argc, char* argv[])
{
	print_config();
	for (int i = 1; i < argc;)
	{
		if (std::string(argv[i]) == "-ocr:cfg")
		{
			assert(i + 1 < argc);
			for (int j = i + 2; j <= argc; ++j)
			{
				argv[j - 2] = argv[j];
			}
			argc -= 2;
		}
		else
		{
			++i;
		}
	}
	int num_threads = tbb::task_scheduler_init::default_num_threads() / CORE_REDUCTION_FACTOR;
	std::vector<char*> new_argv;
	new_argv.reserve(argc);
	new_argv.push_back(argv[0]);
	for (int i = 1; i < argc; ++i)
	{
		if (std::string(argv[i]) == "--ocr:num-threads")
		{
			++i;
			assert(i < argc);
			num_threads = (std::size_t)atoi(argv[i]);
		}
		else
		{
			new_argv.push_back(argv[i]);
		}
	}
	//std::thread killer_thread(killer);
	//if (argc == 2) num_threads = atoi(argv[1]);
	//ocr_tbb::pack_arguments(argc, argv, arg_data);
	ocr_tbb::pack_arguments((int)new_argv.size(), &new_argv.front(), arg_data);
	tbb::task_scheduler_init init(num_threads);
#if (SCHEDULER_INT)
	ocr_tbb::tasking::scheduler_init init_int;
#endif
#if(PUBLISH_METRICS)
	ocr_tbb::runtime::get().metrics.start();
#endif
#if(COLLECT_PTRACE)
	ocr_tbb::performance_modeling::task_modeler::dump_meta();
#endif
	ocr_tbb::logging::log::start();
	ocr_tbb::tasking::scheduler::spawn_root_and_wait(*ocr_tbb::tasking::task_factory<spawn_main>::root());
	ocr_tbb::logging::log::stop();

#ifdef WIN32
	ocr_tbb::logging::log::dump("log/", 0);
#else
	ocr_tbb::logging::log::dump("log/", 0);
#endif
	ocr_tbb::verbose::performance::total_time() << ocr_tbb::logging::log::total_seconds() << ocr_tbb::verbose::end;
#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif
#if(PUBLISH_METRICS)
	ocr_tbb::runtime::get().metrics.stop();
#endif
	return ocr_tbb::runtime::get().result_code;
}

