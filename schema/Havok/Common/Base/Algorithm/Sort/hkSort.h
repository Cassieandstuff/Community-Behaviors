/// @file
/// @brief hkAlgorithm: Havok's in-place sorts.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    namespace hkAlgorithm
    {
        template <typename T> struct less
        {
            HK_FORCE_INLINE bool operator()(const T& a, const T& b) { return a < b; }
        };

        template <typename T> HK_FORCE_INLINE void swap(T& x, T& y)
        {
            const T t = x;
            x         = y;
            y         = t;
        }

        /// @brief Sorts pArr[d..h] (inclusive).
        /// @note Out of line, an instance per T and comparator in the unit that sorts (hkbKeyframeBonesModifierUtils's
        ///       for int, 0x140b0edb0): recurses on the lower part, loops on the upper.
        template <typename T, typename L> void quickSortRecursive(T* pArr, int d, int h, L cmpLess)
        {
            int i, j;
            T   str;

        begin:
            i   = d;
            j   = h;
            str = pArr[(d + h) >> 1];

            do {
                while (cmpLess(pArr[i], str)) i++;
                while (cmpLess(str, pArr[j])) j--;

                if (j < i) break;
                if (j != i) hkAlgorithm::swap(pArr[j], pArr[i]);
                j--;
                i++;
            } while (i <= j);

            if (d < j) quickSortRecursive(pArr, d, j, cmpLess);
            if (i < h) {
                d = i;
                goto begin;
            }
        }

        template <typename T, typename L> HK_FORCE_INLINE void quickSort(T* pArr, int numElem, L cmpLess)
        {
            if (numElem > 1) quickSortRecursive(pArr, 0, numElem - 1, cmpLess);
        }

        template <typename T> HK_FORCE_INLINE void quickSort(T* pArr, int numElem) { quickSort(pArr, numElem, less<T>()); }
    }
} }
