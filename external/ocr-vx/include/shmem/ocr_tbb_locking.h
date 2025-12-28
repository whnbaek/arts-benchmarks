/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_locking_H_GUARD
#define OCR_TBB_ocr_tbb_locking_H_GUARD

#include <algorithm>

namespace ocr_tbb
{
	struct wait_for_graph
	{
	private:
		template<typename T>
		static T get_max(T x, T y) { return (x > y) ? x : y; }
		static int mode_to_order(access_mode_t mode)
		{
			switch (mode)
			{
			case DB_MODE_NULL: return 0;
			case DB_MODE_RO: return 0;
			case DB_MODE_RW: return 1;
			case DB_MODE_CONST: return 2;
			case DB_MODE_EW: return 3;
			}
			assert(0);
			return 0;//this should never happen
		}
		static access_mode_t order_to_mode(int mode)
		{
			switch (mode)
			{
			case 0: return DB_MODE_RO;
			case 1: return DB_MODE_RW;
			case 2: return DB_MODE_CONST;
			case 3: return DB_MODE_EW;
			}
			assert(0);
			return DB_MODE_RO;//this should never happen
		}
		static access_mode_t combine_modes(access_mode_t m1, access_mode_t m2)
		{
			//old terminology
			//     NCR ITW RO  EW
			// NCR NCR ITW RO  EW 
			// ITW ITW ITW EW  EW
			// RO  RO  EW  RO  EW
			// EW  EW  EW  EW  EW
			//new terminology:
			//     RO  RW  CON EW
			// RO  RO  RW  CON EW 
			// RW  RW  RW  EW  EW
			// CON CON EW  CON EW
			// EW  EW  EW  EW  EW
			if ((m1 == DB_MODE_CONST && m2 == DB_MODE_RW) || (m2 == DB_MODE_CONST && m1 == DB_MODE_RW)) return DB_MODE_EW;
			return order_to_mode(get_max(mode_to_order(m1), mode_to_order(m2)));
		}

		struct dual_lock
		{
			dual_lock(node& n1, node& n2)
			{
				acquire(n1, n2);
			}
			dual_lock(node* n1, node& n2)
			{
				acquire(*n1, n2);
			}
			dual_lock(node* n1, node* n2)
			{
				acquire(*n1, *n2);
			}
			dual_lock(node& n1, node* n2)
			{
				acquire(n1, *n2);
			}
			void release()
			{
				l1.release();
				l2.release();
			}
		private:
			void acquire(node& n1, node& n2)
			{
				//lock in the order of increasing types and guids to prevent deadlock
				assert(n1.guid() != n2.guid());
				if (n1.type() < n2.type())
				{
					l1.acquire(n1.wfg_node_data_.mutex);
					l2.acquire(n2.wfg_node_data_.mutex);
				}
				else if (n1.type() > n2.type())
				{
					l2.acquire(n2.wfg_node_data_.mutex);
					l1.acquire(n1.wfg_node_data_.mutex);
				}
				else if (n1.guid() < n2.guid())
				{
					l1.acquire(n1.wfg_node_data_.mutex);
					l2.acquire(n2.wfg_node_data_.mutex);
				}
				else
				{
					l2.acquire(n2.wfg_node_data_.mutex);
					l1.acquire(n1.wfg_node_data_.mutex);
				}
			}
			tbb::spin_mutex::scoped_lock l1;
			tbb::spin_mutex::scoped_lock l2;
		};

		static bool try_lock__locked(db* data, access_mode_t mode)
		{
			switch (mode)
			{
			case DB_MODE_RW:
			case DB_MODE_RO:
				if (data->wfg_data_.exclusive_locks.load() == 0)
				{
					++data->wfg_data_.shared_locks;
					return true;
				}
				break;
			case DB_MODE_CONST:
				if (data->wfg_data_.shared_locks.load() == 0 && data->wfg_data_.exclusive_write.load() == false)
				{
					++data->wfg_data_.exclusive_locks;
					return true;
				}
				break;
			case DB_MODE_EW:
				if (data->wfg_data_.shared_locks.load() == 0 && data->wfg_data_.exclusive_locks.load() == 0)
				{
					++data->wfg_data_.exclusive_locks;
					data->wfg_data_.exclusive_write = true;
					return true;
				}
				break;
			default:
				assert(0);
				break;
			}
			return false;
		}

		static void unlock(db* data, access_mode_t mode)
		{
			tbb::spin_mutex::scoped_lock lock(data->wfg_data_.mutex);
			edt* new_owner = 0;
			typename memory::list<edt*>::the *new_owners = 0;
			switch (mode)
			{
			case DB_MODE_RW:
			case DB_MODE_RO:
				if (--data->wfg_data_.shared_locks == 0)
				{
					assert(data->wfg_data_.shared_waitlist.empty());
					if (!data->wfg_data_.exclusive_write_waitlist.empty())
					{
						new_owner = data->wfg_data_.exclusive_write_waitlist.front();
						data->wfg_data_.exclusive_write_waitlist.pop_front();
					}
					else if (!data->wfg_data_.exclusive_read_waitlist.empty())
					{
						new_owners = &data->wfg_data_.exclusive_read_waitlist;
						//data->wfg_data_.exclusive_read_waitlist.pop_front();
					}
				}
				break;
			case DB_MODE_CONST:
				if (--data->wfg_data_.exclusive_locks == 0)
				{
					assert(data->wfg_data_.exclusive_read_waitlist.empty());
					if (!data->wfg_data_.exclusive_write_waitlist.empty())
					{
						new_owner = data->wfg_data_.exclusive_write_waitlist.front();
						data->wfg_data_.exclusive_write_waitlist.pop_front();
					}
					else if (!data->wfg_data_.shared_waitlist.empty())
					{
						new_owners = &data->wfg_data_.shared_waitlist;
						//data->wfg_data_.shared_waitlist.pop_front();
					}
				}
				break;
			case DB_MODE_EW:
				data->wfg_data_.exclusive_write = false;
				if (--data->wfg_data_.exclusive_locks == 0)
				{
					if (!data->wfg_data_.exclusive_write_waitlist.empty())
					{
						new_owner = data->wfg_data_.exclusive_write_waitlist.front();
						data->wfg_data_.exclusive_write_waitlist.pop_front();
					}
					else if (!data->wfg_data_.exclusive_read_waitlist.empty())
					{
						new_owners = &data->wfg_data_.exclusive_read_waitlist;
						//data->wfg_data_.exclusive_read_waitlist.pop_front();
					}
					else if (!data->wfg_data_.shared_waitlist.empty())
					{
						new_owners = &data->wfg_data_.shared_waitlist;
						//data->wfg_data_.shared_waitlist.pop_front();
					}
				}
				break;
			default:
				assert(0);
				break;
			}
			if (new_owner)
			{
				tbb::spin_mutex::scoped_lock lock2(new_owner->wfg_node_data_.mutex);
				assert(new_owner->wfg_node_data_.ordered_guids[new_owner->wfg_node_data_.acquired] == data->guid());
				if (try_lock_all__locked(new_owner, data)) new_owner->spawn();
			}
			if (new_owners)
			{
				while (!new_owners->empty())
				{
					new_owner = new_owners->front();
					new_owners->pop_front();
					tbb::spin_mutex::scoped_lock lock2(new_owner->wfg_node_data_.mutex);
					assert(new_owner->wfg_node_data_.ordered_guids[new_owner->wfg_node_data_.acquired] == data->guid());
					if (try_lock_all__locked(new_owner, data)) new_owner->spawn();

				}
			}
		}

		static void unlock_all(edt* n)
		{
			//I should be the sole owner of n, since it has just been executed
			for (std::size_t j = 0; j < n->wfg_node_data_.ordered_guids.size(); ++j)
			{
				if (n->wfg_node_data_.ordered_guids[j])
				{
					for (std::size_t i = 0; i < n->held_dbs_.size(); ++i)
					{
						if (n->wfg_node_data_.ordered_guids[j] == n->held_dbs_[i]->guid()) n->held_dbs_[i] = 0;
					}
					unlock(guided::from_guid(n->wfg_node_data_.ordered_guids[j])->as_db(), n->wfg_node_data_.lock_modes[j]);
				}
			}
			for (std::size_t i = 0; i < n->held_dbs_.size(); ++i)
			{
				if (n->held_dbs_[i]) unlock(n->held_dbs_[i], n->held_db_modes_[i]);
			}
		}

		void release_db_internal(guid_t task_guid, guid_t data_guid, access_mode_t mode)
		{
			//I should be the sole owner of task, since it is currently running
			edt* task = guided::from_guid(task_guid)->as_edt();
			db* data = guided::from_guid(data_guid)->as_db();
			for (std::size_t j = 0; j < task->wfg_node_data_.ordered_guids.size(); ++j)
			{
				if (task->wfg_node_data_.ordered_guids[j] == data_guid)
				{
					task->wfg_node_data_.ordered_guids[j] = NULL_GUID;
					unlock(data, task->wfg_node_data_.lock_modes[j]);
					return;
				}
			}
			unlock(data, mode);
		}

		static bool try_lock_all__locked(edt* task, db* locked_db)
		{
			for (std::size_t i = task->wfg_node_data_.acquired; i < task->wfg_node_data_.ordered_guids.size(); ++i)
			{
				db* data = guided::from_guid(task->wfg_node_data_.ordered_guids[i])->as_db();
				tbb::spin_mutex::scoped_lock lock;
				if (data != locked_db) lock.acquire(data->wfg_data_.mutex);
				if (!try_lock__locked(data, task->wfg_node_data_.lock_modes[i]))
				{
					switch (task->wfg_node_data_.lock_modes[i])
					{
					case DB_MODE_RW:
					case DB_MODE_RO:
						data->wfg_data_.shared_waitlist.push_back(task);
						break;
					case DB_MODE_CONST:
						data->wfg_data_.exclusive_read_waitlist.push_back(task);
						break;
					case DB_MODE_EW:
						data->wfg_data_.exclusive_write_waitlist.push_back(task);
						break;
					default:
						assert(0);
						break;
					}
					return false;
				}
				++task->wfg_node_data_.acquired;
			}
			return true;
		}

		u8 satisfy_event_internal__locked(guid_t event_guid, guid_t data_guid, u32 slot)
		{
			if (guided::from_guid(event_guid)->type() == G_event)
			{
				event* event = guided::from_guid(event_guid)->as_event();
				//if (!event->takes_arg() && data_guid != NULL_GUID) return EINVAL;//if the event is said to not carry data, but data is provided return EINVAL
				if (event->is_triggered() && event->get_type() != OCR_EVENT_IDEM_T) return EINVAL;//only idempotent event may be re-triggered
				if (event->get_type() == OCR_EVENT_CHANNEL_T)
				{
					assert(slot == 0);
					if (event->channel_has_sink())
					{
						node::postslot_t sink = event->pop_channel_sink();
						satisfy_event(sink.node, data_guid, sink.slot);
					}
					else
					{
						event->add_channel_data(data_guid);
					}
				}
				else if (event->satisfy_preslot(slot, data_guid))
				{
					for (std::size_t i = 0; i < event->get_postslot_count(); ++i)
					{
						node* successor = guided::from_guid(event->get_postslot_node(i))->as_node();
						tbb::spin_mutex::scoped_lock lock(successor->wfg_node_data_.mutex);//this should not deadlock, as we only hold the event lock. The dual_lock is used for dependency creataion and there is no node->event
						satisfy_event_internal__locked(successor->guid(), data_guid, event->get_postslot_slot(i));
						if (successor->type() == G_event && successor->as_event()->was_destroyed())
						{
							lock.release();
							successor->as_event()->eliminate();
						}
					}
				}
			}
			else if (guided::from_guid(event_guid)->type() == G_edt)
			{
				edt* e = guided::from_guid(event_guid)->as_edt();
				db* data = guided::from_guid(data_guid)->as_db();
				assert(slot < e->get_preslot_count());
				access_mode_t mode = e->get_preslot_mode(slot);
				e->set_preslot_data(slot, data_guid);
				if (e->all_satisfied())
				{
					e->wfg_node_data_.ordered_guids.clear();
					e->wfg_node_data_.ordered_guids.reserve(e->get_preslot_count());
					for (u32 i = 0; i < e->get_preslot_count(); ++i)
					{
						if (e->get_preslot_data(i) && std::find(e->wfg_node_data_.ordered_guids.begin(), e->wfg_node_data_.ordered_guids.end(), e->get_preslot_data(i)) == e->wfg_node_data_.ordered_guids.end())
						{
							e->wfg_node_data_.ordered_guids.push_back(e->get_preslot_data(i));
						}
					}
					std::sort(e->wfg_node_data_.ordered_guids.begin(), e->wfg_node_data_.ordered_guids.end());
					e->wfg_node_data_.lock_modes.resize(e->wfg_node_data_.ordered_guids.size(), order_to_mode(0));
					for (u32 i = 0; i < e->get_preslot_count(); ++i)
					{
						guid_t preslot_data = e->get_preslot_data(i);
						if (preslot_data)
						{
							for (std::size_t j = 0; j < e->wfg_node_data_.ordered_guids.size(); ++j)
							{
								if (e->wfg_node_data_.ordered_guids[j] == preslot_data)
								{
									e->wfg_node_data_.lock_modes[j] = combine_modes(e->wfg_node_data_.lock_modes[j], e->get_preslot_mode(i));
								}
							}
						}
					}
					if (try_lock_all__locked(e, 0))
					{
						e->spawn();
					}
				}
			}
			return 0;
		}
	public:

		void release_db(guid_t task_guid, guid_t data_guid, access_mode_t mode)
		{
			DEBUG_COUT("release DB " << guid_out(data_guid) << " by EDT " << guid_out(task_guid));
			release_db_internal(task_guid, data_guid, mode);
		}

		u8 satisfy_event(guid_t event_guid, guid_t data_guid, u32 slot)
		{
			DEBUG_COUT("satisfy event " << guid_out(event_guid) << " with data " << guid_out(data_guid) << " on slot " << slot);
			guided* g_source = guided::from_guid(event_guid);
			if (g_source && (g_source->type() == G_edt || g_source->type() == G_event))
			{
				tbb::spin_mutex::scoped_lock lock(g_source->as_node()->wfg_node_data_.mutex);
				u8 res = satisfy_event_internal__locked(event_guid, data_guid, slot);
				if (g_source->type() == G_event && g_source->as_event()->was_destroyed())
				{
					lock.release();
					g_source->as_event()->eliminate();
				}
				return res;
			}
			else
			{
				assert(0);//this does not seem to make sense
				return satisfy_event_internal__locked(event_guid, data_guid, slot);
			}
		}

		void notify_task_finished(guid_t task_guid, guid_t data_guid)
		{
			edt* task = guided::from_guid(task_guid)->as_edt();
			unlock_all(task);
			for (std::size_t i = 0; i < task->get_postslot_count(); ++i)
			{
				node* successor = guided::from_guid(task->get_postslot_node(i))->as_node();
				tbb::spin_mutex::scoped_lock lock(successor->wfg_node_data_.mutex);//this should not deadlock, as we only hold the event lock. The dual_lock is used for dependency creataion and there is no node->event
				satisfy_event_internal__locked(successor->guid(), data_guid, task->get_postslot_slot(i));
				if (successor->type()==G_event && successor->as_event()->was_destroyed())
				{
					lock.release();
					successor->as_event()->eliminate();
				}
			}
		}

		edt* get_my_edt();

		u8 add_dependency(guid_t source, guid_t destination, u32 slot, access_mode_t mode)
		{
			DEBUG_COUT("add dependency " << guid_out(source) << " to " << guid_out(destination) << " slot " << slot << " mode " << mode_out(mode));
			if (mode == DB_DEFAULT_MODE) mode = DB_MODE_RW;
			//event->event, event->edt, db->event, db->edt
			guided* g_source = guided::from_guid(source);
			guided* g_destination = guided::from_guid(destination);
			if (g_destination->type() != G_edt && g_destination->type() != G_event) return EPERM;
			node* n_destination = g_destination->as_node();
			if (slot >= n_destination->get_preslot_count()) return EINVAL;
			if (source == NULL_GUID)
			{
				tbb::spin_mutex::scoped_lock lock(n_destination->wfg_node_data_.mutex);
				n_destination->set_preslot_mode(slot, ocrDbAccessMode_t(0));
				satisfy_event_internal__locked(destination, NULL_GUID, slot);
				if (n_destination->type() == G_event && n_destination->as_event()->was_destroyed())
				{
					lock.release();
					n_destination->as_event()->eliminate();
				}
				return 0;
			}
			n_destination->set_preslot_mode(slot, mode);
			if (g_source->type() == G_db)
			{
				db* db = g_source->as_db();
				tbb::spin_mutex::scoped_lock lock(n_destination->wfg_node_data_.mutex);
				satisfy_event_internal__locked(destination, source, slot);
				if (n_destination->type() == G_event && n_destination->as_event()->was_destroyed())
				{
					lock.release();
					n_destination->as_event()->eliminate();
				}
			}
			else if (g_source->type() == G_event)
			{
				event* e_source = g_source->as_event();
				dual_lock lock(e_source, n_destination);
				if (e_source->get_type() == OCR_EVENT_CHANNEL_T)
				{
					if (e_source->channel_has_data())
					{
						guid_t data = e_source->pop_channel_data();
						satisfy_event_internal__locked(destination, data, slot);
						if (n_destination->type() == G_event && n_destination->as_event()->was_destroyed())
						{
							lock.release();
							n_destination->as_event()->eliminate();
						}
					}
					else
					{
						e_source->add_channel_sink(node::postslot_t(destination, slot));
					}
				}
				else
				{
					e_source->add_postslot(node::postslot_t(destination, slot));
					if (e_source->is_triggered())
					{
						satisfy_event_internal__locked(destination, e_source->get_preslot_data(0), slot);
						if (n_destination->type() == G_event && n_destination->as_event()->was_destroyed())
						{
							lock.release();
							n_destination->as_event()->eliminate();
						}
					}
				}
			}
			else if (g_source->type() == G_edt)
			{
				edt* e_source = g_source->as_edt();
				dual_lock lock(e_source, n_destination);
				e_source->add_postslot(node::postslot_t(destination, slot));
			}
			return 0;
		}

	};
}

#endif
