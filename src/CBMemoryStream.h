#pragma once
#include "PCH.h"

#include <cstring>
#include <memory>
#include <vector>

// CBMemoryStream — a read-only RE::BSResource::Stream backed by an in-memory,
// immutable byte buffer. Modeled on Engine Relay's ERMemoryStream; kept local so
// the Community Behaviors spine has zero dependency on Engine Relay. When the two Relays
// share transport later, this + the DoCreateStream hook are what get factored out.
// Memory goes through the game heap (TES_HEAP_REDEFINE_NEW inherited from StreamBase).

namespace CB {

    class CBMemoryStream : public RE::BSResource::Stream
    {
    public:
        explicit CBMemoryStream(std::vector<std::uint8_t> a_data)
            : RE::BSResource::Stream(static_cast<std::uint32_t>(a_data.size()))
            , m_data(std::make_shared<const std::vector<std::uint8_t>>(std::move(a_data)))
            , m_pos(0)
        {}

        ~CBMemoryStream() override = default;

        RE::BSResource::ErrorCode DoOpen() override
        {
            m_pos = 0;
            return RE::BSResource::ErrorCode::kNone;
        }

        void DoClose() override
        {
            m_pos = 0;
        }

        // Clone shares the buffer; the new instance starts at position 0.
        void DoClone(RE::BSTSmartPointer<RE::BSResource::Stream>& a_out) const override
        {
            a_out = RE::BSTSmartPointer<RE::BSResource::Stream>{ new CBMemoryStream(m_data) };
        }

        RE::BSResource::ErrorCode DoRead(
            void*          a_buf,
            std::uint64_t  a_toRead,
            std::uint64_t& a_read) const override
        {
            const auto& data  = *m_data;
            const auto  avail = static_cast<std::uint64_t>(data.size()) - m_pos;
            a_read            = (a_toRead < avail) ? a_toRead : avail;
            if (a_read > 0) {
                std::memcpy(a_buf, data.data() + static_cast<std::size_t>(m_pos),
                            static_cast<std::size_t>(a_read));
                m_pos += a_read;
            }
            return RE::BSResource::ErrorCode::kNone;
        }

        RE::BSResource::ErrorCode DoWrite(
            const void*, std::uint64_t, std::uint64_t& a_written) const override
        {
            a_written = 0;
            return RE::BSResource::ErrorCode::kUnsupported;
        }

        RE::BSResource::ErrorCode DoSeek(
            std::uint64_t            a_offset,
            RE::BSResource::SeekMode a_mode,
            std::uint64_t&           a_sought) const override
        {
            const auto size = static_cast<std::uint64_t>(m_data->size());
            std::uint64_t newPos;
            switch (a_mode) {
            case RE::BSResource::SeekMode::kSet: newPos = a_offset;         break;
            case RE::BSResource::SeekMode::kCur: newPos = m_pos + a_offset; break;
            case RE::BSResource::SeekMode::kEnd: newPos = size + a_offset;  break;
            default:
                a_sought = m_pos;
                return RE::BSResource::ErrorCode::kUnsupported;
            }
            m_pos    = (newPos < size) ? newPos : size;
            a_sought = m_pos;
            return RE::BSResource::ErrorCode::kNone;
        }

    private:
        // Clone constructor — shares an existing buffer, fresh cursor at 0.
        explicit CBMemoryStream(std::shared_ptr<const std::vector<std::uint8_t>> a_data)
            : RE::BSResource::Stream(static_cast<std::uint32_t>(a_data->size()))
            , m_data(std::move(a_data))
            , m_pos(0)
        {}

        std::shared_ptr<const std::vector<std::uint8_t>> m_data;
        mutable std::uint64_t                            m_pos;
    };

}  // namespace CB
