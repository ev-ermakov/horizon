#pragma once
#include "core/tool.hpp"

namespace horizon {
class ToolPlacePicture : public ToolBase {
public:
    ToolPlacePicture(IDocument *c, ToolID tid);
    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool can_begin() override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {
                I::LMB, I::CANCEL, I::RMB, I::ROTATE, I::ENTER_SIZE,
        };
    }

private:
    class Picture *temp = nullptr;
};
} // namespace horizon
