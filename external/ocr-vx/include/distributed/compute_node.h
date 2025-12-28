/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__compute_node_H_GUARD
#define OCR_TBB_distributed__compute_node_H_GUARD

namespace ocr_tbb
{
	namespace distributed
	{
		struct compute_node
		{
			static node_id get_my_id(thread_context* ctx)
			{
				return the(ctx).id_;
			}
			compute_node(node_id id) : id_(id) {}
			/*static void set_my_id(node_id id)
			{
				the().id_ = id;
			}*/
			template<typename Writer>
			void write(Writer& w) const
			{
				w.write_val("id", id_);
			}
			void clear(thread_context* ctx)
			{

			}
			template<typename Reader>
			void read(Reader& r)
			{
				node_id rid = r.read_val(&id_);
				assert(rid == id_);
			}
		private:
			static compute_node& the(thread_context* ctx);
			node_id id_;
		};
	}
}

#endif
