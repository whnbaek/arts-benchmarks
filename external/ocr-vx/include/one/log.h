/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__log_H_GUARD
#define OCR_V1__log_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct log
		{
			static void shutdown_ready_tasks_remain(std::size_t count)
			{
				std::cerr << "WARNING: ready tasks still remain at shutdown (count " << count << ")" << std::endl;
			}
			static void redefined_preslot(guid_t guid, u32 slot)
			{
				std::cerr << "WARNING: redefining preslot " << slot << " of object " << guid << ", which has already been set before" << std::endl;
			}
			static void db_acquired_multiple_times(guid_t edt, guid_t db, u32 slot)
			{
				std::cerr << "WARNING: edt " << edt << " acquired data block " << db << " multiple times (second was on slot " << slot << ")" << std::endl;
			}
			static void aborted(u8 code)
			{
				std::cerr << "WARNING: execution was aborted by the application with coude " << code << std::endl;
			}
		};
	}
}

#endif
