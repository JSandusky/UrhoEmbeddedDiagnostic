#pragma once

#include "../Network/DevServer.h"

#include "../Scene/Scene.h"

namespace Urho3D
{

	struct SceneLister : DevServerHandler {
		const String uriBase = "Scenes";
		Vector<WeakPtr<Scene>> scenes_;

		virtual bool Handles(DevServer*, const Vector<String>& uri) override { return false; }
		virtual String EmitHTML(DevServer*, const Vector<String>& uri, const VariantMap& params) override { return String(); }

		virtual void Search(DevServer* server, const StringVector& searchTerms, PODVector<Pair<String, String>>& results) override;
		virtual void WriteRawNavigation(DevServer* server, String& data) override;
	};

	struct SceneContent : DevServerHandler {
		const String uriBase = "Scenes";

		virtual bool Handles(DevServer*, const Vector<String>& uri) override;
		virtual String EmitHTML(DevServer*, const Vector<String>& uri, const VariantMap& params) override;

		virtual bool HandlesPost(DevServer*, const Vector<String>& uri) override;
		virtual void DoPost(DevServer*, const Vector<String>& uri, const String& data) override;

		void Print(String& html, const Node* node, String sceneURL, int depth = 0);
	};
}
