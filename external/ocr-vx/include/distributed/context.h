/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__context_H_GUARD
#define OCR_TBB_distributed__context_H_GUARD


namespace ocr_tbb
{
	namespace distributed
	{
		struct thread_context
		{
			thread_context(
#if(SIMULATE_MULTIPLE_NODES)
				node_id node
#endif
			)
				: current_edt(0),
				message_was_forwarded(false),
				message_as_edt(NULL_GUID),
#if(SIMULATE_MULTIPLE_NODES)
				node(node),
#endif
				parent(try_get_local())
			{
				set_local(this);
			}
			~thread_context()
			{
				set_local(parent);
			}
			edt* current_edt;
			message_header message_being_processed;
			bool message_was_forwarded;
			guid message_as_edt;
#if(SIMULATE_MULTIPLE_NODES)
			node_id node;
#endif
			thread_context* parent;
			static thread_context* get_local();
			static thread_context* try_get_local();
			static void set_local(thread_context* ctx);
		};
	}
}

#endif
