
#include <iostream>
#include <utility>
#include <ranges>
#include <utility>
#include "VECS.h"


int main() {

	{
		vecs::SlotMap<int> sm;
		auto [i1, v1] = sm.Insert(1);
		auto [i2, v2] = sm.Insert(2);
		auto [i3, v3] = sm.Insert(3);
		assert( sm.Size() == 3 );

		assert( sm[i1].m_value == 1 );
		assert( sm[i2].m_value == 2 );
		assert( sm[i3].m_value == 3 );

		sm.Erase(1);
		sm.Erase(2);

		assert( sm.Size() == 1 );
		assert( sm[i3].m_value == 3 );
	}

    vecs::Registry<vecs::RegistryType::SEQUENTIAL> system;

    vecs::Handle h1 = system.Create(5, 5.5f);
    assert( system.Exists(h1) );
    auto t1 = system.Types(h1);
    auto v1 = system.Get<int>(h1);
    assert( system.Has<int>(h1) );
    system.Erase(h1);
    assert( !system.Exists(h1) );

    //vecs::Handle hx = system.create(5, 6); //compile error
    struct height_t { int i; }; 
    struct weight_t { int i; }; 
    vecs::Handle hx1 = system.Create(5, height_t{6}, weight_t{6}); //works

    auto h2 = system.Create(5, 6.9f, 7.3);
    assert( system.Exists(h2) );
    auto t2 = system.Types(h2);
    auto [v2a, v2b] = system.Get<float, double>(h2);

    system.Put(h2, 50, 69.0f, 73.0);
    assert( system.Get<float>(h2) == 69.0f && system.Get<double>(h2) == 73.0 );
    std::tuple<float,double> tup = system.Get<float, double>(h2);
    std::get<float>(tup) = 101.0f;    
    std::get<double>(tup) = 102.0;    
    system.Put(h2, tup);
    assert( system.Get<float>(h2) == 101.0f && system.Get<double>(h2) == 102.0 );

    auto tup2 = system.Get<int, float, double>(h2);
    int ii = std::get<int>(tup2);
    auto [ivalue, fvalue, dvalue] = system.Get<int, float, double>(h2);

    assert( system.Has<int>(h2) );
    assert( system.Has<float>(h2) );
    assert( system.Has<double>(h2) );

    /*system.Erase<int, float>(h2);
    assert( !system.Has<int>(h2) );
    assert( !system.Has<float>(h2) );
    assert( system.Has<double>(h2) );
    system.Erase<double>(h2); //remove also the last component
    assert( system.Exists(h2)); //check that the entity still exists
	*/

    system.Erase(h2);
    assert( !system.Exists(h2) );
 
    auto hd1 = system.Create(1, 10.0f, 10.0);
    auto hd2 = system.Create(2, 20.0f );
    auto hd3 = system.Create(3, 30.0, "AAA");
    auto hd4 = system.Create(4, 40.0f, 40.0);
    auto hd5 = system.Create(5);
    auto hd6 = system.Create(6, 60.0f, 60.0);

	/*
    for( auto handle : system.view<vecs::Handle>() ) {
        std::cout << "Handle: "<< handle << std::endl;
    }

    for( auto [handle, i, f] : system.view<vecs::Handle, int, float>() ) {
        std::cout << "Handle: "<< handle << " int: " << i << " float: " << f << std::endl;
    }*/

        
    assert( system.Size() > 0 );
    system.Clear();
    assert( system.Size() == 0 );

    return 0;   
}