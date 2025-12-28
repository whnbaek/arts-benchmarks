/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_memory_H_GUARD
#define OCR_TBB_ocr_tbb_memory_H_GUARD

//#define ALLOCATOR std::allocator
//#define MALLOC malloc
//#define FREE free

//#define ALLOCATOR tbb::scalable_allocator
//#define MALLOC scalable_malloc
//#define FREE scalable_free

#if (ALLOCATOR_INT==2 || ALLOCATOR_HBW)
#include <hbwmalloc.h>
#endif
#if(ALLOCATOR_INT==3)
#include <numa.h>
#endif
namespace ocr_tbb
{
	namespace memory
	{
#if (ALLOCATOR_STD)
		template<typename T>
		struct allocator : public std::allocator<T> {};
#endif
#if (ALLOCATOR_TBB==1)
		template<typename T>
		struct allocator : public tbb::scalable_allocator<T> {};
#endif
#if (ALLOCATOR_TBB==2)
		template<typename T>
		struct allocator : public tbb::cache_aligned_allocator<T> {};
#endif
#if (ALLOCATOR_HBW==1)
		template<typename T>
		struct allocator : public tbb::scalable_allocator<T> {};
#endif

		//typedef tbb::scalable_allocator<int> allocator;

		template<typename T>
		struct list
		{
			typedef std::list<edt*, allocator<edt*> > the;
		};

		struct manager
		{
			static void* allocate_db_buffer(std::size_t size, std::size_t allignment, std::size_t padding, u64 affinity);
			static void free_db_buffer(void* ptr, std::size_t size, std::size_t allignment, std::size_t padding);
			template<typename T>
			static T* allocate_object()
			{
				T* res = (T*)the().malloc(sizeof(T), 0, 0);
				if (!res) throw std::bad_alloc();
				return res;
			}
			template<typename T>
			static void free_object(void* obj)
			{
				the().free(obj);
			}
			static void free_object_by_ptr(void* obj)
			{
				the().free(obj);
			}
		private:
			static manager& the()
			{
				return the_;
			}
			static manager the_;
			void* malloc(std::size_t size, std::size_t allignment, std::size_t padding)
			{
#if (ALLOCATOR_STD)
				return ::malloc(size + allignment + padding);
#endif
#if (ALLOCATOR_TBB==1)
				return scalable_malloc(size + allignment + padding);
#endif
#if (ALLOCATOR_TBB==2)
				std::size_t actual_size = size + allignment + padding;
				if (actual_size < 64) actual_size = 64;//bump up to the cacheline size and scalable_malloc (at least now) aligns it properly for us
				return scalable_malloc(actual_size);
#endif
#if (ALLOCATOR_HBW==1)
				return hbw_malloc(size + allignment + padding);
#endif
			}
			void free(void* ptr)
			{
				
#if (ALLOCATOR_STD)
				::free(ptr);
#endif
#if (ALLOCATOR_TBB==1)
				scalable_free(ptr);
#endif
#if (ALLOCATOR_TBB==2)
				scalable_free(ptr);//it was allocated with scalable_malloc anyway
#endif
#if (ALLOCATOR_HBW==1)
				return hbw_free(ptr);
#endif
			}
		};
	}
}

#endif
