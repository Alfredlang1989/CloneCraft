#include <entt/entt.hpp>

#include <cassert>

namespace {

struct Probe final {
    int value{};
};

} // namespace

int main() {
    static_assert(ENTT_VERSION_MAJOR == 3);
    static_assert(ENTT_VERSION_MINOR == 16);
    static_assert(ENTT_VERSION_PATCH == 0);

    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<Probe>(entity, 42);

    assert(registry.valid(entity));
    assert(registry.get<Probe>(entity).value == 42);
    registry.destroy(entity);
    assert(!registry.valid(entity));
    return 0;
}
