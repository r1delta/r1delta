#pragma once

void security_fixes_init();
void security_fixes_engine(uintptr_t engine_base);
void security_fixes_server(uintptr_t engine_base, uintptr_t server_base);