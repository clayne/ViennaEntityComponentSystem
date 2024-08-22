#pragma once

#include <limits>
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <variant>
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>
#include <ranges>
#include <map>
#include <set>
#include <any>
#include <cassert>
#include <bitset>
#include <algorithm>    // std::sort
#include <VTLL.h>


using namespace std::chrono_literals;

namespace std {
	template<>
	struct hash<std::vector<size_t>*> {
		std::size_t operator()(std::vector<std::size_t>* hashes) const {
			std::size_t seed = 0;
			std::sort(hashes->begin(), hashes->end());
			for( auto& v : *hashes ) {
				seed ^= v + 0x9e3779b9 + (seed<<6) + (seed>>2);
			}
			return seed;
		}
	};

	template<>
	struct hash<std::vector<size_t>&> {
		std::size_t operator()(std::vector<size_t>& types) const {
			return std::hash<std::vector<size_t>*>{}(&types);
		}
	};

}


namespace vecs
{
	using Handle = std::size_t;

	template <typename> struct is_tuple : std::false_type {};
	template <typename ...Ts> struct is_tuple<std::tuple<Ts...>> : std::true_type {};


	template<typename T>
	auto type() -> std::size_t {
		return std::type_index(typeid(std::decay_t<T>)).hash_code();
	}

	template<typename... Ts>
		requires (vtll::unique<vtll::tl<Ts...>>::value)
	class Iterator;

	template<typename... Ts>
		requires (vtll::unique<vtll::tl<Ts...>>::value)
	class View;


	/// @brief A registry for entities and components.
	class Registry {

		template<typename... Ts> requires (vtll::unique<vtll::tl<Ts...>>::value) friend class Iterator;
		template<typename... Ts> requires (vtll::unique<vtll::tl<Ts...>>::value) friend class View;

	private:
	
		//----------------------------------------------------------------------------------------------

		/// @brief Base class for component maps.
		class ComponentMapBase {
		public:
			ComponentMapBase() = default;
			~ComponentMapBase() = default;
			
			size_t insert(Handle handle, auto&& v) {
				using T = std::decay_t<decltype(v)>;
				return static_cast<ComponentMap<T>*>(this)->insert(handle, std::forward<T>(v));
			}

			virtual auto erase(std::size_t index) -> std::optional<Handle> = 0;
			virtual auto move(ComponentMapBase* other, size_t from) -> size_t = 0;
			virtual auto size() -> size_t= 0;
			virtual auto create() -> std::unique_ptr<ComponentMapBase> = 0;
		};

		/// @brief A map of components of the same type.
		/// @tparam T The value type for this comoonent map.
		template<typename T>
			requires std::same_as<T, std::decay_t<T>>
		class ComponentMap : public ComponentMapBase {
		public:

			ComponentMap() = default; //constructor

			/// @brief Entry of the map, containing the handle and the component value.
			struct Entry {
				Handle m_handle;
				T m_value;
			};

			/// @brief Insert a new component value.
			/// @param handle The handle of the entity.
			/// @param v The component value.
			/// @return The index of the component value in the map.
			[[nodiscard]] auto insert(Handle handle, auto&& v) -> std::size_t {
				using T = std::decay_t<decltype(v)>;
				m_data.emplace_back( handle, std::forward<T>(v) );
				return m_data.size() - 1;
			};

			/// @brief Get reference to the component of an entity.
			/// @param index The index of the component value in the map.
			/// @return Reference to the component value.
			[[nodiscard]] auto get(std::size_t index) -> Entry& {
				assert(index < m_data.size());
				return m_data[index];
			};

			/// @brief Get the data vector of the components.
			/// @return Reference to the data vector.
			[[nodiscard]] auto& data() {
				return m_data;
			};

			/// @brief Erase a component from the vector.
			/// @param index The index of the component value in the map.
			/// @return The handle of the entity that was moved over the erased one so its index can be changed, or nullopt
			[[nodiscard]] virtual auto erase(size_t index) -> std::optional<Handle> override {
				std::optional<Handle> ret{std::nullopt};
				size_t last = m_data.size() - 1;
				if( index < last ) {
					ret = { m_data[last].m_handle }; //return the handle of the last entity that was moved over the erased one
					m_data[index] = m_data[last]; //move the last entity to the erased one
				}
				m_data.pop_back(); //erase the last entity
				return ret;
			};

			/// @brief Move a component from another map to this map.
			/// @param other The other map.
			/// @param from The index of the component in the other map.
			virtual size_t move(ComponentMapBase* other, size_t from) override {
				m_data.push_back( static_cast<ComponentMap<std::decay_t<T>>*>(other)->get(from) );
				return m_data.size() - 1;
			};

			/// @brief Get the size of the map.
			/// @return The size of the map.
			[[nodiscard]] virtual auto size() -> size_t override {
				return m_data.size();
			}

			/// @brief Create a new map.
			/// @return A unique pointer to the new map.
			[[nodiscard]] virtual auto create() -> std::unique_ptr<ComponentMapBase> override {
				return std::make_unique<ComponentMap<T>>();
			};

		private:
			std::vector<Entry> m_data; //vector of component values
		};

		//----------------------------------------------------------------------------------------------

		struct ActionCreate{};
		struct ActionInsert{};
		struct ActionRemove{};

		class Archetype {

			template<typename... Ts> requires (vtll::unique<vtll::tl<Ts...>>::value) friend class View;
			template<typename... Ts> requires (vtll::unique<vtll::tl<Ts...>>::value) friend class Iterator;

		public:

			/// @brief Constructor, called if a new entity should be created with components, and the archetype does not exist yet.
			/// @tparam ...Ts 
			/// @param handle The handle of the entity.
			/// @param ...values The values of the components.
			template<typename... Ts>
				requires (vtll::unique<vtll::tl<Ts...>>::value)
			Archetype( ActionCreate c, Handle handle, Ts&& ...values ) {

				size_t index{0};
				auto func = [&]( auto&& v ) {
					using T = std::decay_t<decltype(v)>;
					m_maps[type<T>()] = std::make_unique<ComponentMap<T>>();
					index = m_maps[type<T>()]->insert(handle, std::forward<T>(v));
					m_types.push_back(type<T>());
				};

				(func( std::forward<Ts>(values) ), ...);

				m_index[handle] = index;
				assert(validate());
			}

			/// @brief Constructor, called if the components of an entity are changed, but the new archetype does not exist yet.
			/// @tparam T Possible new component type.
			/// @param other The original archetype.
			/// @param index The index of the entity in the old archetype.
			/// @param value Pointer to a new component value if component is added, or nullptr if the component is removed.
			template<typename... Ts>
				requires (!std::is_pointer<Ts>::value && ...)
			Archetype( ActionInsert i, Archetype& other, Handle handle, Ts&&... value) {
				size_t index = other.m_index[handle]; //get the index of the entity in the old archetype
				m_types = other.m_types; //make a copy of the types

				auto func = [&](auto&& v) {
					using T = std::decay_t<decltype(v)>;
					assert( std::find(m_types.begin(), m_types.end(), type<T>()) == m_types.end());
					m_types.push_back(type<T>()); //add the new type
					auto map = std::make_unique<ComponentMap<std::decay_t<T>>>(); //create the new component map
					map->insert( handle, std::forward<T>(v) ); //insert the new value
					m_maps[type<T>()] = std::move(map); //move to the map list
				};
				(func( value ), ...);
				std::sort(m_types.begin(), m_types.end()); //sort the types

				//copy the other components
				for( auto& it : other.m_types ) { //go through all maps
					m_maps[it] = other.m_maps[it]->create(); //make a component map like this one
					m_maps[it]->move(other.map(it), index); //insert the new value
				}
				other.erase(index); //erase the old entity
				assert(validate());
			}

			template<typename... Ts>
				//requires (std::is_pointer<Ts>::value && ...)
			Archetype(ActionRemove r, Archetype& other, Handle handle, Ts*... value) {
				size_t index = other.m_index[handle]; //get the index of the entity in the old archetype
				m_types = other.m_types; //make a copy of the types

				auto func = [&](auto* v) {
					using T = std::decay_t<std::remove_pointer_t<decltype(v)>>;
					auto it = std::find(m_types.begin(), m_types.end(), type<T>());
					assert(it != m_types.end());
					m_types.erase(it); //remove the type from the list
				};

				(func( value ), ...);

				//copy the other components
				for( auto& it : other.m_maps ) { //go through all maps
					m_maps[it.first] = other.m_maps[it.first]->create(); //make a component map like this one
					m_maps[it.first]->move(other.map(it.first), index); //insert the new value
				}
				other.erase(index); //erase the old entity
				assert(validate());
			}

			/// @brief Get the types of the components.
			/// @return A vector of type indices.
			[[nodiscard]] auto& types() {
				return m_types;
			}

			/// @brief Test if the archetype has a component.
			/// @param ti The type index of the component.
			/// @return true if the archetype has the component, else false.
			[[nodiscard]] bool has(const size_t ti) {
				return (std::find(m_types.begin(), m_types.end(), ti) != std::end(m_types));
			}

			/// @brief Create an entity with components.
			/// @tparam ...Ts The types of the components, must be unique.
			/// @param handle The handle of the entity.
			/// @param ...component The new values.
			/// @return The index of the entity in the archetype.
			template<typename... Ts>
				requires ((sizeof...(Ts) > 0) && (vtll::unique<vtll::tl<Ts...>>::value))
			[[nodiscard]] auto insert( Handle handle, Ts&&... value ) -> size_t {
				size_t index{0};

				auto func = [&](auto&& v) {
					using T = std::decay_t<decltype(v)>;
					auto* map = static_cast<ComponentMap<std::decay_t<T>>*>(m_maps[type<T>()].get());
					index = map->insert(handle, std::forward<T>(v));
				};
				(func( std::forward<Ts>(value) ), ...); //insert all components
				assert(validate()); //all component maps should have the same size
				m_index[handle] = index; //store the index of the new entity in the archetype
				return index; //return the index of the new entity in the archetype
			}

			/// @brief Get component value of an entity. 
			/// @tparam T The type of the component.
			/// @param index The index of the entity in the archetype.
			/// @return The component value.
			template<typename T>
			[[nodiscard]] auto get(Handle handle) -> T& {
				assert(std::find(m_types.begin(), m_types.end(), type<T>()) != std::end(m_types) && m_maps.find(type<T>()) != m_maps.end() ); 
				size_t index = m_index[handle];
				assert( index < m_maps[type<T>()]->size());
				return static_cast<ComponentMap<std::decay_t<T>>*>(m_maps[type<T>()].get())->get(index).m_value;
			}

			/// @brief Erase all components of an entity.
			/// @param index The index of the entity in the archetype.
			/// @return The handle of the entity that was moved over the erased one so its index can be changed.
			void erase(Handle handle) {
				size_t index = m_index[handle];
				std::optional<Handle> hd{std::nullopt};
				for( auto& it : m_maps ) {
					hd = it.second->erase(index); //should always be the same handle
				}
				if( hd.has_value() ) { //an entity was moved to fill the empty slot in the vector
					m_index[hd.value()] = index; //update the index of the moved entity
				}
				assert(validate());
			}

			/// @brief Get the map of the components.
			/// @tparam T The type of the component.
			/// @return Pointer to the component map.
			template<typename T>
			auto map() -> ComponentMap<T>* {
				auto it = m_maps.find(type<T>());
				assert(it != m_maps.end());
				return static_cast<ComponentMap<T>*>(it->second.get());
			}

			/// @brief Get the data of the components.
			/// @param ti Type index of the component.
			/// @return Pointer to the component map base class.
			auto map(size_t ti) -> ComponentMapBase* {
				auto it = m_maps.find(ti);
				assert(it != m_maps.end());
				return it->second.get();
			}

			/// @brief Get the number of entites in this archetype.
			/// @return The number of entities.
			size_t size() {
				return m_maps.begin()->second->size();
			}

		private:

			/// @brief Test if all component maps have the same size.
			/// @return  true if all component maps have the same size, else false.
			bool validate() {
				bool first{true};
				size_t sz{0};
				for( auto& it : m_maps ) {
					size_t s = it.second->size();
					if( first || s == sz) {
						sz = s;
						first = false;
					} else return false;
				}
				return true;
			}

			std::vector<size_t> m_types; //types of components
			std::unordered_map<Handle, size_t> m_index; //index of entity in archetype
			std::unordered_map<size_t, std::unique_ptr<ComponentMapBase>> m_maps;
		};


		//----------------------------------------------------------------------------------------------

	public:

		/// @brief Test if a handle is valid, i.e., not 0.
		/// @param handle 
		/// @return true if the handle is valid, else false.
		bool valid(Handle handle) {
			return handle != 0;
		}

		/// @brief Create an entity with components.
		/// @tparam ...Ts The types of the components.
		/// @param ...component The new values.
		/// @return Handle of new entity.
		template<typename... Ts>
			requires ((sizeof...(Ts) > 0) && (vtll::unique<vtll::tl<Ts...>>::value))
		[[nodiscard]] auto create( Ts&&... component ) -> Handle {
			Handle handle{ ++m_next_id };

			std::vector<std::size_t> types = {type<Ts>()...};
			auto it = m_archetypes.find(&types);
			if( it == m_archetypes.end() ) {
				m_archetypes[&types] = std::make_unique<Archetype>( ActionCreate{}, handle, std::forward<Ts>(component)... );
				m_entities[handle] = m_archetypes[&types].get();
				return handle;
			}

			auto index = it->second->insert(handle, std::forward<Ts>(component)...);
			m_entities[handle] = it->second.get();
			return handle;
		}

		/// @brief Test if an entity exists.
		/// @param handle The handle of the entity.
		/// @return true if the entity exists, else false.
		bool exists(Handle handle) {
			assert(valid(handle));
			return m_entities.find(handle) != m_entities.end();
		}

		/// @brief Test if an entity has a component.
		/// @tparam T The type of the component.
		/// @param handle The handle of the entity.
		/// @return true if the entity has the component, else false.
		template<typename T>
		bool has(Handle handle) {
			assert(valid(handle));
			return 	exists(handle) && m_entities[handle]->has(type<T>());
		}

		/// @brief Get the types of the components of an entity.
		/// @param handle The handle of the entity.
		/// @return A vector of type indices of the components.
		const auto& types(Handle handle) {
			assert(exists(handle));
			return m_entities[handle]->types();
		}

		/// @brief Get component value of an entity. If the component does not exist, it will be created.
		/// This might result in moving the entity to another archetype.
		/// @tparam T The type of the component.
		/// @param handle The handle of the entity.	
		/// @return The component value.
		template<typename T>
		[[nodiscard]] auto get(Handle handle) -> T {
			assert(has<T>(handle));
			return m_entities[handle]->get<T>(handle);
		}

		/// @brief Get component values of an entity.
		/// @tparam Ts The types of the components.
		/// @param handle The handle of the entity.
		/// @return A tuple of the component values.
		template<typename... Ts>
			requires ((sizeof...(Ts) > 1) && (vtll::unique<vtll::tl<Ts...>>::value))
		[[nodiscard]] auto get(Handle handle) -> std::tuple<Ts...> {
			return std::tuple<Ts...>(get<Ts>(handle)...);
		}

		/// @brief Put a new component value to an entity. If the entity does not have the component, it will be created.
		/// This might result in moving the entity to another archetype.
		/// @tparam T The type of the component.
		/// @param handle The handle of the entity.
		/// @param v The new value.
		template<typename T>
			requires (!is_tuple<T>::value)
		void put(Handle handle, T&& v) {
			assert(exists(handle));
			if(!has<T>(handle)) {
				auto typ = m_entities[handle]->types();
				typ.push_back(type<T>());
				auto it = m_archetypes.find(&typ);
				if( it == m_archetypes.end() ) {
					m_archetypes[&typ] = std::make_unique<Archetype>( ActionInsert{}, *m_entities[handle], handle, std::forward<T>(v) );
					m_entities[handle] = m_archetypes[&typ].get();
					return;
				}	
			}
			m_entities[handle]->get<T>(handle) = v; //get the component value and assign the new value
		}

		/// @brief Put new component values to an entity.
		/// @tparam Ts The types of the components.
		/// @param handle The handle of the entity.
		/// @param v The new values in a tuple
		template<typename... Ts>
			requires (vtll::unique<vtll::tl<Ts...>>::value)
		void put(Handle handle, std::tuple<Ts...>& v) {
			(put(handle, std::get<Ts>(v)), ...);
		}

		/// @brief Put new component values to an entity.
		/// @tparam Ts The types of the components.
		/// @param handle The handle of the entity.
		/// @param ...vs The new values.
		template<typename... Ts>
			requires ((sizeof...(Ts) > 1) && (vtll::unique<vtll::tl<Ts...>>::value))
		void put(Handle handle, Ts&&... vs) {
			(put(handle, vs), ...);
		}

		/// @brief Erase components from an entity.
		/// @tparam Ts The types of the components.
		/// @param handle The handle of the entity.
		template<typename... Ts>
		void erase(Handle handle) {
			assert( (has<Ts>(handle) && ...) );
			auto entt = m_entities.find(handle)->second; //get the archetype of the entity
			auto typ = entt->types(); //get the types from the archetype

			auto func = [&]<typename T>() { //remove the types from the list
				auto it = std::find(typ.begin(), typ.end(), type<T>());
				typ.erase(it);
			};
			(func.template operator()<Ts>(), ...);

			auto it = m_archetypes.find(&typ); //find the new archetype if possible
			if( it == m_archetypes.end() ) { //if the archetype does not exist, create it
				auto map = std::make_unique<Archetype>( ActionRemove{}, *entt, handle, (Ts*)nullptr... );
				m_entities[handle] = map.get();
				m_archetypes[&typ] = std::move(map);
				return;
			}
			m_entities[handle] = it->second.get();
		}

		/// @brief Erase an entity from the registry.
		/// @param handle The handle of the entity.
		void erase(Handle handle) {
			assert(exists(handle));
			m_entities[handle]->erase(handle); //erase the entity from the archetype
			m_entities.erase(handle); //erase the entity from the entity list
		}

		template<typename... Ts>
			requires (vtll::unique<vtll::tl<Ts...>>::value)
		[[nodiscard]] decltype(auto) view() {
			return View<Ts...>{*this};
		}


	private:
		std::size_t m_next_id{0};
		std::unordered_map<Handle, Archetype*> m_entities; //Archetype and index in archetype
		std::unordered_map<std::vector<size_t>*, std::unique_ptr<Archetype>> m_archetypes; //Mapping vector of type index to archetype
		//std::unordered_map<size_t, std::set<Archetype*>> m_types; //Mapping type index to archetype
	};

	
	//----------------------------------------------------------------------------------------------


	template<typename... Ts>
		requires (vtll::unique<vtll::tl<Ts...>>::value)
	class Iterator {

	public:
		Iterator(std::vector<Registry::Archetype*>& arch, size_t archidx) : m_archetypes{arch}, m_archidx{archidx} {}

		auto operator++() {
			if( ++m_entidx == m_archetypes[m_archidx]->m_maps.begin()->second->size() ) {
				++m_archidx;
				m_entidx = 0;
			}
			return *this;
		}

		auto operator*() {
			
			Handle handle;

			auto func = [&]<typename T>() {
				handle = static_cast<Registry::ComponentMap<T>*>(m_archetypes[m_archidx]->m_maps[type<T>()].get())->get(m_entidx).m_handle;
				return static_cast<Registry::ComponentMap<T>*>(m_archetypes[m_archidx]->m_maps[type<T>()].get())->get(m_entidx).m_value;
			};

			auto tup = std::tuple<Ts...>(func.template operator()<Ts>()...);
			return std::tuple_cat( tup, std::make_tuple(handle) );
		}

		auto operator!=(const Iterator& other) {
			return m_archidx != other.m_archidx || m_entidx != other.m_entidx;
		}

	private:
		size_t m_archidx{0};
		size_t m_entidx{0};
		std::vector<Registry::Archetype*>& m_archetypes;
	};


	//----------------------------------------------------------------------------------------------

	template<typename... Ts>
		requires (vtll::unique<vtll::tl<Ts...>>::value)
	class View {

	public:
		View(Registry& system) : m_system{system} {}

		auto begin() {
			m_archetypes.clear();
			for( auto& it : m_system.m_archetypes ) {
				bool found{true};
				auto arch = it.second.get();

				auto func = [&]<typename T>() {
					if( std::find(arch->m_types.begin(), arch->m_types.end(), type<T>()) == arch->m_types.end() ) {
						found = false;
					}
				};
				(func.template operator()<Ts>(), ...);

				if( found ) m_archetypes.push_back(it.second.get());
			}
			return Iterator<Ts...>{m_archetypes, 0};
		}

		auto end() {
			return Iterator<Ts...>{m_archetypes, m_archetypes.size()};
		}

	private:
		Registry& m_system;
		std::vector<Registry::Archetype*> m_archetypes;
	};

}


