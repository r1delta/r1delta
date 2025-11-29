#include "eos_layer.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

#include <Lmcons.h>

#include "core.h"
#include "eos_logging_manager.h"
#include "eos_threading.h"
#include "logging.h"

namespace
{

constexpr int kAsyncPollIntervalMs = 10;
constexpr char kDefaultSocketName[] = "r1delta";
std::recursive_mutex g_sdkMutex;

std::string Base64Decode(const std::string& encoded)
{
    static constexpr char kBase64Chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string decoded;
    decoded.reserve((encoded.size() * 3) / 4);

    std::array<int, 256> decodeTable{};
    decodeTable.fill(-1);
    for (int i = 0; i < 64; ++i)
        decodeTable[static_cast<unsigned char>(kBase64Chars[i])] = i;

    int val = 0;
    int bits = -8;
    for (unsigned char c : encoded)
    {
        if (decodeTable[c] == -1)
            break;
        val = (val << 6) + decodeTable[c];
        bits += 6;
        if (bits >= 0)
        {
            decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return decoded;
}

struct PromiseCleanup
{
    template <typename T>
    static void SetValue(std::promise<T>* promise, const T& value)
    {
        if (!promise)
            return;
        try
        {
            promise->set_value(value);
        }
        catch (...)
        {
            // Ignore multiple set attempts.
        }
    }
};

std::string BuildDisplayName()
{
    std::array<char, EOS_CONNECT_USERLOGININFO_DISPLAYNAME_MAX_LENGTH> buffer{};

    DWORD size = static_cast<DWORD>(buffer.size() - 1);
    if (GetUserNameA(buffer.data(), &size) && size > 0)
        return std::string(buffer.data(), size);

    size = static_cast<DWORD>(buffer.size() - 1);
    if (GetComputerNameA(buffer.data(), &size) && size > 0)
        return std::string(buffer.data(), size);

    return "r1delta_user";
}

void EOS_CALL OnCreateDeviceIdCallback(const EOS_Connect_CreateDeviceIdCallbackInfo* data)
{
    auto* promise = static_cast<std::promise<EOS_EResult>*>(data->ClientData);
    PromiseCleanup::SetValue(promise, data->ResultCode);
}

void EOS_CALL OnConnectLoginCallback(const EOS_Connect_LoginCallbackInfo* data)
{
    auto* promise = static_cast<std::promise<eos::LoginCallbackPayload>*>(data->ClientData);
    eos::LoginCallbackPayload payload{};
    payload.result = data->ResultCode;
    payload.user = data->LocalUserId;
    payload.continuanceToken = data->ContinuanceToken;
    PromiseCleanup::SetValue(promise, payload);
}

void EOS_CALL OnIncomingConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* data)
{
    auto* layer = data ? static_cast<eos::EosLayer*>(data->ClientData) : nullptr;
    if (layer)
    {
        layer->HandleConnectionRequest(data);
    }
}

void EOS_CALL OnPeerConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* data)
{
    auto* layer = data ? static_cast<eos::EosLayer*>(data->ClientData) : nullptr;
    if (layer)
    {
        layer->HandleConnectionEstablished(data);
    }
}

} // namespace

namespace eos
{

std::recursive_mutex& GetSdkMutex()
{
    return g_sdkMutex;
}

EosLayer& EosLayer::Instance()
{
    static EosLayer instance;
    return instance;
}

EosLayer::~EosLayer()
{
    Shutdown();
}

bool EosLayer::Initialize(const char* productId,
                          const char* sandboxId,
                          const char* deploymentId,
                          const char* productName,
                          const char* productVersion)
{
    if (IsInitialized())
        return true;

    if (!productId || !sandboxId || !deploymentId)
        return false;

    m_productName = productName ? productName : "r1delta";
    m_productVersion = productVersion ? productVersion : "1.0.0";

    EOS_InitializeOptions initOptions{};
    initOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
    initOptions.ProductName = m_productName.c_str();
    initOptions.ProductVersion = m_productVersion.c_str();

    EOS_EResult initResult = EOS_EResult::EOS_UnexpectedError;
    {
        SdkLock lock(GetSdkMutex());
        initResult = EOS_Initialize(&initOptions);
    }
    if (initResult != EOS_EResult::EOS_Success && initResult != EOS_EResult::EOS_AlreadyConfigured)
    {
        Msg("EOS: Initialize failed (%d)\n", static_cast<int>(initResult));
        return false;
    }

    m_sdkInitialized.store(true, std::memory_order_release);
    Logging::Initialize();

    static const std::string clientId = Base64Decode("eHl6YTc4OTFYTXlzTEhJZ0pqN0hsWkppZWtBVFMwRWI=");
    static const std::string clientSecret = Base64Decode("ZjFjV3FuRFcxOC8wNnlZS2J3Qk1tR0pIY05QREVMVlo1eWh0THdrRUl2UQ==");

    EOS_Platform_Options platformOptions{};
    platformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platformOptions.ProductId = productId;
    platformOptions.SandboxId = sandboxId;
    platformOptions.DeploymentId = deploymentId;
    platformOptions.ClientCredentials.ClientId = clientId.c_str();
    platformOptions.ClientCredentials.ClientSecret = clientSecret.c_str();
    platformOptions.Flags = EOS_PF_DISABLE_OVERLAY;
    platformOptions.TickBudgetInMilliseconds = 1;

    {
        SdkLock lock(GetSdkMutex());
        m_platformHandle = EOS_Platform_Create(&platformOptions);
    }
    if (!m_platformHandle)
    {
        Msg("EOS: Failed to create platform handle\n");
        Shutdown();
        return false;
    }

    {
        SdkLock lock(GetSdkMutex());
        m_connectHandle = EOS_Platform_GetConnectInterface(m_platformHandle);
        m_p2pHandle = EOS_Platform_GetP2PInterface(m_platformHandle);
    }
    if (!m_connectHandle || !m_p2pHandle)
    {
        Msg("EOS: Missing required interfaces\n");
        Shutdown();
        return false;
    }

    m_defaultSocketId.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::snprintf(m_defaultSocketId.SocketName,
                  EOS_P2P_SOCKETID_SOCKETNAME_SIZE,
                  "%s",
                  kDefaultSocketName);

    EOS_P2P_SetRelayControlOptions relayOptions{};
    relayOptions.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
    relayOptions.RelayControl = EOS_ERelayControl::EOS_RC_NoRelays;
    {
        SdkLock lock(GetSdkMutex());
        EOS_P2P_SetRelayControl(m_p2pHandle, &relayOptions);
    }

    m_fakeIpLayer = std::make_unique<FakeIpLayer>(m_p2pHandle, nullptr);

    if (!CreateDeviceId())
    {
        Shutdown();
        return false;
    }

    if (!LoginWithDeviceId())
    {
        Shutdown();
        return false;
    }

    StartPumpThread();
    m_initialized.store(true, std::memory_order_release);
    return true;
}

void EosLayer::Shutdown()
{
    StopPumpThread();
    UnregisterSocketNotifications();

    if (m_fakeIpLayer)
    {
        m_fakeIpLayer->Clear();
        m_fakeIpLayer.reset();
    }

    m_localUser.store(nullptr, std::memory_order_release);

    if (m_platformHandle)
    {
        {
            SdkLock lock(GetSdkMutex());
            EOS_Platform_Release(m_platformHandle);
        }
        m_platformHandle = nullptr;
    }

    m_connectHandle = nullptr;
    m_p2pHandle = nullptr;

    if (m_sdkInitialized.exchange(false))
    {
        Logging::Shutdown();
        {
            SdkLock lock(GetSdkMutex());
            EOS_Shutdown();
        }
    }

    m_initialized.store(false, std::memory_order_release);
}

bool EosLayer::CreateDeviceId()
{
    if (!m_connectHandle)
        return false;

    auto promise = std::make_shared<std::promise<EOS_EResult>>();
    auto future = promise->get_future();

    EOS_Connect_CreateDeviceIdOptions options{};
    options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    options.DeviceModel = "PC Windows";

    {
        SdkLock lock(GetSdkMutex());
        EOS_Connect_CreateDeviceId(m_connectHandle, &options, promise.get(), &OnCreateDeviceIdCallback);
    }

    EOS_EResult result = EOS_EResult::EOS_UnexpectedError;
    if (!WaitForResult(future, &result, 6000))
    {
        Msg("EOS: Timed out waiting for CreateDeviceId\n");
        return false;
    }

    if (result == EOS_EResult::EOS_Success || result == EOS_EResult::EOS_DuplicateNotAllowed)
        return true;

    Msg("EOS: CreateDeviceId failed (%d)\n", static_cast<int>(result));
    return false;
}

bool EosLayer::LoginWithDeviceId()
{
    if (!m_connectHandle)
        return false;

    auto promise = std::make_shared<std::promise<LoginCallbackPayload>>();
    auto future = promise->get_future();

    auto displayName = std::make_shared<std::string>(BuildDisplayName());

    EOS_Connect_UserLoginInfo userInfo{};
    userInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    userInfo.DisplayName = displayName->c_str();

    EOS_Connect_Credentials credentials{};
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    credentials.Token = nullptr;

    EOS_Connect_LoginOptions loginOptions{};
    loginOptions.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    loginOptions.Credentials = &credentials;
    loginOptions.UserLoginInfo = &userInfo;

    {
        SdkLock lock(GetSdkMutex());
        EOS_Connect_Login(m_connectHandle, &loginOptions, promise.get(), &OnConnectLoginCallback);
    }

    LoginCallbackPayload payload{};
    if (!WaitForLogin(future, &payload, 6000))
    {
        Msg("EOS: Timed out waiting for login\n");
        return false;
    }

    if (payload.result == EOS_EResult::EOS_Success && payload.user)
    {
        m_localUser.store(payload.user, std::memory_order_release);

        char puidBuffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
        int32_t puidBufferLen = static_cast<int32_t>(sizeof(puidBuffer));
        bool printedUser = false;
        {
            SdkLock lock(GetSdkMutex());
            printedUser = EOS_ProductUserId_ToString(payload.user, puidBuffer, &puidBufferLen) == EOS_EResult::EOS_Success;
        }
        if (printedUser)
        {
            Msg("EOS: Logged in as %s\n", puidBuffer);
        }
        else
        {
            Msg("EOS: Logged in with ProductUserId (string conversion failed)\n");
        }

        if (m_fakeIpLayer)
        {
            m_fakeIpLayer->SetLocalUser(payload.user);
        }
        if (!RegisterSocketNotifications())
        {
            Msg("EOS: Failed to register socket notifications\n");
            return false;
        }
        return true;
    }

    if (payload.result == EOS_EResult::EOS_InvalidUser && payload.continuanceToken)
    {
        Msg("EOS: DeviceID login requires account linking (continuance token)\n");
    }
    else
    {
        Msg("EOS: Login failed (%d)\n", static_cast<int>(payload.result));
    }

    return false;
}

bool EosLayer::WaitForResult(std::future<EOS_EResult>& future,
                             EOS_EResult* outResult,
                             int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        PumpOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPollIntervalMs));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs)
            return false;
    }

    if (outResult)
    {
        *outResult = future.get();
    }
    else
    {
        future.get();
    }
    return true;
}

bool EosLayer::WaitForLogin(std::future<LoginCallbackPayload>& future,
                            LoginCallbackPayload* outPayload,
                            int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        PumpOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPollIntervalMs));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs)
            return false;
    }

    if (outPayload)
    {
        *outPayload = future.get();
    }
    else
    {
        future.get();
    }
    return true;
}

void EosLayer::PumpOnce()
{
    std::lock_guard guard(m_tickMutex);
    if (m_platformHandle)
    {
        SdkLock lock(GetSdkMutex());
        EOS_Platform_Tick(m_platformHandle);
    }
    if (m_fakeIpLayer)
    {
        m_fakeIpLayer->PumpIncoming();
    }
}

void EosLayer::StartPumpThread()
{
    if (m_shouldRun.exchange(true))
        return;

    m_pumpThread = std::thread([this]()
    {
        while (m_shouldRun.load(std::memory_order_acquire))
        {
            PumpOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(kAsyncPollIntervalMs));
        }
    });
}

void EosLayer::StopPumpThread()
{
    if (!m_shouldRun.exchange(false))
        return;

    if (m_pumpThread.joinable())
    {
        m_pumpThread.join();
    }
}

bool EosLayer::RegisterSocketNotifications()
{
    if (!m_p2pHandle)
        return false;

    auto* localUser = m_localUser.load(std::memory_order_acquire);
    if (!localUser)
        return false;

    UnregisterSocketNotifications();

    if (m_defaultSocketId.ApiVersion == 0)
    {
        m_defaultSocketId.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
        std::snprintf(m_defaultSocketId.SocketName,
                      EOS_P2P_SOCKETID_SOCKETNAME_SIZE,
                      "%s",
                      kDefaultSocketName);
    }

    EOS_P2P_AddNotifyPeerConnectionRequestOptions requestOptions{};
    requestOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    requestOptions.LocalUserId = localUser;
    requestOptions.SocketId = &m_defaultSocketId;
    {
        SdkLock lock(GetSdkMutex());
        m_connectionRequestNotificationId = EOS_P2P_AddNotifyPeerConnectionRequest(
            m_p2pHandle, &requestOptions, this, &OnIncomingConnectionRequest);
    }
    if (m_connectionRequestNotificationId == EOS_INVALID_NOTIFICATIONID)
        return false;

    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions establishedOptions{};
    establishedOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
    establishedOptions.LocalUserId = localUser;
    establishedOptions.SocketId = &m_defaultSocketId;
    {
        SdkLock lock(GetSdkMutex());
        m_connectionEstablishedNotificationId = EOS_P2P_AddNotifyPeerConnectionEstablished(
            m_p2pHandle, &establishedOptions, this, &OnPeerConnectionEstablished);
    }
    if (m_connectionEstablishedNotificationId == EOS_INVALID_NOTIFICATIONID)
    {
        {
            SdkLock lock(GetSdkMutex());
            EOS_P2P_RemoveNotifyPeerConnectionRequest(m_p2pHandle, m_connectionRequestNotificationId);
        }
        m_connectionRequestNotificationId = EOS_INVALID_NOTIFICATIONID;
        return false;
    }

    Msg("EOS: Registered socket notifications for \"%s\"\n", m_defaultSocketId.SocketName);
    return true;
}

void EosLayer::UnregisterSocketNotifications()
{
    if (m_p2pHandle && m_connectionRequestNotificationId != EOS_INVALID_NOTIFICATIONID)
    {
        SdkLock lock(GetSdkMutex());
        EOS_P2P_RemoveNotifyPeerConnectionRequest(m_p2pHandle, m_connectionRequestNotificationId);
        m_connectionRequestNotificationId = EOS_INVALID_NOTIFICATIONID;
    }
    if (m_p2pHandle && m_connectionEstablishedNotificationId != EOS_INVALID_NOTIFICATIONID)
    {
        SdkLock lock(GetSdkMutex());
        EOS_P2P_RemoveNotifyPeerConnectionEstablished(m_p2pHandle, m_connectionEstablishedNotificationId);
        m_connectionEstablishedNotificationId = EOS_INVALID_NOTIFICATIONID;
    }
}

void EosLayer::HandleConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* info)
{
    if (!info || !m_p2pHandle)
        return;

    const EOS_P2P_SocketId* socketId = info->SocketId ? info->SocketId : &m_defaultSocketId;

    EOS_P2P_AcceptConnectionOptions acceptOptions{};
    acceptOptions.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    acceptOptions.LocalUserId = info->LocalUserId;
    acceptOptions.RemoteUserId = info->RemoteUserId;
    acceptOptions.SocketId = socketId;

    EOS_EResult result = EOS_EResult::EOS_UnexpectedError;
    {
        SdkLock lock(GetSdkMutex());
        result = EOS_P2P_AcceptConnection(m_p2pHandle, &acceptOptions);
    }
    if (result != EOS_EResult::EOS_Success)
    {
        Msg("EOS: AcceptConnection failed (%d)\n", static_cast<int>(result));
        return;
    }

    char remoteId[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
    int32_t remoteIdLen = static_cast<int32_t>(sizeof(remoteId));
    bool haveRemoteId = false;
    if (info->RemoteUserId)
    {
        SdkLock lock(GetSdkMutex());
        haveRemoteId = EOS_ProductUserId_ToString(info->RemoteUserId, remoteId, &remoteIdLen) ==
                       EOS_EResult::EOS_Success;
    }
    if (haveRemoteId)
    {
        Msg("EOS: Accepted connection request from %s on socket %s\n", remoteId, socketId->SocketName);
    }
    else
    {
        Msg("EOS: Accepted connection request on socket %s\n", socketId->SocketName);
    }

    if (m_fakeIpLayer && info->RemoteUserId)
    {
        m_fakeIpLayer->RegisterPeer(info->RemoteUserId, socketId->SocketName, 0);
    }
}

void EosLayer::HandleConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* info)
{
    if (!info)
        return;

    char remoteId[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
    int32_t remoteIdLen = static_cast<int32_t>(sizeof(remoteId));
    bool haveRemoteId = false;
    if (info->RemoteUserId)
    {
        SdkLock lock(GetSdkMutex());
        haveRemoteId = EOS_ProductUserId_ToString(info->RemoteUserId, remoteId, &remoteIdLen) ==
                       EOS_EResult::EOS_Success;
    }
    if (haveRemoteId)
    {
        Msg("EOS: Connection established with %s (type=%d)\n",
            remoteId,
            static_cast<int>(info->ConnectionType));
    }
}

} // namespace eos
