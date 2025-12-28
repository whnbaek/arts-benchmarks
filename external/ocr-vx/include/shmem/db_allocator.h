/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_db_allocator_H_GUARD
#define OCR_TBB_db_allocator_H_GUARD

#include <cassert>

namespace ocr_tbb
{
	//size_t offsets, locked with spin_mutex, first fit, defragmentation if no large enough block is available
	struct db_allocator
	{
		db_allocator(void* buffer, std::size_t size) : buffer_((char*)buffer), size_(size)
		{
			assert(size == (offset_t)size);
			assert(size >= header_size);
			ref<char>(0) = 0;
			ref<offset_t>(1) = (offset_t)size - header_size;
		}
		void* malloc(std::size_t size)
		{
			lock_t lock(mutex_);
			void* res = try_malloc(size);
			if (res) return res;
			defragment_intenal();
			return try_malloc(size);
		}
		void free(void* ptr)
		{
			//lock_t lock(mutex_); -- we only set one byte, which happens atomically, so no need to lock here
			if (!ptr) return;
			offset_t off = (offset_t)((char*)ptr - buffer_);
			ref<char>(off - header_size) = 0;
		}
		void defragment()
		{
			lock_t lock(mutex_);
			defragment_intenal();
		}
	private:
		void defragment_intenal()
		{
			offset_t last_empty = offset_t(-1);
			offset_t off = 0;
			while (off < size_)
			{
				bool used = !!ref<char>(off);
				offset_t block_size = ref<offset_t>(off + 1);
				if (!used)
				{
					if (last_empty == offset_t(-1))
					{
						last_empty = off;
					}
					else
					{
						ref<offset_t>(last_empty + 1) += block_size + header_size;
					}
				}
				else
				{
					last_empty = offset_t(-1);
				}
				off += block_size + header_size;
			}
		}
		template<typename T> T& ref(std::size_t off) { assert(off + sizeof(T) <= size_); return *(T*)(buffer_ + off); }
		void* try_malloc(std::size_t size)
		{
			offset_t off = 0;
			for (;;)
			{
				bool used = !!ref<char>(off);
				offset_t block_size = ref<offset_t>(off + 1);
				if (!used && (std::size_t)block_size >= (size + header_size))
				{
					//the block is large enoug
					ref<char>(off) = 1;
					ref<offset_t>(off + 1) = (offset_t)size;
					void* res = buffer_ + header_size + off;
					off += header_size + size;
					block_size -= header_size + size;
					if (block_size > 0)
					{
						//if we chop the block, we still have some space left
						ref<char>(off) = 0;
						ref<offset_t>(off + 1) = (offset_t)block_size;
					}
					return res;
				}
				off += header_size + block_size;
				if (off == size_) return 0;
			}
		}

		typedef std::size_t offset_t;
		typedef tbb::spin_mutex mutex_t;
		typedef mutex_t::scoped_lock lock_t;
		static const std::size_t header_size = 1 + sizeof(offset_t);
		char* buffer_;
		std::size_t size_;
		mutex_t mutex_;
	};
}

#endif
