/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_guided_H_GUARD
#define OCR_TBB_ocr_tbb_guided_H_GUARD

namespace ocr_tbb
{
	enum object_type
	{
		G_event = 32,
		G_edt,
		G_edt_template,
		G_db,
		G_range,
		G_unknown,
	};

	struct guid_t
	{
		guid_t(ocrGuid_t x) : value(x) {}
		guid_t(void* ptr)
		{
			value.guid = (intptr_t)ptr;
		}
		void* as_ptr() { return (void*)value.guid; }
		ocrGuid_t as_ocr_guid() { return value; }
		typedef void (guid_t::* unspecified_bool_type)();
		void testable() {};
		operator unspecified_bool_type() const { if (ocrGuidIsNull(value)) return 0; return &guid_t::testable; }


		static int compare(ocrGuid_t l, ocrGuid_t r)
		{
			if (ocrGuidIsEq(l,r)) return 0;
			if (ocrGuidIsLt(l,r)) return -1;
			return 1;
		}
		static int compare(guid_t l, guid_t r)
		{
			return compare(l.value, r.value);
		}
	private:
		ocrGuid_t value;
	};

	inline bool operator==(guid_t l, guid_t r)
	{
		return guid_t::compare(l, r) == 0;
	}
	inline bool operator==(guid_t l, ocrGuid_t r)
	{
		return guid_t::compare(l, r) == 0;
	}
	inline bool operator!=(guid_t l, guid_t r)
	{
		return guid_t::compare(l, r) != 0;
	}
	inline bool operator!=(guid_t l, ocrGuid_t r)
	{
		return guid_t::compare(l, r) != 0;
	}
	inline bool operator<(guid_t l, guid_t r)
	{
		return guid_t::compare(l, r) < 0;
	}

	struct guided
	{
		guid_t guid() { if (this == 0) return NULL_GUID; return guid_t(this); }
		static guided* from_guid(guid_t guid) { return (guided*)guid.as_ptr(); }
		object_type type() { return t_; }
		db* as_db();//the as_xyzzy must be implemented after it is known that db is a descendant of guided
		event* as_event();
		node* as_node();
		edt* as_edt();
		edt_template* as_edt_template();
		range* as_range();
	protected:
		guided(object_type t) : t_(t) {}
	private:
		object_type t_;
	};
}

#endif
