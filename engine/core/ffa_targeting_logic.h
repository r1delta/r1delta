#pragma once

namespace r1delta::ffa_targeting
{
constexpr int kFriendlyMinimapClassification = 0;
constexpr int kEnemyMinimapClassification = 1;

constexpr bool AreOpposingFfaPlayers(
	bool ffaBased,
	bool firstIsPlayer,
	bool secondIsPlayer,
	bool sameEntity) noexcept
{
	return ffaBased && firstIsPlayer && secondIsPlayer && !sameEntity;
}

enum class FfaOwnerRelation : unsigned char
{
	Native,
	Friendly,
	Hostile
};

constexpr FfaOwnerRelation ResolveFfaOwnerRelation(
	bool ffaBased,
	bool firstHasPlayerOwner,
	bool firstOwnerIsAlive,
	bool secondHasPlayerOwner,
	bool secondOwnerIsAlive,
	bool samePlayerOwner,
	bool requireLiveOwners) noexcept
{
	if (!ffaBased || !firstHasPlayerOwner || !secondHasPlayerOwner)
		return FfaOwnerRelation::Native;
	if (samePlayerOwner)
		return FfaOwnerRelation::Friendly;
	if (requireLiveOwners
		&& (!firstOwnerIsAlive || !secondOwnerIsAlive)) {
		return FfaOwnerRelation::Native;
	}
	return FfaOwnerRelation::Hostile;
}

template <typename IsPlayer, typename GetBossPlayer, typename GetOwner>
void* ResolveOwningPlayer(
	void* entity,
	IsPlayer&& isPlayer,
	GetBossPlayer&& getBossPlayer,
	GetOwner&& getOwner)
{
	void* current = entity;
	for (int depth = 0; depth < 4 && current; ++depth) {
		if (isPlayer(current))
			return current;

		void* bossPlayer = getBossPlayer(current);
		if (bossPlayer && isPlayer(bossPlayer))
			return bossPlayer;

		void* owner = getOwner(current);
		if (!owner || owner == current)
			break;
		current = owner;
	}
	return nullptr;
}

constexpr bool ShouldAcceptSmartAmmoTarget(
	bool teamsDiffer,
	bool ffaBased,
	bool candidateHasPlayerOwner,
	bool candidateOwnerIsAlive,
	bool attackerHasPlayerOwner,
	bool attackerOwnerIsAlive,
	bool samePlayerOwner) noexcept
{
	const FfaOwnerRelation relation = ResolveFfaOwnerRelation(
		ffaBased,
		candidateHasPlayerOwner,
		candidateOwnerIsAlive,
		attackerHasPlayerOwner,
		attackerOwnerIsAlive,
		samePlayerOwner,
		true);
	if (relation == FfaOwnerRelation::Friendly)
		return false;
	if (relation == FfaOwnerRelation::Hostile)
		return true;
	return teamsDiffer;
}

constexpr bool ShouldAcceptObserverTarget(
	bool nativeTeamsEqual,
	bool ffaBased,
	bool observerIsPlayer,
	bool candidateIsPlayer,
	bool sameEntity,
	bool candidateObserverModeZero,
	bool candidateActive) noexcept
{
	return nativeTeamsEqual
		|| (ffaBased
			&& observerIsPlayer
			&& candidateIsPlayer
			&& !sameEntity
			&& candidateObserverModeZero
			&& candidateActive);
}

constexpr bool ShouldRouteTeamChatToSenderOnly(
	bool ffaBased,
	bool teamChat) noexcept
{
	return ffaBased && teamChat;
}

constexpr bool ShouldSuppressGameplayVoice(
	bool ffaBased,
	bool allTalkEnabled,
	int senderTeam) noexcept
{
	return ffaBased
		&& !allTalkEnabled
		&& (senderTeam == 2 || senderTeam == 3);
}

constexpr int ResolveMinimapClassification(
	int originalClassification,
	bool ffaBased,
	bool targetHasPlayerOwner,
	bool viewerHasPlayerOwner,
	bool samePlayerOwner) noexcept
{
	const FfaOwnerRelation relation = ResolveFfaOwnerRelation(
		ffaBased,
		targetHasPlayerOwner,
		true,
		viewerHasPlayerOwner,
		true,
		samePlayerOwner,
		false);
	if (relation == FfaOwnerRelation::Friendly) {
		return originalClassification == kEnemyMinimapClassification
			? kFriendlyMinimapClassification
			: originalClassification;
	}
	if (relation == FfaOwnerRelation::Hostile)
		return kEnemyMinimapClassification;
	return originalClassification;
}
}
