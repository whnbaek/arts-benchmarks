/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#include <ocr.h>
#include <extensions/ocr-affinity.h>
#ifdef ENABLE_EXTENSION_DEBUG
#include <extensions/ocr-debug.h>
#endif
#ifdef ENABLE_EXTENSION_HETEROGENEOUS
#include <extensions/ocr-heterogeneous.h>
#endif
#include <cassert>
#include <iostream>
#include "parallelization.h"//for tbb thread
#include <vector>
#include <string.h>

#define TEST_MAP 0
#define TEST_FILE 0
#define TEST_PARTITION 0
#define TEST_BOBOX 1
#ifdef ENABLE_EXTENSION_BYVALUE_DB
#define TEST_BYVALUEDB 1
#else
#define TEST_BYVALUEDB 0
#endif
#ifdef ENABLE_EXTENSION_LABELING
#include <extensions/ocr-labeling.h>
#define TEST_RANGE 1
#else
#define TEST_RANGE 0
#endif
#ifdef ENABLE_EXTENSION_CHANNEL_EVT
#define TEST_CHANNEL 1
#else
#define TEST_CHANNEL 0
#endif

#if (TEST_MAP)
#include "ocr-map-creator.h"
#endif
#if (TEST_FILE)
#include "ocr-file.h"
#endif
#if (TEST_PARTITION)
#include "ocr-db-partitioning.h"
#endif

typedef ocrGuid_t guid_t;

void fake_work(double intensity)
{
	sleep_seconds(intensity);
}

struct test1_args
{
	guid_t data_event;
	guid_t data;
	guid_t consumer;
	u64 task_count;
};

ocrGuid_t test1_nop(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << "exclusive task start" << std::endl;
	std::cout << "change from " << (*(int*)depv[0].ptr);
	++*(int*)depv[0].ptr;
	std::cout << " to " << (*(int*)depv[0].ptr)<<std::endl;
	fake_work(4);
	std::cout << "exclusive task end" << std::endl;
	return NULL_GUID;
}

ocrGuid_t test1_produce(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	test1_args* args = (test1_args*)paramv;
	guid_t nop_template, data;
	ocrEdtTemplateCreate(&nop_template, test1_nop, 0, 1);
	std::size_t task_count = args->task_count;
	u64 af_count;
	ocrAffinityCount(AFFINITY_PD, &af_count);
	guid_t* affs = new guid_t[(af_count > 0) ? (std::size_t)af_count : 1];
	if (af_count == 0) {
		af_count = 1; affs[0] = NULL_GUID;
	}
	ocrAffinityGet(AFFINITY_PD, &af_count, affs);
	data = args->data;
	guid_t consumer = args->consumer;
	for (std::size_t i = 0; i < task_count; ++i)
	{
		guid_t nop, nop_event;
#if(OCR_WITH_PDL)
		u64 node, threads;
		pdlGetDevicePropertyInt(affs[i % af_count], "node-index", &node);
		pdlGetDevicePropertyInt(affs[i % af_count], "thread-count", &threads);
		std::cout << "Will use " << node << " " << pdlGetDevicePropertyString(affs[i % af_count], "hostname") << " (" << threads << " threads)" << std::endl;
#endif
		ocrHint_t hint;
		ocrHintInit(&hint, OCR_HINT_EDT_T);
		ocrSetHintValue(&hint, OCR_HINT_EDT_AFFINITY, ocrAffinityToHintValue(affs[i % af_count]));
		ocrEdtCreate(&nop, nop_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, &hint, &nop_event);
		u64 params[] = { (u64)i };
#ifdef ENABLE_EXTENSION_DEBUG
		ocrAttachDebugLabel(nop, "op", 1, params);
#endif
		ocrAddDependence(nop_event, consumer, (u32)(2 + i), DB_DEFAULT_MODE);
		ocrAddDependence(data, nop, 0, DB_MODE_EW);
	}
	delete[] affs;
	//ocrDbCreate(&nop_data, &nop_data_ptr, 1024 * 1024, 0, NULL_GUID, SIMPLE_ALLOC);
	//conjure the data GUID out of thin air
	guid_t data_event = args->data_event;
	ocrAddDependence(data, data_event, 0, DB_MODE_CONST);
	ocrEdtTemplateDestroy(nop_template);
	return NULL_GUID;
}

ocrGuid_t test1_consume(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << "Expected: " << 1 + paramv[0] << ", actual: " << *(int*)depv[1].ptr << std::endl;
	ocrDbRelease(depv[1].guid);
	ocrDbDestroy(depv[1].guid);
	ocrShutdown();
	return NULL_GUID;
}

#define ARG_SIZE(X) ((sizeof(X)+sizeof(u64)-1)/sizeof(u64))

ocrGuid_t test1_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t data;
	void* ptr;
	ocrDbCreate(&data, &ptr, sizeof(int), 0, NULL_HINT, NO_ALLOC);
#ifdef ENABLE_EXTENSION_DEBUG
	ocrAttachDebugLabel(data, "data", 0, 0);
#endif
	*(int*)ptr = 1;
	ocrDbRelease(data);
	u64 task_count;
	ocrAffinityCount(AFFINITY_PD, &task_count);
	if (task_count < 2) task_count = 2;
	guid_t producer_template, consumer_template, producer, consumer, producer_event, data_event;
	ocrEdtTemplateCreate(&producer_template, test1_produce, ARG_SIZE(test1_args), 1);
	ocrEdtTemplateCreate(&consumer_template, test1_consume, 1, 2 + (u32)task_count);
	ocrEventCreate(&data_event, OCR_EVENT_ONCE_T, 1);
	ocrEdtCreate(&consumer, consumer_template, EDT_PARAM_DEF, &task_count, EDT_PARAM_DEF, 0, 0, NULL_HINT, 0);
	test1_args args;
	args.consumer = consumer;
	args.data = data;
	args.data_event = data_event;
	args.task_count = task_count;
	ocrEdtCreate(&producer, producer_template, EDT_PARAM_DEF, (u64*)&args, EDT_PARAM_DEF, 0, EDT_PROP_FINISH, NULL_HINT, &producer_event);
	ocrAddDependence(producer_event, consumer, 0, DB_MODE_CONST);
	ocrAddDependence(data_event, consumer, 1, DB_MODE_CONST);
	ocrAddDependence(NULL_GUID, producer, 0, DB_MODE_CONST);
	ocrEdtTemplateDestroy(producer_template);
	ocrEdtTemplateDestroy(consumer_template);
	return NULL_GUID;
}

ocrGuid_t test2_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << "work start" << std::endl;
	std::cout << paramv[0] << ": " << *(int*)depv[0].ptr << " (should be 1)" << std::endl;
	fake_work(1 * ((double)paramv[0] + 1));
	std::cout << "work end" << std::endl;
	return NULL_GUID;
}

ocrGuid_t test2_fork(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::size_t task_count = (std::size_t)paramv[0];
	guid_t work_template;
	ocrEdtTemplateCreate(&work_template, test2_work, 1, 1);
	u64 af_count;
	ocrAffinityCount(AFFINITY_PD, &af_count);
	guid_t* affs = new guid_t[(af_count > 0) ? (std::size_t)af_count : 1];
	if (af_count == 0) {
		af_count = 1; affs[0] = NULL_GUID;
	}
	assert(task_count <= af_count);
	ocrAffinityGet(AFFINITY_PD, &af_count, affs);
	ocrDbRelease(depv[0].guid);
	for (std::size_t i = 0; i < task_count; ++i)
	{
		guid_t work, work_event;
		u64 params[] = { (u64)i };
		ocrHint_t hint;
		ocrHintInit(&hint, OCR_HINT_EDT_T);
		ocrSetHintValue(&hint, OCR_HINT_EDT_AFFINITY, ocrAffinityToHintValue(affs[i]));
		ocrEdtCreate(&work, work_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, 0, &hint, &work_event);
		ocrAddDependence(depv[0].guid, work, 0, DB_MODE_CONST);
	}
	delete[] affs;
	return NULL_GUID;
}

ocrGuid_t test2_join(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << *(int*)depv[1].ptr << std::endl;
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test2_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t data;
	void* ptr;
	ocrDbCreate(&data, &ptr, sizeof(int), 0, NULL_HINT, NO_ALLOC);
	*(int*)ptr = 1;
	ocrDbRelease(data);

	u64 task_count;
	ocrAffinityCount(AFFINITY_PD, &task_count);
	guid_t fork_template, join_template, fork, join, fork_event;
	ocrEdtTemplateCreate(&fork_template, test2_fork, 1, 1);
	ocrEdtTemplateCreate(&join_template, test2_join, 1, 2);
	ocrEdtCreate(&join, join_template, EDT_PARAM_DEF, &task_count, EDT_PARAM_DEF, 0, 0, NULL_HINT, 0);
	u64 params[] = { task_count };
	ocrEdtCreate(&fork, fork_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_FINISH, NULL_HINT, &fork_event);
	ocrAddDependence(fork_event, join, 0, DB_MODE_CONST);
	ocrAddDependence(data, join, 1, DB_MODE_CONST);
	ocrAddDependence(data, fork, 0, DB_MODE_CONST);
	return NULL_GUID;
}

#if(OCR_WITH_OPENCL)
ocrGuid_t test3_consume(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << *(int*)depv[0].ptr << " (should be " << paramv[0] << ")" <<  std::endl;
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test3_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count;
	ocrAffinityCount(AFFINITY_OCL, &count);
	std::vector<ocrGuid_t> affinities(count ? (std::size_t)count : 1);
	ocrAffinityGet(AFFINITY_OCL, &count, &affinities.front());
	guid_t data;
	void* ptr;
	ocrDbCreate(&data, &ptr, sizeof(int), 0, NULL_HINT, NO_ALLOC);
	*(int*)ptr = 1;
	guid_t fin_task_template, fin_task, fin_event;
	ocrEdtTemplateCreate(&fin_task_template, test3_consume, 1, 0);
	u64 fin_params[] = { 1 + count };
	ocrEdtCreate(&fin_task, fin_task_template, 1, fin_params, 1 + (u32)count, 0, 0, 0, &fin_event);
	guid_t ocl_task_template;
	ocrOpenclTemplateCreate(&ocl_task_template, "__kernel void test3_kernel(__global int* i) { int x=*i; for(int a=0;a<1000*1000*1000;++a) { ++x; x*=3; } if(x!=123456) ++*i; }", "", "test3_kernel", 0, 1, "test3_kernel");
	std::size_t global_work_offest[] = { 0 };
	std::size_t global_work_size[] = { 1 };
	std::vector<ocrGuid_t> ocl_tasks((std::size_t)count);
	std::vector<ocrGuid_t> ocl_events((std::size_t)count);
	for (std::size_t i = 0; i < count; ++i)
	{
#if(OCR_WITH_PDL)
		u64 node, device;
		pdlGetDevicePropertyInt(affinities[i], "node-index", &node);
		pdlGetDevicePropertyInt(affinities[i], "device-index", &device);
		std::cout << "Will use " << node << "." << device << " " << pdlGetDevicePropertyString(affinities[i], "device-name") << " [" << pdlGetDevicePropertyString(affinities[i], "platform-name") << "]" << std::endl;
#endif
		ocrOpenclTaskCreate(&ocl_tasks[i], ocl_task_template, 1, global_work_offest, global_work_size, 0, 0, 0, 1, 0, 0, affinities[i], &ocl_events[i]);
		ocrAddDependence(ocl_events[i], fin_task, i + 1, DB_MODE_CONST);
	}
	ocrAddDependence(data, fin_task, 0, DB_MODE_CONST);
	for (std::size_t i = 0; i < count; ++i)
	{
		ocrAddDependence(data, ocl_tasks[i], 0, DB_MODE_EW);
	}
	return NULL_GUID;
}

ocrGuid_t test4_consume(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	std::cout << *(int*)depv[0].ptr << " (should be " << paramv[0] << ")" << std::endl;
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test4_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count;
	ocrAffinityCount(AFFINITY_OCL, &count);
	std::vector<ocrGuid_t> affinities(count ? (std::size_t)count : 1);
	ocrAffinityGet(AFFINITY_OCL, &count, &affinities.front());
	guid_t data;
	void* ptr;
	ocrDbCreate(&data, &ptr, sizeof(int), 0, NULL_HINT, NO_ALLOC);
	*(int*)ptr = 1;
	guid_t fin_task_template, fin_task, fin_event;
	ocrEdtTemplateCreate(&fin_task_template, test3_consume, 1, 0);
	u64 fin_params[] = { 1 + count };
	ocrEdtCreate(&fin_task, fin_task_template, 1, fin_params, 1 + (u32)count, 0, 0, 0, &fin_event);
	guid_t ocl_task_template;
	ocrOpenclTemplateCreate(&ocl_task_template, "__kernel void test3_kernel(__global int* i) { int x=*i; for(int a=0;a<1000*1000*1000;++a) { ++x; x*=3; } if(x!=123456) ++*i; }", "", "test3_kernel", 0, 1, "test3_kernel");
	std::size_t global_work_offest[] = { 0 };
	std::size_t global_work_size[] = { 1 };
	std::vector<ocrGuid_t> ocl_tasks((std::size_t)count);
	std::vector<ocrGuid_t> ocl_events((std::size_t)count);
	for (std::size_t i = 0; i < count; ++i)
	{
		ocrOpenclTaskCreate(&ocl_tasks[i], ocl_task_template, 1, global_work_offest, global_work_size, 0, 0, 0, 1, 0, 0, affinities[i], &ocl_events[i]);
		ocrAddDependence(ocl_events[i], fin_task, i + 1, DB_MODE_CONST);
	}
	ocrAddDependence(data, fin_task, 0, DB_MODE_CONST);
	for (std::size_t i = 0; i < count; ++i)
	{
		ocrAddDependence(data, ocl_tasks[i], 0, DB_MODE_RW);
	}
	return NULL_GUID;
}
#endif

#if(TEST_BOBOX)

//Bobox
//EDT calling convention
//prefixes paramv: box id, [inarc]
//prefixes depv: model, prefix data, envelope*
//boxes paramv: /*empty*/
//boxes depv: model, prefix data, box data, [ envelope, columns* ]*

enum bobox_box_id
{
	BOX_init,
	BOX_generate,
	BOX_sum,
	BOX_term,
	BOX_COUNT
};

struct bobox_envelope
{
	guid_t columns[8];
	u64 row_count;
	bool is_poisoned;
};

struct bobox_envelope_data
{
	u32 column_count;
	u64 row_count;
	void* column_data[8];
	bool is_poisoned;
	guid_t guid;
};

struct bobox_box_prefix
{
	guid_t queues_in[8][8];
	guid_t queues_out[8][8];
	guid_t last_action_event;
	u32 pending_task_count;
};

struct bobox_context
{
	u32 paramc;
	u64* paramv;
	u32 depc;
	ocrEdtDep_t* depv;
	bobox_box_prefix* prefix;
	bool data_sent;
};

typedef void (*bobox_box_action)(bobox_context* ctx, void* box_data, u32 inarg_count, bobox_envelope_data* data);

struct box_model_data
{
	guid_t prefix_data;
	guid_t box_data;
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
	u64 box_action;
#else
	bobox_box_action box_action;
#endif
	u32 outarc_count;
	ocrHint_t affinity;
};

struct bobox_model
{
	guid_t prefix_template;
	guid_t box_template;
	u32 box_count;
	u32 link_count;
	//box_model_data boxes[BOX_COUNT];
};

box_model_data* bobox_get_boxes(bobox_model* model)
{
	char* p = (char*)model;
	return (box_model_data*)(p + sizeof(bobox_model));
}

u32* bobox_get_offsets(bobox_model* model)
{
	char* p = (char*)model;
	return (u32*)(p + sizeof(bobox_model) + sizeof(box_model_data)*model->box_count);
}

struct bobox_outarc
{
	u32 destination;
	u32 inarc_index;
};

bobox_outarc* bobox_get_outarcs(bobox_model* model, std::size_t box_id)
{
	char* p = (char*)model;
	return (bobox_outarc*)(p + sizeof(bobox_model) + sizeof(box_model_data)*model->box_count + sizeof(u32)*model->box_count + sizeof(bobox_outarc) * bobox_get_offsets(model)[box_id]);
}

guid_t bobox_create_prefix()
{
	guid_t res;
	bobox_box_prefix* prefix;
	ocrDbCreate(&res, (void**)&prefix, sizeof(bobox_box_prefix), 0, NULL_HINT, NO_ALLOC);
	::memset(prefix, 0, sizeof(bobox_box_prefix));
	ocrDbRelease(res);
	return res;
}

class bobox_structured_model
{
public:
	u32 add_box(bobox_box_action action, std::size_t data_size, guid_t affinity)
	{
		box b;
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
		b.action = ocrEncodeFunctionPointer((ocrFuncPtr_t)action);
#else
		b.action = action;
#endif
		b.affinity = affinity;
		if (data_size > 0)
		{
			ocrDbCreate(&b.data, &b.data_ptr, data_size, 0, 0, NO_ALLOC);
		}
		else
		{
			b.data = NULL_GUID;
			b.data_ptr = 0;
		}
		boxes_.push_back(b);
		return u32(boxes_.size() - 1);
	}
	void* get_box_data(u32 id)
	{
		return boxes_[id].data_ptr;
	}
	void add_link(u32 from, u32 to, u32 outarc, u32 inarc)
	{
		link l;
		l.from = from;
		l.to = to;
		l.inarc = inarc;
		l.outarc = outarc;
		links_.push_back(l);
	}
	void validate()
	{
		for (std::size_t i = 0; i < links_.size(); ++i)
		{
			assert(links_[i].from < boxes_.size());
			assert(links_[i].to < boxes_.size());
		}
	}
	std::size_t flat_size()
	{
		return sizeof(bobox_model) + sizeof(box_model_data)*boxes_.size() + sizeof(u32)*boxes_.size() + sizeof(bobox_outarc)*links_.size();
	}
	void flatten(bobox_model* flat)
	{
		flat->box_count = (u32)boxes_.size();
		flat->link_count = (u32)links_.size();
		for (std::size_t i = 0; i < boxes_.size(); ++i)
		{
			bobox_get_boxes(flat)[i].box_action = boxes_[i].action;
			bobox_get_boxes(flat)[i].box_data = boxes_[i].data;
			ocrHint_t* hint = &bobox_get_boxes(flat)[i].affinity;
			ocrHintInit(hint, OCR_HINT_EDT_T);
			ocrSetHintValue(hint, OCR_HINT_EDT_AFFINITY, ocrAffinityToHintValue(boxes_[i].affinity));
			bobox_get_boxes(flat)[i].prefix_data = bobox_create_prefix();
			bobox_get_boxes(flat)[i].outarc_count = 0;
			u32 outarc_offset = 0;
			if (i == 0) bobox_get_offsets(flat)[0] = 0;
			else outarc_offset = bobox_get_offsets(flat)[i];
			for (std::size_t j = 0; j < links_.size(); ++j)
			{
				if (links_[j].from == i)
				{
					++bobox_get_boxes(flat)[i].outarc_count;
					++outarc_offset;
					bobox_get_outarcs(flat, i)[links_[j].outarc].destination = links_[j].to;
					bobox_get_outarcs(flat, i)[links_[j].outarc].inarc_index = links_[j].inarc;
				}
			}
			if (i + 1 < boxes_.size()) bobox_get_offsets(flat)[i + 1] = outarc_offset;
		}
	
	}
private:
	struct box
	{
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
		u64 action;
#else
		bobox_box_action action;
#endif
		guid_t data;
		void* data_ptr;
		guid_t affinity;
	};
	std::vector<box> boxes_;
	struct link
	{
		u32 from, to, outarc, inarc;
	};
	std::vector<link> links_;
};

void bobox_dump_model(bobox_model* model)
{
	std::cout << "Model with " << model->box_count << " boxes and " << model->link_count << " links." << std::endl;
	for (std::size_t i = 0; i < model->box_count; ++i)
	{
		std::cout << "Box " << i << " has " << bobox_get_boxes(model)[i].outarc_count << " outarcs." << std::endl;
		for (std::size_t j = 0; j < bobox_get_boxes(model)[i].outarc_count; ++j)
		{
			std::cout << "\t" << i << "(" << j << ") -> " << bobox_get_outarcs(model, i)[j].destination << "(" << bobox_get_outarcs(model, i)[j].inarc_index << ")" << std::endl;
		}
	}
}

ocrEdtDep_t bobox_create_envelope_old(bool poisoned)
{
	ocrEdtDep_t res;
	guid_t data;
	bobox_envelope* envelope;
	ocrDbCreate(&data, (void**)&envelope, sizeof(bobox_envelope), 0, NULL_HINT, NO_ALLOC);
	envelope->is_poisoned = poisoned;
	res.guid = data;
	res.ptr = envelope;
	return res;
}

void* bobox_add_column(bobox_envelope* envelope, u32 index, u64 size)
{
	void* ptr;
	ocrDbCreate(&envelope->columns[index], &ptr, size, 0, 0, NO_ALLOC);
	return ptr;
}

bobox_envelope_data bobox_create_envelope(bobox_context* ctx, u64 row_count, u32 column_count, u64* column_element_sizes)
{
	bobox_envelope_data res;
	guid_t data;
	bobox_envelope* envelope;
	ocrDbCreate(&data, (void**)&envelope, sizeof(bobox_envelope), 0, NULL_HINT, NO_ALLOC);
	envelope->is_poisoned = false;
	envelope->row_count = row_count;
	res.guid = data;
	res.is_poisoned = false;
	res.column_count = column_count;
	res.row_count = row_count;
	for (u32 i = 0; i < column_count; ++i)
	{
		res.column_data[i] = bobox_add_column(envelope, i, row_count * column_element_sizes[i]);
	}
	return res;
}

bobox_envelope_data bobox_create_poisoned_pill(bobox_context* ctx)
{
	bobox_envelope_data res;
	guid_t data;
	bobox_envelope* envelope;
	ocrDbCreate(&data, (void**)&envelope, sizeof(bobox_envelope), 0, NULL_HINT, NO_ALLOC);
	envelope->is_poisoned = true;
	envelope->row_count = 0;
	res.guid = data;
	res.is_poisoned = true;
	res.column_count = 0;
	res.row_count = 0;
	return res;
}

void bobox_send_envelope(guid_t envelope, bobox_box_prefix* prefix, u64 output_index)
{
	std::size_t i = 0;
	while (!ocrGuidIsNull(prefix->queues_out[output_index][i]) && i<8) ++i;
	assert(i < 8);
	prefix->queues_out[output_index][i] = envelope;
}

void bobox_send_envelope(bobox_context* ctx, bobox_envelope_data* data, u32 outarc)
{
	bobox_send_envelope(data->guid, ctx->prefix, (u64)outarc);
	ctx->data_sent = true;
}

void bobox_run_prefix(ocrEdtDep_t model, bobox_box_id target)
{
	guid_t edt;
	u64 paramv[1] = { (u64)target };
	ocrEdtCreate(&edt, ((bobox_model*)model.ptr)->prefix_template, 1, paramv, 3, 0, 0, &bobox_get_boxes((bobox_model*)model.ptr)[target].affinity, 0);
	guid_t prefix_data = bobox_get_boxes((bobox_model*)model.ptr)[target].prefix_data;
	ocrDbRelease(model.guid);
	ocrDbRelease(prefix_data);
	ocrAddDependence(model.guid, edt, 0, DB_MODE_CONST);
	ocrAddDependence(prefix_data, edt, 1, DB_MODE_EW);
	ocrAddDependence(NULL_GUID, edt, 2, DB_DEFAULT_MODE);
}

guid_t bobox_forward_envelope(guid_t envelope, ocrEdtDep_t model, bobox_box_id target, u64 input_index, guid_t dependency)
{
	guid_t edt, event;
	u64 paramv[2] = { (u64)target, input_index };
	ocrEdtCreate(&edt, ((bobox_model*)model.ptr)->prefix_template, 2, paramv, 4, 0, 0, &bobox_get_boxes((bobox_model*)model.ptr)[target].affinity, &event);
	ocrAddDependence(model.guid, edt, 0, DB_MODE_CONST);
	ocrAddDependence(bobox_get_boxes((bobox_model*)model.ptr)[target].prefix_data, edt, 1, DB_MODE_EW);
	ocrAddDependence(envelope, edt, 2, DB_MODE_CONST);
	ocrAddDependence(dependency, edt, 3, DB_DEFAULT_MODE);
	return event;
}

void bobox_init_box_action(bobox_context* ctx, void* box_data, u32 inarg_count, bobox_envelope_data* data)
{
	bobox_envelope_data envelope = bobox_create_poisoned_pill(ctx);
	bobox_send_envelope(ctx, &envelope, 0);
}

void bobox_term_box_action(bobox_context* ctx, void* box_data, u32 inarg_count, bobox_envelope_data* data)
{
	assert(data[0].is_poisoned);
	ocrShutdown();
}

ocrGuid_t bobox_run_box(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	bobox_model* model = (bobox_model*)depv[0].ptr;
	bobox_box_prefix* prefix = (bobox_box_prefix*)depv[1].ptr;
	bobox_box_id self = (bobox_box_id)paramv[0];
	bobox_context ctx;
	ctx.data_sent = false;
	ctx.paramc = paramc;
	ctx.paramv = paramv;
	ctx.depc = depc;
	ctx.depv = depv;
	ctx.prefix = prefix;
	std::vector<bobox_envelope_data> data;
	for (std::size_t i = 3; i < depc; ++i)
	{
		if (ocrGuidIsNull(depv[i].guid)) continue;
		bobox_envelope_data bed;
		bed.column_count = 0;
		bed.guid = depv[i].guid;
		bed.is_poisoned = ((bobox_envelope*)depv[i].ptr)->is_poisoned;
		bed.row_count = ((bobox_envelope*)depv[i].ptr)->row_count;
		for (std::size_t j = 0; j < 8; ++j)
		{
			std::size_t offset = 0;
			if (!ocrGuidIsNull(((bobox_envelope*)depv[i].ptr)->columns[j]))
			{
				++bed.column_count;
				++offset;
				assert(i + offset < depc);
				assert(ocrGuidIsEq(((bobox_envelope*)depv[i].ptr)->columns[j],depv[i + offset].guid));
				bed.column_data[j] = depv[i + offset].ptr;
			}
			else bed.column_data[j] = 0;
		}
		i += bed.column_count;
		data.push_back(bed);
	}
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
	bobox_box_action action = (bobox_box_action)ocrDecodeFunctionPointer(bobox_get_boxes(model)[self].box_action);
#else
	bobox_box_action action = bobox_get_boxes(model)[self].box_action;
#endif
	action(&ctx, depv[2].ptr, (u32)data.size(), (data.size() > 0) ? &data.front() : 0);
	assert(prefix->pending_task_count > 0);
	if (--prefix->pending_task_count == 0) prefix->last_action_event = NULL_GUID;
	if (ctx.data_sent) bobox_run_prefix(depv[0], self);
	return NULL_GUID;
}

bobox_box_id bobox_next_box(bobox_model* model, bobox_box_id self, u32 out_index)
{
	assert(out_index < bobox_get_boxes(model)[self].outarc_count);
	return (bobox_box_id)bobox_get_outarcs(model, self)[out_index].destination;
}

u32 bobox_next_box_inarc(bobox_model* model, bobox_box_id self, u32 out_index)
{
	assert(out_index < bobox_get_boxes(model)[self].outarc_count);
	return (bobox_box_id)bobox_get_outarcs(model, self)[out_index].inarc_index;
}

ocrGuid_t bobox_run_prefix(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	bobox_model* model = (bobox_model*)depv[0].ptr;
	bobox_box_prefix* prefix = (bobox_box_prefix*)depv[1].ptr;
	bobox_box_id self = (bobox_box_id)paramv[0];
	guid_t initiator = NULL_GUID;
	ocrEventCreate(&initiator, OCR_EVENT_ONCE_T, 0);
	guid_t dep = initiator;
	while (!ocrGuidIsNull(prefix->queues_out[0][0]))
	{
		u32 outarc = 0;
		dep = bobox_forward_envelope(prefix->queues_out[0][0], depv[0], bobox_next_box(model, self, outarc), bobox_next_box_inarc(model, self, outarc), dep);
		for (std::size_t i = 0; i < 8 - 1; ++i)
		{
			prefix->queues_out[0][i] = prefix->queues_out[0][i + 1];
		}
		prefix->queues_out[0][8 - 1] = NULL_GUID;
	}
	ocrEventSatisfy(initiator, NULL_GUID);
	if (paramc > 1)
	{
		guid_t the_box, the_event;
		bobox_envelope* envelope = (bobox_envelope*)depv[2].ptr;
		u32 columns = 0;
		if (envelope) for (std::size_t i = 0; i < 8; ++i) if (!ocrGuidIsNull(envelope->columns[i])) ++columns;
		u64 paramv[1] = { (u64)self };
		ocrEdtCreate(&the_box, model->box_template, 1, paramv, (u32)(4 + columns + 1), 0, 0, &bobox_get_boxes(model)[self].affinity, &the_event);
		guid_t the_box_data = bobox_get_boxes(model)[self].box_data;
		guid_t last_action_event = prefix->last_action_event;
		prefix->last_action_event = the_event;
		++prefix->pending_task_count;
		prefix = 0;
		ocrDbRelease(depv[0].guid);
		ocrDbRelease(depv[1].guid);
		u32 j = 4;
		if (envelope) for (std::size_t i = 0; i < 8; ++i) if (!ocrGuidIsNull(envelope->columns[i])) ocrAddDependence(envelope->columns[i], the_box, j++, DB_MODE_RW);
		if (!ocrGuidIsNull(depv[2].guid)) ocrDbRelease(depv[2].guid);
		ocrAddDependence(depv[0].guid, the_box, 0, DB_MODE_CONST);
		ocrAddDependence(depv[1].guid, the_box, 1, DB_MODE_EW);
		ocrAddDependence(the_box_data, the_box, 2, DB_MODE_EW);
		ocrAddDependence(depv[2].guid, the_box, 3, DB_MODE_RW);
		ocrAddDependence(last_action_event, the_box, (u32)(4 + columns), DB_DEFAULT_MODE);
	}
	return NULL_GUID;
}

void test5_generate_box_action(bobox_context* ctx, void* box_data, u32 inarg_count, bobox_envelope_data* data)
{
	assert(data[0].is_poisoned);
	int val = 0;
	for (std::size_t j = 0; j < 2; ++j)
	{
		u64 sizes[] = { sizeof(u32) };
		bobox_envelope_data data_envelope = bobox_create_envelope(ctx, 10, 1, sizes);
		u32* data = (u32*)data_envelope.column_data[0];
		for (std::size_t i = 0; i < 10; ++i)
		{
			data[i] = (u32)val++;
		}
		bobox_send_envelope(ctx, &data_envelope, 0);
	}
	bobox_send_envelope(ctx, data, 0);
}

void test5_sum_box_action(bobox_context* ctx, void* box_data, u32 inarg_count, bobox_envelope_data* data)
{
	if (data[0].is_poisoned)
	{
		std::cout << *(u32*)box_data << std::endl;
		bobox_send_envelope(ctx, data + 0, 0);
	}
	else
	{
		for (std::size_t i = 0; i < data[0].row_count; ++i)
		{
			*(u32*)box_data += ((u32*)data[0].column_data[0])[i];
		}
	}
}

ocrGuid_t test5_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count;
	ocrAffinityCount(AFFINITY_PD, &count);
	std::vector<ocrGuid_t> affinities(count ? (std::size_t)count : 1);
	ocrAffinityGet(AFFINITY_PD, &count, &affinities.front());

	bobox_structured_model model;
	model.add_box(bobox_init_box_action, 0, affinities[0 % count]);
	model.add_box(test5_generate_box_action, 0, affinities[1 % count]);
	model.add_box(test5_sum_box_action, sizeof(u32), affinities[2 % count]);
	*(int*)model.get_box_data(2) = 0;
	model.add_box(bobox_term_box_action, 0, affinities[3 % count]);
	model.add_link(0, 1, 0, 0);
	model.add_link(1, 2, 0, 0);
	model.add_link(2, 3, 0, 0);
	model.validate();

	guid_t data;
	bobox_model* flat_model;
	ocrDbCreate(&data, (void**)&flat_model, model.flat_size(), 0, NULL_HINT, NO_ALLOC);
	::memset(flat_model, 255, model.flat_size());
	model.flatten(flat_model);
	bobox_dump_model(flat_model);

	guid_t prefix_template;
	ocrEdtTemplateCreate(&prefix_template, bobox_run_prefix, 1, 3);
	flat_model->prefix_template = prefix_template;

	guid_t action_template;
	ocrEdtTemplateCreate(&action_template, bobox_run_box, 1, 3);
	flat_model->box_template = action_template;

	guid_t init;
	u64 params[2] = { 0, (u64)-1 };
	ocrEdtCreate(&init, flat_model->prefix_template, 2, params, 3, 0, 0, 0, 0);
	guid_t init_prefix_data = bobox_get_boxes(flat_model)[0].prefix_data;
	ocrDbRelease(data);
	ocrAddDependence(data, init, 0, DB_MODE_CONST);
	ocrAddDependence(init_prefix_data, init, 1, DB_MODE_EW);
	ocrAddDependence(NULL_GUID, init, 2, DB_DEFAULT_MODE);
	return NULL_GUID;
}
#endif

#if (TEST_MAP)
//NOTES:
//What about finish EDTs? Could be a problem. The map may have an event that is triggered once all tasks created within the map finish. This has two options: wait far all of the map to be created, wait for the number of tasks to reach zero (creating tasks after that is an error)
//What about maps that I want to save data explicitly, taking over the responsibility for creation. Should probably also support that.
//The creator function should get a lid, but the ocrXyzzyCreate call may convert the lid into a GUID

void test6_creator(ocrGuid_t objectLid, u64 index, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv)
{
	ocrEventCreate(&objectLid, OCR_EVENT_STICKY_T, EVT_PROP_MAPPED);
	//The objectLid should now contain a valid GUID, but we don't need it here.
}

struct test6_data
{
	guid_t map;
	guid_t task_template;
	u64 width;
	u64 height;
};

u64 test6_coords(test6_data* data, u64 x, u64 y)
{
	//Map the coordinates. The first low and last column are not valid coordinates, only use (width-1) x (height-1) events.
	assert(x < data->width - 1);
	assert(y > 0);
	return x + (y-1) * (data->width - 1);
}

ocrGuid_t test6_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 x = paramv[0];
	u64 y = paramv[1];
	test6_data* data = (test6_data*)depv[0].ptr;
	std::cout << x << "," << y << std::endl;
	if (x > 0 && y > 0)
	{
		//Not the first column or the first row, destroy the event representing the item to the left, which allowed this task to start.
		guid_t old_event_lid;
		ocrMapGet(&old_event_lid, data->map, test6_coords(data, x - 1, y));
		ocrEventDestroy(old_event_lid);
	}
	if (x == data->width-1 && y == data->height-1)
	{
		//The last item, we are done
		ocrEdtTemplateDestroy(data->task_template);
		ocrMapDestroy(data->map);
		ocrDbDestroy(depv[0].guid);
		ocrShutdown();
		return NULL_GUID;
	}
	//Do the work here.
	//...
	//Done.
	if (y == 0 && x < data->width - 1)
	{
		//First row, not last column, also create the task on the right
		guid_t new_task;
		u64 params[] = { x + 1 , y };
		guid_t deps[] = { depv[0].guid, NULL_GUID };
		ocrEdtCreate(&new_task, data->task_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, deps, 0, NULL_GUID, 0);
	}
	if (y < data->height - 1)
	{
		//Not the last row, create the task to process the item down of here.
		guid_t new_task, my_event_lid;
		if (x > 0) ocrMapGet(&my_event_lid, data->map, test6_coords(data, x - 1, y + 1));
		else my_event_lid = NULL_GUID;
		u64 params[] = { x, y + 1 };
		guid_t deps[] = { depv[0].guid, my_event_lid };
		ocrEdtCreate(&new_task, data->task_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, deps, 0, NULL_GUID, 0);
	}
	if (x < data->width - 1 && y > 0)
	{
		//Not the last column or first row, satisfy the event representing this item, allowing the item to the right to start.
		guid_t new_event_lid;
		ocrMapGet(&new_event_lid, data->map, test6_coords(data, x, y));
		ocrEventSatisfy(new_event_lid, NULL_GUID);
	}
	return NULL_GUID;
}

ocrGuid_t test6_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t db, task;
	void* ptr;
	ocrDbCreate(&db, &ptr, sizeof(test6_data), 0, NULL_GUID, NO_ALLOC);
	test6_data* data = (test6_data*)ptr;
	data->width = 3;
	data->height = 3;
	u64 params[] = { data->width, data->height };
	ocrMapCreate(&data->map, (data->height-1) * (data->width-1), &test6_creator, 2, params, 0, 0);
	ocrEdtTemplateCreate(&data->task_template, test6_work, 2, 2);
	u64 task_params[] = { 0,0 };
	guid_t task_deps[] = { db, NULL_GUID };
	ocrEdtCreate(&task, data->task_template, EDT_PARAM_DEF, task_params, EDT_PARAM_DEF, task_deps, 0, NULL_GUID, 0);
	return NULL_GUID;
}


void test7_creator(ocrGuid_t objectLid, u64 index, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv)
{
	u64 width = paramv[0];
	u64 height = paramv[1];
	u64 x = index % width;
	u64 y = index / width;
	u64 params[] = { x, y };
	guid_t deps[] = { guidv[0], UNINITIALIZED_GUID, UNINITIALIZED_GUID };
	if (x == 0) deps[1] = NULL_GUID;
	if (y == 0) deps[2] = NULL_GUID;
	ocrEdtCreate(&objectLid, guidv[1], EDT_PARAM_DEF, params, EDT_PARAM_DEF, deps, EDT_PROP_MAPPED, NULL_GUID, 0);
	//The objectLid should now contain a valid GUID, but we don't need it here.
}

struct test7_data
{
	guid_t map;
	guid_t task_template;
	u64 width;
	u64 height;
};

u64 test7_coords(test7_data* data, u64 x, u64 y)
{
	return x + y * data->width;
}

ocrGuid_t test7_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 x = paramv[0];
	u64 y = paramv[1];
	test7_data* data = (test7_data*)depv[0].ptr;
	std::cout << x << "," << y << std::endl;
	if (x == data->width - 1 && y == data->height - 1)
	{
		//The last item, we are done
		ocrEdtTemplateDestroy(data->task_template);
		ocrMapDestroy(data->map);
		ocrDbDestroy(depv[0].guid);
		ocrShutdown();
		return NULL_GUID;
	}
	//Do the work here.
	//...
	//Done.
	if (x < data->width - 1)
	{
		//Not the last column, satisfy preslot of the task to the right
		guid_t task;
		ocrMapGet(&task, data->map, test7_coords(data, x + 1, y));
		ocrAddDependence(NULL_GUID, task, 1, DB_DEFAULT_MODE);
	}
	if (y < data->height - 1)
	{
		//Not the last row, satisfy preslot of the task below
		guid_t task;
		ocrMapGet(&task, data->map, test7_coords(data, x, y + 1));
		ocrAddDependence(NULL_GUID, task, 2, DB_DEFAULT_MODE);
	}
	return NULL_GUID;
}

ocrGuid_t test7_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t db, task;
	void* ptr;
	ocrDbCreate(&db, &ptr, sizeof(test7_data), 0, NULL_GUID, NO_ALLOC);
	test7_data* data = (test7_data*)ptr;
	data->width = 3;
	data->height = 3;
	ocrEdtTemplateCreate(&data->task_template, test7_work, 2, 3);
	u64 params[] = { data->width, data->height };
	guid_t guids[] = { db, data->task_template };
	ocrMapCreate(&data->map, data->height * data->width, &test7_creator, 2, params, 2, guids);
	ocrMapGet(&task, data->map, 0);
	//ocrAddDependence(NULL_GUID, task, 1, DB_DEFAULT_MODE);
	//ocrAddDependence(NULL_GUID, task, 2, DB_DEFAULT_MODE);
	return NULL_GUID;
}
#endif

#if (TEST_FILE)
ocrGuid_t test8_finish(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test8_filler(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u32* data = (u32*)depv[0].ptr;
	for (std::size_t i = 0; i < paramv[1]; ++i)
	{
		data[i] = (u32)(i+paramv[0]+1);
	}
	ocrDbDestroy(depv[0].guid);
	return NULL_GUID;
}

ocrGuid_t test8_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t file, chunk1, chunk2, task1, task2, filler_template, finish_template, finish_task, event1, event2;
	ocrFileOpen(&file, "data.dat", "wb+", 0, 0);
	ocrFileGetChunk(&chunk1, file, 0, 512 * sizeof(u32));
	ocrFileGetChunk(&chunk2, file, 512 * sizeof(u32), 512 * sizeof(u32));
	ocrFileRelease(file);
	ocrEdtTemplateCreate(&filler_template, test8_filler, 2, 1);
	ocrEdtTemplateCreate(&finish_template, test8_finish, 0, 2);
	u64 params1[] = { 0, 512 };
	u64 params2[] = { 512, 512 };
	ocrEdtCreate(&task1, filler_template, EDT_PARAM_DEF, params1, EDT_PARAM_DEF, 0, 0, NULL_GUID, &event1);
	ocrEdtCreate(&task2, filler_template, EDT_PARAM_DEF, params2, EDT_PARAM_DEF, 0, 0, NULL_GUID, &event2);
	ocrEdtCreate(&finish_task, finish_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	ocrAddDependence(event1, finish_task, 0, DB_DEFAULT_MODE);
	ocrAddDependence(event2, finish_task, 1, DB_DEFAULT_MODE);
	ocrAddDependence(chunk1, task1, 0, DB_MODE_EW);
	ocrAddDependence(chunk2, task2, 0, DB_MODE_EW);
	ocrEdtTemplateDestroy(filler_template);
	ocrEdtTemplateDestroy(finish_template);
	return NULL_GUID;
}

ocrGuid_t test9_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u32* data = (u32*)depv[0].ptr;
	for (std::size_t i = 0; i < paramv[0]; ++i)
	{
		data[i] *= 2;
	}
	ocrDbDestroy(depv[0].guid);
	return NULL_GUID;
}

ocrGuid_t test9_check(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t chunk1, chunk2, worker1, worker2, worker_template, finish_template, finish_task, event1, event2;
	u64 size = ocrFileGetSize(depv[0].ptr);
	ocrFileGetChunk(&chunk1, ocrFileGetGuid(depv[0].ptr), 0, size/2);
	ocrFileGetChunk(&chunk2, ocrFileGetGuid(depv[0].ptr), size/2, size/2);
	ocrFileRelease(ocrFileGetGuid(depv[0].ptr));
	ocrDbDestroy(depv[0].guid);
	ocrEdtTemplateCreate(&worker_template, test9_work, 1, 1);
	ocrEdtTemplateCreate(&finish_template, test8_finish, 0, 2);
	u64 params[] = { (size / sizeof(u32))/2 };
	ocrEdtCreate(&worker1, worker_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, 0, NULL_GUID, &event1);
	ocrEdtCreate(&worker2, worker_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, 0, NULL_GUID, &event2);
	ocrEdtCreate(&finish_task, finish_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	ocrAddDependence(event1, finish_task, 0, DB_DEFAULT_MODE);
	ocrAddDependence(event2, finish_task, 1, DB_DEFAULT_MODE);
	ocrAddDependence(chunk1, worker1, 0, DB_MODE_RO);
	ocrAddDependence(chunk2, worker2, 0, DB_MODE_RO);
	ocrEdtTemplateDestroy(worker_template);
	ocrEdtTemplateDestroy(finish_template);
	return NULL_GUID;
}

ocrGuid_t test9_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t info, file, task, checker_template;
	ocrFileOpen(&file, "data.dat", "rb+", &info, 0);
	ocrEdtTemplateCreate(&checker_template, test9_check, 0, 1);
	guid_t deps[] = { info };
	ocrEdtCreate(&task, checker_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, deps, 0, NULL_GUID, 0);
	ocrEdtTemplateDestroy(checker_template);
	return NULL_GUID;
}
#endif

#if (TEST_PARTITION)
ocrGuid_t test10_finish(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 sum = 0;
	u32* data = (u32*)depv[0].ptr;
	for (std::size_t i = 0; i < 1024; ++i)
	{
		sum+=data[i];
	}
	PRINTF("%lu\n", sum);
	ocrDbDestroy(depv[0].guid);
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test10_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u32* data = (u32*)depv[0].ptr;
	for (std::size_t i = 0; i < 512; ++i)
	{
		data[i] *= paramv[0];
	}
	ocrDbDestroy(depv[0].guid);
	return NULL_GUID;
}

ocrGuid_t test10_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t block, worker_template, worker1, worker1_event, worker2, worker2_event, finish_template, finish_task;
	void* ptr;
	ocrDbCreate(&block, &ptr, 1024*sizeof(u32), 0, NULL_GUID, NO_ALLOC);
	u32* data = (u32*)ptr;
	for (u32 i = 0; i < 1024; ++i)
	{
		data[i] = 1;// i + 1;
	}
	ocrDbPart_t parts[2];
	parts[0].offset = 0;
	parts[0].size = 512 * sizeof(u32);
	parts[1].offset = 512 * sizeof(u32);
	parts[1].size = 512 * sizeof(u32);
	ocrDbRelease(block);//could also come later, but the data is no longer needed
	ocrDbPartition(block, 2, parts, 0);
	ocrEdtTemplateCreate(&worker_template, test10_work, 1, 1);
	ocrEdtTemplateCreate(&finish_template, test10_finish, 0, 3);
	u64 params1[] = { 2 };
	u64 params2[] = { 6 };
	ocrEdtCreate(&finish_task, finish_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	ocrEdtCreate(&worker1, worker_template, EDT_PARAM_DEF, params1, EDT_PARAM_DEF, 0, 0, NULL_GUID, &worker1_event);
	ocrEdtCreate(&worker2, worker_template, EDT_PARAM_DEF, params2, EDT_PARAM_DEF, 0, 0, NULL_GUID, &worker2_event);
	ocrEdtTemplateDestroy(worker_template);
	ocrEdtTemplateDestroy(finish_template);
	ocrAddDependence(block, finish_task, 0, DB_MODE_RO);
	ocrAddDependence(worker1_event, finish_task, 1, DB_DEFAULT_MODE);
	ocrAddDependence(worker2_event, finish_task, 2, DB_DEFAULT_MODE);
	ocrAddDependence(parts[0].guid, worker1, 0, DB_MODE_EW);
	ocrAddDependence(parts[1].guid, worker2, 0, DB_MODE_EW);
	return NULL_GUID;
}

ocrGuid_t test11_finish(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrDbDestroy(depv[3].guid);//destroy the "params" data block
	u64 sum = 0;
	u32* data = (u32*)depv[0].ptr;
	for (std::size_t i = 0; i < 1024; ++i)
	{
		sum += data[i];
	}
	PRINTF("%lu\n", sum);
	ocrDbDestroy(depv[0].guid);
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test11_work(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u32* data = (u32*)depv[0].ptr;
	guid_t* guids = (guid_t*)depv[1].ptr;
	for (std::size_t i = 0; i < 512; ++i)
	{
		data[i] *= paramv[0];
	}
	ocrDbRelease(depv[0].guid);
	guid_t event;
	ocrDbCopy(guids[1], paramv[2] * sizeof(u32), depv[0].guid, 0, 512 * sizeof(u32), DB_COPY_PARTITION_BACK, &event);//DB_COPY_PARTITION_BACK entails destruction of the source
	ocrAddDependence(event, guids[0], paramv[1], DB_MODE_NULL);
	return NULL_GUID;
}

ocrGuid_t test11_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t block, params, worker_template, worker1, worker2, finish_template, finish_task, chunk1, chunk2, chunk1_copied, chunk2_copied;
	void *ptr, *params_ptr;
	ocrDbCreate(&block, &ptr, 1024 * sizeof(u32), 0, NULL_GUID, NO_ALLOC);
	u32* data = (u32*)ptr;
	for (u32 i = 0; i < 1024; ++i)
	{
		data[i] = 1;// i + 1;
	}
	ocrDbRelease(block);//could also come later, but the data is no longer needed
	ocrDbCreate(&params, &params_ptr, 2 * sizeof(guid_t), 0, NULL_GUID, NO_ALLOC);
	ocrDbCreate(&chunk1, &ptr, 512 * sizeof(u32), DB_PROP_NO_ACQUIRE, NULL_GUID, NO_ALLOC);//with DB_PROP_NO_ACQUIRE, the runtime may decide not to allocate any memory at this time
	ocrDbCreate(&chunk2, &ptr, 512 * sizeof(u32), DB_PROP_NO_ACQUIRE, NULL_GUID, NO_ALLOC);
	ocrDbCopy(chunk1, 0, block, 0, 512 * sizeof(u32), DB_COPY_PARTITION, &chunk1_copied);
	ocrDbCopy(chunk2, 0, block, 512 * sizeof(u32), 512 * sizeof(u32), DB_COPY_PARTITION, &chunk2_copied);
	ocrEdtTemplateCreate(&worker_template, test11_work, 3, 2);
	ocrEdtTemplateCreate(&finish_template, test11_finish, 0, 4);
	u64 params1[] = { 2, 1, 0 };
	u64 params2[] = { 6, 2, 512 };
	ocrEdtCreate(&finish_task, finish_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	ocrEdtCreate(&worker1, worker_template, EDT_PARAM_DEF, params1, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	ocrEdtCreate(&worker2, worker_template, EDT_PARAM_DEF, params2, EDT_PARAM_DEF, 0, 0, NULL_GUID, 0);
	((guid_t*)params_ptr)[0] = finish_task;
	((guid_t*)params_ptr)[1] = block;
	ocrDbRelease(params);
	ocrEdtTemplateDestroy(worker_template);
	ocrEdtTemplateDestroy(finish_template);
	ocrAddDependence(block, finish_task, 0, DB_MODE_RO);
	ocrAddDependence(params, finish_task, 3, DB_MODE_RO);
	ocrAddDependence(chunk1_copied, worker1, 0, DB_MODE_EW);
	ocrAddDependence(chunk2_copied, worker2, 0, DB_MODE_EW);
	ocrAddDependence(params, worker1, 1, DB_MODE_CONST);
	ocrAddDependence(params, worker2, 1, DB_MODE_CONST);
	return NULL_GUID;
}
#endif

#if(TEST_RANGE)
ocrGuid_t test12_producer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t range = *(guid_t*)depv[0].ptr;
	u64 idx = paramv[0];
	guid_t event;
	ocrGuidFromIndex(&event, range, idx);
	ocrEventCreate(&event, OCR_EVENT_STICKY_T, GUID_PROP_CHECK | EVT_PROP_TAKES_ARG);
	ocrEventSatisfy(event, depv[0].guid);
	return NULL_GUID;
}
ocrGuid_t test12_consumer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t range = *(guid_t*)depv[0].ptr;
	u64 idx = paramv[0];
	PRINTF("%i\n", (int)idx);
	guid_t event;
	ocrGuidFromIndex(&event, range, idx);
	ocrEventDestroy(event);
	ocrEventCreate(&event, OCR_EVENT_STICKY_T, GUID_PROP_CHECK | EVT_PROP_TAKES_ARG);
	ocrEventDestroy(event);
	ocrDbDestroy(depv[0].guid);
	return NULL_GUID;
}
ocrGuid_t test12_connecter(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t range = *(guid_t*)depv[0].ptr;
	u64 idx = paramv[0];
	guid_t consumer_template, consumer;
	ocrEdtTemplateCreate(&consumer_template, test12_consumer, 1, 1);
	ocrEdtCreate(&consumer, consumer_template, EDT_PARAM_DEF, paramv, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrEdtTemplateDestroy(consumer_template);
	guid_t event;
	ocrGuidFromIndex(&event, range, idx);
	ocrEventCreate(&event, OCR_EVENT_STICKY_T, GUID_PROP_CHECK | EVT_PROP_TAKES_ARG);
	ocrAddDependence(event, consumer, 0, DB_DEFAULT_MODE);
	return NULL_GUID;
}
ocrGuid_t test12_terminator(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test12_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t range,producer_template,connecter_template,producer,connecter,db, terminator_template, terminator, connector_event, starter;
	std::size_t count = 10;
	ocrGuidRangeCreate(&range, count, GUID_USER_EVENT_STICKY);
	guid_t* ptr;
	ocrDbCreate(&db, (void**)&ptr, sizeof(guid_t), DB_PROP_NONE, NULL_HINT, NO_ALLOC);
	*ptr = range;
	ocrDbRelease(db);
	ocrEdtTemplateCreate(&producer_template, test12_producer, 1, 1);
	ocrEdtTemplateCreate(&connecter_template, test12_connecter, 1, 1);
	ocrEdtTemplateCreate(&terminator_template, test12_terminator, 0, (u32)count);
	ocrEdtCreate(&terminator, terminator_template, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, 0, NULL_HINT, 0);
	ocrEventCreate(&starter, OCR_EVENT_ONCE_T, EVT_PROP_TAKES_ARG);
	for (std::size_t i = 0; i < count; ++i)
	{
		u64 params[] = { (u64)i };
		ocrEdtCreate(&producer, producer_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, &starter, EDT_PROP_NONE, NULL_HINT, 0);
		ocrEdtCreate(&connecter, connecter_template, EDT_PARAM_DEF, params, EDT_PARAM_DEF, &db, EDT_PROP_FINISH, NULL_HINT, &connector_event);
		ocrAddDependence(connector_event, terminator, (u32)i, DB_DEFAULT_MODE);
	}
	ocrEdtTemplateDestroy(producer_template);
	ocrEdtTemplateDestroy(connecter_template);
	ocrEventSatisfy(starter, db);
	return NULL_GUID;
}
#endif

#if (TEST_CHANNEL)
struct test13_params
{
	guid_t producerTML;
	guid_t consumerTML;
	guid_t channel;
};

ocrGuid_t test13_producer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = paramv[0];
	test13_params* paramsPTR = (test13_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	fake_work(1);
	PRINTF("produced %i\n", (int)count);
	--count;
	ocrEventSatisfy(paramsPTR->channel, NULL_GUID);
	if (count == 0) return NULL_GUID;
	guid_t continuation;
	ocrEdtCreate(&continuation, paramsPTR->producerTML, EDT_PARAM_DEF, &count, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, continuation, 0, DB_MODE_CONST);
	return NULL_GUID;
}

ocrGuid_t test13_consumer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = paramv[0];
	test13_params* paramsPTR = (test13_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	PRINTF("consume %i\n", (int)count);
	fake_work(1.5);
	--count;
	if (count == 0)
	{
		ocrShutdown();
		return NULL_GUID;
	}
	guid_t continuation;
	ocrEdtCreate(&continuation, paramsPTR->consumerTML, EDT_PARAM_DEF, &count, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, continuation, 0, DB_MODE_CONST);
	ocrAddDependence(paramsPTR->channel, continuation, 1, DB_DEFAULT_MODE);
	return NULL_GUID;
}

ocrGuid_t test13_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = 10;
	assert(count > 0);
	guid_t paramsDBK, producerEDT, consumerEDT;
	test13_params* paramsPTR;
	ocrDbCreate(&paramsDBK, (void**)&paramsPTR, sizeof(test13_params), 0, NULL_HINT, NO_ALLOC);
	ocrEdtTemplateCreate(&paramsPTR->producerTML, test13_producer, 1, 1);
	ocrEdtTemplateCreate(&paramsPTR->consumerTML, test13_consumer, 1, 2);
	ocrEventCreate(&paramsPTR->channel, OCR_EVENT_CHANNEL_T, EVT_PROP_TAKES_ARG);
	guid_t channel = paramsPTR->channel;

	ocrEdtCreate(&producerEDT, paramsPTR->producerTML, EDT_PARAM_DEF, &count, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrEdtCreate(&consumerEDT, paramsPTR->consumerTML, EDT_PARAM_DEF, &count, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrDbRelease(paramsDBK);
	ocrAddDependence(paramsDBK, producerEDT, 0, DB_MODE_CONST);
	ocrAddDependence(paramsDBK, consumerEDT, 0, DB_MODE_CONST);
	ocrAddDependence(channel, consumerEDT, 1, DB_DEFAULT_MODE);
	return NULL_GUID;
}
#endif

#if (TEST_CHANNEL && TEST_RANGE)
struct test14_params
{
	guid_t producerTML;
	guid_t consumerTML;
	guid_t channels;
	guid_t terminator;
};

ocrGuid_t test14_producer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = paramv[0];
	u64 chain = paramv[1];
	test14_params* paramsPTR = (test14_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	PRINTF("producer %i,%i\n", (int)chain, (int)count);
	fake_work(1);
	--count;
	guid_t channel;
	ocrGuidFromIndex(&channel, paramsPTR->channels, chain);
	ocrEventSatisfy(channel, NULL_GUID);
	if (count == 0) return NULL_GUID;
	guid_t continuation;
	u64 params[] = { count, chain };
	ocrEdtCreate(&continuation, paramsPTR->producerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, continuation, 0, DB_MODE_CONST);
	return NULL_GUID;
}

ocrGuid_t test14_consumer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = paramv[0];
	u64 chain = paramv[1];
	test14_params* paramsPTR = (test14_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	PRINTF("consumer %i,%i\n", (int)chain, (int)count);
	fake_work(1.5);
	--count;
	if (count == 0)
	{
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_DECR_SLOT);
		return NULL_GUID;
	}
	guid_t continuation, channel;
	ocrGuidFromIndex(&channel, paramsPTR->channels, chain);
	u64 params[] = { count, chain };
	ocrEdtCreate(&continuation, paramsPTR->consumerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, continuation, 0, DB_MODE_CONST);
	ocrAddDependence(channel, continuation, 1, DB_DEFAULT_MODE);
	return NULL_GUID;
}

ocrGuid_t test14_terminate(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}

ocrGuid_t test14_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = 10;
	u64 chains = 2;
	assert(count > 0);
	guid_t paramsDBK, producerEDT, consumerEDT, terminateTML, terminateEDT, channel;
	test14_params* paramsPTR;
	ocrDbCreate(&paramsDBK, (void**)&paramsPTR, sizeof(test14_params), 0, NULL_HINT, NO_ALLOC);
	ocrEdtTemplateCreate(&paramsPTR->producerTML, test14_producer, 2, 1);
	ocrEdtTemplateCreate(&paramsPTR->consumerTML, test14_consumer, 2, 2);
	ocrEdtTemplateCreate(&terminateTML, test14_terminate, 0, 1);
	ocrGuidRangeCreate(&paramsPTR->channels, chains, GUID_USER_EVENT_CHANNEL);
	ocrEventCreate(&paramsPTR->terminator, OCR_EVENT_LATCH_T, EVT_PROP_NONE);
	ocrEdtCreate(&terminateEDT, terminateTML, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsPTR->terminator, terminateEDT, 0, DB_DEFAULT_MODE);
	for (u64 chain = 0; chain < chains; ++chain)
	{
		u64 params[] = { count, chain };
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
		ocrEdtCreate(&producerEDT, paramsPTR->producerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
		ocrEdtCreate(&consumerEDT, paramsPTR->consumerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
		ocrGuidFromIndex(&channel, paramsPTR->channels, chain);
		ocrEventCreate(&channel, OCR_EVENT_CHANNEL_T, GUID_PROP_IS_LABELED | EVT_PROP_TAKES_ARG);
		ocrAddDependence(channel, consumerEDT, 1, DB_DEFAULT_MODE);
		ocrAddDependence(paramsDBK, producerEDT, 0, DB_MODE_CONST);
		ocrAddDependence(paramsDBK, consumerEDT, 0, DB_MODE_CONST);
	}

	return NULL_GUID;
}
#endif

#if (TEST_RANGE)
struct test15_params
{
	guid_t range;
	guid_t workerTML;
	guid_t terminator;
};

ocrGuid_t test15_worker(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = paramv[0];
	u64 chain = paramv[1];
	test15_params* paramsPTR = (test15_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	PRINTF("worker %i,%i\n", (int)chain, (int)count);
	fake_work(1);
	--count;
	if (count == 0)
	{
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_DECR_SLOT);
		return NULL_GUID;
	}
	guid_t workerEDT;
	ocrGuidFromIndex(&workerEDT, paramsPTR->range, chain);
	u64 params[] = { count, chain };
	ocrEdtCreate(&workerEDT, paramsPTR->workerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, GUID_PROP_IS_LABELED, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, workerEDT, 0, DB_MODE_CONST);
	return NULL_GUID;
}

ocrGuid_t test15_terminate(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}
ocrGuid_t test15_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 count = 10;
	u64 chains = 2;
	assert(count > 0);
	guid_t paramsDBK, terminateTML, terminateEDT;
	test15_params* paramsPTR;
	ocrDbCreate(&paramsDBK, (void**)&paramsPTR, sizeof(test15_params), 0, NULL_HINT, NO_ALLOC);
	ocrEdtTemplateCreate(&paramsPTR->workerTML, test15_worker, 2, 1);
	ocrEdtTemplateCreate(&terminateTML, test15_terminate, 0, 1);
	ocrGuidRangeCreate(&paramsPTR->range, chains, GUID_USER_EDT);
	ocrEventCreate(&paramsPTR->terminator, OCR_EVENT_LATCH_T, EVT_PROP_NONE);
	ocrEdtCreate(&terminateEDT, terminateTML, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsPTR->terminator, terminateEDT, 0, DB_DEFAULT_MODE);
	for (u64 chain = 0; chain < chains; ++chain)
	{
		guid_t workerEDT;
		ocrGuidFromIndex(&workerEDT, paramsPTR->range, chain);
		u64 params[] = { count, chain };
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
		ocrEdtCreate(&workerEDT, paramsPTR->workerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, GUID_PROP_IS_LABELED, NULL_HINT, 0);
		ocrAddDependence(paramsDBK, workerEDT, 0, DB_MODE_CONST);
	}

	return NULL_GUID;
}
#endif
#if (TEST_RANGE)
struct test16_params
{
	guid_t range;
	guid_t range_db;
	guid_t workerTML;
	guid_t terminator;
};

ocrGuid_t test16_worker(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 it = paramv[0];
	u64 iterations = paramv[1];
	u64 chain = paramv[2];
	u64 chains = paramv[3];
	test16_params* paramsPTR = (test16_params*)depv[0].ptr;
	guid_t paramsDBK = depv[0].guid;
	u64* dataPTR = (u64*)depv[1].ptr;
	guid_t dataDBK = depv[1].guid;
	PRINTF("worker %i,%i: %i (should be %i)\n", (int)chain, (int)it, (int)dataPTR[0], (int)((chain+it)%chains));
	fake_work(1);
	++it;
	if (it == iterations)
	{
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_DECR_SLOT);
		return NULL_GUID;
	}
	guid_t workerEDT;
	ocrGuidFromIndex(&workerEDT, paramsPTR->range, chain);
	u64 params[] = { it, iterations, chain, chains };
	ocrEdtCreate(&workerEDT, paramsPTR->workerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, GUID_PROP_IS_LABELED, NULL_HINT, 0);
	ocrAddDependence(paramsDBK, workerEDT, 0, DB_MODE_CONST);
	guid_t next_data;
	ocrGuidFromIndex(&next_data, paramsPTR->range_db, (chain + it) % chains);
	ocrAddDependence(next_data, workerEDT, 1, DB_MODE_CONST);
	return NULL_GUID;
}

ocrGuid_t test16_terminate(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}
ocrGuid_t test16_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64 iterations = 10;
	u64 chains = 4;
	assert(iterations > 0);
	guid_t paramsDBK, terminateTML, terminateEDT;
	test16_params* paramsPTR;
	ocrDbCreate(&paramsDBK, (void**)&paramsPTR, sizeof(test16_params), 0, NULL_HINT, NO_ALLOC);
	ocrEdtTemplateCreate(&paramsPTR->workerTML, test16_worker, 4, 2);
	ocrEdtTemplateCreate(&terminateTML, test16_terminate, 0, 1);
	ocrGuidRangeCreate(&paramsPTR->range, chains, GUID_USER_EDT);
	ocrGuidRangeCreate(&paramsPTR->range_db, chains, GUID_USER_DB);
#ifdef ENABLE_EXTENSION_PARAMS_EVT
	ocrEventParams_t eparams;
	eparams.EVENT_LATCH.counter = chains;
	ocrEventCreateParams(&paramsPTR->terminator, OCR_EVENT_LATCH_T, EVT_PROP_NONE, &eparams);
#else
	ocrEventCreate(&paramsPTR->terminator, OCR_EVENT_LATCH_T, EVT_PROP_NONE);
#endif
	ocrEdtCreate(&terminateEDT, terminateTML, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	ocrAddDependence(paramsPTR->terminator, terminateEDT, 0, DB_DEFAULT_MODE);
	for (u64 chain = 0; chain < chains; ++chain)
	{
		guid_t dataDBK;
		u64* dataPTR;
		ocrGuidFromIndex(&dataDBK, paramsPTR->range_db, chain);
		ocrDbCreate(&dataDBK, (void**)&dataPTR, sizeof(u64), GUID_PROP_IS_LABELED, NULL_HINT, NO_ALLOC);
		dataPTR[0] = chain;
		ocrDbRelease(dataDBK);
	}
	for (u64 chain = 0; chain < chains; ++chain)
	{
		guid_t workerEDT, dataDBK;
		ocrGuidFromIndex(&workerEDT, paramsPTR->range, chain);
		ocrGuidFromIndex(&dataDBK, paramsPTR->range_db, chain);
		u64 params[] = { 0, iterations, chain, chains };
#ifndef ENABLE_EXTENSION_PARAMS_EVT
		ocrEventSatisfySlot(paramsPTR->terminator, NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
#endif
		ocrEdtCreate(&workerEDT, paramsPTR->workerTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, GUID_PROP_IS_LABELED, NULL_HINT, 0);
		ocrAddDependence(paramsDBK, workerEDT, 0, DB_MODE_CONST);
		ocrAddDependence(dataDBK, workerEDT, 1, DB_MODE_CONST);
	}

	return NULL_GUID;
}
#endif

struct config { ocrGuid_t inc, dec; };

ocrGuid_t latch_test(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	config* cfg = (config*)depv[0].ptr;
	ocrEventSatisfy(cfg->inc, NULL_GUID);
	ocrEventSatisfy(cfg->inc, NULL_GUID);
	return NULL_GUID;
}

#if (TEST_BYVALUEDB)
ocrGuid_t test17_read(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	u64* ptr = (u64*)depv[0].ptr;
	std::cout << paramv[0]<< ": " << *ptr << " at " << (void*)ptr << std::endl;
	ocrDbDestroy(depv[0].guid);
	return NULL_GUID;
}

ocrGuid_t test17_terminate(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrShutdown();
	return NULL_GUID;
}
ocrGuid_t test17_start(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	guid_t dataDBK;
	u64* dataPTR;
	ocrDbCreate(&dataDBK, (void**)&dataPTR, sizeof(u64), DB_PROP_NONE, NULL_HINT, NO_ALLOC);
	guid_t terminateTML, terminateEDT, readTML;
	dataPTR[0] = 1;
	u32 count = 16;
	ocrEdtTemplateCreate(&terminateTML, test17_terminate, 0, count);
	ocrEdtTemplateCreate(&readTML, test17_read, 1, 1);
	ocrEdtCreate(&terminateEDT, terminateTML, EDT_PARAM_DEF, 0, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, 0);
	std::vector<guid_t> workers;
	for (u32 i = 0; i < count; ++i)
	{
		guid_t workEDT, workEVT;
		u64 params[] = { i };
		ocrEdtCreate(&workEDT, readTML, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, EDT_PROP_NONE, NULL_HINT, &workEVT);
		ocrAddDependence(workEVT, terminateEDT, i, DB_MODE_NULL);
		workers.push_back(workEDT);
	}
	std::cout << "created at " << (void*)dataPTR << std::endl;
	ocrGroupBegin();
	for (u32 i = 0; i < count; ++i)
	{
		ocrAddDependenceByValue(dataDBK, workers[(std::size_t)i], 0, DB_MODE_CONST);
	}
	//ocrDbRelease(dataDBK);
	ocrDbDestroy(dataDBK);
	ocrGroupEnd();

	return NULL_GUID;
}
#endif

extern "C" ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	if (getArgc(depv[0].ptr) != 2)
	{
		std::cout << "Specify the test to run:" << std::endl;
		std::cout << "1 - test affinity and exclusive data access" << std::endl;
		std::cout << "2 - test affinity and fork-join" << std::endl;
#if(OCR_WITH_OPENCL)
		std::cout << "3 - test OpenCL exclusive" << std::endl;
		std::cout << "4 - test OpenCL shared" << std::endl;
#endif
#if(TEST_BOBOX)
		std::cout << "5 - Bobox" << std::endl;
#endif
#if (TEST_MAP)
		std::cout << "6 - test labeled map interface" << std::endl;
		std::cout << "7 - test labeled map interface" << std::endl;
#endif
#if (TEST_FILE)
		std::cout << "8 - test file (create)" << std::endl;
		std::cout << "9 - test file (update)" << std::endl;
#endif
#if (TEST_PARTITION)
		std::cout << "10 - test DB partitioning" << std::endl;
		std::cout << "11 - test partitioning with ocrDbCopy" << std::endl;
#endif
#if (TEST_RANGE)
		std::cout << "12 - test range-based labels" << std::endl;
#endif
#if (TEST_CHANNEL)
		std::cout << "13 - test channel events" << std::endl;
#endif
#if (TEST_CHANNEL && TEST_RANGE)
		std::cout << "14 - test labeled channel events" << std::endl;
#endif
#if (TEST_RANGE)
		std::cout << "15 - test range-based labels with EDTs" << std::endl;
#endif
#if (TEST_RANGE)
		std::cout << "16 - test range-based labels with DBs" << std::endl;
#endif
#if (TEST_BYVALUEDB)
		std::cout << "17 - test by-value DBs" << std::endl;
#endif
		ocrShutdown();
		return NULL_GUID;
	}
	int test = atoi(getArgv(depv[0].ptr, 1));
	ocrDbDestroy(depv[0].guid);
	switch (test)
	{
	case 1: return test1_start(paramc, paramv, depc, depv);
	case 2: return test2_start(paramc, paramv, depc, depv);
#if(OCR_WITH_OPENCL)
	case 3: return test3_start(paramc, paramv, depc, depv);
	case 4: return test4_start(paramc, paramv, depc, depv);
#endif
#if(TEST_BOBOX)
	case 5: return test5_start(paramc, paramv, depc, depv);
#endif
#if (TEST_MAP)
	case 6: return test6_start(paramc, paramv, depc, depv);
	case 7: return test7_start(paramc, paramv, depc, depv);
#endif
#if (TEST_FILE)
	case 8: return test8_start(paramc, paramv, depc, depv);
	case 9: return test9_start(paramc, paramv, depc, depv);
#endif
#if (TEST_PARTITION)
	case 10: return test10_start(paramc, paramv, depc, depv);
	case 11: return test11_start(paramc, paramv, depc, depv);
#endif
#if (TEST_RANGE)
	case 12: return test12_start(paramc, paramv, depc, depv);
#endif
#if (TEST_CHANNEL)
	case 13: return test13_start(paramc, paramv, depc, depv);
#endif
#if (TEST_CHANNEL && TEST_RANGE)
	case 14: return test14_start(paramc, paramv, depc, depv);
#endif
#if (TEST_RANGE)
	case 15: return test15_start(paramc, paramv, depc, depv);
#endif
#if (TEST_RANGE)
	case 16: return test16_start(paramc, paramv, depc, depv);
#endif
#if (TEST_BYVALUEDB)
	case 17: return test17_start(paramc, paramv, depc, depv);
#endif
	}
	std::cout << "Invalid test number" << std::endl;
	ocrShutdown();
	return NULL_GUID;

}

#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
extern "C" void registerEdtFunctions()
{
	ocrRegisterEdtFuntion(mainEdt);
	ocrRegisterEdtFuntion(test1_nop);
	ocrRegisterEdtFuntion(test1_produce);
	ocrRegisterEdtFuntion(test1_consume);
	ocrRegisterEdtFuntion(test2_work);
	ocrRegisterEdtFuntion(test2_fork);
	ocrRegisterEdtFuntion(test2_join);
#if(TEST_BOBOX)
	ocrRegisterEdtFuntion(bobox_run_box);
	ocrRegisterEdtFuntion(bobox_run_prefix);
	ocrRegisterFunctionPointer((ocrFuncPtr_t)&bobox_init_box_action);
	ocrRegisterFunctionPointer((ocrFuncPtr_t)&bobox_term_box_action);
	ocrRegisterFunctionPointer((ocrFuncPtr_t)&test5_generate_box_action);
	ocrRegisterFunctionPointer((ocrFuncPtr_t)&test5_sum_box_action);
#endif
#if (TEST_RANGE)
	ocrRegisterEdtFuntion(test12_producer);
	ocrRegisterEdtFuntion(test12_consumer);
	ocrRegisterEdtFuntion(test12_connecter);
	ocrRegisterEdtFuntion(test12_terminator);
#endif
#if (TEST_CHANNEL)
	ocrRegisterEdtFuntion(test13_producer);
	ocrRegisterEdtFuntion(test13_consumer);
#endif
#if (TEST_CHANNEL && TEST_RANGE)
	ocrRegisterEdtFuntion(test14_producer);
	ocrRegisterEdtFuntion(test14_consumer);
	ocrRegisterEdtFuntion(test14_terminate);
#endif
#if (TEST_RANGE)
	ocrRegisterEdtFuntion(test15_worker);
	ocrRegisterEdtFuntion(test15_terminate);
#endif
#if (TEST_RANGE)
	ocrRegisterEdtFuntion(test16_worker);
	ocrRegisterEdtFuntion(test16_terminate);
#endif
#if (TEST_BYVALUEDB)
	ocrRegisterEdtFuntion(test17_read);
	ocrRegisterEdtFuntion(test17_terminate);
#endif
}
#endif
