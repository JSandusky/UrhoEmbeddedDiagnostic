#include "DevInspector.h"

#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Component.h"

namespace Urho3D
{

	String VarTypeToHTML(VariantType t)
	{
		switch (t)
		{
		case VAR_NONE:
			return "None";
		case VAR_STRING:
			return "String";
		case VAR_FLOAT:
			return "Float";
		case VAR_DOUBLE:
			return "Double";
		case VAR_INT:
			return "Int";
		case VAR_INT64:
			return "Int64";
		case VAR_BOOL:
			return "Bool";
		case VAR_COLOR:
			return "Color";
		case VAR_BUFFER:
			return "Buffer";
		case VAR_MATRIX3:
			return "Matrix3";
		case VAR_MATRIX3X4:
			return "Matrix3x4";
		case VAR_MATRIX4:
			return "Matrix4";
		case VAR_INTRECT:
			return "IntRect";
		case VAR_INTVECTOR2:
			return "Vector2";
		case VAR_VECTOR2:
			return "Vector2";
		case VAR_VECTOR3:
			return "Vector3";
		case VAR_VECTOR4:
			return "Vector4";
		case VAR_QUATERNION:
			return "Quaternion";
		case VAR_VARIANTMAP:
			return "VariantMap";
		case VAR_VARIANTVECTOR:
			return "VariantVector";
		case VAR_STRINGVECTOR:
			return "StringVector";
		case VAR_RESOURCEREF:
			return "ResourceRef";
		case VAR_RESOURCEREFLIST:
			return "ResourceRef List";
		}
		return "Unknown";
	}

	String VarToString(Variant var, Context* context)
	{
		switch (var.GetType())
		{
		case VAR_RESOURCEREF: {
			ResourceRef ref = var.GetResourceRef();			
			return context->GetTypeName(ref.type_) + ";" + ref.name_;
		} break;
		case VAR_RESOURCEREFLIST: {
			ResourceRefList list = var.GetResourceRefList();
			String ret = context->GetTypeName(list.type_);
			for (unsigned i = 0; i < list.names_.Size(); ++i)
			{
				ret += ";";
				ret += list.names_[i];
			}
			return ret;
		} break;
		case VAR_STRINGVECTOR: {
			String ret;
			StringVector list = var.GetStringVector();
			for (unsigned i = 0; i < list.Size(); ++i)
			{
				if (i > 0)
					ret += ";";
				ret += list[i];
			}
			return ret;
		} break;
		case VAR_VARIANTVECTOR: {
			String ret;
			VariantVector list = var.GetVariantVector();
			for (unsigned i = 0; i < list.Size(); ++i)
			{
				if (i > 0)
					ret += ", ";
				ret += "{ ";
				ret += VarTypeToHTML(list[i].GetType());
				ret += " : ";
				ret += VarToString(list[i], context);
				ret += " }";
			}
			return ret;
		} break;
		case VAR_VARIANTMAP: {
			String ret;
			VariantMap list = var.GetVariantMap();
			auto start = list.Begin();
			for (auto item = list.Begin(); item != list.End(); ++item)
			{
				if (item != start)
					ret += ", ";
				ret += "[";
				ret += String(item->first_);
				ret += "]=";
				ret += VarToString(item->second_, context);
			}
			return ret;
		} break;
		default:
			return var.ToString();
		}
	}

	String SerializableToHTML(const Serializable* object, String url)
	{
		String ret;
		auto attrs = object->GetAttributes();
		ret += "<table class=\"table\">";
		ret += "<colgroup><col span=\"1\" style=\"width: 15%\">";
		ret += "<col style=\"width: 85%\"></colgroup>";
		ret += "<tr><th scope =\"col\">Field</th><th scope=\"col\">Value</th></tr>";
		for (const auto& attr : *attrs)
		{
			if (attr.mode_ & AM_NOEDIT)
				continue;

			auto value = object->GetAttribute(attr.name_);
			ret += "<tr>";
			ret += "<td>" + attr.name_ + "<br/><span style=\"font-size: 8pt\" class=\"text-info\">" + VarTypeToHTML(attr.type_) + "</span></td>";
			ret += "<td>";
			auto inputValue = VarToString(value, object->GetContext());
			if (inputValue.Length() < 30)
				ret += "<input style=\"width: 100%\" type=\"text\" value=\"" + inputValue + "\" data-bind=\"" + url + "/" + attr.name_ + "\">";
			else
			{
				ret += "<textarea  style=\"width: 100%\" type=\"text\" data-bind=\"" + url + "/" + attr.name_ + "\">" + inputValue + "</textarea>";
			}
			ret += "</td>";
			ret += "</tr>";
		}
		ret += "</table>";

		return ret;
	}

	void SceneLister::Search(DevServer* server, const StringVector& searchTerms, PODVector<Pair<String, String>>& results)
	{
		auto sceneList = server->scenes_;
		for (auto s : sceneList)
		{
			if (s)
			{
				if (searchTerms.Contains(s->GetName()))
				{
					results.Push(Pair<String, String>(s->GetName(), "/Scenes"));
					continue;
				}
			}
		}
	}

	void SceneLister::WriteRawNavigation(DevServer* server, String& data)
	{
		if (server->scenes_.Size() > 0)
		{
			data += "<li class=\"nav-item dropdown\">";
			data += "<a class=\"nav-link dropdown-toggle\" href=\"#\" id=\"navbarDropdown\" role=\"button\" data-toggle=\"dropdown\" aria-haspopup=\"true\" aria-expanded=\"false\">Scenes</a>";
			data += "<div class=\"dropdown-menu\" aria-labelledby=\"navbarDropdown\">";
			auto sceneList = server->scenes_;
			for (auto scene : sceneList)
			{
				String name = scene->GetName();
				if (name.Trimmed().Empty())
					name = "Unnamed scene";

				String safeName = name;
				safeName.Replace(" ", "_");
				data += "<a class=\"dropdown-item\" href=\"/Scenes/" + safeName + "\">" + name + "</a>";
			}
			data += "</div>";
			data += "</li>";
		}
	}

	bool SceneContent::Handles(DevServer*, const Vector<String>& uri)
	{
		return uri.Size() > 1 && uri[0].Compare(uriBase, false) == 0;
	}

	String SceneContent::EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params)
	{
		auto sceneList = server->scenes_;
		String body;
		for (auto scene : sceneList)
		{
			String name = scene->GetName();
			if (name.Trimmed().Empty())
				name = "Unnamed scene";
			String term = uri[1];
			name.Replace(" ", "_");
			if (name.Compare(term, false) == 0)
			{
				if (uri.Size() > 2)
				{
					unsigned val = FromString<unsigned>(uri[3]);
					if (auto node = scene->GetNode(val))
					{
						body = SerializableToHTML(node, "/" + String::Joined(uri, "/"));
						if (node->GetNumComponents() > 0)
						{
							body += "<h3>Components</h3><ul>";
							for (unsigned i = 0; i < node->GetNumComponents(); ++i)
							{
								auto c = node->GetComponents()[i];
								String compURI = "/Scenes/" + name + "/Component/" + String(c->GetID());
								String compHeader = c->GetTypeName() + " [" + String(c->GetID()) + "]";
								if (c->IsTemporary())
									compHeader += " (temporary)";
								String subBody = SerializableToHTML(c, compURI);
								body += "<button type=\"button\" class=\"close\" aria-label=\"Close\" onclick=\"$.post('" + compURI + "/DELETE').always(function() { location.reload(); });\"><span aria-hidden=\"true\">&times;</span></button>";
								body += server->Accordian("component_" + String(i), compHeader, subBody);
							}
							body += "</ul>";
						}
						else
						{
							body += "<h3>No Components</h3>";
						}
					}
				}
				else
				{
					body += "<ul>";
					Print(body, scene, name + "/Node");
					body += "</ul>";
				}
				break;
			}
		}

		if (body.Empty())
			body = "<div class=\"well\">No contents for scene</div>";
		
		return server->FillTemplate("template_object.html", {
				{ "${TITLE}", "Scene Content" },
				{ "${BODY}", body },
			});
	}

	void SceneContent::Print(String& html, const Node* node, String sceneURL, int depth)
	{
		html += "<li>";
		html += "<a href=\"/Scenes/" + sceneURL + "/" + String(node->GetID()) + "\">" + node->GetName() + " [" + String(node->GetID()) + "]";
		if (node->IsTemporary())
			html += "(temporary)";
		html += "</a>";

		auto children = node->GetChildren();
		if (children.Size() > 0)
		{
			html += "<ul>";
			for (auto child : children)
				Print(html, child, sceneURL, depth + 1);
			html += "</ul>";
		}
		html += "</li>";
	}

	bool SceneContent::HandlesPost(DevServer* server, const Vector<String>& uri)
	{
		return Handles(server, uri);
	}

	void SceneContent::DoPost(DevServer* server, const Vector<String>& uri, const String& data)
	{
		if (uri.Size() < 4)
			return;

		String sceneName = uri[1];
		String nodeName = uri[2];
		String nodeIDStr = uri[3];
		String attrName = uri[4];
		attrName.Replace('_', ' ');
		unsigned nodeID = FromString<unsigned>(nodeIDStr);

		auto sceneList = server->scenes_;
		for (auto scene : sceneList)
		{
			String name = scene->GetName();
			if (name.Trimmed().Empty())
				name = "Unnamed scene";
			String term = uri[1];
			name.Replace(" ", "_");
			if (name.Compare(term, false) == 0)
			{
				if (nodeName == "Node")
				{
					Variant var;
					if (auto n = scene->GetNode(nodeID))
					{
						if (attrName.Compare("delete", false) == 0)
						{
							SharedPtr<Node> sharedNode(n);
							server->AddDeferredCommand([=]() {
								sharedNode->Remove();
							});
						}
						else
						{
							auto attrs = n->GetAttributes();
							for (auto attr : *attrs)
							{
								if (attr.name_.Compare(attrName) == 0)
								{
									var.FromString(attr.type_, data);
									SharedPtr<Node> node(n);
									server->AddDeferredCommand([=]() {
										node->SetAttribute(attrName, var);
									});
									break;
								}
							}
						}
					}
				}
				else if (nodeName == "Component")
				{
					if (auto c = scene->GetComponent(nodeID))
					{
						if (attrName.Compare("delete", false) == 0)
						{
							SharedPtr<Component> comp(c);
							server->AddDeferredCommand([=]() {
								comp->Remove();
							});
						}
						else
						{
							auto attrs = c->GetAttributes();
							for (auto attr : *attrs)
							{
								if (attr.name_.Compare(attrName) == 0)
								{
									Variant var;
									var.FromString(attr.type_, data);
									SharedPtr<Component> comp(c);
									server->AddDeferredCommand([=]() {
										comp->SetAttribute(attrName, var);
									});
									break;
								}
							}
						}
					}
				}
				break;
			}
		}
	}
}