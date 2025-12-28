/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_db_H_GUARD
#define OCR_TBB_ocr_tbb_db_H_GUARD

namespace ocr_tbb
{
	struct db_data
	{
		db_data(bool lock)
		{
			assert(DB_DEFAULT_MODE == DB_MODE_RW);
			if (lock) shared_locks = 1;//the lock of the owner
			else shared_locks = 0;
			exclusive_locks = 0;
			exclusive_write = false;
		}
		tbb::atomic<u32> shared_locks;
		tbb::atomic<u32> exclusive_locks;
		tbb::atomic<bool> exclusive_write;
		typename memory::list<edt*>::the shared_waitlist;
		typename memory::list<edt*>::the exclusive_read_waitlist;
		typename memory::list<edt*>::the exclusive_write_waitlist;
		tbb::spin_mutex mutex;
	};

	struct db : public guided
	{
		struct alligned_buffer
		{
			static const std::size_t alignment = 4096;
			static const std::size_t padding = 4096;
			alligned_buffer(std::size_t size, u64 affinity) : len_(size)
			{
				whole_buffer_ = (char*)memory::manager::allocate_db_buffer(size, alignment, padding, affinity);
			}
			~alligned_buffer()
			{
				memory::manager::free_db_buffer(whole_buffer_, len_, alignment, padding);
			}
			char* ptr()
			{
				uintptr_t res = (uintptr_t)whole_buffer_;
				res += alignment;
				res &= ~(alignment - 1);
				return (char*)res;
			}
			std::size_t padded_len()
			{
				if ((len_ & (padding - 1)) == 0) return (std::size_t)len_;
				std::size_t res = (std::size_t)len_ + padding;
				res &= ~(padding - 1);
				return res;
			}
			std::size_t len_;
			char* whole_buffer_;

		};
		db(u64 len, ocrInDbAllocator_t allocator, bool lock, u64 affinity) : guided(G_db), len_(len), buffer_(static_cast<std::size_t>(len), affinity), allocator_(allocator), wfg_data_(lock)
		{
#if (WITH_ALLOCATORS)
			if (allocator == SCALABLE_ALLOC)
			{
				try
				{
					pool_ = std::unique_ptr<tbb::fixed_pool>(new tbb::fixed_pool(buffer_.ptr(), static_cast<std::size_t>(len)));
				}
				catch (...)
				{
					assert(0);
				}
			}
			else if (allocator == SIMPLE_ALLOC)
			{
				try
				{
					simple_pool_ = std::unique_ptr<db_allocator>(new db_allocator(buffer_.ptr(), static_cast<std::size_t>(len)));
				}
				catch (...)
				{
					assert(0);
				}
			}
#endif
			reference_count_ = 1;
		}
		void add_ref()
		{
			++reference_count_;
		}
		void dec_ref();
		u64 len_;
		alligned_buffer buffer_;
		ocrInDbAllocator_t allocator_;
		std::unique_ptr<tbb::fixed_pool> pool_;
		std::unique_ptr<db_allocator> simple_pool_;
		char* ptr()
		{
			return buffer_.ptr();
		}
		std::size_t padded_len()
		{
			return buffer_.padded_len();
		}
		bool has_allocator()
		{
			return allocator_ != NO_ALLOC;
		}
		void* internal_malloc(std::size_t size)
		{
#if (WITH_ALLOCATORS)
			switch (allocator_)
			{
			case SCALABLE_ALLOC: return pool_->malloc(size);
			case SIMPLE_ALLOC: return simple_pool_->malloc(size);
			}
#endif
			assert(0);
			return 0;
		}
		void internal_free(void* ptr)
		{
#if (WITH_ALLOCATORS)
			switch (allocator_)
			{
			case SCALABLE_ALLOC: return pool_->free(ptr);
			case SIMPLE_ALLOC: return simple_pool_->free(ptr);
			}
#endif
			assert(0);
		}
		tbb::atomic<u32> reference_count_;
		db_data wfg_data_;
#if(COLLECT_PTRACE!=0)
		struct trace_data_type
		{
			trace_data_type() : id_(0) {}
			std::string debug_name_;
			std::vector<u64> debug_label_;
			u64 id_;
			u64 seq_id_;
		};
		trace_data_type trace_data_;
#endif
		static void* operator new(std::size_t size)
		{
			return memory::manager::allocate_object<db>();
		}
		static void operator delete (void *p)
		{
			memory::manager::free_object<db>(p);
		}
	};

}
#endif
