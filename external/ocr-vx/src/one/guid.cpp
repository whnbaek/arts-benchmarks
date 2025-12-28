/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#include "boot.h"

namespace ocr_vx
{
	namespace one
	{
		db* guided::as_db()
		{
			ocr_assert(t_ == G_db, "the object is not a DB");
			return static_cast<db*>(this);
		}
		event* guided::as_event() { ocr_assert(t_ == G_event, "object is not an event"); return static_cast<event*>(this); }
		node* guided::as_node() { ocr_assert(t_ == G_event || t_ == G_edt, "object is not an event or an EDT"); return static_cast<node*>(this); }
		edt* guided::as_edt() { ocr_assert(t_ == G_edt, "object is not an EDT"); return static_cast<edt*>(this); }
		edt_template* guided::as_edt_template() { ocr_assert(t_ == G_edt_template, "object is not an EDT template"); return static_cast<edt_template*>(this); }
		map* guided::as_map() { ocr_assert(t_ == G_map, "object is not a map"); return static_cast<map*>(this); }
		lid* guided::as_lid() { ocr_assert(t_ == G_lid, "object is not a lid"); return static_cast<lid*>(this); }
		file* guided::as_file() { ocr_assert(t_ == G_file, "object is not a file"); return static_cast<file*>(this); }
		range* guided::as_range() { ocr_assert(t_ == G_range, "object is not a range"); return static_cast<range*>(this); }
		void guided::handle_delete() { runtime::get().handle_delete(this); }
	}
}
