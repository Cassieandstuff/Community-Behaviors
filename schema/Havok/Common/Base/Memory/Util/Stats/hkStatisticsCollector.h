/// @file
/// @brief hkStatisticsCollector: the visitor calcContentStatistics reports an object's memory to.
/// @note The folder is a guess: the exe keeps no __FILE__ string for this class (RTTI `.?AVhkStatisticsCollector@@`).
#pragma once

#include <Common/Base/Container/Array/hkArray.h>
#include <Common/Base/Object/hkReferencedObject.h>

namespace CB { namespace schema
{
    class hkStatisticsCollector : public hkReferencedObject
    {
    public:
        /// @note Only the values vanilla passes are known: addNormalChunk's 1, and 3 for the clip's echo array. Names
        ///       provisional.
        enum MemoryType
        {
            MEMORY_NORMAL  = 1,
            MEMORY_RUNTIME = 3,
        };

        char   m_pad10[0x20 - 0x10];
        /// @brief Also report arrays that don't own their storage.
        /// @note Name provisional.
        hkBool m_reportUnownedArrays;   ///< +0x20

        /// @note The third argument's meaning is unknown; the callers seen pass 0.
        virtual void addReferencedObject(const char* fieldName, const hkReferencedObject* obj, int flags = 0);              ///< slot 3
        /// @note Signature unverified.
        virtual void addObject(const char* fieldName, const hkClass* cls, const void* obj);                               ///< slot 4
        virtual void addChunk(MemoryType type, const char* fieldName, const void* chunkAddress, int usedSize, int allocatedSize);   ///< slot 5
        virtual void pushDir(const char* dirName);                                                                         ///< slot 6
        virtual void popDir();                                                                                             ///< slot 7

        /// @brief addChunk(MEMORY_NORMAL, ...); for a non-empty chunk, an allocatedSize of 0 asks the heap for the real size.
        void addNormalChunk(const char* fieldName, const void* chunkAddress, int usedSize, int allocatedSize = 0);
        /// @brief The heap's allocated size for a block of `size` bytes at `p`; `size` itself when `p` is null.
        /// @note Name provisional.
        static int getAllocatedSize(const void* p, int size);

        /// @brief Reports an array's storage: skipped for an array that doesn't own it, unless m_reportUnownedArrays.
        template <class T> HK_FORCE_INLINE void addArray(const char* fieldName, const hkArray<T>& array, MemoryType type)
        {
            if (array.m_capacityAndFlags >= 0 || m_reportUnownedArrays) {
                const int allocatedSize = getAllocatedSize(array.m_data, array.getCapacity() * int(sizeof(T)));
                addChunk(type, fieldName, array.m_data, array.getSize() * int(sizeof(T)), allocatedSize);
            }
        }
    };

    /// @brief Reflection-driven statistics.
    /// @note Name provisional, and so is the class it is filed under.
    struct hkStatisticsCollectorUtil
    {
        /// @brief Reports `obj`'s reflected members of `cls`.
        /// @note Presumably only those `cls` declares: calcContentStatistics overrides hand the parent class on to
        ///       hkReferencedObject::calcContentStatistics separately.
        static void addDeclaredMembers(const void* obj, const hkClass* cls, hkStatisticsCollector* collector);
    };
} }
