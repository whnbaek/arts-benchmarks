/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__db_H_GUARD
#define OCR_V1__db_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct db : public guided
		{
			db(guid_t guid, std::size_t size) : guided(guid, G_db), size_(size), buffer_private_(size), acquired_(false), file_(0), parent_(0), destroy_(false), refs_(0) {}
			db(guid_t guid, std::size_t size, file* f, u64 offset);
			db(guid_t guid, std::size_t size, db* parent, u64 offset) : guided(guid, G_db), size_(size), buffer_public_(size), acquired_(false), file_(0), parent_(parent), offset_(offset), destroy_(false), refs_(0)
			{
				::memcpy(ptr_private(), ((char*)parent_->ptr_private()) + offset_, size_);
			}
			void* ptr_public()
			{
				assert(acquired_);
				return &buffer_public_.front();
			}
			void* ptr_private() { return &buffer_private_.front(); }
			u64 size() { return (u64)size_; }
			ocrDbAccessMode_t acquire_mode() { return acquire_mode_; }
			void acquire(ocrDbAccessMode_t mode, guid_t owner)
			{
				if (owner)
				{
					if (dump::trace::dbs)
					{
						deid de1 = dump::event("acquire", owner, guid(), (u64)mode);
						if (dump::trace::standalone_dbs)
						{
							deid de2 = dump::event("acquired", guid(), owner, (u64)mode);
							dump::edge(de1, de2);
						}
					}
				}
				ocr_assert(!acquired_, "DB already acquired");
				acquired_ = true;
				acquire_mode_ = mode;
				owner_ = owner;
				assert(buffer_public_.empty());
				buffer_public_.resize(size_);
				::memcpy(ptr_public(), ptr_private(), size_);
			}
			bool was_modified()
			{
				assert(acquired_);
				int dif = ::memcmp(ptr_private(), ptr_public(), size_);
				return dif != 0;
			}
			void release()
			{
				if (owner_)
				{
					if (dump::trace::dbs)
					{
						deid de1 = dump::event("release", owner_, guid(), (u64)acquire_mode_);
						if (dump::trace::standalone_dbs)
						{
							deid de2 = dump::event("released", guid(), owner_, (u64)acquire_mode_);
							dump::edge(de1, de2);
						}
					}
				}
				ocr_assert(acquired_, "DB not acquired");
				if (acquire_mode_ == DB_MODE_RW || acquire_mode_ == DB_MODE_EW)
				{
					::memcpy(ptr_private(), ptr_public(), size_);
				}
				else
				{
					assert(acquire_mode_ == DB_MODE_CONST || acquire_mode_ == DB_MODE_RO);
					ocr_assert(!was_modified(), "read-only data block was modified by the EDT");
				}
				for (std::size_t i = 0; i < size_; ++i) (unsigned char&)buffer_public_[i] = 0xfe;
				std::vector<char> empty;
				buffer_public_.swap(empty);
				acquired_ = false;
			}
			void add_partition(db*) { refs_++; }
			void remove_partition(db* part)
			{
				assert(refs_ > 0);
				--refs_;
				if (refs_ == 0 && destroy_)
				{
					assert(0);
				}
			}
			void prepare_destroy();
			bool is_acquired() { return acquired_; }
		private:
			std::size_t size_;
			std::vector<char> buffer_private_;
			std::vector<char> buffer_public_;
			file* file_;
			db* parent_;
			u64 offset_;
			bool destroy_;
			u64 refs_;
			bool acquired_;
			guid_t owner_;
			ocrDbAccessMode_t acquire_mode_;
		};
	}
}
#endif