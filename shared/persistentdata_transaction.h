#pragma once

namespace PersistentDataTransaction {

enum class RecoveryAction
{
	CommitPrimary,
	RestoreBackup,
	PreserveForRecovery,
};

constexpr RecoveryAction SelectRecoveryAction(bool primaryValid, bool backupValid)
{
	if (primaryValid)
		return RecoveryAction::CommitPrimary;
	if (backupValid)
		return RecoveryAction::RestoreBackup;
	return RecoveryAction::PreserveForRecovery;
}

}
