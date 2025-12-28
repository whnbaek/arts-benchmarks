/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__guid_H_GUARD
#define OCR_TBB_distributed__guid_H_GUARD

namespace ocr_tbb
{
	namespace distributed
	{

		template<unsigned node_bits>
		struct guid_template
		{
			struct map_tag {};
			struct mapped_tag {};
			static const unsigned flag_bits = 1;
			static const unsigned object_bits = 64 - (node_bits + flag_bits);
			u8 get_flag() const
			{
				return u8(as_u64() >> (node_bits + object_bits));
			}
			bool is_mapped() const
			{
				return get_flag() == 1;
			}
			bool is_map() const
			{
				return get_node_id() == (((u64)1 << node_bits) - 1);
			}
			u64 get_map_id() const
			{
				assert(is_mapped());
				return (node_id)(as_u64() >> object_bits) & (((u64)1 << node_bits) - 1);
			}
			u64 get_index() const
			{
				assert(is_mapped());
				return (((u64)1 << object_bits) - 1) & as_u64();
			}
			node_id get_mapped_node_id() const;
			node_id get_node_id() const
			{
				if (is_mapped())
				{
					return get_mapped_node_id();
				}
				else
				{
					return (node_id)(as_u64() >> object_bits) & (((u64)1 << node_bits) - 1);
				}
			}
			static node_id get_node_id(ocrGuid_t guid)
			{
				return guid_template(guid).get_node_id();
			}
			object_id get_object_id() const
			{
				assert(!is_mapped());
				return (((u64)1 << object_bits) - 1) & as_u64();
			}
			static object_id get_object_id(ocrGuid_t guid)
			{
				return guid_template(guid).get_object_id();
			}
			bool is_local(thread_context* ctx) const;
			static bool is_local(ocrGuid_t guid) { return guid_template(guid)->is_local(); }
			guid_template() : guid_({ 0 }) {}
			guid_template(ocrGuid_t x) : guid_(x) {}
			guid_template(u64 x) : guid_({ (s64)x }) {}
			guid_template(node_id nid, object_id oid)
			{
				assert(nid < ((u64)1 << node_bits));
				assert(oid < ((u64)1 << object_bits));//the unsigned is there to get rid of VS warning
				guid_.guid = (nid << object_bits) | oid;
			}
			guid_template(u64 map_id, u64 index, mapped_tag)
			{
				assert(map_id < ((u64)1 << node_bits));
				assert(index < ((u64)1 << object_bits));//the unsigned is there to get rid of VS warning
				guid_.guid = ((u64)1 << (node_bits+ object_bits)) | (map_id << object_bits) | index;

			}
			guid_template(u64 map_id, map_tag)
			{
				assert(map_id < ((u64)1 << node_bits));
				guid_.guid = ((u64(-1) & (((u64)1 << node_bits) - 1)) << object_bits) | map_id;

			}
			u64 as_u64() const { return (u64)guid_.guid; }
			operator ocrGuid_t() const
			{
				return guid_;
			}
			ocrGuid_t as_ocr_guid() const
			{
				return guid_;
			}
			u64 as_message_field() const
			{
				return (u64)guid_.guid;
			}
			std::ostream& print(std::ostream& str) const
			{
				if (is_mapped()) str << get_map_id() << '[' << get_index() << ']';
				else str << '{' << get_node_id() << ',' << get_object_id() << '}';
				//if (is_mapped()) str << (uintptr_t)guid_.guid << "=" << get_map_id() << '[' << get_index() << ']';
				//else str << (uintptr_t)guid_.guid << "={" << get_node_id() << ',' << get_object_id() << '}';
				return str;
			}
			static int compare(const guid_template& l, const guid_template& r)
			{
				if (ocrGuidIsLt(l.guid_, r.guid_)) return -1;
				if (ocrGuidIsEq(l.guid_, r.guid_)) return 0;
				return 1;
			}
			typedef void (guid_template::* unspecified_bool_type)();
			void testable() {};
			operator unspecified_bool_type() const { if (ocrGuidIsNull(guid_)) return 0; return &guid_template::testable; }
			explicit operator std::size_t() const //hash operator used by TBB
			{
				return (std::size_t)as_u64();
			}
		private:
			ocrGuid_t guid_;//make sure that this class is compatible with ocrGuid_t on bit-by-bit level
		};

		template<unsigned node_bits>
		std::ostream& operator<<(std::ostream& str, const guid_template<node_bits>& g)
		{
			return g.print(str);
		}

		template<unsigned node_bits>
		bool operator<(const guid_template<node_bits>& l, const guid_template<node_bits>& r)
		{
			return guid_template<node_bits>::compare(l, r) < 0;
		}

		template<unsigned node_bits>
		bool operator>=(const guid_template<node_bits>& l, const guid_template<node_bits>& r)
		{
			return guid_template<node_bits>::compare(l, r) >= 0;
		}

		template<unsigned node_bits>
		bool operator==(const guid_template<node_bits>& l, ocrGuid_t r)
		{
			return guid_template<node_bits>::compare(l, r) == 0;
		}

		template<unsigned node_bits>
		bool operator!=(const guid_template<node_bits>& l, ocrGuid_t r)
		{
			return guid_template<node_bits>::compare(l, r) != 0;
		}


		template<unsigned node_bits>
		bool operator==(const guid_template<node_bits>& l, int r)
		{
			assert(r == 0);
			return guid_template<node_bits>::compare(l, r) == 0;
		}

		template<unsigned node_bits>
		bool operator!=(const guid_template<node_bits>& l, const guid_template<node_bits>& r)
		{
			return guid_template<node_bits>::compare(l, r) != 0;
		}
		typedef guid_template<32> guid;


		template<typename Reader>
		guided* guided_read(object_type ot, guid g, Reader& r);


	}
}

namespace std
{
	inline std::string to_string(ocr_tbb::distributed::guid g)
	{
		std::ostringstream str;
		str << g;
		return str.str();
	}
	template<>
	struct hash<ocr_tbb::distributed::guid>
	{
		std::size_t operator()(const ocr_tbb::distributed::guid& x) const
		{
			return (std::size_t)x.as_u64();
		}
	};
}
/*
this did not work with GCC, changed to the explicit std::size_t conversion operator in the class
namespace tbb
{
	inline size_t tbb_hasher(ocr_tbb::distributed::guid g) {
		size_t h = (size_t)g.as_u64();
		return h;
	};
}
*/
#endif
