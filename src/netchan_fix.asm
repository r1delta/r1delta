PUBLIC _CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck
EXTERN CNetChan__ProcessSubChannelData_ret0:QWORD
EXTERN CNetChan__ProcessSubChannelData_Asm_continue:QWORD

.code
_CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck PROC
    mov     rax, rbp
    cmp     eax, 250h
    jg      jump_to_ret0

jump_to_continue:
    jmp     QWORD PTR [CNetChan__ProcessSubChannelData_Asm_continue]

jump_to_ret0:
    jmp     QWORD PTR [CNetChan__ProcessSubChannelData_ret0]
_CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck ENDP
END 