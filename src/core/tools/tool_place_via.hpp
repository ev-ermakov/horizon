#pragma once
#include "core/tool.hpp"
#include "tool_place_junction.hpp"
#include <forward_list>

namespace horizon {

class ToolPlaceVia : public ToolPlaceJunction {
public:
    ToolPlaceVia(IDocument *c, ToolID tid);
    bool can_begin() override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {
                I::LMB,
                I::CANCEL,
                I::RMB,
                I::EDIT,
        };
    }

protected:
    void create_attached() override;
    void delete_attached() override;
    bool begin_attached() override;
    bool update_attached(const ToolArgs &args) override;
    class Via *via = nullptr;
    class Net *net = nullptr;

    std::forward_list<class Via *> vias_placed;

private:
    const class BoardRules *rules = nullptr;
    void update_tip();
};
} // namespace horizon
