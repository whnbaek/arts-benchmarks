/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__event_dump_H_GUARD
#define OCR_V1__event_dump_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct dump_arg
		{
			dump_arg() : data() {}
			dump_arg(u16 x) : data(x) {}
			dump_arg(u32 x) : data(x) {}
			dump_arg(u64 x) : data(x) {}
			dump_arg(guid_t x) : data(x.as_ocr_guid().guid) {}
			dump_arg(ocrGuid_t x) : data(x.guid) {}
			explicit dump_arg(const char* str)
			{
				if (!str)
				{
					data = 0;
					return;
				}
				u64 x = 0;
				for (int i = 0; i < sizeof(u64); ++i)
				{
					if (!str[i]) break;
					((char*)&x)[i] = str[i];
				}
				data = x;
			}
			explicit dump_arg(const char* str, std::size_t off64)
			{
				if (!str)
				{
					data = 0;
					return;
				}
				u64 x = 0;
				if (strlen(str) > sizeof(u64) * off64)
				{
					for (int i = 0; i < sizeof(u64); ++i)
					{
						if (!str[i + sizeof(u64)*off64]) break;
						((char*)&x)[i] = str[i + sizeof(u64)*off64];
					}
				}
				data = x;
			}
			explicit dump_arg(const std::string& str)
			{
				u64 x = 0;
				for (int i = 0; i < sizeof(u64); ++i)
				{
					if (i == str.length()) break;
					((char*)&x)[i] = str[i];
				}
				data = x;
			}
			u64 as_u64() { return data; }
		private:
			u64 data;
		};

		struct deid
		{
			deid() : owner(), event_id(-1) {}
			deid(guid_t owner, u64 event_id) : owner(owner), event_id(event_id) {}
			guid_t owner;
			u64 event_id;
		};

		struct event_id
		{
			event_id() : id(-1) {}
			event_id(u64 id) : id(id) {}
			event_id(deid id) : id(id.event_id) {}
			operator u64() { return id; }
			u64 id;
		};

		struct dump
		{
			static deid event(const char* name, guid_t owner);
			static deid event(const char* name, guid_t owner, dump_arg a1);
			static deid event(const char* name, guid_t owner, dump_arg a1, dump_arg a2);
			static deid event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3);
			static deid event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3, dump_arg a4);
			static void edge(deid from, deid to);
			static void back(deid op);
			struct dot
			{
#ifdef WIN32
				static const bool save = false;
#else
				static const bool save = false;
#endif

				static const bool back_edges = false;
				static const bool templates = true;
				static const bool dbs = true;
				static const bool events = true;
			};
			struct trace
			{
				static const bool standalone_templates = true;
				static const bool standalone_dbs = true;
				static const bool templates = true;
				static const bool dbs = true;
			};
		};

	}
}

#endif
