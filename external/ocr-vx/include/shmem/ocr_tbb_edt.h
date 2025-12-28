/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_edt_H_GUARD
#define OCR_TBB_ocr_tbb_edt_H_GUARD

namespace ocr_tbb
{
	struct edt_template : public guided
	{
		edt_template(ocrEdt_t func, u32 paramc, u32 depc, const char* name) : guided(G_edt_template), func_(func), paramc_(paramc), depc_(depc), name_(name)
		{}
		ocrEdt_t func_;
		u32 paramc_;
		u32 depc_;
		const char* name_;
		static void* operator new(std::size_t size)
		{
			return memory::manager::allocate_object<edt_template>();
		}
		static void operator delete (void *p)
		{
			memory::manager::free_object<event>(p);
		}
	};

	struct edt : public node
	{
		edt(edt_template* t, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrHint_t* hint, event* e);
		~edt()
		{
		}
		db* get_held_db(ocrGuid_t guid);
		void add_db(db* ptr, access_mode_t mode)
		{
			if (ptr)
			{
				ptr->add_ref();
				held_dbs_.push_back(ptr);
				held_db_modes_.push_back(mode);
			}
		}
		void remove_db(ocrGuid_t guid);
		void clear_dbs()
		{
			for (std::size_t i = 0; i < held_dbs_.size(); ++i) if (held_dbs_[i]) held_dbs_[i]->dec_ref();
			held_dbs_.clear();
			held_db_modes_.clear();
		}
		bool is_held_db(guid_t guid)
		{
			if (guid == NULL_GUID) return 0;
			for (std::size_t i = 0; i < held_dbs_.size(); ++i) if (held_dbs_[i]->guid() == guid) return true;
			return false;
		}
		void spawn();
		bool is_fake()
		{
			return func_ == 0;
		}
		u32 decrement_ref_count() { return --reference_count_; }

		std::deque<db*> held_dbs_;
		std::deque<access_mode_t> held_db_modes_;
		ocrEdt_t func_;
		guid_t template_guid;
		std::vector<u64> params_;
		bool is_destroyed_;
		tbb::atomic<u32> reference_count_;
		guid_t finish_for_children_;
		u16 properties;
#if(COLLECT_PTRACE!=0)
		struct trace_data_type
		{
			trace_data_type() : created_task_id(0), created_db_id(0), id(0) {}
			std::string debug_name;
			std::vector<u64> debug_label;
			std::string preattached_debug_name;//this is not the name atteached to this task, but rather the one that should be attached to the next DB that will be created by this task
			std::vector<u64> preattached_debug_label;//dtto
			u64 created_task_id;
			u64 created_db_id;
			u64 id;
		};
		trace_data_type trace_data;
#endif
#if(USE_PLAN==2)
		struct guide_data_type
		{
			guide_data_type() : node_id(0), created_task_id(0), created_db_id(0) {}
			s32 node_id;
			u64 created_task_id;
			u64 created_db_id;
		};
		guide_data_type guide_data;
#endif
		ocrHint_t hint;
		static void* operator new(std::size_t size)
		{
			return memory::manager::allocate_object<edt>();
		}
		static void operator delete (void *p)
		{
			memory::manager::free_object<edt>(p);
		}
	};
}

#endif
