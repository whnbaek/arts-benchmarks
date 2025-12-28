/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__file_io_H_GUARD
#define OCR_V1__file_io_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct file : public guided
		{
			file(guid_t guid, FILE* f) : guided(guid, G_file), f_(f), is_ok_(!!f), destroy_(false), refs_(0)
			{
				fseek(f, 0, SEEK_END);
				size_ = ftell(f);
				fseek(f, 0, SEEK_SET);
			}
			void add_ref(db* data)
			{
				++refs_;
			}
			void remove_ref(db* data)
			{
				assert(refs_ > 0);
				--refs_;
				if (refs_ == 0 && destroy_) handle_delete();
			}
			void destroy()
			{
				destroy_ = true;
				if (refs_ == 0) handle_delete();
			}
		private:
			FILE* f_;
			bool is_ok_;
			bool destroy_;
			u64 size_;
			u64 refs_;
			friend struct db;
		};
	}
}
#endif
