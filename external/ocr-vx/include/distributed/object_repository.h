/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__object_repository_H_GUARD
#define OCR_TBB_distributed__object_repository_H_GUARD

namespace ocr_tbb
{
	namespace distributed
	{
		struct object_repository
		{
			static guid add_object(thread_context* ctx, guided* ptr)
			{
				tbb::concurrent_vector<guided*>::iterator it = the(ctx).data_.push_back(ptr);
				return guid(compute_node::get_my_id(ctx), std::distance(the(ctx).data_.begin(), it));
			}
			static bool add_mapped_object(thread_context* ctx, guid g, guided* ptr);
			static guided* get_mapped_object(thread_context* ctx, guid g)
			{
				assert(g.is_mapped());
				assert(g.get_mapped_node_id() == compute_node::get_my_id(ctx));
				accessor ac;
				bool found = the(ctx).mapped_objects_.find(ac, g);
				assert(found);
				return ac->second;
			}
			static guided* remove_mapped_object(thread_context* ctx, guid g, bool archive)
			{
				assert(g.is_mapped());
				assert(g.get_mapped_node_id() == compute_node::get_my_id(ctx));
				accessor ac;
				bool found = the(ctx).mapped_objects_.find(ac, g);
				assert(found);
				guided* res = ac->second;
				the(ctx).mapped_objects_.erase(ac);
				if (archive) the(ctx).mapped_objects_graveyard_.push_back(std::make_pair(g, res));
				return res;
			}
			static guid preallocate_object(thread_context* ctx)
			{
				tbb::concurrent_vector<guided*>::iterator it = the(ctx).data_.push_back(0);
				return guid(compute_node::get_my_id(ctx), std::distance(the(ctx).data_.begin(), it));
			}
			static guid preallocate_objects(thread_context* ctx, std::size_t count)
			{
				tbb::concurrent_vector<guided*>::iterator it = the(ctx).data_.grow_by(count);
				return guid(compute_node::get_my_id(ctx), std::distance(the(ctx).data_.begin(), it));
			}
			static void set_object(thread_context* ctx, guid g, guided* ptr)
			{
				assert(g.get_node_id() == compute_node::get_my_id(ctx));
				assert(g.get_object_id() < the(ctx).data_.size());
				the(ctx).data_[(std::size_t)g.get_object_id()] = ptr;
			}
			static guided* get_object(thread_context* ctx, guid g)
			{
				assert(g.get_node_id() == compute_node::get_my_id(ctx));
				if (g.is_mapped())
				{
					mapped_object_storage_type::accessor ac;
					bool found = the(ctx).mapped_objects_.find(ac, g);
					assert(found);
					return ac->second;
				}
				else
				{
					assert(g.get_object_id() < the(ctx).data_.size());
					return the(ctx).data_[(std::size_t)g.get_object_id()];//use [] instead of at to not throw an exception
				}
			}
			static guided* remove_object(thread_context* ctx, guid g)
			{
				assert(g.get_node_id() == compute_node::get_my_id(ctx));
				assert(g.get_object_id() < the(ctx).data_.size());
				guided* res = the(ctx).data_[(std::size_t)g.get_object_id()];//use [] instead of at to not throw an exception
				the(ctx).data_[(std::size_t)g.get_object_id()] = 0;
				return res;
			}
			static bool validate(thread_context* ctx, guid g)
			{
				if (!g.is_local(ctx)) return false;
				return g.get_object_id() < the(ctx).data_.size();
			}
			object_repository()
			{
				data_.push_back(0);//null pointer to reserve the first item and prevent the index (and therefore the GUID) from being 0
				data_.push_back(0);//null pointer to reserve the second item for the affinity object
			}
			template<typename Writer>
			void write(Writer& w) const
			{
				w.template write_val<u64>("object_repository_size", data_.size());
				for (tbb::concurrent_vector<guided*>::const_iterator it = data_.begin(); it != data_.end(); ++it)
				{
					if (!*it) w.write_val("object_type",(u8)0);
					else w.write_obj("object",**it);
				}
				w.template write_val<u64>("mapped_objects_size", mapped_objects_.size());
				for (mapped_object_storage_type::const_iterator it = mapped_objects_.begin(); it != mapped_objects_.end(); ++it)
				{
					w.write_val("guid", it->first);
					if (!it->second) w.write_val("object_type", (u8)0);
					else w.write_obj("object", *(it->second));
				}
				w.template write_val<u64>("mapped_objects_graveyard_size", mapped_objects_graveyard_.size());
				for (mapped_object_graveyard_type::const_iterator it = mapped_objects_graveyard_.begin(); it != mapped_objects_graveyard_.end(); ++it)
				{
					w.write_val("guid", it->first);
					if (!it->second) w.write_val("object_type", (u8)0);
					else w.write_obj("object", *(it->second));
				}
			}
			void clear(thread_context* ctx);
			template<typename Reader>
			void read(Reader& r)
			{
				object_id oid = 0;
				u64 size = r.template read_val<u64>();
				while (size--)
				{
					object_type ot = (object_type)r.template read_val<u8>();
					guid g = guid(compute_node::get_my_id(r.ctx), oid++);
					if (ot != 0)
					{
						data_.push_back(guided_read(ot, g, r));
					}
					else
					{
						data_.push_back(0);
					}
				}
				size = r.template read_val<u64>();
				while (size--)
				{
					guid g = r.template read_val<guid>();
					object_type ot = (object_type)r.template read_val<u8>();
					if (ot != 0)
					{
						mapped_objects_.insert(std::make_pair(g, guided_read(ot, g, r)));
					}
					else
					{
						mapped_objects_.insert(std::make_pair(g, (guided*)0));
					}
				}
				size = r.template read_val<u64>();
				while (size--)
				{
					guid g = r.template read_val<guid>();
					object_type ot = (object_type)r.template read_val<u8>();
					if (ot != 0)
					{
						mapped_objects_graveyard_.push_back(std::make_pair(g, guided_read(ot, g, r)));
					}
					else
					{
						mapped_objects_graveyard_.push_back(std::make_pair(g, (guided*)0));
					}
				}
			}
#if(OCR_WITH_OPENCL)
			static void reserve_guids_for_opencl_affinities(std::size_t device_count, std::size_t node_count)
			{
#if (SIMULATE_MULTIPLE_NODES)
				node_id old = thread_to_node_id.local();
				for (std::size_t i = 0; i < node_count; ++i)
				{
					thread_to_node_id.local() = (node_id)i;
					for (std::size_t d = 0; d < device_count; ++d)
					{
						std::size_t off = std::distance(the(ctx).data_.begin(), the(ctx).data_.push_back(0));
						assert(off == d+2);
					}
				}
				thread_to_node_id.local() = old;
#else
				for (std::size_t d = 0; d < device_count; ++d)
				{
					std::size_t off = std::distance(the(ctx).data_.begin(), the(ctx).data_.push_back(0));
					assert(off == d + 2);
				}
#endif
			}
#endif
		private:
			static object_repository& the(thread_context* ctx);
			tbb::concurrent_vector<guided*> data_;
			typedef tbb::concurrent_hash_map<guid, guided*> mapped_object_storage_type;
			typedef mapped_object_storage_type::accessor accessor;
			mapped_object_storage_type mapped_objects_;
			typedef tbb::concurrent_vector<std::pair<guid, guided*> > mapped_object_graveyard_type;
			mapped_object_graveyard_type mapped_objects_graveyard_;
			friend struct observer;
		};
	}
}

#endif
