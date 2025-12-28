/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__runtime_H_GUARD
#define OCR_V1__runtime_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct guid_generator
		{
			guid_generator() : sequence_(8) {}
			guid_t get_next(object_type)
			{
				return guid_t(sequence_++);
			}
		private:
			std::size_t sequence_;
		};

		struct object_list
		{
			void add(guid_t guid, guided* obj);
			guided* get(guid_t what)
			{
				data_type::iterator it = data_.find(what);
				ocr_assert(it != data_.end(), "guid does not exist");
				return it->second;
			}
			bool exists(guid_t what)
			{
				return data_.find(what) != data_.end();
			}
			void remove(guid_t what)
			{
				data_type::iterator it = data_.find(what);
				ocr_assert(it != data_.end(), "guid does not exist");
				data_.erase(it);
			}
			void dump_counts(std::ostream& str)
			{
				std::size_t count_edt_template = 0;
				std::size_t count_db = 0;
				std::size_t count_event = 0;
				std::size_t count_edt = 0;
				std::size_t count_map = 0;
				std::size_t count_file = 0;
				for (data_type::iterator it = data_.begin(); it != data_.end(); ++it)
				{
					if (it->second == 0) continue;
					switch (it->second->type())
					{
					case G_db: ++count_db; break;
					case G_edt_template: ++count_edt_template; break;
					case G_edt: ++count_edt; break;
					case G_event: ++count_event; break;
					case G_map: ++count_map; break;
					case G_file: ++count_file; break;
					}
				}
				str << "LOG: remaining object counts: \nLOG:\t" << count_db << " DBs\nLOG:\t" << count_edt << " EDTs\nLOG:\t" << count_event << " events\nLOG:\t" << count_edt_template << " EDT templates\nLOG:\t" << count_map << " maps\nLOG:\t" << count_file << " files" << std::endl;
			}
		private:
			typedef std::unordered_map<guid_t, guided*> data_type;
			data_type data_;
		};

		struct task_list
		{
			std::size_t size() { return 0; }
			void add(guid_t task_guid, edt* task_ptr)
			{
				tasks_.push_back(task_t(task_guid, task_ptr));
			}
			edt* get_next();
		private:
			struct task_t
			{
				task_t(guid_t guid, edt* ptr) : guid(guid), ptr(ptr) {}
				guid_t guid;
				edt* ptr;
			};
			std::deque<task_t> tasks_;
		};


		struct runtime
		{
			runtime() : shutdown_issued_(false), this_edt_(0), event_id_sequence_(0), events_("trace_ops.dat"), edges_("trace_edges.dat"), this_call_site_index_(-1), last_event_of_current_edt_(-1)
			{

			}
			void shutdown()
			{
				if (ready_tasks_.size()) log::shutdown_ready_tasks_remain(ready_tasks_.size());
				shutdown_issued_ = true;
				deid de = dump::event("shutdown", get_current_edt_guid());
				e_down_ = de;
			}
			void abort(u8 code)
			{
				abort_issued_ = true;
				abort_error_code_ = code;
				deid de = dump::event("abort", get_current_edt_guid());
				e_down_ = de;
			}
			u64 get_argc(void* dbPtr)
			{
				ocr_assert(dbPtr == get(arg_db_)->as_db()->ptr_public(), "pointer to the arg DB is not the right one");
				ocr_assert(get(arg_db_)->as_db()->is_acquired(), "arg DB was not acquired");
				return *(u64*)dbPtr;
			}

			char* get_argv(void* dbPtr, u64 count)
			{
				ocr_assert(dbPtr == get(arg_db_)->as_db()->ptr_public(), "pointer to the arg DB is not the right one");
				ocr_assert(get(arg_db_)->as_db()->is_acquired(), "arg DB was not acquired");
				ocr_assert(count < get_argc(dbPtr), "bad argument index");
				char* ptr = (char*)dbPtr;
				std::size_t offset = std::size_t(*(((u64*)ptr) + count + 1/*one for argc*/));
				return ptr + offset;
			}
			void pack_arguments(int argc, char* argv[], std::vector<char>& buf)
			{
				std::size_t arg_size = sizeof(u64);
				for (int i = 0; i < argc; ++i)
				{
					arg_size += sizeof(u64);
					arg_size += strlen(argv[i]) + 1;
				}
				buf.resize(arg_size);
				char* ptr = &buf.front();
				std::size_t offset = 0;
				*(u64*)ptr = argc;
				offset += sizeof(u64);
				std::size_t string_offset = offset + argc * sizeof(u64);
				for (int i = 0; i < argc; ++i)
				{
					*(u64*)(ptr + offset) = string_offset;
					std::size_t size = strlen(argv[i]) + 1;
					string_offset += size;
					offset += sizeof(u64);
				}

				for (int i = 0; i < argc; ++i)
				{
					std::size_t size = strlen(argv[i]) + 1;
					memcpy(ptr + offset, argv[i], size);
					offset += size;
				}
			}
			guided* get(guid_t what)
			{
				guided* res = objects_.get(what);
				return res;
			}
			bool exists(guid_t what)
			{
				return objects_.exists(what);
			}
			void resolve_lid(guid_t& what)
			{
				if (what == NULL_GUID) return;
				guided* res = objects_.get(what);
				if (res->type() == G_lid)
				{
					ocr_assert(res->as_lid()->guid_ != NULL_GUID, "undefined LID was used");
					what = res->as_lid()->guid_;
				}
			}
			u8 create_db(ocrGuid_t *db_guid, void** addr, u64 len, u16 flags, ocrHint_t* hint, ocrInDbAllocator_t allocator)
			{
				ocr_assert(db_guid, "NULL db used to call ocrDbCreate");
				ocr_assert(addr, "NULL addr used to call ocrDbCreate");
				ocr_assert(flags == DB_PROP_NONE || flags == DB_PROP_NO_ACQUIRE || flags == DB_PROP_SINGLE_ASSIGNMENT, "incorrect flags used to call ocrDbCreate");
				//ocr_assert(affinity == NULL_GUID, "the affinity used to call ocrDbCreate should be NULL_GUID");
				ocr_assert(allocator == NO_ALLOC, "the allocator used to call ocrDbCreate should be NO_ALLOC");
				if (config::return_error_codes() && (!(flags == DB_PROP_NONE || flags == DB_PROP_NO_ACQUIRE || flags == DB_PROP_SINGLE_ASSIGNMENT) || /*!(affinity == NULL_GUID) ||*/ !(allocator == NO_ALLOC))) return OCR_EINVAL;
				guid_t guid = generator_.get_next(G_db);
				*db_guid = guid.as_ocr_guid();
				db* x = new db(guid, (std::size_t)len);
				objects_.add(guid, x);
				if (dump::trace::dbs)
				{
					deid de1 = dump::event("create.DBK", get_current_edt_guid(), guid);
					if (dump::trace::standalone_dbs)
					{
						deid de2 = dump::event("created.DBK", guid);
						dump::edge(de1, de2);
					}
				}
				if (flags & DB_PROP_NO_ACQUIRE) *addr = 0;
				else
				{
					x->acquire(DB_DEFAULT_MODE, this_edt_ ? this_edt_->guid() : NULL_GUID);
					if (this_edt_) this_edt_->add_acquired_block(guid);
					*addr = x->ptr_public();
				}
				return 0;
			}
			u8 release_db(guid_t guid)
			{
				resolve_lid(guid);
				db* x = objects_.get(guid)->as_db();
				ocr_assert(x->is_acquired(), "attempting to release a DB which was not acquired");
				if (this_edt_) this_edt_->remove_acquired_block(guid);
				x->release();
				return 0;
			}
			u8 destroy_db(guid_t guid)
			{
				resolve_lid(guid);
				db* x = objects_.get(guid)->as_db();
				x->prepare_destroy();
				if (x->is_acquired()) release_db(guid);
				objects_.remove(guid);
				deid de1 = dump::event("destroy.DBK", get_current_edt_guid(), guid);
				if (dump::trace::dbs)
				{
					if (dump::trace::standalone_dbs)
					{
						deid de2 = dump::event("destroyed.DBK", guid);
						dump::edge(de1, de2);
					}
				}
				delete x;
				return 0;
			}
			const char* dump_name_create_evt(ocrEventTypes_t type)
			{
				switch (type)
				{
				case OCR_EVENT_ONCE_T: return "create.EVT.ONC";
				case OCR_EVENT_LATCH_T: return "create.EVT.LAT";
				case OCR_EVENT_STICKY_T: return "create.EVT.STI";
				}
				return "create.EVT.XXX";
			}
			const char* dump_name_created_evt(ocrEventTypes_t type)
			{
				switch (type)
				{
				case OCR_EVENT_ONCE_T: return "created.EVT.ONC";
				case OCR_EVENT_LATCH_T: return "created.EVT.LAT";
				case OCR_EVENT_STICKY_T: return "created.EVT.STI";
				}
				return "create.EVT.XXX";
			}
			u8 create_event(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties, guid_t parent_edt)
			{
				ocr_assert(guid, "NULL guid used to call ocrEventCreate");
				ocr_assert((properties & GUID_PROP_BLOCK) != GUID_PROP_BLOCK, "GUID_PROP_BLOCK is not supported");
				bool event_type_is_valid = eventType == OCR_EVENT_IDEM_T || eventType == OCR_EVENT_LATCH_T || eventType == OCR_EVENT_ONCE_T || eventType == OCR_EVENT_STICKY_T || eventType == OCR_EVENT_CHANNEL_T;
				bool event_type_is_compatible_with_props = eventType != OCR_EVENT_LATCH_T || properties != EVT_PROP_TAKES_ARG;
				ocr_assert(event_type_is_valid, "invalid event type used to call ocrEventCreate");
				//ocr_assert(properties == EVT_PROP_NONE || properties == EVT_PROP_TAKES_ARG || properties == EVT_PROP_MAPPED, "invalid properties used to call ocrEventCreate");
				ocr_assert(event_type_is_compatible_with_props, "latch event cannot pass data");
				if (config::return_error_codes() && (!event_type_is_valid || !event_type_is_compatible_with_props)) return OCR_EINVAL;
				if (properties & EVT_PROP_MAPPED)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped event");
					guid_t actual_guid = generator_.get_next(G_event);
					get(*guid)->as_lid()->guid_ = actual_guid;
					*guid = actual_guid.as_ocr_guid();
				}
				else if ((properties & GUID_PROP_CHECK) == GUID_PROP_CHECK)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped event");
					if (objects_.exists(*guid)) return 0;
				}
				else if ((properties & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped event");
					ocr_assert(!objects_.exists(*guid), "GUID_PROP_IS_LABELED was provided, but the object already exists");
				}
				else
				{
					*guid = generator_.get_next(G_event).as_ocr_guid();
				}
				event* e = new event(*guid, eventType, properties & EVT_PROP_TAKES_ARG);
				deid de1 = dump::event(dump_name_create_evt(eventType), get_current_edt_guid(), *guid, parent_edt);
				deid de2 = dump::event(dump_name_created_evt(eventType), *guid, get_current_edt_guid(), parent_edt);
				e->e_created_ = de2;
				dump::edge(de1, de2);
				objects_.add(*guid, e);
				return 0;
			}
			u8 destroy_event(guid_t guid)
			{
				resolve_lid(guid);
				ocr_assert(objects_.exists(guid), "the GUID does not exist");
				if (config::return_error_codes() && !objects_.exists(guid)) return OCR_EINVAL;
				ocr_assert(get(guid)->type() == G_event, "the GUID does not represent an event");
				if (config::return_error_codes() && get(guid)->type() != G_event) return OCR_EINVAL;
				event* e = get(guid)->as_event();
				//TODO: if there is a runnable EDT waiting for the event, the behavior is undefined
				objects_.remove(e->guid());
				deid de1 = dump::event("destroy.EVT", get_current_edt_guid(), guid);
				deid de2 = dump::event("destroyed.EVT", guid, get_current_edt_guid());
				dump::edge(de1, de2);
				delete e;
				return 0;
			}
			u8 satisfy_event(guid_t event_guid, guid_t data_guid, u32 slot)
			{
				resolve_lid(event_guid);
				resolve_lid(data_guid);
				ocr_assert(objects_.exists(event_guid), "the event GUID does not exist");
				if (config::return_error_codes() && !objects_.exists(event_guid)) return OCR_EINVAL;
				ocr_assert(get(event_guid)->type() == G_event, "the event GUID does not represent an event");
				if (config::return_error_codes() && get(event_guid)->type() != G_event) return OCR_EINVAL;
				if (data_guid != NULL_GUID)
				{
					ocr_assert(objects_.exists(data_guid), "the data GUID does not exist");
					if (config::return_error_codes() && !objects_.exists(data_guid)) return OCR_EINVAL;
					ocr_assert(get(data_guid)->type() == G_db, "the data GUID does not represent a DB");
					if (config::return_error_codes() && get(data_guid)->type() != G_db) return OCR_EINVAL;
					if (get(data_guid)->as_db()->is_acquired())
					{
						bool may_be_modified = get(data_guid)->as_db()->acquire_mode() == DB_MODE_EW || get(data_guid)->as_db()->acquire_mode() == DB_MODE_RW;
						bool was_modified = may_be_modified && get(data_guid)->as_db()->was_modified();
						ocr_assert(!was_modified, "data block was modified and used to satisfy a dependence without releasing it, the recipient may not receive the changed data");
						if (was_modified)
						{
							std::cout << "context: event is " << event_guid << ", data is " << data_guid << ", slot is " << slot << std::endl;
						}
					}
				}
				event* e = get(event_guid)->as_event();
				if (data_guid != NULL_GUID) ocr_assert_warn(e->get_event_type() == OCR_EVENT_LATCH_T || e->takes_arg(), "data was passed to a non-latch event which does not have EVT_PROP_TAKES_ARG");
				if (data_guid != NULL_GUID && !e->takes_arg()) data_guid = NULL_GUID;
				deid de = dump::event("addDep.DBK.EVT", get_current_edt_guid(), data_guid, event_guid);
				return e->satisfy(de, data_guid, slot);
			}
			u8 satisfy_preslot(deid initiator, guid_t target_guid, guid_t data_guid, u32 slot)
			{
				resolve_lid(target_guid);
				resolve_lid(data_guid);
				ocr_assert(objects_.exists(target_guid), "the destination GUID does not exist");
				ocr_assert(data_guid == NULL_GUID || objects_.exists(data_guid), "invalid data GUID");
				node* n = get(target_guid)->as_node();
				if (n->type() == G_event) return satisfy_event(target_guid, data_guid, slot);
				assert(n->type() == G_edt);
				return n->as_edt()->satisfy(initiator, data_guid, slot);
			}
			void handle_delete(guided* obj)
			{
				if (obj->type() == G_edt)
				{
					remove_lids(obj->as_edt()->lids_begin(), obj->as_edt()->lids_end());
				}
				else
				{
					//if it is an EDT, it should have already been removed
					objects_.remove(obj->guid());
				}
				delete obj;
			}
			guid_t get_affinity_guid(std::size_t index)
			{
				assert(index == 0);
				return (guid_t)1;
			}
			u8 create_edt_template(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc, u32 depc, const char* funcName)
			{
				*guid = generator_.get_next(G_edt_template).as_ocr_guid();
				edt_template* et = new edt_template(*guid, funcPtr, paramc, depc, funcName);
				objects_.add(*guid, et);
				deid de1 = dump::event("create.TML", get_current_edt_guid(), *guid, dump_arg(funcName), dump_arg(funcName, 1), dump_arg(funcName, 2));
				if (dump::trace::templates)
				{
					if (dump::trace::standalone_templates)
					{
						deid de2 = dump::event("created.TML", *guid, dump_arg(funcName), dump_arg(funcName, 1), dump_arg(funcName, 2));
						dump::edge(de1, de2);
					}
				}
				return 0;
			}
			u8 destroy_edt_template(guid_t guid)
			{
				resolve_lid(guid);
				ocr_assert(objects_.exists(guid), "the GUID does not exist");
				if (config::return_error_codes() && !objects_.exists(guid)) return OCR_EINVAL;
				ocr_assert(get(guid)->type() == G_edt_template, "the GUID does not represent an EDT template");
				if (config::return_error_codes() && get(guid)->type() != G_edt_template) return OCR_EINVAL;
				edt_template* et = get(guid)->as_edt_template();
				objects_.remove(guid);
				if (dump::trace::templates)
				{
					deid de1 = dump::event("destroy.TML", get_current_edt_guid(), guid);
					if (dump::trace::standalone_templates)
					{
						deid de2 = dump::event("destroyed.TML", guid);
						dump::edge(de1, de2);
					}
				}
				delete et;
				return 0;
			}
			u8 create_edt(ocrGuid_t * guid, guid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrHint_t* hint, ocrGuid_t *outputEvent)
			{
				resolve_lid(templateGuid);
				ocr_assert(objects_.exists(templateGuid), "the GUID does not exist");
				ocr_assert(get(templateGuid)->type() == G_edt_template, "templateGuid is not an EDT template");
				if (config::return_error_codes() && (!objects_.exists(templateGuid) || get(templateGuid)->type() != G_edt_template)) return OCR_EINVAL;
				edt_template* et = get(templateGuid)->as_edt_template();
				if (paramc == EDT_PARAM_DEF) paramc = et->paramc_;
				if (depc == EDT_PARAM_DEF) depc = et->depc_;
				ocr_assert(guid, "NULL used as guid to call ocrEdtCreate");
				ocr_assert(paramc != EDT_PARAM_UNK, "the number of parameters is not specified both in template and ocrEdtCreate call");
				ocr_assert(depc != EDT_PARAM_UNK, "the number of dependences is not specified both in template and ocrEdtCreate call");
				ocr_assert(et->paramc_ == EDT_PARAM_UNK || paramc == et->paramc_, "the number of parameters must be the same in ocrEdtTemplateCreate and ocrEdtCreate calls");
				ocr_assert(et->depc_ == EDT_PARAM_UNK || depc == et->depc_, "the number of dependences must be the same in ocrEdtTemplateCreate and ocrEdtCreate calls");
				//ocr_assert(properties == EDT_PROP_NONE || properties == EDT_PROP_FINISH, "invalid flags used to call ocrEdtCreate");
				if (paramc > 0)
				{
					ocr_assert(paramv != 0, "if paramc is not 0, paramv must be provided");
				}
				else
				{
					ocr_assert(paramv == 0, "if paramc is 0, paramv must be NULL");
				}
				if (properties & EDT_PROP_MAPPED)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped EDT");
					guid_t actual_guid = generator_.get_next(G_edt);
					get(*guid)->as_lid()->guid_ = actual_guid;
					*guid = actual_guid.as_ocr_guid();
				}
				else if ((properties & GUID_PROP_CHECK) == GUID_PROP_CHECK)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped EDT");
					if (objects_.exists(*guid)) return 0;
				}
				else if ((properties & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED)
				{
					ocr_assert(*guid != NULL_GUID, "no GUID was provided to create a mapped EDT");
					ocr_assert(!objects_.exists(*guid), "GUID_PROP_IS_LABELED was provided, but the object already exists");
				}
				else
				{
					*guid = generator_.get_next(G_edt).as_ocr_guid();
				}
				ocrGuid_t event_guid;
				u8 err = create_event(&event_guid, (properties & EDT_PROP_FINISH) ? OCR_EVENT_LATCH_T : OCR_EVENT_ONCE_T, (properties & EDT_PROP_FINISH) ? EVT_PROP_NONE : EVT_PROP_TAKES_ARG, *guid);
				assert(err == 0);
				if (outputEvent) *outputEvent = event_guid;
				edt* e = new edt(*guid, et, paramc, paramv, depc, event_guid, properties & EDT_PROP_FINISH);
				objects_.add(*guid, e);
				e->initialize_deps(depc, (guid_t*)depv);
				if (depc == 0)
				{
					spawn(*guid);
				}
				return 0;
			}
			u8 add_dependence(guid_t source, guid_t destination, u32 slot, ocrDbAccessMode_t mode)
			{
				guid_t initiator = get_current_edt_guid();
				resolve_lid(source);
				resolve_lid(destination);
				node* dest = get(destination)->as_node();
				dest->set_preslot(slot, mode, source);
				if (source == NULL_GUID)
				{
					if (dest->type() == G_event)
					{
						deid de1 = dump::event("addDep.DBK.EVT", initiator, source, destination);
						dest->as_event()->satisfy(de1, source, slot);
					}
					else
					{
						assert(dest->type() == G_edt);
						deid de1 = dump::event("addDep.DBK.EDT", initiator, source, destination);
						dest->as_edt()->satisfy(de1, source, slot);
					}
					return 0;
				}
				guided* src = get(source);
				if (src->type() == G_db)
				{
					if (get(source)->as_db()->is_acquired())
					{
						bool may_be_modified = get(source)->as_db()->acquire_mode() == DB_MODE_EW || get(source)->as_db()->acquire_mode() == DB_MODE_RW;
						bool was_modified = may_be_modified && get(source)->as_db()->was_modified();
						bool consistent_read_expected = mode == DB_MODE_CONST || mode == DB_MODE_EW;
						bool inconsistent = was_modified && consistent_read_expected;
						ocr_assert_warn(!inconsistent, "data block was modified and used to define a dependence without releasing it, the recipient requires consistent view, but it may not receive the changed data");
					}
					if (dest->type() == G_event)
					{
						deid de1 = dump::event("addDep.DBK.EVT", initiator, source, destination);
						dest->as_event()->satisfy(de1, source, slot);
					}
					else
					{
						assert(dest->type() == G_edt);
						deid de1 = dump::event("addDep.DBK.EDT", initiator, source, destination);
						dest->as_edt()->satisfy(de1, source, slot);
					}
					return 0;
				}
				ocr_assert(src->type() == G_event, "dependence can only be set up with a DB or an event as the source");
				event* e = src->as_event();
				deid de2 = dump::event("addPostslot", source, initiator);
				if (dest->type() == G_event)
				{
					deid de1 = dump::event("addDep.EVT.EVT", initiator, source, destination);
					dump::edge(de1, de2);
				}
				else
				{
					assert(dest->type() == G_edt);
					deid de1 = dump::event("addDep.EVT.EDT", initiator, source, destination);
					dump::edge(de1, de2);
				}
				e->add_postslot(destination, slot, mode, de2.event_id);
				if (e->is_satisfied())
				{
					guid_t data = e->data();
					if (dest->type() == G_event)
					{
						deid de3 = dump::event("addDep.DBK.EVT", e->guid(), data, dest->guid());
						dump::edge(de2, de3);
						dump::edge(deid(e->guid(), e->e_satisfied_), de3);
						dest->as_event()->satisfy(de3, data, slot);
					}
					else
					{
						deid de3 = dump::event("addDep.DBK.EDT", e->guid(), data, dest->guid());
						dump::edge(de2, de3);
						dump::edge(deid(e->guid(), e->e_satisfied_), de3);
						assert(dest->type() == G_edt);
						dest->as_edt()->satisfy(de3, data, slot);
					}
				}
				return 0;
			}
			void spawn(guid_t task)
			{
				edt* t = get(task)->as_edt();
				ready_tasks_.add(task, t);
			}
			int start(int argc, char* argv[]);
			edt* get_current_edt()
			{
				return this_edt_;
			}
			guid_t get_current_edt_guid()
			{
				return this_edt_ ? this_edt_->guid() : NULL_GUID;
			}
			ocrIdType get_id_type(ocrGuid_t id)
			{
				if (get(id)->type() == G_lid) return OCR_ID_LID;
				return OCR_ID_GUID;
			}
			u8 get_guid(ocrGuid_t* guid, guid_t sourceId)
			{
				resolve_lid(sourceId);
				*guid = sourceId.as_ocr_guid();
				return 0;
			}
			u8 create_map(ocrGuid_t* guid, u64 size, ocrCreator_t creatorFunc, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv)
			{
				*guid = generator_.get_next(G_map).as_ocr_guid();
				map* m = new map(*guid, size, creatorFunc, paramc, paramv, guidc, guidv);
				objects_.add(*guid, m);
				return 0;
			}
			u8 destroy_map(guid_t guid)
			{
				resolve_lid(guid);
				ocr_assert(objects_.exists(guid), "the GUID does not exist");
				if (config::return_error_codes() && !objects_.exists(guid)) return OCR_EINVAL;
				ocr_assert(get(guid)->type() == G_map, "the GUID does not represent a map");
				if (config::return_error_codes() && get(guid)->type() != G_map) return OCR_EINVAL;
				map* m = get(guid)->as_map();
				objects_.remove(guid);
				delete m;
				return 0;
			}
			u8 get_from_map(ocrGuid_t* objet_lid, guid_t map_guid, u64 index)
			{
				resolve_lid(map_guid);
				map* m = get(map_guid)->as_map();
				guid_t& g = m->at(index);
				if (g == NULL_GUID)
				{
					edt* x = this_edt_;
					this_edt_ = 0;
					guid_t source_lid = generator_.get_next(G_lid);
					lids_outside_edt_.push_back(source_lid);
					objects_.add(source_lid, new lid(source_lid));
					m->create(source_lid, index);
					ocr_assert(objects_.get(source_lid)->as_lid()->guid_ != NULL_GUID, "the creator has to use the LID to create an object");
					g = objects_.get(source_lid)->as_lid()->guid_;
					remove_lids(lids_outside_edt_.begin(), lids_outside_edt_.end());
					lids_outside_edt_.clear();
					this_edt_ = x;
				}
				lid* l = new lid(generator_.get_next(G_lid), g);
				objects_.add(l->guid(), l);
				*objet_lid = l->guid().as_ocr_guid();
				if (this_edt_) this_edt_->add_lid(l->guid());
				else lids_outside_edt_.push_back(l->guid());
				return 0;
			}
			struct file_descriptor
			{
				guid_t guid;
				u64 size;
				u8 status;
			};
			u8 open_file(ocrGuid_t* fileGuid, const char* path, const char* mode, ocrGuid_t* descriptorDb)
			{
				FILE* f = fopen(path, mode);
				*fileGuid = generator_.get_next(G_file).as_ocr_guid();
				if (descriptorDb)
				{
					void* ptr;
					u8 res = create_db(descriptorDb, &ptr, sizeof(file_descriptor), 0, NULL_HINT, NO_ALLOC);
					file_descriptor* desc = (file_descriptor*)ptr;
					desc->status = 0;
					if (f)
					{
						desc->guid = *fileGuid;
						desc->status = OCR_FILE_EXISTS;
						fseek(f, 0, SEEK_END);
						desc->size = ftell(f);
						fseek(f, 0, SEEK_SET);
					}
					assert(res == 0);
				}
				file* ff = new file(*fileGuid, f);
				objects_.add(*fileGuid, ff);
				return 0;
			}
			u8 release_file(guid_t fileGuid)
			{
				resolve_lid(fileGuid);
				file* f = get(fileGuid)->as_file();
				f->destroy();
				return 0;
			}
			u8 get_chunk(ocrGuid_t* chunkDbGuid, guid_t fileGuid, u64 offset, u64 size)
			{
				resolve_lid(fileGuid);
				file* f = get(fileGuid)->as_file();
				guid_t guid = generator_.get_next(G_db);
				*chunkDbGuid = guid.as_ocr_guid();
				db* x = new db(guid, (std::size_t)size, f, offset);
				objects_.add(guid, x);
				return 0;
			}
			u8 partition_db(ocrGuid_t dbGuid, u32 partCount, ocrDbPart_t* partitions, u32 properties)
			{
				db* x = get(dbGuid)->as_db();
				for (u32 i = 0; i < partCount; ++i)
				{
					ocr_assert(partitions[i].offset + partitions[i].size <= x->size(), "the partition extends beyond the end of the buffer");
					for (u32 j = 0; j < partCount; ++j)
					{
						if (i == j) continue;
						ocr_assert(!(partitions[i].offset<partitions[j].offset + partitions[j].size && partitions[i].offset + partitions[j].size>partitions[j].offset), "partitions overlap");
					}
					guid_t guid = generator_.get_next(G_db);
					partitions[i].guid = guid.as_ocr_guid();
					db* y = new db(guid, partitions[i].size, x, partitions[i].offset);
					objects_.add(guid, y);
				}
				return 0;
			}
			static ocrGuid_t copy_edt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
			{
				guid_t src = depv[0].guid;
				guid_t dst = depv[1].guid;
				const char* src_ptr = (const char*)depv[0].ptr;
				char* dst_ptr = (char*)depv[1].ptr;
				u64 dst_off = paramv[0];
				u64 src_off = paramv[1];
				u64 size = paramv[2];
				::memcpy(dst_ptr + dst_off, src_ptr + src_off, (std::size_t)size);
				if (paramv[3] == DB_COPY_PARTITION_BACK)
				{
					ocrDbRelease(src.as_ocr_guid());
					ocrDbDestroy(src.as_ocr_guid());
				}
				return dst.as_ocr_guid();
			}
			u8 copy_db(ocrGuid_t destination, u64 destinationOffset, ocrGuid_t source, u64 sourceOffset, u64 size, u64 copyType, ocrGuid_t * completionEvt)
			{
				guid_t task;
				//db* src = get(source)->as_db();--the source may be an event
				db* dst = get(destination)->as_db();
				if (get(source)->type() == G_db) ocr_assert(get(source)->as_db()->size() >= sourceOffset + size, "invalid offset and/or size for source DB");
				ocr_assert(dst->size() >= destinationOffset + size, "invalid offset and/or size for destination DB");
				u64 params[] = { destinationOffset,sourceOffset,size,copyType };
				u8 err = create_edt(&task.get_ref(), copy_edt_template_, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, 0, NULL_HINT, completionEvt);
				assert(err == 0);
				err = add_dependence(source, task, 0, DB_MODE_CONST);
				assert(err == 0);
				err = add_dependence(destination, task, 1, DB_MODE_EW);
				assert(err == 0);
				return 0;
			}
			u8 guid_range_create(ocrGuid_t *rangeGuid, u64 numberGuid, ocrGuidUserKind kind)
			{
				ocr_assert(rangeGuid, "rangeGuid cannot be null");
				ocr_assert((kind == GUID_USER_DB || kind == GUID_USER_EDT || kind == GUID_USER_EDT_TEMPLATE || kind == GUID_USER_EVENT_ONCE || kind == GUID_USER_EVENT_IDEM || kind == GUID_USER_EVENT_STICKY || kind == GUID_USER_EVENT_LATCH || kind == GUID_USER_EVENT_CHANNEL), "invalid range kind");
				ocr_assert(kind == GUID_USER_EDT || kind == GUID_USER_EVENT_ONCE || kind == GUID_USER_EVENT_LATCH || kind == GUID_USER_EVENT_STICKY || kind == GUID_USER_EVENT_CHANNEL, "only sticky and channel events are supported at the moment");
				*rangeGuid = generator_.get_next(G_range).as_ocr_guid();
				range* rng = new range(*rangeGuid, numberGuid, kind);
				objects_.add(*rangeGuid, rng);
				/*for (u64 i = 0; i < numberGuid; ++i)
				{
				(*rng)[i] = generator_.get_next(G_system_object);
				}*/
				return 0;
			}

			guid_t els_get(u8 offset)
			{
				ocr_assert(this_edt_, "ELS cannot be used outside of an EDT");
				return this_edt_->els_get(offset);
			}

			void els_set(u8 offset, guid_t data)
			{
				ocr_assert(this_edt_, "ELS cannot be used outside of an EDT");
				this_edt_->els_set(offset, data);
			}

			u8 guid_from_index(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx)
			{
				ocr_assert(outGuid, "outGuid cannot be null");
				range* rng = get(rangeGuid)->as_range();
				ocr_assert(idx < rng->size(), "invalid range index");
				guid_t res = (*rng)[idx];
				if (!res || !exists(res))
				{
					(*rng)[idx] = generator_.get_next(G_system_object);
				}
				*outGuid = (*rng)[idx].as_ocr_guid();
				return 0;
			}

			void remove_object(guid_t g)
			{
				objects_.remove(g);
			}

			template<typename IT> void remove_lids(IT begin, IT end)
			{
				for (IT it = begin; it != end; ++it)
				{
					lid* l = objects_.get(*it)->as_lid();
					objects_.remove(*it);
					delete l;
				}
			}
			template<typename IT> void release_blocks(IT begin, IT end)
			{
				for (IT it = begin; it != end; ++it)
				{
					objects_.get(*it)->as_db()->release();
				}
			}
			struct debug_label
			{
				debug_label() {}
				debug_label(const std::string& name, const std::vector<u64>& params) : name(name), params(params) {}
				std::string name;
				std::vector<u64> params;
			};
			u8 attach_debug_label(ocrGuid_t object, const char* label, u32 paramc, u64* paramv)
			{
				//assert(exists(object));
				std::vector<u64> params(paramv, paramv + paramc);
				std::string str_label(label);
				debug_labels_[object] = debug_label(str_label, params);
				std::string l1 = str_label.substr(0, 8);
				std::string l2;
				if (str_label.length() > 8) l2 = str_label.substr(8, 8);
				dump::event("debugLabel", object, dump_arg(l1), dump_arg(l2), (params.size() > 0) ? params[0] : -1, (params.size() > 1) ? params[1] : -1);
				return 0;
			}
			u8 note_causality(ocrGuid_t from, ocrGuid_t to)
			{
				assert(task_ends_.find(from) != task_ends_.end());
				incomming_task_begins_[to].push_back(deid(from, task_ends_[from]));
				return 0;
			}
			void note_end(deid event)
			{
				task_ends_[event.owner] = event.event_id;
			}
			void note_begin(deid event)
			{
				task_begins_[event.owner] = event.event_id;
				std::vector<deid> incomming;
				incomming.swap(incomming_task_begins_[event.owner]);
				for (std::vector<deid>::iterator it = incomming.begin(); it != incomming.end(); ++it)
				{
					dump::edge(*it, event);
				}
			}
			static runtime& get() { return the_; }
			void set_call_site(const char* name, int line)
			{
				std::string label = name;
				label += ':' + std::to_string((long long)line);
				std::vector<std::string>::iterator it = std::find(call_sites_.begin(), call_sites_.end(), label);
				if (it == call_sites_.end())
				{
					this_call_site_index_ = call_sites_.size();
					call_sites_.push_back(label);
				}
				else
				{
					this_call_site_index_ = std::distance(call_sites_.begin(), it);
				}
			}
			void unset_call_site()
			{
				this_call_site_index_ = -1;
			}
			u64 next_event_id()
			{
				return ++event_id_sequence_;
			}
			void add_event(guid_t owner, u64 next_event)
			{
				if (owner != get_current_edt_guid()) return;
				if (event_to_confirm_.event_id != -1)
				{
					assert(event_to_confirm_sender_ == get_current_edt_guid());
					dump_edge(event_to_confirm_, deid(owner, next_event), dump::dot::back_edges);
					event_to_confirm_ = deid();
				}
				if (last_event_of_current_edt_ == -1)
				{
					last_event_of_current_edt_ = next_event;
				}
				else
				{
					dump_edge(deid(owner, last_event_of_current_edt_), deid(owner, next_event), true);
					last_event_of_current_edt_ = next_event;
				}
			}
			bool show_in_dot(const char* name, guid_t owner)
			{
				if (!dump::dot::save) return false;
				if (object_types_[owner] == G_edt_template) return dump::dot::templates;
				if (object_types_[owner] == G_db) return dump::dot::dbs;
				if (object_types_[owner] == G_event) return dump::dot::events;
				return true;
			}
			std::string make_event_label(u64 id, const char* name, guid_t owner)
			{
				return " [label=\"" + std::to_string((long long unsigned int)id) + ": " + name + "\"]";
			}
			deid dump_event(const char* name, guid_t owner)
			{
				u64 id = runtime::next_event_id();
				events_(id)(this_call_site_index_)(name)(owner)(dump_arg())(dump_arg())(dump_arg())(dump_arg());
				add_event(owner, id);
				if (show_in_dot(name, owner)) dot_data_[owner].nodes.push_back(std::to_string((long long unsigned int)id) + make_event_label(id, name, owner));
				return deid(owner, id);
			}
			deid dump_event(const char* name, guid_t owner, dump_arg a1)
			{
				u64 id = runtime::next_event_id();
				events_(id)(this_call_site_index_)(name)(owner)(a1)(dump_arg())(dump_arg())(dump_arg());
				add_event(owner, id);
				if (show_in_dot(name, owner)) dot_data_[owner].nodes.push_back(std::to_string((long long unsigned int)id) + make_event_label(id, name, owner));
				return deid(owner, id);
			}
			deid dump_event(const char* name, guid_t owner, dump_arg a1, dump_arg a2)
			{
				u64 id = runtime::next_event_id();
				events_(id)(this_call_site_index_)(name)(owner)(a1)(a2)(dump_arg())(dump_arg());
				add_event(owner, id);
				if (show_in_dot(name, owner)) dot_data_[owner].nodes.push_back(std::to_string((long long unsigned int)id) + make_event_label(id, name, owner));
				return deid(owner, id);
			}
			deid dump_event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3)
			{
				u64 id = runtime::next_event_id();
				events_(id)(this_call_site_index_)(name)(owner)(a1)(a2)(a3)(dump_arg());
				add_event(owner, id);
				if (show_in_dot(name, owner)) dot_data_[owner].nodes.push_back(std::to_string((long long unsigned int)id) + make_event_label(id, name, owner));
				return deid(owner, id);
			}
			deid dump_event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3, dump_arg a4)
			{
				u64 id = runtime::next_event_id();
				events_(id)(this_call_site_index_)(name)(owner)(a1)(a2)(a3)(a4);
				add_event(owner, id);
				if (show_in_dot(name, owner)) dot_data_[owner].nodes.push_back(std::to_string((long long unsigned int)id) + make_event_label(id, name, owner));
				return deid(owner, id);
			}
			void dump_edge(deid from, deid to, bool add_to_dot)
			{
				assert(from.event_id != -1);
				assert(to.event_id != -1);
				if (from.owner == get_current_edt_guid() && to.owner != get_current_edt_guid()) dump::back(to);
				if (add_to_dot && show_in_dot(0, from.owner) && show_in_dot(0, to.owner))
				{
					if (from.owner == to.owner) dot_data_[from.owner].edges.push_back(std::to_string((long long unsigned int)from.event_id) + " -> " + std::to_string((long long unsigned int)to.event_id));
					else dot_cross_edges_.push_back(std::to_string((long long unsigned int)from.event_id) + " -> " + std::to_string((long long unsigned int)to.event_id));
				}
				edges_(from.event_id)(to.event_id);
			}
			void confirm_event_back(deid op)
			{
				event_to_confirm_ = op;
				event_to_confirm_sender_ = get_current_edt_guid();
			}
			void note_object_type(guid_t g, object_type ot)
			{
				object_types_[g] = ot;
			}
			struct dumper
			{
				dumper(const char* file_name)
				{
					f = fopen(file_name, "wb");
					assert(f);
				}
				~dumper()
				{
					fclose(f);
				}
				dumper& operator()(dump_arg arg)
				{
					write(arg.as_u64());
					return *this;
				}
				dumper& operator()(int index)
				{
					write(uint64_t(index));
					return *this;
				}
				dumper& operator()(const char* str)
				{
					assert(strlen(str) <= 2 * sizeof(u64));
					u64 x[2] = { 0,0 };
					for (int i = 0; i < 2 * sizeof(u64); ++i)
					{
						if (!str[i]) break;
						((char*)&x)[i] = str[i];
					}
					write(x[0]);
					write(x[1]);
					return *this;
				}
				/*dumper& operator()(const std::string& str)
				{
				u64 x = 0;
				for (int i = 0; i < sizeof(u64); ++i)
				{
				if (i == str.length()) break;
				((char*)&x)[i] = str[i];
				}
				write(x);
				return *this;
				}*/
				void write(u64 x)
				{
					std::size_t written = fwrite(&x, sizeof(u64), 1, f);
					assert(written == 1);
				}
				void write(guid_t x)
				{
					write((u64)x.as_ocr_guid().guid);
				}
			private:
				FILE* f;
			};
			void dump_sites()
			{
				std::ofstream out("trace_sites.dat");
				for (std::vector<std::string>::const_iterator it = call_sites_.begin(); it != call_sites_.end(); ++it)
				{
					out << *it << std::endl;
				}
			}
			void dump_dot()
			{
				if (dump::dot::save)
				{
					std::ofstream out("trace.dot");
					out << "digraph G {" << std::endl;
					for (dot_data_type::iterator it = dot_data_.begin(); it != dot_data_.end(); ++it)
					{
						out << "\tsubgraph cluster" << std::to_string(it->first) << " {" << std::endl;
						for (dot_chain_type::iterator it2 = it->second.edges.begin(); it2 != it->second.edges.end(); ++it2)
						{
							out << "\t\t" << *it2 << ';' << std::endl;
						}
						for (dot_chain_type::iterator it2 = it->second.nodes.begin(); it2 != it->second.nodes.end(); ++it2)
						{
							out << "\t\t" << *it2 << ';' << std::endl;
						}
						std::string debug;
						if (debug_labels_.find(it->first) != debug_labels_.end())
						{
							debug_label& lbl = debug_labels_[it->first];
							debug = lbl.name;
							debug += "(";
							for (std::size_t i = 0; i < lbl.params.size(); ++i)
							{
								if (i != 0) debug += ",";
								debug += std::to_string((long long unsigned int)lbl.params[i]);
							}
							debug += ")";
						}
						out << "\t\tlabel = \"" << std::to_string(it->first) << " (" + std::string(object_type_to_string(object_types_[it->first])) + ") " + debug + "\"" << std::endl;
						out << "\t}" << std::endl;
					}
					for (dot_chain_type::iterator it2 = dot_cross_edges_.begin(); it2 != dot_cross_edges_.end(); ++it2)
					{
						out << "\t" << *it2 << ';' << std::endl;
					}
					out << "}" << std::endl;
				}
			}
#ifdef PUBLISH_METRICS
			struct published_metrics
			{
				published_metrics()
				{
#ifdef WIN32
					assert(sizeof(DWORD) <= sizeof(app_id_t));
					DWORD id = GetCurrentProcessId();
#else
					pid_t id = getpid();
#endif
					my_app_id = (app_id_t)id;
				}
				~published_metrics()
				{
					zmq::message_t msg(sizeof(ocr_app_status));
					msg.data<ocr_app_status>()->app_id = my_app_id;
					msg.data<ocr_app_status>()->tasks_executed = (uint64_t)-1;
					msg.data<ocr_app_status>()->number_of_threads = (uint64_t)-1;
					con->sock.send(msg);
				}
				void publish(uint64_t task_count)
				{
					if (!con)
					{
						con = std::make_shared<connection>();
					}
					zmq::message_t msg(sizeof(ocr_app_status));
					msg.data<ocr_app_status>()->app_id = my_app_id;
					msg.data<ocr_app_status>()->tasks_executed = task_count;
					msg.data<ocr_app_status>()->number_of_threads = 1;
					con->sock.send(msg);
				}
			private:

				typedef uint64_t app_id_t;
				app_id_t my_app_id;
				struct ocr_app_status
				{
					app_id_t app_id;
					uint64_t tasks_executed;
					uint64_t number_of_threads;
				};
				struct connection
				{
					connection() : sock(ctx, ZMQ_PUB)
					{
						sock.connect("tcp://127.0.0.1:42910");
					}
					zmq::context_t ctx;
					zmq::socket_t sock;
				};
				std::shared_ptr<connection> con;
			};
#endif
		private:
			static runtime the_;
			guid_generator generator_;
			object_list objects_;
			task_list ready_tasks_;
			bool shutdown_issued_;
			bool abort_issued_;
			u8 abort_error_code_;
			guid_t arg_db_;
			guid_t main_edt_template_;
			guid_t copy_edt_template_;
			edt* this_edt_;
			std::vector<guid_t> lids_outside_edt_;
			u64 event_id_sequence_;
			dumper events_;
			dumper edges_;
			int this_call_site_index_;
			std::vector<std::string> call_sites_;
			std::map<guid_t, object_type> object_types_;
			u64 last_event_of_current_edt_;
			deid event_to_confirm_;
			guid_t event_to_confirm_sender_;
			deid e_down_;
			typedef std::vector<std::string> dot_chain_type;
			struct dot_group_type
			{
				dot_chain_type nodes;
				dot_chain_type edges;
			};
			typedef std::map<guid_t, dot_group_type> dot_data_type;
			dot_data_type dot_data_;
			dot_chain_type dot_cross_edges_;
			typedef std::map<guid_t, debug_label> debug_labels_type;
			debug_labels_type debug_labels_;
			typedef std::map<guid_t, u64> object_to_event_type;
			object_to_event_type task_begins_;
			object_to_event_type task_ends_;
			typedef std::map<guid_t, std::vector<deid> > incoming_edges_type;
			incoming_edges_type incomming_task_begins_;
#ifdef PUBLISH_METRICS
			published_metrics metrics;
#endif
		};
	}
}
#endif
