public CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck

.data
extern CNetChan__ProcessSubChannelData_ret0 : qword
extern CNetChan__ProcessSubChannelData_Asm_continue : qword

.code

CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck proc
	; Check if fragment size of 'last' fragment doesn't exceed stack buffer size
	mov r8d, ebp
	cmp r8d, 250h
	jg ret0 ; if exceeds, make function return 0 right now
	jmp qword ptr [CNetChan__ProcessSubChannelData_Asm_continue] ; otherwise continue where we left off
ret0:
	jmp qword ptr [CNetChan__ProcessSubChannelData_ret0]
CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck endp

end