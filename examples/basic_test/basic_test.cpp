
#include <iostream>
#include <utility>
#include "glm.hpp"
#include "gtc/quaternion.hpp"
#include "VECS.h"
#include "basic_test.h"

using namespace vecs;

int main() {
    
    std::cout << sizeof(VecsHandle) << " " << sizeof(index_t) << std::endl;
    std::cout << vtll::size<VecsEntityTypeList>::value << std::endl;

    auto h1 = VecsRegistry().insert(VeComponentPosition{ glm::vec3{9.0f, 2.0f, 3.0f} }, VeComponentOrientation{}, VeComponentTransform{});
    std::cout << typeid(VeEntityNode).hash_code() << " " << typeid(VeEntityNode).name() << std::endl;

    auto data1b = h1.entity<VeEntityNode>().value();
    auto comp1_1 = data1b.component<VeComponentPosition>();

    auto comp1_2 = h1.component<VeComponentPosition>();
    auto comp1_3 = h1.component<VeComponentMaterial>();

    h1.update(VeComponentPosition{ glm::vec3{-9.0f, -2.0f, -3.0f} });
    auto comp1_4 = h1.component<VeComponentPosition>();

    data1b.local_update(VeComponentPosition{ glm::vec3{-999.0f, -2.0f, -3.0f} });
    data1b.update();
    auto comp1_5 = h1.component<VeComponentPosition>();

    auto h2 = VecsRegistry().insert(VeComponentMaterial{ 99 }, VeComponentGeometry{});
    std::cout << typeid(VeEntityDraw).hash_code() << " " << typeid(VeEntityDraw).name() << std::endl;

    auto data2b = h2.entity<VeEntityDraw>().value();
    auto comp2_1 = data2b.component<VeComponentMaterial>();
    auto comp2_2 = h2.component<VeComponentMaterial>();

    using entity_types = typename vtll::filter_have_all_types< VecsEntityTypeList, vtll::type_list<VeComponentPosition> >::type;
    std::cout << typeid(entity_types).name() << std::endl;

    for_each<VeComponentPosition, VeComponentOrientation>( [&]( auto& iter) {

        auto [handle, pos, orient] = *iter;

        pos = VeComponentPosition{ glm::vec3{12345.0f, -299.0f, -334.0f} };

        std::cout << "entity\n";
    });

    auto comp1_6 = h1.component<VeComponentPosition>().value();

    h1.erase();
    auto data1c = h1.entity<VeEntityNode>();
    h2.erase();

    return 0;
}

