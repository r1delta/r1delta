#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_p2p.h"

#include "fake_ip_layer.h"

namespace eos
{

struct LoginCallbackPayload
{
    EOS_EResult result = EOS_EResult::EOS_UnexpectedError;
    EOS_ProductUserId user = nullptr;
    EOS_ContinuanceToken continuanceToken = nullptr;
};

class EosLayer
{
public:
    static EosLayer& Instance();

    bool Initialize(const char* productId,
                    const char* sandboxId,
                    const char* deploymentId,
                    const char* productName,
                    const char* productVersion);

    void Shutdown();

    bool IsInitialized() const { return m_initialized.load(std::memory_order_acquire); }
    EOS_ProductUserId GetLocalUser() const { return m_localUser.load(std::memory_order_acquire); }
    FakeIpLayer* GetFakeIpLayer() const { return m_fakeIpLayer.get(); }
    EOS_HP2P GetP2PHandle() const { return m_p2pHandle; }
    void HandleConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* info);
    void HandleConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* info);

private:
    EosLayer() = default;
    ~EosLayer();

    bool CreateDeviceId();
    bool LoginWithDeviceId();
    bool WaitForResult(std::future<EOS_EResult>& future, EOS_EResult* outResult, int timeoutMs);
    bool WaitForLogin(std::future<LoginCallbackPayload>& future, LoginCallbackPayload* outPayload, int timeoutMs);
    void PumpOnce();
    void StartPumpThread();
    void StopPumpThread();
    bool RegisterSocketNotifications();
    void UnregisterSocketNotifications();

    std::atomic<bool> m_initialized{ false };
    std::atomic<bool> m_sdkInitialized{ false };
    std::atomic<bool> m_shouldRun{ false };

    EOS_HPlatform m_platformHandle = nullptr;
    EOS_HConnect m_connectHandle = nullptr;
    EOS_HP2P m_p2pHandle = nullptr;

    std::unique_ptr<FakeIpLayer> m_fakeIpLayer;
    std::thread m_pumpThread;
    std::mutex m_tickMutex;

    std::string m_productName;
    std::string m_productVersion;
    EOS_P2P_SocketId m_defaultSocketId{};
    EOS_NotificationId m_connectionRequestNotificationId = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId m_connectionEstablishedNotificationId = EOS_INVALID_NOTIFICATIONID;

    std::atomic<EOS_ProductUserId> m_localUser{ nullptr };
};

} // namespace eos
