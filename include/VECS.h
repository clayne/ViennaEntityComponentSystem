#ifndef VECS_H
#define VECS_H

#include <limits>
#include <typeinfo>
#include <typeindex>
#include <variant>
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>
#include "VTLL.h"
#include "VECSComponent.h"

//user defined component types and entity types
#include "VECSUser.h" 

using namespace std::chrono_literals;

namespace vecs {

	//-------------------------------------------------------------------------
	//component type list and pointer

	using VeComponentTypeList = vtll::cat< VeComponentTypeListSystem, VeComponentTypeListUser >;
	using VeComponentPtr = vtll::variant_type<vtll::to_ptr<VeComponentTypeList>>;


	//-------------------------------------------------------------------------
	//entity type list

	using VeEntityTypeList = vtll::cat< VeEntityTypeListSystem, VeEntityTypeListUser >;

	//-------------------------------------------------------------------------
	//definition of the types used in VECS

	struct VeHandle;

	template <typename E>
	struct VeEntity_t;

	class VeEntityTableBaseClass;

	template<typename E>
	class VeEntityTable;

	//-------------------------------------------------------------------------
	//entity handle

	/**
	* \brief Handles are IDs of entities. Use them to access entitites.
	* VeHandle are used to ID entities of type E by storing their type as an index
	*/

	struct VeHandle {
		index_t		m_entity_index{};			//the slot of the entity in the entity list
		counter16_t	m_generation_counter{};		//generation counter
		index16_t	m_index{};					//type index
		uint32_t index() const { return static_cast<uint32_t>(m_index.value); };
	};


	/**
	* \brief This struct can hold the data of an entity of type E. This includes its handle
	* and all components.
	*/

	template <typename E>
	struct VeEntity_t {
		using tuple_type = typename vtll::to_tuple<E>::type;
		VeHandle	m_handle;
		tuple_type	m_component_data;

		VeEntity_t(const VeHandle& h, const tuple_type& tup) noexcept : m_handle{ h }, m_component_data{ tup } {};

		template<typename C>
		std::optional<C> component() noexcept {
			if constexpr (vtll::has_type<E,C>::value) {
				return { std::get<vtll::index_of<E,C>::value>(m_component_data) };
			}
			return {};
		};

		template<typename C>
		void update(C&& comp ) noexcept {
			if constexpr (vtll::has_type<E,C>::value) {
				std::get<vtll::index_of<E,C>::value>(m_component_data) = comp;
			}
			return;
		};

		std::string name() {
			return typeid(E).name();
		};
	};

	using VeEntity = vtll::variant_type<vtll::transform<VeEntityTypeList, VeEntity_t>>;
	using VeEntityPtr = vtll::variant_type<vtll::to_ptr<vtll::transform<VeEntityTypeList, VeEntity_t>>>;


	//-------------------------------------------------------------------------
	//component vector - each entity type has them


	/**
	* \brief This class stores all components of entities of type E
	*/

	template<typename E>
	class VeComponentVector : public VeMonostate<VeComponentVector<E>> {
		friend class VeEntityTableBaseClass;

		template<typename E>
		friend class VeEntityTable;

	public:
		using tuple_type	 = typename vtll::to_tuple<E>::type;
		using tuple_type_ref = typename vtll::to_ref_tuple<E>::type;
		using tuple_type_vec = typename vtll::to_tuple<vtll::transform<E,std::pmr::vector>>::type;

	protected:
		struct entry_t {
			VeHandle m_handle;
		};

		static inline std::vector<entry_t>	m_handles;
		static inline tuple_type_vec		m_components;

		static inline std::array<std::unique_ptr<VeComponentVector<E>>, vtll::size<VeComponentTypeList>::value> m_dispatch; //one for each component type

		virtual bool updateC(index_t entidx, size_t compidx, void* ptr, size_t size) {
			return m_dispatch[compidx]->updateC(entidx, compidx, ptr, size);
		}

		virtual bool componentE(index_t entidx, size_t compidx, void* ptr, size_t size) { 
			return m_dispatch[compidx]->componentE(entidx, compidx, ptr, size);
		}

	public:
		VeComponentVector(size_t r = 1 << 10);

		index_t			insert(VeHandle& handle, tuple_type&& tuple);
		tuple_type		values(const index_t index);
		tuple_type_ref	references(const index_t index);
		VeHandle		handle(const index_t index);

		template<typename C>
		C				component(const index_t index);

		template<typename C>
		C&				component_ref(const index_t index);

		bool			update(const index_t index, VeEntity_t<E>&& ent);
		size_t			size() { return m_handles.size(); };

		std::tuple<VeHandle, index_t> erase(const index_t idx);
	};

	template<typename E>
	inline index_t VeComponentVector<E>::insert(VeHandle& handle, tuple_type&& tuple) {
		m_handles.emplace_back(handle);

		vtll::static_for<size_t, 0, vtll::size<E>::value >(
			[&](auto i) {
				std::get<i>(m_components).push_back(std::get<i>(tuple));
			}
		);

		return index_t{ static_cast<typename index_t::type_name>(m_handles.size() - 1) };
	};


	template<typename E>
	inline typename VeComponentVector<E>::tuple_type VeComponentVector<E>::values(const index_t index) {
		assert(index.value < m_handles.size());

		auto f = [&]<typename... Cs>(std::tuple<std::pmr::vector<Cs>...>& tup) {
			return std::make_tuple(std::get<vtll::index_of<E, Cs>::value>(tup)[index.value]...);
		};

		return f(m_components);
	}

	template<typename E>
	inline typename VeComponentVector<E>::tuple_type_ref VeComponentVector<E>::references(const index_t index) {
		assert(index.value < m_handles.size());

		auto f = [&]<typename... Cs>(std::tuple<std::pmr::vector<Cs>...>& tup) {
			return std::tie( std::get<vtll::index_of<E,Cs>::value>(tup)[index.value]... );
		};

		return f(m_components);
	}


	template<typename E>
	inline VeHandle VeComponentVector<E>::handle(const index_t index) {
		assert(index.value < m_handles.size());
		return { m_handles[index.value].m_handle };
	}


	template<typename E>
	template<typename C>
	inline C VeComponentVector<E>::component(const index_t index) {
		assert(index.value < m_handles.size());
		return std::get<vtll::index_of<E, C>::value>(m_components)[index.value];
	}

	template<typename E>
	template<typename C>
	inline C& VeComponentVector<E>::component_ref(const index_t index) {
		assert(index.value < m_handles.size());
		return std::get<vtll::index_of<E, C>::value>(m_components)[index.value];
	}

	template<typename E>
	inline bool VeComponentVector<E>::update(const index_t index, VeEntity_t<E>&& ent) {
		vtll::static_for<size_t, 0, vtll::size<E>::value >(
			[&](auto i) {
				using type = vtll::Nth_type<E, i>;
				std::get<i>(m_components)[index.value] = ent.component<type>().value();
			}
		);
		return true;
	}

	template<typename E>
	inline std::tuple<VeHandle, index_t> VeComponentVector<E>::erase(const index_t index) {
		assert(index.value < m_handles.size());
		if (index.value < m_handles.size() - 1) {
			std::swap(m_handles[index.value], m_handles[m_handles.size() - 1]);
			m_handles.pop_back();
			return std::make_pair(m_handles[index.value].m_handle, index);
		}
		m_handles.pop_back();
		return std::make_tuple(VeHandle{}, index_t{});
	}


	//-------------------------------------------------------------------------
	//comnponent vector derived class

	/**
	* \brief This class is derived from the component vector and is used to update or
	* return components C of entities of type E
	*/

	template<typename E, typename C>
	class VeComponentVectorDerived : public VeComponentVector<E> {
	public:
		VeComponentVectorDerived( size_t res = 1 << 10 ) : VeComponentVector<E>(res) {};

		bool update(const index_t index, C&& comp) {
			if constexpr (vtll::has_type<E, C>::value) {
				std::get< vtll::index_of<E, C>::type::value >(this->m_components)[index.value] = comp;
				return true;
			}
			return false;
		}

		bool updateC(index_t entidx, size_t compidx, void* ptr, size_t size) {
			if constexpr (vtll::has_type<E, C>::value) {
				auto tuple = this->references(entidx);
				memcpy((void*)&std::get<vtll::index_of<E,C>::value>(tuple), ptr, size);
				return true;
			}
			return false;
		};

		bool componentE(index_t entidx, size_t compidx, void* ptr, size_t size) {
			if constexpr (vtll::has_type<E,C>::value) {
				auto tuple = this->references(entidx);
				memcpy(ptr, (void*)&std::get<vtll::index_of<E,C>::value>(tuple), size);
				return true;
			}
			return false;
		};
	};


	template<typename E>
	inline VeComponentVector<E>::VeComponentVector(size_t r) {
		if (!this->init()) return;
		m_handles.reserve(r);

		vtll::static_for<size_t, 0, vtll::size<VeComponentTypeList>::value >(
			[&](auto i) {
				using type = vtll::Nth_type<VeComponentTypeList, i>;
				m_dispatch[i] = std::make_unique<VeComponentVectorDerived<E, type>>(r);
			}
		);
	};



	//-------------------------------------------------------------------------
	//entity table base class

	template<typename... Cs>
	class VeIterator;

	/**
	* \brief This class stores all generalized handles of all entities, and can be used
	* to insert, update, read and delete all entity types.
	*/

	class VeEntityTableBaseClass : public VeMonostate<VeEntityTableBaseClass> {
	protected:

		struct entry_t {
			index_t				m_next_free_or_comp_index{};	//next free slot or index of component table
			VeReadWriteMutex	m_mutex;						//per entity synchronization
			counter16_t			m_generation_counter{ 0 };		//generation counter starts with 0

			entry_t() {};
			entry_t(const entry_t& other) {};
		};

		static inline std::vector<entry_t>	m_entity_table;
		static inline index_t				m_first_free{};

		static inline std::array<std::unique_ptr<VeEntityTableBaseClass>, vtll::size<VeEntityTypeList>::value> m_dispatch;

		virtual std::optional<VeEntity> entityE(const VeHandle& handle) { return {}; };
		virtual bool updateE(const VeHandle& handle, VeEntity&& ent) { return false; };
		virtual bool updateC(const VeHandle& handle, size_t compidx, void* ptr, size_t size) { return false; };
		virtual bool componentE(const VeHandle& handle, size_t compidx, void*ptr, size_t size) { return false; };

	public:
		VeEntityTableBaseClass( size_t r = 1 << 10 );

		//-------------------------------------------------------------------------
		//insert data

		template<typename... Ts>
		VeHandle insert(Ts&&... args) {
			static_assert(vtll::is_same<VeEntityType<Ts...>, Ts...>::value);
			return VeEntityTable<VeEntityType<Ts...>>().insert(std::forward<Ts>(args)...);
		}

		//-------------------------------------------------------------------------
		//get data

		std::optional<VeEntity> entity( const VeHandle &handle) {
			return m_dispatch[handle.index()]->entityE(handle);
		}

		template<typename E>
		std::optional<VeEntity_t<E>> entity(const VeHandle& handle);

		template<typename C>
		requires vtll::has_type<VeComponentTypeList, C>::value
		std::optional<C> component(const VeHandle& handle) {
			C res;
			if (m_dispatch[handle.index()]->componentE(handle, vtll::index_of<VeComponentTypeList, C>::value, (void*)&res, sizeof(C))) {
				return { res };
			}
			return {};
		}

		//-------------------------------------------------------------------------
		//update data

		bool update(const VeHandle& handle, VeEntity&& ent) {
			return m_dispatch[handle.index()]->updateE(handle, std::forward<VeEntity>(ent));
		}

		template<typename E>
		requires vtll::has_type<VeEntityTypeList, E>::value
		bool update(const VeHandle& handle, VeEntity_t<E>&& ent);

		template<typename C>
		requires vtll::has_type<VeComponentTypeList, C>::value
		bool update(const VeHandle& handle, C&& comp) {
			return m_dispatch[handle.index()]->updateC(handle, vtll::index_of<VeComponentTypeList,C>::value, (void*)&comp, sizeof(C));
		}

		//-------------------------------------------------------------------------
		//utility

		template<typename E = void>
		size_t size() { return VeComponentVector<E>().size(); };

		template<>
		size_t size<>();

		template<typename... Cs>
		VeIterator<Cs...> begin();

		template<typename... Cs>
		VeIterator<Cs...> end();

		virtual bool contains(const VeHandle& handle) {
			return m_dispatch[handle.index()]->contains(handle);
		}

		virtual void erase(const VeHandle& handle) {
			m_dispatch[handle.index()]->erase(handle);
		}
	};


	template<>
	size_t VeEntityTableBaseClass::size<void>() {
		size_t sum = 0;
		vtll::static_for<size_t, 0, vtll::size<VeEntityTypeList>::value >(
			[&](auto i) {
				using type = vtll::Nth_type<VeEntityTypeList, i>;
				sum += VeComponentVector<type>().size();
			}
		);
		return sum;
	}



	//-------------------------------------------------------------------------
	//entity table

	/**
	* \brief This class is used as access interface for all entities of type E
	*/

	template<typename E = void>
	class VeEntityTable : public VeEntityTableBaseClass {
	protected:
		std::optional<VeEntity> entityE(const VeHandle& handle);
		bool					updateE(const VeHandle& handle, VeEntity&& ent);
		bool					updateC(const VeHandle& handle, size_t compidx, void* ptr, size_t size);
		bool					componentE(const VeHandle& handle, size_t compidx, void* ptr, size_t size);

	public:
		VeEntityTable(size_t r = 1 << 10);

		//-------------------------------------------------------------------------
		//insert data

		template<typename... Cs>
		requires vtll::is_same<E, Cs...>::value
		VeHandle insert(Cs&&... args);

		//-------------------------------------------------------------------------
		//get data

		std::optional<VeEntity_t<E>> entity(const VeHandle& h);

		template<typename C>
		std::optional<C> component(const VeHandle& handle);

		//-------------------------------------------------------------------------
		//update data

		bool update(const VeHandle& handle, VeEntity_t<E>&& ent);

		template<typename C>
		requires (vtll::has_type<VeComponentTypeList, C>::value)
		bool update(const VeHandle& handle, C&& comp);

		//-------------------------------------------------------------------------
		//utility

		size_t size() { return VeComponentVector<E>().size(); };

		bool contains(const VeHandle& handle);

		void erase(const VeHandle& handle);
	};


	template<typename E>
	inline VeEntityTable<E>::VeEntityTable(size_t r) : VeEntityTableBaseClass(r) {}

	template<typename E>
	inline std::optional<VeEntity> VeEntityTable<E>::entityE(const VeHandle& handle) {
		std::optional<VeEntity_t<E>> ent = entity(handle);
		if (ent.has_value()) return { VeEntity{ *ent } };
		return {};
	}
	
	template<typename E>
	inline bool VeEntityTable<E>::updateE(const VeHandle& handle, VeEntity&& ent) {
		return update( handle, std::get<VeEntity_t<E>>(std::forward<VeEntity>(ent)));
	}

	template<typename E>
	inline bool VeEntityTable<E>::updateC(const VeHandle& handle, size_t compidx, void* ptr, size_t size) {
		if (!contains(handle)) return {};
		return VeComponentVector<E>().updateC(m_entity_table[handle.m_entity_index.value].m_next_free_or_comp_index, compidx, ptr, size);
	}

	template<typename E>
	inline bool VeEntityTable<E>::componentE(const VeHandle& handle, size_t compidx, void* ptr, size_t size) {
		if (!contains(handle)) return {};
		return VeComponentVector<E>().componentE( m_entity_table[handle.m_entity_index.value].m_next_free_or_comp_index, compidx, ptr, size );
	}

	template<typename E>
	template<typename... Cs>
	requires vtll::is_same<E, Cs...>::value
	inline VeHandle VeEntityTable<E>::insert(Cs&&... args) {
		index_t idx{};
		if (!m_first_free.is_null()) {
			idx = m_first_free;
			m_first_free = m_entity_table[m_first_free.value].m_next_free_or_comp_index;
		}
		else {
			idx.value = static_cast<typename index_t::type_name>(m_entity_table.size());	//index of new entity
			m_entity_table.emplace_back();		//start with counter 0
		}

		VeHandle handle{ idx, m_entity_table[idx.value].m_generation_counter, index16_t{ vtll::index_of<VeEntityTypeList, E>::value } };
		index_t compidx = VeComponentVector<E>().insert(handle, std::make_tuple(args...));	//add data as tuple
		m_entity_table[idx.value].m_next_free_or_comp_index = compidx;						//index in component vector 
		return { handle };
	};

	template<typename E>
	inline bool VeEntityTable<E>::contains(const VeHandle& handle) {
		if (handle.m_generation_counter != m_entity_table[handle.m_entity_index.value].m_generation_counter) return false;
		return true;
	}

	template<typename E>
	inline std::optional<VeEntity_t<E>> VeEntityTable<E>::entity(const VeHandle& handle) {
		if (!contains(handle)) return {};
		VeEntity_t<E> res( handle, VeComponentVector<E>().values(m_entity_table[handle.m_entity_index.value].m_next_free_or_comp_index) );
		return { res };
	}

	template<typename E>
	template<typename C>
	inline std::optional<C> VeEntityTable<E>::component(const VeHandle& handle) {
		if constexpr (!vtll::has_type<E,C>::value) { return {}; }
		if (!contains(handle)) return {};
		auto compidx = m_entity_table[handle.m_entity_index.value].m_next_free_or_comp_index;
		return { VeComponentVector<E>().component<C>(compidx) };
	}

	template<typename E>
	inline bool VeEntityTable<E>::update(const VeHandle& handle, VeEntity_t<E>&& ent) {
		if (!contains(handle)) return false;
		VeComponentVector<E>().update(handle.m_entity_index, std::forward<VeEntity_t<E>>(ent));
		return true;
	}

	template<typename E>
	template<typename C>
	requires (vtll::has_type<VeComponentTypeList, C>::value)
	inline bool VeEntityTable<E>::update(const VeHandle& handle, C&& comp) {
		if constexpr (!vtll::has_type<E, C>::value) { return false; }
		if (!contains(handle)) return false;
		VeComponentVector<E>().update<C>(handle.m_entity_index, std::forward<C>(comp));
		return true;
	}

	template<typename E>
	inline void VeEntityTable<E>::erase(const VeHandle& handle) {
		if (!contains(handle)) return;
		auto hidx = handle.m_entity_index.value;

		auto [corr_hndl, corr_index] = VeComponentVector<E>().erase(m_entity_table[hidx].m_next_free_or_comp_index);
		if (!corr_index.is_null()) { m_entity_table[corr_hndl.m_entity_index.value].m_next_free_or_comp_index = corr_index; }

		m_entity_table[hidx].m_generation_counter.value++;															 //>invalidate the entity handle
		if( m_entity_table[hidx].m_generation_counter.is_null() ) { m_entity_table[hidx].m_generation_counter.value = 0; } //wrap to zero
		m_entity_table[hidx].m_next_free_or_comp_index = m_first_free;												 //>put old entry into free list
		m_first_free = handle.m_entity_index;
	}


	//-------------------------------------------------------------------------
	//entity table specialization for void

	/**
	* \brief A specialized class to act as convenient access interface instead of the base class.
	*/

	template<>
	class VeEntityTable<void> : public VeEntityTableBaseClass {
	public:
		VeEntityTable(size_t r = 1 << 10) : VeEntityTableBaseClass(r) {};
	};


	//-------------------------------------------------------------------------
	//iterator

	/**
	* \brief Base class for an iterator that iterates over a VeComponentVector of any type 
	* and that is intested into components Cs
	*/

	template<typename... Cs>
	class VeIterator {
	protected:
		using entity_types = typename vtll::filter_have_all_types< VeEntityTypeList, vtll::type_list<Cs...> >::type;

		std::array<std::unique_ptr<VeIterator<Cs...>>, vtll::size<entity_types>::value> m_dispatch;
		index_t m_current_iterator{ 0 };
		index_t m_current_index{ 0 };
		bool	m_is_end{ false };

	public:
		using value_type = std::tuple<VeHandle, Cs&...>;

		VeIterator() {};
		VeIterator( bool is_end );
		VeIterator(const VeIterator& v) : VeIterator(v.m_is_end) {
			if (m_is_end) return;
			m_current_iterator = v.m_current_iterator;
			for (int i = 0; i < m_dispatch.size(); ++i) { m_dispatch[i]->m_current_index = v.m_dispatch[i]->m_current_index; }
		};

		VeIterator<Cs...>& operator=(const VeIterator& v) {
			m_current_iterator = v.m_current_iterator;
			for (int i = 0; i < m_dispatch.size(); ++i) { m_dispatch[i]->m_current_index = v.m_dispatch[i]->m_current_index; }
			return *this;
		}

		virtual value_type operator*() { 
			return *(*m_dispatch[m_current_iterator.value]);
		};

		virtual VeIterator<Cs...>& operator++() {
			(*m_dispatch[m_current_iterator.value])++;
			if (m_dispatch[m_current_iterator.value]->is_vector_end() && m_current_iterator.value < m_dispatch.size() - 1) {
				++m_current_iterator.value;
			}
			return *this;
		};

		virtual VeIterator<Cs...>& operator++(int) { return operator++(); return *this; };

		void operator+=(size_t N) {
			size_t left = N;
			while (left > 0) {
				int num = std::max(m_dispatch[m_current_iterator.value]->size() 
									- m_dispatch[m_current_iterator.value]->m_current_index.value, 0);
				left -= num;
				m_dispatch[m_current_iterator.value]->m_current_index.value += num;
				if (m_dispatch[m_current_iterator.value]->is_vector_end()) {
					if (m_current_iterator.value < m_dispatch.size() - 1) { ++m_current_iterator.value; }
					else return;
				}
			}
		}

		VeIterator<Cs...>& operator+(size_t N) {
			VeIterator<Cs...> temp{ *this };
			temp += N;
			return temp;
		}

		bool operator!=(const VeIterator<Cs...>& v) {
			return !( *this == v );
		}

		bool operator==(const VeIterator<Cs...>& v) {
			return	v.m_current_iterator == m_current_iterator &&
					v.m_dispatch[m_current_iterator.value]->m_current_index == m_dispatch[m_current_iterator.value]->m_current_index;
		}

		virtual bool is_vector_end() { return m_dispatch[m_current_iterator.value]->is_vector_end(); }

		virtual size_t size() {
			size_t sum = 0;
			for (int i = 0; i < m_dispatch.size(); ++i) { sum += m_dispatch[i]->size(); };
			return sum;
		}
	};


	/**
	* \brief Iterator that iterates over a VeComponentVector of type E
	* and that is intested into components Cs
	*/

	template<typename E, typename... Cs>
	class VeIteratorDerived : public VeIterator<Cs...> {
	protected:

	public:
		VeIteratorDerived(bool is_end = false) { //empty constructor does not create new children
			if (is_end) this->m_current_index.value = static_cast<decltype(this->m_current_index.value)>(VeComponentVector<E>().size());
		};

		typename VeIterator<Cs...>::value_type operator*() {
			return std::make_tuple(VeComponentVector<E>().handle(this->m_current_index), std::ref(VeComponentVector<E>().component_ref<Cs>(this->m_current_index))...);
		};

		VeIterator<Cs...>& operator++() { ++this->m_current_index.value; return *this; };
		
		VeIterator<Cs...>& operator++(int) { ++this->m_current_index.value; return *this; };
		
		bool is_vector_end() { return this->m_current_index.value >= VeComponentVector<E>().size(); };

		virtual size_t size() { return VeComponentVector<E>().size(); }

	};


	template<typename... Cs>
	VeIterator<Cs...>::VeIterator(bool is_end) : m_is_end{ is_end } {
		if (is_end) {
			m_current_iterator.value = static_cast<decltype(m_current_iterator.value)>(m_dispatch.size() - 1);
		}

		vtll::static_for<size_t, 0, vtll::size<entity_types>::value >(
			[&](auto i) {
				using type = vtll::Nth_type<entity_types, i>;
				m_dispatch[i] = std::make_unique<VeIteratorDerived<type, Cs...>>(is_end);
			}
		);
	};

	template<typename... Cs>
	using Functor = void(VeIterator<Cs...>&);

	template<typename... Cs>
	void for_each(VeIterator<Cs...>& b, VeIterator<Cs...>& e, std::function<Functor<Cs...>> f) {
		for (; b != e; b++) {
			f(b);
		}
		return; 
	}

	template<typename... Cs>
	void for_each(std::function<Functor<Cs...>> f) {
		auto b = VeEntityTable().begin<Cs...>();
		auto e = VeEntityTable().end<Cs...>();

		return for_each(b, e, f);
	}



	//-------------------------------------------------------------------------
	//entity table base class implementations needing the entity table derived classes

	inline VeEntityTableBaseClass::VeEntityTableBaseClass(size_t r) {
		if (!this->init()) return;
		m_entity_table.reserve(r);

		vtll::static_for<size_t, 0, vtll::size<VeEntityTypeList>::value >(
			[&](auto i) {
				using type = vtll::Nth_type<VeEntityTypeList, i>;
				m_dispatch[i] = std::make_unique<VeEntityTable<type>>();
			}
		);
	}

	template<typename E>
	inline std::optional<VeEntity_t<E>> VeEntityTableBaseClass::entity(const VeHandle& handle) {
		return VeEntityTable<E>().entity(handle);
	}

	template<typename E>
	requires vtll::has_type<VeEntityTypeList, E>::value
	bool VeEntityTableBaseClass::update(const VeHandle& handle, VeEntity_t<E>&& ent) {
		return VeEntityTable<E>().update({ handle }, std::forward<VeEntity_t<E>>(ent));
	}

	template<typename... Cs>
	VeIterator<Cs...> VeEntityTableBaseClass::begin() {
		return VeIterator<Cs...>(false);
	}

	template<typename... Cs>
	VeIterator<Cs...> VeEntityTableBaseClass::end() {
		return VeIterator<Cs...>(true);
	}




	//-------------------------------------------------------------------------
	//system

	/**
	* \brief Systems can access all components in sequence
	*/

	template<typename T, typename... Cs>
	class VeSystem : public VeMonostate<VeSystem<T>> {
	protected:
	public:
		VeSystem() = default;
	};




}

#endif
