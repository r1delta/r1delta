// Connect-path identity rules: the account-hold recovery (engine/core/account_hold.h) and the duplicate connect datagram filter
// (engine/net/connect_duplicate_filter.h). Both are pure logic; the engine glue around them is exercised in the game.
#include "../engine/core/account_hold.h"
#include "../engine/net/connect_duplicate_filter.h"

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

constexpr const char kAccount[] = "1234567890123456789";
constexpr const char kDerived[] = "-1234567890123456789";
constexpr unsigned long long kSecond = 1000;

struct Target {
    unsigned char bytes[AccountHoldEpisode::kTargetBytes];
    explicit Target(unsigned char tag) { std::memset(bytes, tag, sizeof(bytes)); }
};

bool TestDerivation()
{
    bool passed = true;
    long long next = 0;
    passed &= Check(R1DeltaDeriveReleasedAccountId(kAccount, &next) && next == -1234567890123456789LL, "positive id flips its sign");
    passed &= Check(R1DeltaDeriveReleasedAccountId(kDerived, &next) && next == -1234567890123456790LL, "a refused derived id steps one further");
    passed &= Check(!R1DeltaDeriveReleasedAccountId("12ab", &next) && !R1DeltaDeriveReleasedAccountId("", &next)
        && !R1DeltaDeriveReleasedAccountId(nullptr, &next), "non numeric ids are not derived");
    passed &= Check(R1DeltaIsDerivedAccountId(kDerived) && R1DeltaIsDerivedAccountId("-9000000000000000001"), "derived ids are recognised");
    passed &= Check(!R1DeltaIsDerivedAccountId(kAccount) && !R1DeltaIsDerivedAccountId("-123") && !R1DeltaIsDerivedAccountId("-12345678901x")
        && !R1DeltaIsDerivedAccountId(nullptr), "plain, short and malformed ids are not derived ids");
    return passed;
}

bool TestReasons()
{
    bool passed = true;
    passed &= Check(R1DeltaReasonIsHeldAccount("Player's account is already on the server")
        && R1DeltaReasonIsHeldAccount("Connection rejected: Player's account is ALREADY ON THE SERVER") && !R1DeltaReasonIsHeldAccount("Server is full")
        && !R1DeltaReasonIsHeldAccount(nullptr), "held account reason");
    passed &= Check(R1DeltaReasonIsTimeout("#DISCONNECT_TIMEDOUT") && R1DeltaReasonIsTimeout("Connection to server timed out")
        && !R1DeltaReasonIsTimeout("Disconnect by user.") && !R1DeltaReasonIsTimeout(""), "timeout reason");
    return passed;
}

bool TestFirstJoinKeepsIdentity()
{
    bool passed = true;
    AccountHoldEpisode episode;
    passed &= Check(episode.OnHeldRefusal(kAccount, 10 * kSecond) == AccountHoldEpisode::Verdict::NoEarlierConnection,
        "a refusal on a first join must not recover");
    passed &= Check(!episode.Active() && episode.Attempts() == 0, "nothing changed on a first join");

    // A timeout before the server ever accepted us (challenge never answered) leaves nothing held.
    episode.NoteTimeout(1, 20 * kSecond);
    passed &= Check(episode.OnHeldRefusal(kAccount, 21 * kSecond) == AccountHoldEpisode::Verdict::NoEarlierConnection,
        "a timeout at the challenge stage is not our hold");
    return passed;
}

bool TestRecoveryAfterOwnTimeout()
{
    bool passed = true;
    AccountHoldEpisode episode;
    const Target server(7);
    const Target other(9);

    episode.NoteTimeout(8, 100 * kSecond);
    passed &= Check(episode.OnHeldRefusal(kAccount, 105 * kSecond) == AccountHoldEpisode::Verdict::Recover, "own timeout then refusal recovers");
    passed &= Check(episode.Active() && episode.DerivedId() == kDerived && episode.OriginalId() == kAccount, "derived id and original are kept");

    // The retry the recovery issued, then the resends of that attempt, keep the derived id.
    passed &= Check(!episode.BeginConnectAttempt(server.bytes, 106 * kSecond), "the retry keeps the derived id");
    passed &= Check(!episode.BeginConnectAttempt(server.bytes, 111 * kSecond) && !episode.BeginConnectAttempt(server.bytes, 116 * kSecond),
        "resends of the same attempt keep the derived id");
    passed &= Check(episode.Active(), "episode still running");

    // A connect to another server is a new episode: the original id comes back before it is serialized.
    passed &= Check(episode.BeginConnectAttempt(other.bytes, 120 * kSecond), "another target ends the episode");
    passed &= Check(!episode.Active() && episode.OriginalId() == kAccount, "episode over, original id available");
    passed &= Check(!episode.BeginConnectAttempt(other.bytes, 121 * kSecond), "no episode, nothing to restore");

    // The same server after the resend cadence broke is a new attempt too.
    AccountHoldEpisode second;
    second.NoteTimeout(6, 0);
    passed &= Check(second.OnHeldRefusal(kAccount, kSecond) == AccountHoldEpisode::Verdict::Recover, "second episode recovers");
    passed &= Check(!second.BeginConnectAttempt(server.bytes, 2 * kSecond), "second episode retry");
    passed &= Check(second.BeginConnectAttempt(server.bytes, 2 * kSecond + AccountHoldEpisode::kAttemptGapMs + 1),
        "a connect after the resend cadence ended is a new attempt");
    return passed;
}

bool TestStalenessAndLimits()
{
    bool passed = true;
    {
        AccountHoldEpisode episode;
        episode.NoteTimeout(8, 0);
        passed &= Check(episode.OnHeldRefusal(kAccount, AccountHoldEpisode::kOwnTimeoutWindowMs + 1) == AccountHoldEpisode::Verdict::NoEarlierConnection,
            "an old timeout no longer explains a refusal");
    }
    {
        AccountHoldEpisode episode;
        episode.NoteTimeout(8, 0);
        unsigned long long now = kSecond;
        passed &= Check(episode.OnHeldRefusal(kAccount, now) == AccountHoldEpisode::Verdict::Recover, "attempt 1");
        passed &= Check(episode.OnHeldRefusal(kDerived, now + kSecond) == AccountHoldEpisode::Verdict::Exhausted, "cooldown holds back an immediate repeat");
        now += AccountHoldEpisode::kCooldownMs;
        passed &= Check(episode.OnHeldRefusal(kDerived, now) == AccountHoldEpisode::Verdict::Recover && episode.DerivedId() == "-1234567890123456790",
            "attempt 2 walks one step further and keeps the first original");
        passed &= Check(episode.OriginalId() == kAccount, "the original id is the first one, not the intermediate derived id");
        now += AccountHoldEpisode::kCooldownMs;
        passed &= Check(episode.OnHeldRefusal("-1234567890123456790", now) == AccountHoldEpisode::Verdict::Recover, "attempt 3");
        now += AccountHoldEpisode::kCooldownMs;
        passed &= Check(episode.OnHeldRefusal("-1234567890123456791", now) == AccountHoldEpisode::Verdict::Exhausted, "the attempt limit is enforced");
    }
    {
        AccountHoldEpisode episode;
        episode.NoteTimeout(8, 0);
        passed &= Check(episode.OnHeldRefusal("not-a-number", kSecond) == AccountHoldEpisode::Verdict::NotNumeric && !episode.Active(),
            "a non numeric id is never rewritten");
    }
    return passed;
}

bool TestDuplicateFilter()
{
    bool passed = true;
    ConnectDuplicateFilter filter;
    unsigned char from[24];
    std::memset(from, 1, sizeof(from));
    unsigned char data[300];
    for (size_t i = 0; i < sizeof(data); ++i)
        data[i] = static_cast<unsigned char>(i * 7);

    passed &= Check(!filter.IsDuplicate(from, 5.0, data, sizeof(data)), "the first datagram is processed");
    passed &= Check(filter.IsDuplicate(from, 5.0, data, sizeof(data)), "an exact repeat in the same frame is dropped");
    passed &= Check(filter.IsDuplicate(from, 5.0, data, sizeof(data)), "a second repeat is dropped too");

    unsigned char otherFrom[24];
    std::memcpy(otherFrom, from, sizeof(otherFrom));
    otherFrom[20] ^= 0x10;   // another source port
    passed &= Check(!filter.IsDuplicate(otherFrom, 5.0, data, sizeof(data)), "another client in the same frame is processed");
    passed &= Check(!filter.IsDuplicate(from, 5.0, data, sizeof(data)), "the original client is processed again after another one in between");

    passed &= Check(!filter.IsDuplicate(from, 5.1, data, sizeof(data)), "the same bytes in a later frame are processed");
    data[100] ^= 1;
    passed &= Check(!filter.IsDuplicate(from, 5.1, data, sizeof(data)), "a fresh challenge number is a different datagram");
    passed &= Check(!filter.IsDuplicate(from, 5.1, data, sizeof(data) - 1), "a different length is a different datagram");

    unsigned char big[2000] = {};
    passed &= Check(!filter.IsDuplicate(from, 6.0, big, sizeof(big)) && !filter.IsDuplicate(from, 6.0, big, sizeof(big)),
        "an oversized datagram is never remembered");
    return passed;
}
}

int main()
{
    bool passed = true;
    passed &= TestDerivation();
    passed &= TestReasons();
    passed &= TestFirstJoinKeepsIdentity();
    passed &= TestRecoveryAfterOwnTimeout();
    passed &= TestStalenessAndLimits();
    passed &= TestDuplicateFilter();
    std::printf("%s\n", passed ? "connect identity tests passed" : "connect identity tests FAILED");
    return passed ? 0 : 1;
}
