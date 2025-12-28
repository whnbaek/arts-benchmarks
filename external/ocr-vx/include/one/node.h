/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__node_H_GUARD
#define OCR_V1__node_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct node : public guided
		{
			struct preslot_t
			{
				preslot_t() : mode(DB_DEFAULT_MODE), source(NULL_GUID), defined(false), satisfied(false), data(NULL_GUID) {}
				preslot_t(ocrDbAccessMode_t mode) : mode(mode), source(NULL_GUID), defined(true), satisfied(false), data(NULL_GUID) {}
				preslot_t(ocrDbAccessMode_t mode, guid_t source) : mode(mode), source(source), defined(true), satisfied(false), data(NULL_GUID) {}
				ocrDbAccessMode_t mode;
				guid_t source;
				bool defined;
				bool satisfied;
				guid_t data;
				deid e_satisfy;
			};
			struct postslot_t
			{
				postslot_t(guid_t destination, u32 preslot_index, ocrDbAccessMode_t preslot_mode, u64 defining_event_id) : destination(destination), preslot_index(preslot_index), preslot_mode(preslot_mode), defining_event_id(defining_event_id) {}
				guid_t destination;
				u32 preslot_index;
				ocrDbAccessMode_t preslot_mode;
				u64 defining_event_id;
				deid e_satisfy;
			};
			void set_preslot(u32 preslot_index, ocrDbAccessMode_t mode, guid_t source);
			void set_preslot(u32 preslot_index, ocrDbAccessMode_t mode) { set_preslot(preslot_index, mode, NULL_GUID); }
			void add_postslot(guid_t destination, u32 slot_index, ocrDbAccessMode_t mode, u64 defining_event_id);
		protected:
			node(guid_t guid, object_type type, std::size_t preslot_count) : guided(guid, type), preslots_(preslot_count) {}
			std::vector<preslot_t> preslots_;
			std::vector<postslot_t> postslots_;
		};
	}
}
#endif
