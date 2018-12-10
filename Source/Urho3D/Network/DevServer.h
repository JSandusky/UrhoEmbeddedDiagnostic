#pragma once

#include "../Core/Object.h"
#include "../IO/VectorBuffer.h"
#include "../Resource/Image.h"
#include "../Scene/Scene.h"
#include "../Core/Mutex.h"

#include <Civetweb/civetweb.h>

#include <algorithm>

namespace Urho3D
{
	class DevServer;

	/// Interface for overriding URI handling.
	struct URHO3D_API DevServerHandler {
		virtual bool Handles(DevServer*, const Vector<String>& uri) = 0;
		virtual bool HandlesPost(DevServer*, const Vector<String>& uri) { return false; }
		virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) = 0;
		virtual void DoPost(DevServer*, const Vector<String>& uri, const String& postData) { }

		virtual void Search(DevServer* server, const StringVector& searchTerms, PODVector<Pair<String,String>>& results) { }
		virtual void WriteNavigation(DevServer* server, Vector<Pair<String,String>>& titleAndURI) { }
		virtual void WriteRawNavigation(DevServer* server, String& data) { }
	};

	/// A data handler is a special base, one that serves binary data (downloads)
	struct URHO3D_API DevServerDataHandler : DevServerHandler {
		virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) { return String(); }
		virtual bool EmitData(DevServer* server, const Vector<String>& uri, String& mimeType, VectorBuffer& buffer) = 0;
	};

	/// An embedded HTTP server for retrieving diagnostic information at runtime.
	/// Built-in features:
	///		- trivial publishing of text/images to urls
	///		- localhost/Log, displays the Urho3D log
	///		- localhost/Resources, displays the resource cache contents
	///		- localhost/ShaderCache, displays the loaded shader combinations
	///		- localhost/ResourceCache/__resource_name__, retrieves data for a resource (if possible)
	///		- localhost/Search, performs basic search functionality
	///		- localhost/Scenes, displays registered scenes for viewing
	class URHO3D_API DevServer : public Object
	{
		URHO3D_OBJECT(DevServer, Object);
		friend class SceneLister;
		friend class SceneContent;
	public:
		DevServer(Context*);
		virtual ~DevServer();
		static void RegisterObject(Context*);

		/// Restarts the server for the target port.
		void RestartServer(int port);
		/// Returns true if the server is presumably actively running.
		bool IsServerLive() const;
		/// Add a response handler implementation.
		void AddHandler(DevServerHandler* handler);

		/// Use to read a file from the /web directory - for mail-merge/templates.
		String GetWebFile(const String& path) const;

	// HTML emission utilities
		/// Standard bootstrap headers.
		void StandardHeader(const String& title, String& holder) const;
		/// Closes the body and html.
		void StandardFooter(String& holder) const;
		/// Emits a bootstrap collapsing list.
		void CollapsibleList(String& holder, const String& header, const StringVector& items, const String& key) const;
		String Accordian(const String& key, const String& header, const String& content) const;
		/// Emits a bootstrap grouped list.
		void GroupedList(String& holder, const StringVector& items) const;
		String FillTemplate(const String& templateFile, const HashMap<String, String>& items) const;
		 
	// Utilities
		/// Creates a simple-page handler for a time-stamped preformated set of text.
		void Publish(const String& title, const String& content);
		/// Creates a simple-page handler for a time-stamped image.
		void Publish(const String& title, const SharedPtr<Image>& content);
		/// Adds a link to the standard generated menu, use for adding custom links (ie. link to PBR theory or something).
		void AddStaticLink(const String& title, const String& url);
		void RegisterCommand(const String& name, std::function<void(Context*)> cmd) { RegisterCommand(name, String(), cmd); }
		void RegisterCommand(const String& name, const String& tip, std::function<void(Context*)> cmd);

		void AddScene(SharedPtr<Scene> scene) { scenes_.Push(scene); }
		void RemoveScene(SharedPtr<Scene> scene) { scenes_.Remove(scene); }

	private:
		void AddDeferredCommand(std::function<void()> cmd);

		/// Emits navigation links.
		String GenerateNavigation() const;

		/// When a new frame is called we'll execute any queued commands.
		void OnNewFrame(StringHash, VariantMap&);
		/// Handler for Urho3D log event.
		void OnLog(StringHash, VariantMap&);

		static int BeginRequest(struct mg_connection*);
		static int SendErrorPage(struct mg_connection*, int status);

		/// Used to send a regular HTML 200 response.
		static void SendHTMLResponse(struct mg_connection*, const String& html);
		/// Used to send a file 200 response.
		static void SendDataResponse(struct mg_connection*, const String& mimeType, const VectorBuffer& data);

		mg_callbacks callbacks_;
		mg_context* netContext_;

		/// Holds either text or an image.
		struct StaticItem {
			SharedPtr<Image> image_;
			String timeStamp_;
			String text_;
		};
		/// Collection of simple pages for text/image dumps.
		HashMap<String, StaticItem> simpleTexts_;
		/// History of log messages (printed in reverse).
		Vector<String> log_;
		/// All available handlers currently registered, processed in sequence.
		Vector<DevServerHandler*> handlers_;
		/// Error-handler function.
		std::function<String(int status)> errorHandler_;
		/// Extension links for the generated menu (to link to custom content, help URLs, etc).
		Vector<Pair<String, String>> staticLinks_;
		Vector<SharedPtr<Scene>> scenes_;

		struct CommandItem {
			String title_;
			String tip_;
			String url_;
			std::function<void(Context*)> command_;
		};
		std::vector<CommandItem> commands_;

		Mutex mutex_;
		std::vector<std::function<void()>> deferredCommand_;

		/// Internal handler for displaying the /Log page
		struct LogHandler : DevServerHandler {
			virtual bool Handles(DevServer*, const Vector<String>& uri) override;
			virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) override;
			virtual void WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI) override;
		};

		/// Internal handler for displaying the simple text/image items.
		struct SimpleHandler : DevServerHandler {
			virtual bool Handles(DevServer*, const Vector<String>& uri) override;
			virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) override;
			virtual void WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI) override;
			virtual void WriteRawNavigation(DevServer* server, String& data) override;
		};

		struct CommandHandler : DevServerHandler {
			virtual bool Handles(DevServer*, const Vector<String>& uri) override;
			virtual bool HandlesPost(DevServer*, const Vector<String>& uri) override;
			virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) override;
			virtual void DoPost(DevServer*, const Vector<String>& uri, const String& postData) override;
			virtual void WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI) override;
		};
	};

	URHO3D_API String ToHTMLSafe(const String& src);
	URHO3D_API String FromHTMLSafe(const String& src);
	URHO3D_API StringVector SliceURI(const String& uri);
	URHO3D_API String ComposeURI(const StringVector&);
}