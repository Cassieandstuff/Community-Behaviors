#pragma once
#include "havok/classes/IHavokObject.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// className -> factory map, the C++ stand-in for HKX2E's Type.GetType("HKX2."+name).
// The deserializer constructs each object by class name (from a virtual fixup).

namespace havok {

// Registry INFRASTRUCTURE only (className -> factory) — this lives in havok-framing, the
// leaf, so havok-io/havok-model can use the packfile framing without depending on
// havok-core. The CONTENT (the typed-class factories) is registered by havok-core at
// static-init (ClassRead.cpp calls RegisterAllHavokClasses eagerly). A consumer that only
// uses the generic SchemaObject path (havok-io sets PackFileDeserializer::ObjectFactory)
// never touches the typed registry, so its Create() simply returns null for unregistered
// names — no havok-core link required.
class HavokRegistry {
public:
    using Factory = std::function<std::shared_ptr<IHavokObject>()>;

    static void Register(const std::string& name, Factory f) { table()[name] = std::move(f); }

    static std::shared_ptr<IHavokObject> Create(const std::string& name) {
        auto it = table().find(name);
        return it == table().end() ? nullptr : it->second();
    }

private:
    static std::unordered_map<std::string, Factory>& table() {
        static std::unordered_map<std::string, Factory> t;
        return t;
    }
};

} // namespace havok
