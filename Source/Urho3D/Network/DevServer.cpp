#include "DevServer.h"

#include "../Core/CoreEvents.h"
#include "../Core/Context.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../IO/IOEvents.h"
#include "../Math/Base64.h"

#include "../Resource/XMLFile.h"
#include "../Resource/ResourceCache.h"

#include "../Resource/Image.h"
#include "../Graphics/Texture2D.h"

#include "../Network/DevInspector.h"

#ifdef URHO3D_ANGELSCRIPT
	#include "../AngelScript/Script.h"
#endif

#include <STB/stb_image.h>
#include <STB/stb_image_write.h>

extern unsigned char *stbi_write_png_to_mem(unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);

namespace Urho3D
{

	struct ResourceCacheProvider : public DevServerDataHandler {
		const String uriBase = "ResourceCache";

		virtual bool Handles(DevServer*, const Vector<String>& uri) override {
			return uri.Size() > 0 && uri[0].Compare(uriBase, false) == 0;
		}
		virtual bool EmitData(DevServer* server, const Vector<String>& uri, String& mimeType, VectorBuffer& buffer) override {
			String trimmed = uri[1];
			auto ctx = server->GetContext();
			if (auto cache = ctx->GetSubsystem<ResourceCache>())
			{
				if (auto img = cache->GetExistingResource<Image>(trimmed))
				{
					int len;
					unsigned char* png = stbi_write_png_to_mem(img->GetData(), 0, img->GetWidth(), img->GetHeight(), img->GetComponents(), &len);
					bool success = buffer.Write(png, (unsigned)len) == (unsigned)len;
					free(png);

					mimeType = "image/png";
					return true;
				}
				else if (auto xml = cache->GetExistingResource<XMLFile>(trimmed))
				{
					xml->Save(buffer);
					mimeType = "application/xml";
					return true;
				}
				return false;
			}
			return false;
		}

		virtual void WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI) override { }
	};

	struct ResourceListProvider : public DevServerHandler {
		const String uriBase = "Resources";
		virtual bool Handles(DevServer*, const Vector<String>& uri) override { 
			return uri.Size() > 0 && uri[0].Compare(uriBase, false) == 0; 
		}
		virtual String EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params) {
			String ret;
			ret += "<div class=\"panel-group\" id=\"accordion\">";

			auto ctx = server->GetContext();
			if (auto cache = ctx->GetSubsystem<ResourceCache>())
			{
				auto resourceGroups = cache->GetAllResources();
				for (auto grp : resourceGroups)
				{
					const unsigned resourceCt = grp.second_.resources_.Size();
					unsigned long long average = 0;
					if (resourceCt > 0)
						average = grp.second_.memoryUse_ / Max(resourceCt, 1);
					unsigned long long largest = 0;
					for (auto resIt : grp.second_.resources_)
					{
						if (resIt.second_->GetMemoryUse() > largest)
							largest = resIt.second_->GetMemoryUse();
					}

					const String countString(grp.second_.resources_.Size());
					const String memUseString = GetFileSizeString(average);
					const String memMaxString = GetFileSizeString(largest);
					const String memBudgetString = GetFileSizeString(grp.second_.memoryBudget_);
					const String memTotalString = GetFileSizeString(grp.second_.memoryUse_);
					const String resTypeName = ctx->GetTypeName(grp.first_);

					ret += "<div class=\"panel panel-default\">";
					ret += "<div class=\"panel-heading\"><h4 class=\"panel-title\"><a data-toggle=\"collapse\" href=\"#" + resTypeName + "\">" + resTypeName + " - " + memTotalString + "</a></h4></div>";
					const auto resources = grp.second_.resources_;
					ret += "<div id=\"" + resTypeName + "\" class=\"panel-collapse collapse\">";
					ret += "<ul class=\"list-group\">";
					for (auto res : resources)
					{
						ret += "<li class=\"list-group-item\"><b>" + res.second_->GetName() + "</b> - " + GetFileSizeString(res.second_->GetMemoryUse()) + "</li>";
					}
					ret += "</ul>";
					ret += "</div>";
				}
				ret += "</div>";
			}
			ret += "</div>";
			
			return server->FillTemplate("template_page.html", {
					{ "${TITLE}", "Resource Cache" },
					{ "${BODY}", ret },
				});
		}
		
		virtual void WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI) override {
			titleAndURI.Push(MakePair<String, String>("Resource Cache", "/Resources"));
		}
	};

	DevServer::DevServer(Context* ctx) : 
		Object(ctx),
		netContext_(nullptr)
	{
		static int defaultPort = 80;
		RestartServer(defaultPort);
		handlers_.Push(new SceneLister());
		handlers_.Push(new SceneContent());
		handlers_.Push(new LogHandler());
		handlers_.Push(new ResourceListProvider());
		handlers_.Push(new ResourceCacheProvider());
		handlers_.Push(new SimpleHandler());
		handlers_.Push(new CommandHandler());

		SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(DevServer, OnNewFrame));
		SubscribeToEvent(E_LOGMESSAGE, URHO3D_HANDLER(DevServer, OnLog));

#ifdef URHO3D_ANGELSCRIPT
		RegisterCommand("Dump Script Header", [](Context* ctx) {
			auto log = ctx->GetSubsystem<Log>();
			log->SetBlockEvents(true);
			log->Open("dump.h");
			log->SetTimeStamp(false);
			ctx->GetSubsystem<Script>()->DumpAPI(C_HEADER);
			log->Close();
			log->SetBlockEvents(false);
			log->SetTimeStamp(true);
		});
		RegisterCommand("Dump Script Doxygen", [](Context* ctx) {
			auto log = ctx->GetSubsystem<Log>();
			log->SetBlockEvents(true);
			log->Open("dump.dox");
			log->SetTimeStamp(false);
			ctx->GetSubsystem<Script>()->DumpAPI(DOXYGEN, "../");
			log->Close();
			log->SetBlockEvents(false);
			log->SetTimeStamp(true);
		});
#endif
	}

	DevServer::~DevServer()
	{
		if (netContext_)
		{
			mg_stop(netContext_);
			netContext_ = nullptr;
		}
		while (!handlers_.Empty())
		{
			delete handlers_.Front();
			handlers_.Erase(0);
		}
	}

	void DevServer::RegisterObject(Context* ctx)
	{
		ctx->RegisterFactory<DevServer>();
	}

	void DevServer::RestartServer(int port)
	{
		if (netContext_)
		{
			mg_stop(netContext_);
			netContext_ = nullptr;
		}
			
		memset(&callbacks_, 0, sizeof(mg_callbacks));
		callbacks_.begin_request = BeginRequest;
		callbacks_.http_error = SendErrorPage;

		String docRoot = GetContext()->GetSubsystem<FileSystem>()->GetProgramDir() + "web";
		String portStr = String(port);
		const char *options[] = {
			"document_root",
			docRoot.CString(),
			"listening_ports",
			portStr.CString(),
			"request_timeout_ms",
			"10000",
			0
		};

		netContext_ = mg_start(&callbacks_, this, options);
		if (netContext_ == nullptr)
			URHO3D_LOGERRORF("Failed to start civetweb server at port: %u", port);
		else
		{
			URHO3D_LOGDEBUGF("Started debug server on port: %u", port);
		}
	}

	bool DevServer::IsServerLive() const
	{
		if (netContext_)
		{
			return true;
		}
		return false;
	}

	void DevServer::AddHandler(DevServerHandler* handler)
	{
		handlers_.Push(handler);
	}

	String DevServer::GetWebFile(const String& path) const
	{
		// always grab a fresh file so that html/javascript can be edited on the fly
		const auto webDir = GetContext()->GetSubsystem<FileSystem>()->GetProgramDir() + "web/";
		File file(GetContext(), webDir + path);
		return file.ReadString();
	}

	String DevServer::GenerateNavigation() const
	{
		String ret;
		Vector<Pair<String, String>> titles;
		for (unsigned i = 0; i < handlers_.Size(); ++i)
		{
			auto handler = handlers_[i];
			handler->WriteNavigation(const_cast<DevServer*>(this), titles);
		}
		for (auto link : staticLinks_)
		{
			ret += "<li class=\"nav-item\">";
			ret += "<a class=\"nav-link\" href=\"" + link.second_ + "\">" + link.first_ + "</a>";
			ret += "</li>";
		}
		for (auto title : titles)
		{
			ret += "<li class=\"nav-item\">";
			ret += "<a class=\"nav-link\" href=\"" + title.second_ + "\">" + title.first_ + "</a>";
			ret += "</li>";
		}
		for (auto handler : handlers_)
			handler->WriteRawNavigation(const_cast<DevServer*>(this), ret);
		return ret;
	}

	int DevServer::BeginRequest(struct mg_connection* conn)
	{
		if (mg_context* ctx = mg_get_context(conn))
		{
			DevServer* server = (DevServer*)mg_get_user_data(ctx);
			auto requestInfo = mg_get_request_info(conn);
			String uri(requestInfo->uri);
			String query(requestInfo->query_string);

			VariantMap params;

			Vector<String> uriList = uri.Split('/');
			if (strcmp("GET", requestInfo->request_method) == 0)
			{
				if (uri.Empty() || uri == "/")
				{
					auto data = server->FillTemplate("template_page.html", {
						{ "${TITLE}", "DebugServer is Live" },
						{ "${BODY}", "<h3>Use the navigation above!</h3>" },
					});
					VectorBuffer buff;
					buff.Write(data.CString(), data.Length());
					SendDataResponse(conn, "text/html", buff);
					return 1;
				}
				
				// HTTP GET
				for (auto handler : server->handlers_)
				{
					if (handler->Handles(server, uriList))
					{
						if (DevServerDataHandler* datahandler = dynamic_cast<DevServerDataHandler*>(handler))
						{
							VectorBuffer buffer;
							String mimeType;
							if (datahandler->EmitData(server, uriList, mimeType, buffer))
							{
								buffer.Seek(0);
								SendDataResponse(conn, mimeType, buffer);
								return 1;
							}
						}
						else
						{
							auto htmlData = handler->EmitHTML(server, uriList, params);
							SendHTMLResponse(conn, htmlData);
							return 1;
						}
					}
				}
			}
			else
			{
				// HTTP POST
				char buffer[4096 * 8];
				int bytesRead = mg_read(conn, buffer, 4096 * 8);
				String buffText(buffer, bytesRead);
				for (auto handler : server->handlers_)
				{
					if (handler->HandlesPost(server, uriList))
					{
						handler->DoPost(server, uriList, buffText);
						SendHTMLResponse(conn, "Success");
						return 1;
					}
				}
			}
		}
		return 0;
	}

	int DevServer::SendErrorPage(struct mg_connection* conn, int status)
	{
		if (mg_context* ctx = mg_get_context(conn))
		{
			DevServer* server = (DevServer*)mg_get_user_data(ctx);
		}
		return 1;
	}

	void DevServer::SendHTMLResponse(struct mg_connection* conn, const String& html)
	{
		String rsp = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
		mg_write(conn, rsp.CString(), rsp.Length());
		mg_write(conn, html.CString(), html.Length());
	}

	void DevServer::SendDataResponse(struct mg_connection* conn, const String& mimeType, const VectorBuffer& data)
	{
		mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-type: %s\r\nContent-length: %u\r\n\r\n", mimeType.CString(), data.GetSize());
		mg_write(conn, data.GetData(), data.GetSize());
	}

	void DevServer::AddDeferredCommand(std::function<void()> cmd)
	{
		MutexLock lock(mutex_);
		deferredCommand_.push_back(cmd);
	}

	void DevServer::OnNewFrame(StringHash, VariantMap&)
	{
		MutexLock lock(mutex_);
		for (unsigned i = 0; i < deferredCommand_.size(); ++i)
			deferredCommand_[i]();
		deferredCommand_.clear();
	}

	void DevServer::OnLog(StringHash, VariantMap& data)
	{
		using namespace LogMessage;
		int logLevel = data[P_LEVEL].GetInt();
		String logMsg = data[P_MESSAGE].GetString();

		String logHTML;
		switch (logLevel) {
		case LOG_DEBUG:
			logHTML = "<div class=\"alert alert-primary\">" + logMsg + "</div>";
			break;
		case LOG_WARNING:
			logHTML = "<div class=\"alert alert-warning\">" + logMsg + "</div>";
			break;
		case LOG_ERROR:
			logHTML = "<div class=\"alert alert-danger\">" + logMsg + "</div>";
			break;
		case LOG_INFO:
			logHTML = "<div class=\"alert alert-secondary\">" + logMsg + "</div>";
			break;
		}
		log_.Push(logHTML);
	}

	bool DevServer::LogHandler::Handles(DevServer*, const Vector<String>& uri)
	{
		return uri.Size() > 0 &&  uri[0].Compare("Log", false) == 0;
	}

	String DevServer::LogHandler::EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params)
	{
		String ret;

		auto logCopy = server->log_;
		for (int i = logCopy.Size() - 1; i >= 0; --i)
			ret += logCopy[i];

		return server->FillTemplate( "template_page.html", {
				{ "${TITLE}", "Urho3D Log" },
				{ "${BODY}", ret },
			});
	}

	void DevServer::LogHandler::WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI)
	{
		titleAndURI.Push(Pair<String, String>("Log", "/Log"));
	}

	void DevServer::StandardHeader(const String& title, String& holder) const
	{
		holder += "<html><head><title>" + title + "</title>";
		holder += "<link href=\"/css/bootstrap.min.css\" rel=\"stylesheet\">";
		holder += "</head><body style=\"margin: 10px\">";
		holder += "<h1>" + title + "</h1>";
	}
	void DevServer::StandardFooter(String& holder) const
	{
		holder += "<script src=\"/js/jquery.js\"></script>";
		holder += "<script src=\"/js/bootstrap.min.js\"></script>";
		holder += "</body></html>";
	}
	void DevServer::CollapsibleList(String& holder, const String& header, const StringVector& items, const String& key) const
	{
		holder += "<div class=\"panel-group\" id=\"" + key + "\">";
			holder += "<div class=\"panel panel-default\">";
		
				holder += "<div class=\"panel-heading\"><h4 class=\"panel-title\"><a data-toggle=\"collapse\" href=\"#" + key + "_target\">" + header + "</a></h4></div>";
				holder += "<div id=\"" + key + "_target\" class=\"panel-collapse collapse\">";
				holder += "<ul class=\"list-group\">";
				for (auto item : items)
					holder += "<li class=\"list-group-item\">" + item + "</li>";
				holder += "</ul>";
				holder += "</div>";

		holder += "</div>";
		holder += "</div>";
	}
	String DevServer::Accordian(const String& key, const String& title, const String& content) const
	{
		String holder;
		holder += "<div class=\"panel-group\" id=\"" + key + "\">";
		holder += "<div class=\"panel panel-default\">";
		holder += "<div class=\"panel-heading\"><h4 class=\"panel-title\"><a data-toggle=\"collapse\" href=\"#" + key + "_target\">" + title + "</a></h4></div>";
		holder += "<div id=\"" + key + "_target\" class=\"panel-collapse collapse\">";
		holder += content;
		holder += "</div>";
		holder += "</div>";
		holder += "</div>";
		return holder;
	}
	void DevServer::GroupedList(String& holder, const StringVector& items) const
	{
		holder += "<ul class=\"list-group\">";
		for (auto item : items)
			holder += "<li class=\"list-group-item\">" + item + "</li>";
		holder += "</ul>";
	}
	String DevServer::FillTemplate(const String& templateFile, const HashMap<String, String>& items) const
	{
		String templateData = GetWebFile(templateFile);
		templateData.Replace("${MENU}", GenerateNavigation());
		for (auto entry : items)
			templateData.Replace(entry.first_, entry.second_);
		return templateData;
	}

	void DevServer::Publish(const String& title, const String& content)
	{
		StaticItem item;
		item.text_ = content;
		item.timeStamp_ = Time::GetTimeStamp();

		simpleTexts_.Insert(Pair<String,StaticItem>("/" + title, item));
	}

	void DevServer::Publish(const String& title, const SharedPtr<Image>& content)
	{
		StaticItem item;
		item.image_ = content;
		item.timeStamp_ = Time::GetTimeStamp();

		simpleTexts_.Insert(Pair<String, StaticItem>("/" + title, item));
	}

	void DevServer::AddStaticLink(const String& title, const String& url)
	{
		staticLinks_.Push(Pair<String, String>(title, url));
	}

	void DevServer::RegisterCommand(const String& name, const String& tip, std::function<void(Context*)> cmd)
	{
		CommandItem item;
		item.title_ = name;
		item.tip_ = tip;
		item.url_ = "/Commands/" + name;
		item.url_.Replace(' ', '_');
		item.command_ = cmd;
		commands_.push_back(item);
	}

	bool DevServer::SimpleHandler::Handles(DevServer* server, const Vector<String>& uri)
	{
		if (uri.Size() == 0)
			return false;
		auto found = server->simpleTexts_.Find(uri[0]);
		if (found != server->simpleTexts_.End())
			return true;
		return false;
	}

	String DevServer::SimpleHandler::EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params)
	{
		auto found = server->simpleTexts_.Find(uri[0]);
		String ret;
		//server->StandardHeader(found->first_.Substring(1), ret);
		ret += "<h2>" + found->second_.timeStamp_ + "</h2>\r\n";
		if (auto img = found->second_.image_)
		{
			ret += "<image src=\"data:image/png;base64, ";

			VectorBuffer buffer;
			int len;
			unsigned char* png = stbi_write_png_to_mem(img->GetData(), 0, img->GetWidth(), img->GetHeight(), img->GetComponents(), &len);
			bool success = buffer.Write(png, (unsigned)len) == (unsigned)len;
			free(png);
			
			auto b64Size = Base64::EncodedLength(buffer.GetSize());
			char* data = new char[b64Size];
			if (Base64::Encode((const char*)buffer.GetData(), buffer.GetSize(), data, b64Size))
				ret += String(data, b64Size);
			delete[] data;

			ret += "\" />";
		}
		else
		{
			ret += "<pre>\r\n";
			ret += found->second_.text_;
			ret += "\r\n</pre>";
		}

		return server->FillTemplate("template_page.html", { 
				{ "${TITLE}", found->first_.Substring(1) }, 
				{ "${BODY}", ret } 
			});
	}

	void DevServer::SimpleHandler::WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI)
	{
		
	}

	void DevServer::SimpleHandler::WriteRawNavigation(DevServer* server, String& data) 
	{
		if (server->simpleTexts_.Empty())
			return;

		data += "<li class=\"nav-item dropdown\">";
		data += "<a class=\"nav-link dropdown-toggle\" href=\"#\" id=\"navbarDropdown\" role=\"button\" data-toggle=\"dropdown\" aria-haspopup=\"true\" aria-expanded=\"false\">Diagnostics</a>";
		data += "<div class=\"dropdown-menu\" aria-labelledby=\"navbarDropdown\">";
		for (auto entry : server->simpleTexts_)
			data += "<a class=\"dropdown-item\" href=\"" + entry.first_ + "\">" + entry.first_.Substring(1) + "</a>";
		data += "</div>";
		data += "</li>";
	}

	bool DevServer::CommandHandler::Handles(DevServer*, const Vector<String>& uri)
	{
		return uri.Size() > 0 && uri[0].Compare("Commands", false) == 0;
	}
	bool DevServer::CommandHandler::HandlesPost(DevServer* server, const Vector<String>& uri)
	{
		for (auto com : server->commands_)
		{
			// already passed, don't worry about checking uri 0
			if (com.url_.Compare(uri[1], false) == 0)
				return true;
		}
		return false;
	}
	String DevServer::CommandHandler::EmitHTML(DevServer* server, const Vector<String>& uri, const VariantMap& params)
	{
		String html;
		for (auto com : server->commands_)
		{
			html += "<button type=\"button\" class=\"btn btn-info\" onclick=\"$.post('" + com.url_ + "');\" style=\"margin: 10px\">" + com.title_ + "</button>";
			if (!com.tip_.Empty())
				html += com.tip_;
			html += "<br />";
		}

		return server->FillTemplate("template_page.html", {
			{ "${TITLE}", "Commands" },
			{ "${BODY}", html }
			});
	}
	void DevServer::CommandHandler::DoPost(DevServer* server, const Vector<String>& uri, const String& postData)
	{
		for (auto com : server->commands_)
		{
			if (com.url_.Compare(uri[0], false) == 0)
			{
				server->AddDeferredCommand([=]() {
					com.command_(server->GetContext());
				});
				return;
			}
		}
	}

	void DevServer::CommandHandler::WriteNavigation(DevServer* server, Vector<Pair<String, String>>& titleAndURI)
	{ 
		if (server->commands_.size() > 0)
			titleAndURI.Push(Pair<String, String>("Commands", "/Commands"));
	}

	String ToHTMLSafe(const String& src)
	{
		String r = src;
		r.Replace(" ", "%20");
		return r;
	}
	String FromHTMLSafe(const String& src)
	{
		String r = src;
		r.Replace("%20", " ");
		return r;
	}

	StringVector SliceURI(const String& uri)
	{
		return uri.Split('/');
	}
	String ComposeURI(const StringVector& v)
	{
		String r;
		for (auto s : v)
			r += "/" + s;
		return r;
	}
}