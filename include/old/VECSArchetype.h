#pragma once

#include <VECSHandle.h>



namespace vecs {


	//----------------------------------------------------------------------------------------------
	//Convenience functions

	template<typename>
	struct is_std_vector : std::false_type {};

	template<typename T, typename A>
	struct is_std_vector<std::vector<T,A>> : std::true_type {};

	template <typename> struct is_tuple : std::false_type {};
	template <typename ...Ts> struct is_tuple<std::tuple<Ts...>> : std::true_type {};

	template<typename... Ts>
	struct Yes {};

	template<typename... Ts>
	struct No {};
	
	/// @brief Compute the hash of a list of hashes. If stored in a vector, make sure that hashes are sorted.
	/// @tparam T Container type of the hashes.
	/// @param hashes Reference to the container of the hashes.
	/// @return Overall hash made from the hashes.
	template <typename T>
	inline size_t Hash( T& hashes ) {
		std::size_t seed = 0;
		if constexpr ( is_std_vector<std::decay_t<T>>::value ) {
			std::ranges::sort(hashes);
		}
		for( auto& v : hashes ) {
			seed ^= v + 0x9e3779b9 + (seed<<6) + (seed>>2);
		}
		return seed;
	}

	template <typename T>
	inline size_t Hash( T&& hashes ) {
		std::size_t seed = 0;
		if constexpr ( is_std_vector<std::decay_t<T>>::value ) {
			std::ranges::sort(hashes);
		}
		for( auto& v : hashes ) {
			seed ^= v + 0x9e3779b9 + (seed<<6) + (seed>>2);
		}
		return seed;
	}

	template<typename... Ts>
	concept VecsArchetype = (vtll::unique<vtll::tl<Ts...>>::value && (sizeof...(Ts) > 0) && (!std::is_same_v<Handle, std::decay_t<Ts>> && ...));

	const int ARCHETYPE_SEQUENTIAL = 0;
	const int ARCHETYPE_PARALLEL = 1;

	//----------------------------------------------------------------------------------------------
	//Archetype

	/// @brief An archetype of entities with the same components. 
	/// All entities that have the same components are stored in the same archetype. 
	/// The components are stored in the component maps. Note that the archetype class is not templated,
	/// but some methods including a constructor are templated. Thus the class knows only type indices
	/// of its components, not the types themselves.
	template<int ATYPE>
	class Archetype;



	/// @brief An archetype of entities with the same components. 
	/// All entities that have the same components are stored in the same archetype.
	template<int ATYPE = ARCHETYPE_SEQUENTIAL>
	class Archetype {
		//template <int T> requires RegistryType<T> friend class Registry;

	public:

		/// @brief A pair of an archetype and an index. This is stored in the slot map.
		struct ArchetypeAndIndex {
			Archetype<ATYPE>* m_archetypePtr;	//pointer to the archetype
			size_t m_archIndex;			//index of the entity in the archetype
		};	

		Archetype() = default; //default constructor
		/// @brief Constructor, called if a new entity should be created with components, and the archetype does not exist yet.
		/// @tparam ...Ts 
		/// @param handle The handle of the entity.
		/// @param ...values The values of the components.
		template<typename... Ts>
			requires VecsArchetype<Ts...>
		Archetype(Handle handle, size_t& archIndex, Ts&& ...values ) {
			(AddComponent<Ts>(), ...); //insert component types
			(AddValue( std::forward<Ts>(values) ), ...); //insert all components, get index of the handle
			AddComponent<Handle>(); //insert the handle
			archIndex = AddValue( handle ); //insert the handle
		}

		/// @brief Insert a new entity with components to the archetype.
		/// @tparam ...Ts Value types of the components.
		/// @param handle The handle of the entity.
		/// @param ...values The values of the components.
		/// @return The index of the entity in the archetype.
		template<typename... Ts>
			requires VecsArchetype<Ts...>
		size_t Insert(Handle handle, Ts&& ...values ) {
			assert( m_maps.size() == sizeof...(Ts) + 1 );
			assert( (m_maps.contains(Type<Ts>()) && ...) );
			(AddValue( std::forward<Ts>(values) ), ...); //insert all components, get index of the handle
			return AddValue( handle ); //insert the handle
		}

		/// @brief Get referece to the types of the components.
		/// @return A reference to the container of the types.
		[[nodiscard]] const auto&  Types() const {
			return m_types;
		}

		/// @brief Test if the archetype has a component.
		/// @param ti Hash of the type index of the component.
		/// @return true if the archetype has the component, else false.
		bool Has(const size_t ti) {
			return m_types.contains(ti);
		}

		/// @brief Get component value of an entity. 
		/// @tparam T The type of the component.
		/// @param archIndex The index of the entity in the archetype.
		/// @return The component value.
		template<typename T>
		[[nodiscard]] auto Get(size_t archIndex) -> T& {
			return (*Map<T>())[archIndex];
		}

		/// @brief Get component values of an entity.
		/// @tparam ...Ts Types of the components to get.
		/// @param handle Handle of the entity.
		/// @return A tuple of the component values.
		template<typename... Ts>
			requires ((sizeof...(Ts) > 1) && (vtll::unique<vtll::tl<Ts...>>::value))
		[[nodiscard]] auto Get(size_t archIndex) -> std::tuple<Ts&...> {
			return std::tuple<std::decay_t<Ts>&...>{ Map<std::decay_t<Ts>>()->Get(archIndex)... };
		}

		/// @brief Erase an entity
		/// @param index The index of the entity in the archetype.
		/// @param slotmaps The slot maps vector of the registry.
		void Erase(size_t index, auto& slotmaps) {
			Erase2(index, slotmaps);
		}

		/// @brief Move components from another archetype to this one.
		size_t Move( auto& types, size_t other_index, Archetype& other, auto& slotmaps) {			
			for( auto& ti : types ) { //go through all maps
				if( m_maps.contains(ti) ) m_maps[ti]->copy(other.Map(ti), other_index); //insert the new value
			}
			other.Erase2(other_index, slotmaps); //erase from old component map
			++other.m_changeCounter;
			++m_changeCounter;
			return m_maps[Type<Handle>()]->size() - 1; //return the index of the new entity
		}

		/// @brief Swap two entities in the archetype.
		void Swap(ArchetypeAndIndex& slot1, ArchetypeAndIndex& slot2) {
			assert( slot1.m_value.m_archetypePtr == slot2.m_value.m_archetypePtr );
			for( auto& map : m_maps ) {
				map.second->swap(slot1.m_value.m_archindex, slot2.m_value.m_archindex);
			}
			std::swap(slot1.m_value.m_archindex, slot2.m_value.m_archindex);
			++m_changeCounter;
		}

		/// @brief Clone the archetype.
		/// @param other The archetype to clone.
		/// @param types The types of the components to clone.
		void Clone(Archetype& other, const auto& types) {
			for( auto& ti : types ) { //go through all maps
				m_types.insert(ti); //add the type to the list
				if( other.m_maps.contains(ti) ) m_maps[ti] = other.Map(ti)->clone(); //make a component map like this one
			}
		}

		/// @brief Get the number of entites in this archetype.
		/// @return The number of entities.
		size_t Size() {
			return m_maps[Type<Handle>()]->size();
		}

		/// @brief Clear the archetype.
		void Clear() {
			for( auto& map : m_maps ) {
				map.second->clear();
			}
			++m_changeCounter;
		}

		/// @brief Print the archetype.
		void Print() {
			std::cout << "Archetype: " << Hash(m_types) << std::endl;
			for( auto ti : m_types ) {
				std::cout << "Type: " << ti << " ";
			}
			std::cout << std::endl;
			for( auto& map : m_maps ) {
				std::cout << "Map: ";
				map.second->print();
				std::cout << std::endl;
			}
			std::cout << "Entities: ";
			auto map = Map<Handle>();
			for( auto handle : *map ) {
				std::cout << handle << " ";
			}
			std::cout << std::endl << std::endl;
		}

		/// @brief Validate the archetype. Make sure all maps have the same size.
		void Validate() {
			for( auto& map : m_maps ) {
				assert( map.second->size() == m_maps[Type<Handle>()]->size() );
			}
		}
	
		/// @brief Get the change counter of the archetype. It is increased when a change occurs
		/// that might invalidate a Ref object, e.g. when an entity is moved to another archetype, or erased.
		auto GetChangeCounter() -> size_t {
			return m_changeCounter;
		}

		/// @brief Get the mutex of the archetype.
		/// @return Reference to the mutex.
		[[nodiscard]] auto GetMutex() -> mutex_t& {
			return m_mutex;
		}

		void AddType(size_t ti) {
			assert( !m_types.contains(ti) );
			m_types.insert(ti);	//add the type to the list
		};

		/// @brief Add a new component to the archetype.
		/// @tparam T The type of the component.
		template<typename U>
		void AddComponent() {
			using T = std::decay_t<U>; //remove pointer or reference
			size_t ti = Type<T>();
			assert( !m_types.contains(ti) );
			m_types.insert(ti);	//add the type to the list
			m_maps[ti] = std::make_unique<Vector<T>>(); //create the component map
		};

		/// @brief Add a new component value to the archetype.
		/// @param v The component value.
		/// @return The index of the component value.
		template<typename U>
		auto AddValue( U&& v ) -> size_t {
			using T = std::decay_t<U>;
			return m_maps[Type<T>()]->push_back(std::forward<U>(v));	//insert the component value
		};

		auto AddEmptyValue( size_t ti ) -> size_t {
			return m_maps[ti]->push_back();	//insert the component value
		};

		/// @brief Erase an entity. To ensure thet consistency of the entity indices, the last entity is moved to the erased one.
		/// This might result in a reindexing of the moved entity in the slot map. Thus we need a ref to the slot map
		/// @param index The index of the entity in the archetype.
		/// @param slotmaps Reference to the slot maps vector of the registry.
		void Erase2(size_t index, auto& slotmaps) {
			size_t last{index};
			for( auto& it : m_maps ) {
				last = it.second->erase(index); //Erase from the component map
			}
			if( index < last ) {
				auto& lastHandle = static_cast<Vector<Handle>*>(m_maps[Type<Handle>()].get())->operator[](index);
				slotmaps[lastHandle.GetStorageIndex()].m_slotMap[lastHandle].m_value.m_archIndex = index;
			}
			++m_changeCounter;
		}
		
		/// @brief Get the map of the components.
		/// @tparam T The type of the component.
		/// @return Pointer to the component map.
		template<typename U>
		auto Map() -> Vector<std::decay_t<U>>* {
			auto it = m_maps.find(Type<std::decay_t<U>>());
			assert(it != m_maps.end());
			return static_cast<Vector<std::decay_t<U>>*>(it->second.get());
		}

		/// @brief Get the data of the components.
		/// @param ti Type index of the component.
		/// @return Pointer to the component map base class.
		auto Map(size_t ti) -> VectorBase* {
			auto it = m_maps.find(ti);
			assert(it != m_maps.end());
			return it->second.get();
		}

	private:
		using Size_t = std::conditional_t<ATYPE == ARCHETYPE_SEQUENTIAL, std::size_t, std::atomic<std::size_t>>;
		using Map_t = std::unordered_map<size_t, std::unique_ptr<VectorBase>>;
		mutex_t 			m_mutex; //mutex for thread safety
		Size_t 				m_changeCounter{0}; //changes invalidate references
		std::set<size_t> 	m_types; //types of components
		Map_t 				m_maps; //map from type index to component data

	}; //end of Archetype

} //namespace vecs
