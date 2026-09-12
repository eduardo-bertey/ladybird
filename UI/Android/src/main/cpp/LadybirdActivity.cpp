/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ALooperEventLoopImplementation.h"
#include "JNIHelpers.h"
#include "WebContentService.h"
#include <LibImageDecoderClient/Client.h>
#include <LibRequests/RequestClient.h>
#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/HashMap.h>
#include <AK/LexicalPath.h>
#include <AK/OwnPtr.h>
#include <LibCore/DirIterator.h>
#include <LibCore/Directory.h>
#include <LibCore/EventLoop.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibFileSystem/FileSystem.h>
#include <LibWebView/Application.h>
#include <LibWebView/Utilities.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WorkerProcessManager.h>
#include <jni.h>

JavaVM* global_vm;
static OwnPtr<WebView::Application> s_application;
static OwnPtr<Core::EventLoop> s_main_event_loop;
static jobject s_java_instance;
static jmethodID s_schedule_event_loop_method;

class Application : public WebView::Application {
    WEB_VIEW_APPLICATION(Application);

public:
    explicit Application();

private:
    virtual bool should_coordinate_browser_process() const override { return false; }
    virtual ErrorOr<void> launch_request_server() override;
    virtual ErrorOr<void> launch_image_decoder_server() override;
};

Application::Application() = default;

static jmethodID s_bind_request_server_service_method = nullptr;
static jmethodID s_bind_image_decoder_service_method = nullptr;

static void bind_ui_request_server_service(int ipc_socket)
{
    Ladybird::JavaEnvironment env(global_vm);
    env.get()->CallVoidMethod(s_java_instance, s_bind_request_server_service_method, ipc_socket);
}

static void bind_ui_image_decoder_service(int ipc_socket)
{
    Ladybird::JavaEnvironment env(global_vm);
    env.get()->CallVoidMethod(s_java_instance, s_bind_image_decoder_service_method, ipc_socket);
}

// En Android no hay binarios sueltos: los servicios corren en procesos
// Android (:RequestServer, :ImageDecoder) y se bindean por Binder.
// Se replica launch_request_server/launch_image_decoder_server con
// bind_service en vez de spawn (sin handshake InitTransport, igual que
// hace el proceso WebContent).
ErrorOr<void> Application::launch_request_server()
{
    m_request_server_client = TRY(bind_service<Requests::RequestClient>(&bind_ui_request_server_service));

    auto const& browsing_data_settings = Application::settings().browsing_data_settings();
    m_request_server_client->async_set_disk_cache_settings(browsing_data_settings.disk_cache_settings);

    Application::settings().dns_settings().visit(
        [](WebView::SystemDNS) {},
        [&](WebView::DNSOverTLS const& dns_over_tls) {
            m_request_server_client->async_set_dns_server(dns_over_tls.server_address, dns_over_tls.port, true, dns_over_tls.validate_dnssec_locally);
        },
        [&](WebView::DNSOverUDP const& dns_over_udp) {
            m_request_server_client->async_set_dns_server(dns_over_udp.server_address, dns_over_udp.port, false, dns_over_udp.validate_dnssec_locally);
        });

    m_request_server_client->on_retrieve_http_cookie = [](URL::URL const& url, RequestServer::IsPrivate is_private) -> String {
        auto& cookie_jar = Application::cookie_jar(is_private == RequestServer::IsPrivate::Yes ? WebView::IsPrivate::Yes : WebView::IsPrivate::No);
        return cookie_jar.get_cookie(url, HTTP::Cookie::Source::Http);
    };

    m_request_server_client->on_request_server_died = [this]() {
        m_request_server_client = nullptr;
        m_private_request_server_client = nullptr;

        if (Core::EventLoop::current().was_exit_requested())
            return;

        if (auto result = launch_request_server(); result.is_error()) {
            warnln("\033[31;1mUnable to launch replacement RequestServer: {}\033[0m", result.error());
            VERIFY_NOT_REACHED();
        }

        size_t normal_client_count = 0;
        size_t private_client_count = 0;
        WebView::WebContentClient::for_each_client([&](WebView::WebContentClient& client) {
            client.is_private() == WebView::IsPrivate::No ? ++normal_client_count : ++private_client_count;
            return IterationDecision::Continue;
        });

        auto create_handles = [&](auto is_private, auto client_count) -> Vector<IPC::TransportHandle> {
            if (client_count == 0)
                return {};

            auto response = m_request_server_client->send_sync_but_allow_failure<Messages::RequestServer::ConnectNewClients>(client_count, is_private);
            if (!response || response->handles().size() != client_count) {
                warnln("Failed to connect {} new clients to RequestServer", client_count);
                VERIFY_NOT_REACHED();
            }

            return response->take_handles();
        };

        auto normal_handles = create_handles(RequestServer::IsPrivate::No, normal_client_count);
        auto private_handles = create_handles(RequestServer::IsPrivate::Yes, private_client_count);

        WebView::WebContentClient::for_each_client([&](WebView::WebContentClient& client) {
            auto& handles = client.is_private() == WebView::IsPrivate::No ? normal_handles : private_handles;
            client.async_connect_to_request_server(handles.take_last());
            return IterationDecision::Continue;
        });

        if (auto result = WebView::WorkerProcessManager::the().reconnect_to_request_server(); result.is_error()) {
            warnln("Unable to reconnect WebWorker processes to RequestServer: {}", result.error());
            VERIFY_NOT_REACHED();
        }
    };

    if (Application::browser_options().dns_settings.has_value())
        Application::settings().set_dns_settings(Application::browser_options().dns_settings.value(), true);

    return {};
}

ErrorOr<void> Application::launch_image_decoder_server()
{
    m_image_decoder_client = TRY(bind_service<ImageDecoderClient::Client>(&bind_ui_image_decoder_service));

    m_image_decoder_client->on_death = [this]() {
        m_image_decoder_client = nullptr;

        if (Core::EventLoop::current().was_exit_requested())
            return;

        if (auto result = launch_image_decoder_server(); result.is_error()) {
            dbgln("Failed to restart image decoder: {}", result.error());
            VERIFY_NOT_REACHED();
        }
    };

    return {};
}

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_initNativeCode(JNIEnv*, jobject, jstring, jstring, jobject, jstring);

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_initNativeCode(JNIEnv* env, jobject thiz, jstring resource_dir, jstring tag_name, jobject timer_service, jstring user_dir)
{
    char const* raw_resource_dir = env->GetStringUTFChars(resource_dir, nullptr);
    WebView::s_ladybird_resource_root = raw_resource_dir;
    env->ReleaseStringUTFChars(resource_dir, raw_resource_dir);

    // While setting XDG environment variables in order to store user data may seem silly
    // but in our case it seems to be the most rational idea.
    char const* raw_user_dir = env->GetStringUTFChars(user_dir, nullptr);
    setenv("XDG_CONFIG_HOME", ByteString::formatted("{}/config", raw_user_dir).characters(), 1);
    setenv("XDG_DATA_HOME", ByteString::formatted("{}/userdata", raw_user_dir).characters(), 1);
    // En Android NO existen /run/user, ~/.cache ni $HOME: hay que ponerlos
    // bajo user_dir o Profile::create_legacy muere en el primer mkdir.
    setenv("XDG_CACHE_HOME", ByteString::formatted("{}/cache", raw_user_dir).characters(), 1);
    setenv("XDG_RUNTIME_DIR", ByteString::formatted("{}/runtime", raw_user_dir).characters(), 1);
    setenv("XDG_STATE_HOME", ByteString::formatted("{}/state", raw_user_dir).characters(), 1);
    setenv("HOME", raw_user_dir, 1);
    setenv("TMPDIR", ByteString::formatted("{}/tmp", raw_user_dir).characters(), 1);
    for (auto const& subdir : { "config"sv, "userdata"sv, "cache"sv, "runtime"sv, "state"sv, "tmp"sv }) {
        auto dir = ByteString::formatted("{}/{}", raw_user_dir, subdir);
        if (auto result = Core::Directory::create(dir, Core::Directory::CreateDirectories::Yes, 0700); result.is_error())
            dbgln("No pude crear {}: {}", dir, result.error());
    }
    env->ReleaseStringUTFChars(user_dir, raw_user_dir);

    char const* raw_tag_name = env->GetStringUTFChars(tag_name, nullptr);
    AK::set_log_tag_name(raw_tag_name);
    env->ReleaseStringUTFChars(tag_name, raw_tag_name);

    dbgln("Set resource dir to {}", WebView::s_ladybird_resource_root);

    auto file_or_error = Core::System::open(MUST(String::formatted("{}/icons/48x48/app-browser.png", WebView::s_ladybird_resource_root)), O_RDONLY);
    if (file_or_error.is_error()) {
        dbgln("No resource files, perhaps extracting went wrong?");
    } else {
        dbgln("Found app-browser.png");
        dbgln("Hopefully no developer changed the asset files and expected them to be re-extracted!");
    }

    env->GetJavaVM(&global_vm);
    VERIFY(global_vm);

    s_java_instance = env->NewGlobalRef(thiz);
    jclass clazz = env->GetObjectClass(s_java_instance);
    VERIFY(clazz);
    s_schedule_event_loop_method = env->GetMethodID(clazz, "scheduleEventLoop", "()V");
    VERIFY(s_schedule_event_loop_method);
    s_bind_request_server_service_method = env->GetMethodID(clazz, "bindRequestServerService", "(I)V");
    VERIFY(s_bind_request_server_service_method);
    s_bind_image_decoder_service_method = env->GetMethodID(clazz, "bindImageDecoderService", "(I)V");
    VERIFY(s_bind_image_decoder_service_method);
    env->DeleteLocalRef(clazz);

    jobject timer_service_ref = env->NewGlobalRef(timer_service);

    auto* event_loop_manager = new Ladybird::ALooperEventLoopManager(timer_service_ref);
    event_loop_manager->on_did_post_event = [] {
        Ladybird::JavaEnvironment env(global_vm);
        env.get()->CallVoidMethod(s_java_instance, s_schedule_event_loop_method);
    };
    Core::EventLoopManager::install(*event_loop_manager);
    s_main_event_loop = make<Core::EventLoop>();

    // The strings cannot be empty
    Main::Arguments arguments = {
        .argc = 0,
        .argv = nullptr,
        .strings = Span<StringView> { new StringView("ladybird"sv), 1 }
    };

    // FIXME: We are not making use of this Application object to track our processes.
    // So, right now, the Application's ProcessManager is constantly empty.
    // (However, LibWebView depends on an Application object existing, so we do have to actually create one.)
    s_application = Application::create(arguments).release_value_but_fixme_should_propagate_errors();
}

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_execMainEventLoop(JNIEnv*, jobject /* thiz */);

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_execMainEventLoop(JNIEnv*, jobject /* thiz */)
{
    if (s_main_event_loop) {
        s_main_event_loop->pump(Core::EventLoop::WaitMode::PollForEvents);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_disposeNativeCode(JNIEnv*, jobject /* thiz */);

extern "C" JNIEXPORT void JNICALL
Java_org_serenityos_ladybird_LadybirdActivity_disposeNativeCode(JNIEnv* env, jobject /* thiz */)
{
    s_main_event_loop = nullptr;
    s_schedule_event_loop_method = nullptr;
    s_application = nullptr;
    env->DeleteGlobalRef(s_java_instance);

    delete &Core::EventLoopManager::the();
}
