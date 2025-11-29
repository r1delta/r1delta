#include "predictionerror.h"

// write access to const memory has been detected, the output may be wrong!
const char* PredictionErrorFn(__int64 thisptr, const datamap_t* pCurrentMap, const typedescription_t* pField, const char* fmt, ...)
{
	ZoneScoped;
	
	++*(DWORD*)(thisptr + 32);
	const char* fieldname = "empty";
	const char* classname = "empty";
	int flags = 0;

	if (pField)
	{
		flags = pField->flags;
		fieldname = pField->fieldName ? pField->fieldName : "NULL";
		classname = pCurrentMap->dataClassName;
	}

	va_list argptr;
	char data[4096];
	int len;
	va_start(argptr, fmt);
	len = _vsnprintf(data, sizeof(data), fmt, argptr);
	va_end(argptr);
	data[strcspn(data, "\n")] = 0;

	int v6 = 0; // edi
	__int64 v7 = 0; // rbx
	bool bUseLongName = true;
	std::string longName;
	if (*(int*)(thisptr + 80) > 0)
	{
		do
		{
			const char* result = *(const char**)(v7 + *(unsigned __int64*)(thisptr + 56));
			if (result)
			{
				const char* v8 = (const char*)*((unsigned __int64*)result + 1);
				const char* v9 = "NULL";
				if (v8)
					v9 = v8;
				longName += v9;
				longName += "/";
			}
		} while (v6 < *(DWORD*)(thisptr + 80));
	}

	Msg("%2d (%d)%s%s::%s - %s\n", *(DWORD*)(thisptr + 32), *(DWORD*)(thisptr + 36), longName.c_str(), classname, fieldname, data);
	return 0;
}