/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__object_cache_H_GUARD
#define OCR_TBB_distributed__object_cache_H_GUARD

namespace ocr_tbb
{
	namespace distributed
	{
		struct object_cache
		{
			object_cache(std::size_t node_count)
			{
				data_.resize(node_count);
			}
			typedef communicator_base Communicator;
			static guid reserve_guid(thread_context* ctx, node_id node)
			{
				if (node == compute_node::get_my_id(ctx))
				{
					return object_repository::preallocate_object(ctx);
				}
				node_data_container& ndc(the(ctx).data_[(std::size_t)node]);
				tbb::spin_mutex::scoped_lock lock(ndc.preallocations_mutex);
				if (ndc.preallocation_next >= ndc.preallocation_max)
				{
					guid next = Communicator::send_and_wait::CMD_allocate_guid(ctx, node);
					ndc.preallocation_next = next;
					ndc.preallocation_max = guid(node, next.get_object_id() + PREALLOCATE_COUNT);
				}
				guid res = ndc.preallocation_next.as_ocr_guid();
				ndc.preallocation_next = guid(node, ndc.preallocation_next.get_object_id() + 1);
				return res;
				
			}
			static guided* try_get_object_locally(thread_context* ctx, guid g);
			static guided* get_object(thread_context* ctx, guid g)
			{
				node_data_container::accessor ac;
				if (the(ctx).data_[(std::size_t)g.get_node_id()].insert(ac, g))
				{
					//the object is not present in the cache
					ac->second = Communicator::fetch(ctx, g);
					return ac->second;
				}
				else
				{
					return ac->second;
				}
			}
			static guided* get_object_locally(thread_context* ctx, guid g)
			{
				node_data_container::accessor ac;
				if (the(ctx).data_[(std::size_t)g.get_node_id()].insert(ac, g))
				{
					assert(0);
					//the object is not present in the cache
					ac->second = Communicator::fetch(ctx, g);
					return ac->second;
				}
				else
				{
					return ac->second;
				}
			}
			static bool add_object(thread_context* ctx, guid g, guided* obj);
			template<typename Writer>
			void write(Writer& w) const
			{
				w.template write_val<u64>("object_cache_size", data_.size());
				for (std::vector<node_data_container>::const_iterator it = data_.begin(); it != data_.end(); ++it)
				{
					w.write_obj("node_cache", *it);
				}
			}
			void clear(thread_context* ctx)
			{
				for (std::vector<node_data_container>::iterator it = data_.begin(); it != data_.end(); ++it)
				{
					it->clear(ctx);
				}
			}
			template<typename Reader>
			void read(Reader& r)
			{
				std::size_t count = r.template read_val<u64>();
				data_.resize(count);
				node_id node = 0;
				for (std::vector<node_data_container>::iterator it = data_.begin(); it != data_.end(); ++it,++node)
				{
					it->read(node, r);
				}
			}
		private:
			static object_cache& the(thread_context* ctx);
			struct node_data_container
			{
				node_data_container() : preallocation_next(0), preallocation_max(0)
				{
				}
				typedef tbb::concurrent_hash_map<guid, guided*> object_storage_type;
				typedef object_storage_type::accessor accessor;
				bool insert(accessor& ac, guid id)
				{
					return objects.insert(ac, id);
				}
				node_data_container(const node_data_container& other) : preallocation_next(0), preallocation_max(0) {}
				template<typename Writer>
				void write(Writer& w) const
				{
					w.template write_val<u64>("node_cache_size", objects.size());
					for (object_storage_type::const_iterator it = objects.begin(); it != objects.end(); ++it)
					{
						w.write_val("id", it->first);
						if (!it->second) w.write_val("object_type", (u8)0);
						else w.write_obj("object", *it->second);
					}
					w.write_val("preallocation_next", preallocation_next);
					w.write_val("preallocation_max", preallocation_max);
				}
				void clear_data();
				void clear(thread_context* ctx)
				{
					clear_data();
					preallocation_next = NULL_GUID;
					preallocation_max = NULL_GUID;
				}
				template<typename Reader>
				void read(node_id node, Reader& r)
				{
					u64 size = r.template read_val<u64>();
					while (size--)
					{
						guid g = r.template read_val<guid>();
						object_type ot = (object_type)r.template read_val<u8>();
						if (ot != 0)
						{
							objects.insert(std::make_pair(g, guided_read(ot, g, r)));
						}
						else
						{
							objects.insert(std::make_pair(g, (guided*)0));
						}
					}
					r.read_ref(preallocation_next);
					r.read_ref(preallocation_max);
				}
			//private:
				object_storage_type objects;
				tbb::spin_mutex preallocations_mutex;
				guid preallocation_next;
				guid preallocation_max;
			};
			std::vector<node_data_container> data_;//one for each node
			friend struct observer;
		};
	}
}

#endif
