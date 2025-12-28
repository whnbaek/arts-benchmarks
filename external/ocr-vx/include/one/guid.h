/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__guid_H_GUARD
#define OCR_V1__guid_H_GUARD

#include <string>
#include <unordered_map>

namespace ocr_vx
{
	namespace one
	{
		struct guid_t
		{
			guid_t() : value(NULL_GUID) {}
			guid_t(ocrGuid_t x) : value(x) {}
			guid_t(std::size_t x)
			{
				value.guid = (s64)x;
			}
			ocrGuid_t& get_ref() { return value; }
			void* as_ptr() { return (void*)value.guid; }
			ocrGuid_t as_ocr_guid() const { return value; }
			typedef void (guid_t::* unspecified_bool_type)();
			void testable() {};
			operator unspecified_bool_type() const { if (value.guid == 0) return 0; return &guid_t::testable; }


			static int compare(ocrGuid_t l, ocrGuid_t r)
			{
				if (ocrGuidIsEq(l, r)) return 0;
				if (ocrGuidIsLt(l, r)) return -1;
				return 1;
			}
			static int compare(guid_t l, guid_t r)
			{
				return compare(l.value, r.value);
			}
		private:
			ocrGuid_t value;
		};

	}
}
namespace std
{
	template <>
	struct hash<ocr_vx::one::guid_t>
	{
		size_t operator()(const ocr_vx::one::guid_t& x) const
		{
			return hash<std::size_t>()(x.as_ocr_guid().guid);
		}
	};
	inline std::string to_string(ocr_vx::one::guid_t x)
	{
		return to_string((long long unsigned int)(x.as_ocr_guid().guid));
	}
	inline std::ostream& operator<<(std::ostream& str, ocr_vx::one::guid_t x)
	{
		str << to_string(x);
		return str;
	}
}

namespace ocr_vx
{
	namespace one
	{
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
			guid_t guid() { return guid_; }
			object_type type() { return t_; }
			db* as_db();//the as_xyzzy must be implemented after it is known that db is a descendant of guided
			event* as_event();
			node* as_node();
			edt* as_edt();
			edt_template* as_edt_template();
			map* as_map();
			lid* as_lid();
			file* as_file();
			range* as_range();
		protected:
			guided(guid_t guid, object_type t) : guid_(guid), t_(t) {}
			void handle_delete();
		private:
			guid_t guid_;
			object_type t_;
		};

		struct system_object : public guided
		{
			system_object(guid_t guid, const char* description) : guided(guid, G_system_object), description_(description) {}
		private:
			std::string description_;
		};

	}
}

#endif
