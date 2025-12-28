/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__message_H_GUARD
#define OCR_TBB_distributed__message_H_GUARD

namespace ocr_tbb
{
	namespace distributed
	{
		struct message_header
		{
			command cmd;
			node_id from;
			node_id to;
			guid sender_edt;
			u64 id;
			u64 a[10];
			message_header(thread_context* ctx, command cmd, node_id from, node_id to);
			message_header() : cmd(command_code::CMD_invalid), sender_edt(NULL_GUID), id(0) {}
			template<typename Reader>
			message_header(Reader& r, const read_tag&)
				: cmd(r.read_val(&cmd)),
				from(r.read_val(&from)),
				to(r.read_val(&to)),
				sender_edt(r.read_val(&sender_edt)),
				id(r.read_val(&id))
			{
				for (std::size_t i = 0; i < sizeof(a) / sizeof(u64); ++i) r.read_ref(a[i]);
			}
			template<typename Writer>
			void write(Writer& w) const
			{
				assert(command_code::CMD_MAX < (1 << (8 * sizeof(u8))));
				w.write_val("cmd", cmd);
				w.write_val("from", from);
				w.write_val("to", to);
				w.write_val("sender_edt", sender_edt);
				w.write_val("id", id);
				for (std::size_t i = 0; i < sizeof(a) / sizeof(u64); ++i) w.write_val("a", a[i]);
			}
			friend struct command_processor;
			friend struct communicator_base;
		};

	}
}

#endif
