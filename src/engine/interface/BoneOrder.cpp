#include <interface/BoneOrder.h>

#include <codec/resource/PathKey.h>
#include <interface/reflection/HavokSchema.h>
#include <RymlInclude.h>

#include <map>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace CB::engine::skeleton {

namespace {

    std::string toStr(c4::csubstr s) { return std::string(s.str, s.len); }

    /// @brief The shim, parsed: path identity key -> entry.
    using ParsedShim = std::unordered_map<std::string, BoneOrderEntry>;

    ParsedShim parseShim(const std::string& text)
    {
        ParsedShim out;
        std::string    buf = text;
        c4::yml::Tree  tree = c4::yml::parse_in_place(c4::to_substr(buf));
        const auto     root = tree.crootref();
        if (!root.is_map() || !root.has_child("skeletons")) return out;
        for (const auto unit : root["skeletons"]) {
            BoneOrderEntry& e = out[codec::resource::PathKey(toStr(unit.key()))];
            for (const auto role : unit) {
                std::vector<std::string>& bones = e.roles[toStr(role.key())];
                for (const auto layer : role)       // `- vanilla: [...]`: one single-key map per layer
                    for (const auto named : layer)
                        for (const auto bone : named) bones.push_back(toStr(bone.val()));
            }
        }
        return out;
    }

}  // namespace

const BoneOrderEntry* FindBoneOrder(const schema::SchemaRegistry& reg, std::string_view unitPath)
{
    static std::mutex                                                        lock;
    static std::map<const schema::SchemaRegistry*, std::unique_ptr<ParsedShim>> parsed;
    const ParsedShim*                                                        shim = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto&                       slot = parsed[&reg];
        if (!slot) {
            const std::string* text = reg.CompatShim("boneorder");
            slot = std::make_unique<ParsedShim>(text ? parseShim(*text) : ParsedShim{});
        }
        shim = slot.get();
    }
    const auto it = shim->find(codec::resource::PathKey(unitPath));
    return it == shim->end() ? nullptr : &it->second;
}

std::vector<std::string> BindBoneOrder(const std::vector<std::string>* known, const std::vector<std::string>& present)
{
    if (!known) return present;
    const std::unordered_set<std::string> have(present.begin(), present.end());
    const std::unordered_set<std::string> listed(known->begin(), known->end());
    std::vector<std::string>              out;
    out.reserve(present.size());
    for (const std::string& b : *known)
        if (have.count(b)) out.push_back(b);
    for (const std::string& b : present)
        if (!listed.count(b)) out.push_back(b);
    return out;
}

}  // namespace CB::engine::skeleton
