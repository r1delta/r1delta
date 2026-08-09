option casemap:none

public R1DeltaClientSmartAmmoFfaBridge
public R1DeltaClientMinimapFfaBridge
public R1DeltaClientMinimapVisibilityFfaBridge
public R1DeltaClientMinimapDefaultVisibilityFfaBridge
public R1DeltaServerSmartAmmoFfaBridge
public R1DeltaServerObserverInitialFfaBridge
public R1DeltaServerObserverCycleFfaBridge

extern R1DeltaResolveFfaClientRelation : proc
extern R1DeltaResolveLiveFfaClientRelation : proc
extern R1DeltaResolveLiveFfaServerRelation : proc
extern R1DeltaIsValidFfaObserverTarget : proc
extern g_R1DeltaFfaBased : dword
extern g_R1DeltaClientSmartAmmoAccept : qword
extern g_R1DeltaClientSmartAmmoReject : qword
extern g_R1DeltaClientMinimapContinue : qword
extern g_R1DeltaServerSmartAmmoAccept : qword
extern g_R1DeltaClientMinimapVisibilityContinue : qword
extern g_R1DeltaClientMinimapDefaultVisibilityShow : qword
extern g_R1DeltaClientMinimapDefaultVisibilityEnemy : qword
extern g_R1DeltaServerSmartAmmoReject : qword
extern g_R1DeltaServerObserverInitialAccept : qword
extern g_R1DeltaServerObserverInitialReject : qword
extern g_R1DeltaServerObserverCycleAccept : qword
extern g_R1DeltaServerObserverCycleReject : qword

SAVE_VOLATILES macro
	push rax
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11
	sub rsp, 88h
	movdqu xmmword ptr [rsp+20h], xmm0
	movdqu xmmword ptr [rsp+30h], xmm1
	movdqu xmmword ptr [rsp+40h], xmm2
	movdqu xmmword ptr [rsp+50h], xmm3
	movdqu xmmword ptr [rsp+60h], xmm4
	movdqu xmmword ptr [rsp+70h], xmm5
endm

RESTORE_VOLATILES macro
	movdqu xmm0, xmmword ptr [rsp+20h]
	movdqu xmm1, xmmword ptr [rsp+30h]
	movdqu xmm2, xmmword ptr [rsp+40h]
	movdqu xmm3, xmmword ptr [rsp+50h]
	movdqu xmm4, xmmword ptr [rsp+60h]
	movdqu xmm5, xmmword ptr [rsp+70h]
	lea rsp, [rsp+88h]
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rax
endm

.code

R1DeltaClientSmartAmmoFfaBridge proc
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je client_smart_ammo_native

	SAVE_VOLATILES
	mov rcx, r13
	mov rdx, rbx
	call R1DeltaResolveLiveFfaClientRelation
	cmp al, 1
	je client_smart_ammo_friendly
	cmp al, 2
	je client_smart_ammo_hostile
	RESTORE_VOLATILES

client_smart_ammo_native:
	cmp eax, edi
	jne client_smart_ammo_accept
	jmp client_smart_ammo_reject
client_smart_ammo_friendly:
	RESTORE_VOLATILES
	jmp client_smart_ammo_reject
client_smart_ammo_hostile:
	RESTORE_VOLATILES
	jmp client_smart_ammo_accept
client_smart_ammo_reject:
	jmp qword ptr [g_R1DeltaClientSmartAmmoReject]
client_smart_ammo_accept:
	jmp qword ptr [g_R1DeltaClientSmartAmmoAccept]
R1DeltaClientSmartAmmoFfaBridge endp

R1DeltaClientMinimapFfaBridge proc
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je client_minimap_continue

	SAVE_VOLATILES
	mov rcx, rdi
	mov rdx, qword ptr [rsp+128h]
	call R1DeltaResolveFfaClientRelation
	cmp al, 1
	je client_minimap_friendly
	cmp al, 2
	je client_minimap_hostile
	RESTORE_VOLATILES
	jmp client_minimap_continue

client_minimap_friendly:
	RESTORE_VOLATILES
	cmp dword ptr [rbx+5Ch], 1
	jne client_minimap_continue
	mov dword ptr [rbx+5Ch], 0
	jmp client_minimap_continue
client_minimap_hostile:
	RESTORE_VOLATILES
	mov dword ptr [rbx+5Ch], 1

client_minimap_continue:
	movss xmm2, dword ptr [rdi+1A34h]
	jmp qword ptr [g_R1DeltaClientMinimapContinue]
R1DeltaClientMinimapFfaBridge endp


R1DeltaClientMinimapVisibilityFfaBridge proc
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je client_minimap_visibility_native
	lea ecx, [rax+0Eh]
	jmp client_minimap_visibility_shift
client_minimap_visibility_native:
	lea ecx, [rax+04h]
client_minimap_visibility_shift:
	mov edx, edi
	shl edx, cl
	jmp qword ptr [g_R1DeltaClientMinimapVisibilityContinue]
R1DeltaClientMinimapVisibilityFfaBridge endp

R1DeltaClientMinimapDefaultVisibilityFfaBridge proc
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je client_minimap_default_visibility_native

	SAVE_VOLATILES
	mov rcx, rdi
	mov rdx, qword ptr [rsp+128h]
	call R1DeltaResolveFfaClientRelation
	cmp al, 1
	je client_minimap_default_visibility_friendly
	cmp al, 2
	je client_minimap_default_visibility_hostile
	RESTORE_VOLATILES

client_minimap_default_visibility_native:
	cmp esi, edx
	je client_minimap_default_visibility_friendly_jump
	jmp client_minimap_default_visibility_hostile_jump
client_minimap_default_visibility_friendly:
	RESTORE_VOLATILES
client_minimap_default_visibility_friendly_jump:
	jmp qword ptr [g_R1DeltaClientMinimapDefaultVisibilityShow]
client_minimap_default_visibility_hostile:
	RESTORE_VOLATILES
client_minimap_default_visibility_hostile_jump:
	jmp qword ptr [g_R1DeltaClientMinimapDefaultVisibilityEnemy]
R1DeltaClientMinimapDefaultVisibilityFfaBridge endp

R1DeltaServerSmartAmmoFfaBridge proc
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je server_smart_ammo_native

	SAVE_VOLATILES
	mov rcx, rbx
	mov rdx, r15
	call R1DeltaResolveLiveFfaServerRelation
	cmp al, 1
	je server_smart_ammo_friendly
	cmp al, 2
	je server_smart_ammo_hostile
	RESTORE_VOLATILES

server_smart_ammo_native:
	mov eax, dword ptr [r15+47Ch]
	cmp dword ptr [rbx+47Ch], eax
	jne server_smart_ammo_accept
	jmp server_smart_ammo_reject
server_smart_ammo_friendly:
	RESTORE_VOLATILES
	jmp server_smart_ammo_reject
server_smart_ammo_hostile:
	RESTORE_VOLATILES
	jmp server_smart_ammo_accept
server_smart_ammo_reject:
	jmp qword ptr [g_R1DeltaServerSmartAmmoReject]
server_smart_ammo_accept:
	jmp qword ptr [g_R1DeltaServerSmartAmmoAccept]
R1DeltaServerSmartAmmoFfaBridge endp

R1DeltaServerObserverInitialFfaBridge proc
	mov eax, dword ptr [rbx+47Ch]
	cmp dword ptr [r12+47Ch], eax
	je server_observer_initial_accept
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je server_observer_initial_reject

	SAVE_VOLATILES
	mov rcx, r12
	mov rdx, rbx
	call R1DeltaIsValidFfaObserverTarget
	mov byte ptr [rsp+80h], al
	cmp byte ptr [rsp+80h], 0
	RESTORE_VOLATILES
	jne server_observer_initial_accept

server_observer_initial_reject:
	jmp qword ptr [g_R1DeltaServerObserverInitialReject]
server_observer_initial_accept:
	jmp qword ptr [g_R1DeltaServerObserverInitialAccept]
R1DeltaServerObserverInitialFfaBridge endp

R1DeltaServerObserverCycleFfaBridge proc
	mov eax, dword ptr [rbx+47Ch]
	cmp dword ptr [rsi+47Ch], eax
	je server_observer_cycle_accept
	cmp dword ptr [g_R1DeltaFfaBased], 0
	je server_observer_cycle_reject

	SAVE_VOLATILES
	mov rcx, rsi
	mov rdx, rbx
	call R1DeltaIsValidFfaObserverTarget
	mov byte ptr [rsp+80h], al
	cmp byte ptr [rsp+80h], 0
	RESTORE_VOLATILES
	jne server_observer_cycle_accept

server_observer_cycle_reject:
	jmp qword ptr [g_R1DeltaServerObserverCycleReject]
server_observer_cycle_accept:
	jmp qword ptr [g_R1DeltaServerObserverCycleAccept]
R1DeltaServerObserverCycleFfaBridge endp

end
