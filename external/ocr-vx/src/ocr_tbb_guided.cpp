/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#include "ocr_tbb.h"

namespace ocr_tbb
{

	db* guided::as_db()
	{
		assert(this == 0 || t_ == G_db);
#if (CHECKED)
		assert(!this || runtime::get().dbs.find(guid()) != runtime::get().dbs.end());
		assert(!this || runtime::get().dbs[guid()] != 0);
#endif
		return static_cast<db*>(this);
	}
	event* guided::as_event() { assert(this == 0 || t_ == G_event); return static_cast<event*>(this); }
	node* guided::as_node() { assert(this == 0 || t_ == G_event || t_ == G_edt); return static_cast<node*>(this); }
	edt* guided::as_edt() { assert(this == 0 || t_ == G_edt); return static_cast<edt*>(this); }
	edt_template* guided::as_edt_template() { assert(this == 0 || t_ == G_edt_template); return static_cast<edt_template*>(this); }
	range* guided::as_range() { assert(this == 0 || t_ == G_range); return static_cast<range*>(this); }

}
