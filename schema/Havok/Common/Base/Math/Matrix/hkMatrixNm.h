/// @file
/// @brief hkMatrixNm: a heap-backed N x M matrix of reals, stored column-major in 16-byte vectors.
/// @note Declarations only: setSize and invert are vanilla's out-of-line code (Common/Base, not a matching target).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkMatrixNm
    {
    public:
        /// @brief Each column holds the rows padded to a multiple of four.
        hkArray<hkVector4> m_elements;   ///< +0x00
        int                m_numRows;    ///< +0x10
        int                m_numCols;    ///< +0x14

        /// @note Leaves the dimensions unset: vanilla's callers construct it empty and call setSize.
        HK_FORCE_INLINE hkMatrixNm() {}

        /// @brief Resizes to `numRows` x `numCols` (contents undefined).
        /// @note Out of line in vanilla (0x140bdb790).
        void setSize(int numRows, int numCols);
        /// @brief Inverts in place.
        /// @return HK_FAILURE when the matrix is singular.
        /// @note Out of line in vanilla (0x140bda900).
        hkResult invert();

        HK_FORCE_INLINE hkReal& operator()(int row, int col)
        {
            return reinterpret_cast<hkReal*>(m_elements.begin())[col * (((m_numRows + 3) >> 2) * 4) + row];
        }
    };
} }
