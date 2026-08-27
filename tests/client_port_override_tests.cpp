#include "../engine/core/client_port_override.h"

#include <cstdio>
#include <cstring>

namespace
{
bool Check(bool condition, const char* what)
{
    if (!condition)
        std::printf("FAIL: %s\n", what);
    return condition;
}

ConVarR1* capturedClientPort;
int capturedClientPortCalls;
char capturedClientPortValue[16]{};

void CaptureClientPort(ConVarR1* variable, const char* value)
{
    capturedClientPort = variable;
    ++capturedClientPortCalls;
    strcpy_s(capturedClientPortValue, value);
}

bool TestClientPortUsesEngineSetter()
{
    using r1delta::client_port::ApplyOverride;

    bool passed = true;
    auto* const variable = reinterpret_cast<ConVarR1*>(0x1234);
    passed &= Check(
        ApplyOverride(variable, 27012, &CaptureClientPort),
        "clientport override rejected a valid port");
    passed &= Check(
        capturedClientPort == variable
            && capturedClientPortCalls == 1
            && std::strcmp(capturedClientPortValue, "27012") == 0,
        "clientport override did not delegate the decimal value to the engine setter");

    passed &= Check(
        ApplyOverride(variable, 65535, &CaptureClientPort)
            && capturedClientPortCalls == 2
            && std::strcmp(capturedClientPortValue, "65535") == 0,
        "clientport override rejected the maximum valid port");
    passed &= Check(
        !ApplyOverride(variable, 0, &CaptureClientPort)
            && !ApplyOverride(variable, 65536, &CaptureClientPort)
            && !ApplyOverride(nullptr, 27012, &CaptureClientPort)
            && !ApplyOverride(variable, 27012, nullptr)
            && capturedClientPortCalls == 2,
        "clientport override accepted invalid state or invoked the setter");
    return passed;
}
}

int main()
{
    const bool passed = TestClientPortUsesEngineSetter();
    std::printf(
        "%s\n",
        passed
            ? "client port override tests passed"
            : "client port override tests FAILED");
    return passed ? 0 : 1;
}
