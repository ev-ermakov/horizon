#include "tool_rotate_arbitrary.hpp"
#include "canvas/canvas_gl.hpp"
#include "document/idocument_board.hpp"
#include "board/board.hpp"
#include "document/idocument_package.hpp"
#include "pool/package.hpp"
#include "document/idocument_padstack.hpp"
#include "pool/padstack.hpp"
#include "document/idocument_schematic.hpp"
#include "schematic/schematic.hpp"
#include "document/idocument_symbol.hpp"
#include "pool/symbol.hpp"
#include "imp/imp_interface.hpp"
#include "util/accumulator.hpp"
#include "util/util.hpp"
#include <iostream>
#include "core/tool_id.hpp"

namespace horizon {

ToolRotateArbitrary::ToolRotateArbitrary(IDocument *c, ToolID tid) : ToolBase(c, tid)
{
}

void ToolRotateArbitrary::expand_selection()
{
    std::set<SelectableRef> new_sel;
    std::set<SelectableRef> sel_remove;
    for (const auto &it : selection) {
        switch (it.type) {
        case ObjectType::LINE: {
            Line *line = doc.r->get_line(it.uuid);
            new_sel.emplace(line->from.uuid, ObjectType::JUNCTION);
            new_sel.emplace(line->to.uuid, ObjectType::JUNCTION);
        } break;
        case ObjectType::POLYGON_EDGE: {
            Polygon *poly = doc.r->get_polygon(it.uuid);
            auto vs = poly->get_vertices_for_edge(it.vertex);
            new_sel.emplace(poly->uuid, ObjectType::POLYGON_VERTEX, vs.first);
            new_sel.emplace(poly->uuid, ObjectType::POLYGON_VERTEX, vs.second);
        } break;
        case ObjectType::POLYGON: {
            auto poly = doc.r->get_polygon(it.uuid);
            int i = 0;
            for (const auto &itv : poly->vertices) {
                (void)sizeof itv;
                new_sel.emplace(poly->uuid, ObjectType::POLYGON_VERTEX, i);
                i++;
            }
        } break;

        case ObjectType::ARC: {
            Arc *arc = doc.r->get_arc(it.uuid);
            new_sel.emplace(arc->from.uuid, ObjectType::JUNCTION);
            new_sel.emplace(arc->to.uuid, ObjectType::JUNCTION);
            new_sel.emplace(arc->center.uuid, ObjectType::JUNCTION);
        } break;

        case ObjectType::BOARD_PACKAGE: {
            BoardPackage *pkg = &doc.b->get_board()->packages.at(it.uuid);
            if (pkg->fixed) {
                sel_remove.insert(it);
            }
            else {
                for (const auto &itt : pkg->texts) {
                    new_sel.emplace(itt->uuid, ObjectType::TEXT);
                }
            }
        } break;

        default:;
        }
    }
    selection.insert(new_sel.begin(), new_sel.end());

    if (sel_remove.size() && imp)
        imp->tool_bar_flash("can't move fixed package");

    if (doc.c) {
        for (const auto &it : selection) {
            if (it.type == ObjectType::JUNCTION) {
                const auto ju = doc.r->get_junction(it.uuid);
                if (ju->net) {
                    sel_remove.insert(it);
                }
            }
        }
    }

    for (auto it = selection.begin(); it != selection.end();) {
        if (sel_remove.count(*it))
            it = selection.erase(it);
        else
            ++it;
    }
}

ToolResponse ToolRotateArbitrary::begin(const ToolArgs &args)
{
    std::cout << "tool move\n";
    ref = args.coords;
    origin = args.coords;

    save_placements();
    annotation = imp->get_canvas()->create_annotation();
    annotation->set_visible(true);
    annotation->set_display(LayerDisplay(true, LayerDisplay::Mode::OUTLINE));

    update_tip();
    return ToolResponse();
}

ToolRotateArbitrary::~ToolRotateArbitrary()
{
    if (annotation) {
        imp->get_canvas()->remove_annotation(annotation);
        annotation = nullptr;
    }
}

bool ToolRotateArbitrary::can_begin()
{
    expand_selection();
    return selection.size() > 0;
}

void ToolRotateArbitrary::update_tip()
{
    std::vector<ActionLabelInfo> actions;
    actions.reserve(8);


    if (state == State::ORIGIN) {
        actions.emplace_back(InToolActionID::LMB, "set origin");
        actions.emplace_back(InToolActionID::RMB);
    }
    else if (state == State::REF) {
        actions.emplace_back(InToolActionID::LMB, "set ref");
        actions.emplace_back(InToolActionID::RMB);
    }
    else if (state == State::ROTATE) {
        actions.emplace_back(InToolActionID::LMB, "finish");
        actions.emplace_back(InToolActionID::RMB);
        actions.emplace_back(InToolActionID::TOGGLE_ANGLE_SNAP, "toggle snap");
        actions.emplace_back(InToolActionID::ENTER_DATUM, "enter angle");

        std::string s = "Angle: " + angle_to_string(iangle, false);
        if (snap)
            s += " (snapped)";
        imp->tool_bar_set_tip(s);
    }
    else if (state == State::SCALE) {
        actions.emplace_back(InToolActionID::LMB, "finish");
        actions.emplace_back(InToolActionID::RMB);
        std::stringstream ss;
        ss << "Scale: " << scale;
        imp->tool_bar_set_tip(ss.str());
    }

    imp->tool_bar_set_actions(actions);
}

void ToolRotateArbitrary::save_placements()
{
    for (const auto &it : selection) {
        switch (it.type) {
        case ObjectType::JUNCTION:
            placements[it] = Placement(doc.r->get_junction(it.uuid)->position);
            break;
        case ObjectType::POLYGON_VERTEX:
            placements[it] = Placement(doc.r->get_polygon(it.uuid)->vertices.at(it.vertex).position);
            break;
        case ObjectType::POLYGON_ARC_CENTER:
            placements[it] = Placement(doc.r->get_polygon(it.uuid)->vertices.at(it.vertex).arc_center);
            break;
        case ObjectType::TEXT:
            placements[it] = doc.r->get_text(it.uuid)->placement;
            break;
        case ObjectType::BOARD_PACKAGE:
            placements[it] = doc.b->get_board()->packages.at(it.uuid).placement;
            break;
        case ObjectType::PAD:
            placements[it] = doc.k->get_package().pads.at(it.uuid).placement;
            break;
        default:;
        }
    }
}

static Placement rotate_placement(const Placement &p, const Coordi &origin, int angle)
{
    Placement q = p;
    q.shift -= origin;
    Placement t(origin, angle);
    t.accumulate(q);
    return t;
}

void ToolRotateArbitrary::apply_placements_rotation(int angle)
{
    for (const auto &it : placements) {
        switch (it.first.type) {
        case ObjectType::JUNCTION:
            doc.r->get_junction(it.first.uuid)->position = rotate_placement(it.second, origin, angle).shift;
            break;
        case ObjectType::POLYGON_VERTEX:
            doc.r->get_polygon(it.first.uuid)->vertices.at(it.first.vertex).position =
                    rotate_placement(it.second, origin, angle).shift;
            break;
        case ObjectType::POLYGON_ARC_CENTER:
            doc.r->get_polygon(it.first.uuid)->vertices.at(it.first.vertex).arc_center =
                    rotate_placement(it.second, origin, angle).shift;
            break;
        case ObjectType::TEXT: {
            auto &pl = doc.r->get_text(it.first.uuid)->placement;
            pl = rotate_placement(it.second, origin, angle);
            if (pl.mirror)
                pl.inc_angle(-2 * angle);
        } break;
        case ObjectType::BOARD_PACKAGE:
            doc.b->get_board()->packages.at(it.first.uuid).placement = rotate_placement(it.second, origin, angle);
            break;
        case ObjectType::PAD:
            doc.k->get_package().pads.at(it.first.uuid).placement = rotate_placement(it.second, origin, angle);
            break;
        default:;
        }
    }
}

static Placement scale_placement(const Placement &p, const Coordi &origin, double scale)
{
    Placement q = p;
    q.shift -= origin;
    q.shift.x *= scale;
    q.shift.y *= scale;
    q.shift += origin;
    return q;
}

void ToolRotateArbitrary::apply_placements_scale(double sc)
{
    for (const auto &it : placements) {
        switch (it.first.type) {
        case ObjectType::JUNCTION:
            doc.r->get_junction(it.first.uuid)->position = scale_placement(it.second, origin, sc).shift;
            break;
        case ObjectType::POLYGON_VERTEX:
            doc.r->get_polygon(it.first.uuid)->vertices.at(it.first.vertex).position =
                    scale_placement(it.second, origin, sc).shift;
            break;
        case ObjectType::POLYGON_ARC_CENTER:
            doc.r->get_polygon(it.first.uuid)->vertices.at(it.first.vertex).arc_center =
                    scale_placement(it.second, origin, sc).shift;
            break;
        case ObjectType::TEXT:
            doc.r->get_text(it.first.uuid)->placement = scale_placement(it.second, origin, sc);
            break;
        case ObjectType::BOARD_PACKAGE:
            doc.b->get_board()->packages.at(it.first.uuid).placement = scale_placement(it.second, origin, sc);
            break;
        default:;
        }
    }
}

ToolResponse ToolRotateArbitrary::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (state == State::ORIGIN) {
            origin = args.coords;
        }
        else if (state == State::REF) {
            ref = args.coords;
        }
        else if (state == State::SCALE) {
            const double vr = sqrt((ref - origin).mag_sq());
            const double v = sqrt((args.coords - origin).mag_sq());
            scale = v / vr;
            apply_placements_scale(scale);
        }
        else if (state == State::ROTATE) {
            const auto rref = args.coords;
            annotation->clear();
            annotation->draw_line(origin, rref, ColorP::FROM_LAYER, 2);

            const auto v = rref - origin;
            const auto v0 = ref - origin;
            const double angle0 = atan2(v0.y, v0.x);
            const double angle = atan2(v.y, v.x) - angle0;
            iangle = ((angle / (2 * M_PI)) * 65536);
            iangle += 65536 * 2;
            iangle %= 65536;
            if (snap)
                iangle = round_multiple(iangle, 8192);
            apply_placements_rotation(iangle);
        }
        update_tip();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB:
            if (tool_id == ToolID::ROTATE_ARBITRARY) {
                if (state == State::ORIGIN) {
                    state = State::REF;
                }
                else if (state == State::REF) {
                    imp->set_snap_filter_from_selection(selection);
                    update_tip();
                    state = State::ROTATE;
                }
                else {
                    return ToolResponse::commit();
                }
            }
            else { // scale
                if (state == State::ORIGIN) {
                    state = State::REF;
                    ref = args.coords;
                    update_tip();
                }
                else if (state == State::REF) {
                    state = State::SCALE;
                    imp->set_snap_filter_from_selection(selection);
                    update_tip();
                }
                else {
                    return ToolResponse::commit();
                }
            }
            break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        case InToolActionID::TOGGLE_ANGLE_SNAP:
            snap ^= true;
            break;

        case InToolActionID::ENTER_DATUM: {
            if (auto r = imp->dialogs.ask_datum_angle("Enter angle", 0)) {
                apply_placements_rotation(*r);
                return ToolResponse::commit();
            }
        } break;

        default:;
        }
    }
    return ToolResponse();
}
} // namespace horizon
