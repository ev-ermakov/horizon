#include "property_panel.hpp"
#include <iostream>
#include "object_descr.hpp"
#include "property_editor.hpp"
#include "property_panels.hpp"
#include "core/core.hpp"

namespace horizon {

	PropertyPanel::PropertyPanel(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& x, ObjectType ty, Core *c) :
		Gtk::Expander(cobject), type(ty), core(c) {
		x->get_widget("editors_box", editors_box);
		x->get_widget("selector_label", selector_label);
		x->get_widget("button_prev", button_prev);
		x->get_widget("button_next", button_next);

		std::vector<std::pair<ObjectProperty::ID, const ObjectProperty*>> properties_sorted;
		properties_sorted.reserve(object_descriptions.at(type).properties.size());
		for(const auto &it: object_descriptions.at(type).properties) {
			properties_sorted.emplace_back(it.first, &it.second);
		}
		std::sort(properties_sorted.begin(), properties_sorted.end(), [](const auto a, const auto b) {return a.second->order < b.second->order;});

		for(const auto &it: properties_sorted) {
			PropertyEditor *e;
			ObjectProperty::ID property = it.first;
			switch(it.second->type) {
				case ObjectProperty::Type::BOOL :
					e = new PropertyEditorBool(type, property, this);
				break;

				case ObjectProperty::Type::STRING:
					e = new PropertyEditorString(type, property, this);
				break;

				case ObjectProperty::Type::LENGTH: {
					auto pe = new PropertyEditorDim(type, property, this);
					pe->set_range(0, 1e9);
					e = pe;
				}break;

				case ObjectProperty::Type::DIM:
					e = new PropertyEditorDim(type, property, this);
				break;

				case ObjectProperty::Type::ENUM:
					e = new PropertyEditorEnum(type, property, this);
				break;

				case ObjectProperty::Type::STRING_RO:
					e = new PropertyEditorStringRO(type, property, this);
				break;

				case ObjectProperty::Type::NET_CLASS:
					e = new PropertyEditorNetClass(type, property, this);
				break;

				case ObjectProperty::Type::LAYER:
					e = new PropertyEditorLayer(type, property, this);
				break;

				case ObjectProperty::Type::LAYER_COPPER: {
					auto pe = new PropertyEditorLayer(type, property, this);
					pe->copper_only = true;
					e = pe;
				} break;

				case ObjectProperty::Type::ANGLE:
					e = new PropertyEditorAngle(type, property, this);
				break;

				default :
					e = new PropertyEditor(type, property, this);
			}

			e->signal_changed().connect([this, property, e] {
				handle_changed(property, e->get_value());
			});
			e->signal_apply_all().connect([this, property, e] {
				handle_apply_all(property, e->get_value());
			});
			auto em = Gtk::manage(e);
			em->construct();
			editors_box->pack_start(*em, false, false, 0);
			em->show_all();
		}
		reload();
		button_next->signal_clicked().connect(sigc::bind<int>(sigc::mem_fun(this, &PropertyPanel::go), 1));
		button_prev->signal_clicked().connect(sigc::bind<int>(sigc::mem_fun(this, &PropertyPanel::go), -1));
	}

	void PropertyPanel::handle_changed(ObjectProperty::ID property, const PropertyValue &value) {
		parent->set_property(type, objects.at(object_current), property, value);
	}

	void PropertyPanel::handle_apply_all(ObjectProperty::ID property, const PropertyValue &value) {
		if(core->get_property_transaction()) {
			for(const auto &uu: objects) {
				parent->set_property(type, uu, property, value);
			}
			parent->force_commit();
		}
		else {
			core->set_property_begin();
			for(const auto &uu: objects) {
				core->set_property(type, uu, property, value);
			}
			core->set_property_commit();
			parent->signal_update().emit();
		}
	}

	PropertyPanel* PropertyPanel::create(ObjectType t, Core *c, PropertyPanels *parent) {
		PropertyPanel* w;
		Glib::RefPtr<Gtk::Builder> x = Gtk::Builder::create();
		x->add_from_resource("/net/carrotIndustries/horizon/property_panels/property_panel.ui");
		x->get_widget_derived("PropertyPanel", w, t, c);
		w->reference();
		w->parent = parent;

		w->set_use_markup(true);
		w->set_label("<b>"+object_descriptions.at(w->type).name_pl+"</b>");
		return w;
	}

	void PropertyPanel::update_selector() {
		std::string l = std::to_string(object_current+1)+"/"+std::to_string(objects.size());
		selector_label->set_text(l);
	}

	void PropertyPanel::go(int dir) {
		object_current+=dir;
		if(object_current<0) {
			object_current+=objects.size();
		}
		object_current %= objects.size();
		update_selector();
		reload();
	}

	void PropertyPanel::update_objects(const std::set<SelectableRef> &selection) {
		UUID uuid_current;
		if(objects.size())
			uuid_current = objects.at(object_current);
		std::set<UUID> uuids_from_sel;
		for(const auto &it: selection) {
			uuids_from_sel.insert(it.uuid);
		}
		//delete objects not in selection
		objects.erase(std::remove_if(objects.begin(), objects.end(), [&uuids_from_sel](auto &a) {return uuids_from_sel.count(a)==0;}), objects.end());

		//add new objects from selection
		for(const auto &it: selection) {
			if(std::find(objects.begin(), objects.end(), it.uuid) == objects.end()) {
				objects.push_back(it.uuid);
			}
		}

		object_current = 0;
		for(size_t i = 0; i<objects.size(); i++) {
			if(objects[i] == uuid_current) {
				object_current = i;
				break;
			}
		}
		reload();
		update_selector();
	}

	ObjectType PropertyPanel::get_type() {
		return type;
	}

	void PropertyPanel::reload() {
		if(!objects.size())
			return;
		for(const auto ch: editors_box->get_children()) {
			if(auto ed = dynamic_cast<PropertyEditor*>(ch)) {
				auto uu = objects.at(object_current);
				PropertyValue &value = ed->get_value();
				PropertyMeta &meta = ed->get_meta();
				if(value.get_type() != PropertyValue::Type::INVALID) {
					assert(core->get_property(type, uu, ed->property_id, value));
					core->get_property_meta(type, uu, ed->property_id, meta);
					ed->reload();
					ed->set_sensitive(meta.is_settable);
				}
			}
		}
	}
}
