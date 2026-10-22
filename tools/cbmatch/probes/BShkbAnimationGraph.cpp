/// @file
/// @brief Toolchain probe: BShkbAnimationGraph::GetGraphVariableFloat, Bethesda engine code (not a schema class).
/// @note Kept as a probe of how Bethesda-side code compiles: VS2015 matches it REGALLOC (98.9%), VS2017 worse, although
///       the Rich header puts Bethesda's bulk on VS2017 (cl 19.16.27054). Havok types sit in CB::havok so their
///       registry pins are shared with the schema tree.
typedef unsigned int       uint32;
typedef unsigned long long uint64;

/// @brief CRC of the pointer VALUE (BSTScatterTable's hash for BSFixedString keys).
void BSCRC32_Pointer(uint32& out, const void* key);

struct BSFixedString
{
    const char* m_data;
};

template <class K, class V> struct BSTScatterTable
{
    struct Entry
    {
        K      key;
        V      value;
        Entry* next;
    };

    uint64 m_pad00;      ///< +0x00
    uint32 m_pad08;      ///< +0x08
    uint32 m_capacity;   ///< +0x0c
    uint32 m_free;       ///< +0x10
    uint32 m_good;       ///< +0x14
    Entry* m_sentinel;   ///< +0x18
    uint64 m_alloc;      ///< +0x20
    Entry* m_entries;    ///< +0x28

    inline const Entry* find(const K& key) const
    {
        uint32 hash;
        BSCRC32_Pointer(hash, key);
        if (!m_entries) return nullptr;
        const Entry* e = &m_entries[hash & (m_capacity - 1)];
        if (e->next) {
            do {
                if (e->key == key) return e;
                e = e->next;
            } while (e != m_sentinel);
        }
        return nullptr;
    }
};

namespace CB { namespace havok
{
    union hkbVariableValue
    {
        int   i;
        float f;
    };

    template <class T> struct hkArray
    {
        T*  m_data;
        int m_size;
        int m_capacityAndFlags;
    };

    struct hkbVariableValueSet
    {
        void*                     m_vtbl;
        uint64                    m_refs;
        hkArray<hkbVariableValue> m_wordVariableValues;   ///< +0x10
    };

    class hkbBehaviorGraph
    {
    public:
        /// @note Name provisional.
        hkbVariableValueSet* getVariableValueSet();
    };
} }

struct ProjectDBData
{
    char                                 m_pad[0x70];
    BSTScatterTable<const char*, int>    m_variableNameToIndex;   ///< +0x70
};

struct BShkbAnimationGraph
{
    char                          m_pad[0x200];
    ProjectDBData*                m_projectDBData;   ///< +0x200
    CB::havok::hkbBehaviorGraph* m_behaviorGraph;   ///< +0x208

    bool GetGraphVariableFloat(const BSFixedString& name, float& out);
};

bool BShkbAnimationGraph::GetGraphVariableFloat(const BSFixedString& name, float& out)
{
    CB::havok::hkbVariableValueSet* values = m_behaviorGraph->getVariableValueSet();
    if (!values) return false;

    const auto* e = m_projectDBData->m_variableNameToIndex.find(name.m_data);
    if (!e) return false;

    const int index = e->value;
    if (index < 0 || index >= values->m_wordVariableValues.m_size) return false;

    CB::havok::hkbVariableValue v = values->m_wordVariableValues.m_data[index];
    out                            = v.f;
    return true;
}
