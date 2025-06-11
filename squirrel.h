﻿#pragma once
#include <string>
#include "defs.h"
#include <vector>
enum ScriptContext
{
	SCRIPT_CONTEXT_SERVER,
	SCRIPT_CONTEXT_CLIENT,
	SCRIPT_CONTEXT_UI
};

#define sq_pushstring_std(vm, s) sq_pushstring((vm), (s).c_str(), (s).length())
#define sq_pushstring_lit(vm, S) sq_pushstring((vm), (S), sizeof(S) - 1)

#define SQ_SUCCEEDED(res) ((res)>=0)

#define SQTrue	(1)
#define SQFalse	(0)

typedef float SQFloat;
typedef long SQInteger;
typedef unsigned long SQUnsignedInteger;
typedef unsigned long SQHash;
typedef char SQChar;

typedef void* SQUserPointer;
typedef SQUnsignedInteger SQBool;
typedef SQInteger SQRESULT;

typedef struct SQVM* HSQUIRRELVM;
typedef void(*SQPRINTFUNCTION)(HSQUIRRELVM, const SQChar*, ...);
typedef SQInteger(*SQFUNCTION)(HSQUIRRELVM);

typedef SQInteger(*SQLEXREADFUNC)(SQUserPointer);

struct BufState {
	const SQChar* buf;
	SQInteger ptr;
	SQInteger size;
};

#pragma pack(push,1)
struct SQSharedState
{
	unsigned char unknownData[0x4158];
	SQPRINTFUNCTION _printfunc;
};

struct SQVM
{
	unsigned char unknownData[0xE8];
	SQSharedState* _sharedstate;
};
#pragma pack(pop)

#define _ss(_vm_) (_vm_)->_sharedstate

///////////////////////////////////////////////////////////////////////////////////////

typedef SQUnsignedInteger SQRawObjectVal;
#define SQ_OBJECT_RAWINIT()

#define SQOBJECT_REF_COUNTED	0x08000000
#define SQOBJECT_NUMERIC		0x04000000
#define SQOBJECT_DELEGABLE		0x02000000
#define SQOBJECT_CANBEFALSE		0x01000000

#define SQ_MATCHTYPEMASKSTRING (-99999)

#define _RT_MASK 0x00FFFFFF
#define _RAW_TYPE(type) ((type)&_RT_MASK)

#define _RT_NULL			0x00000001
#define _RT_INTEGER			0x00000002
#define _RT_FLOAT			0x00000004
#define _RT_BOOL			0x00000008
#define _RT_STRING			0x00000010
#define _RT_TABLE			0x00000020
#define _RT_ARRAY			0x00000040
#define _RT_USERDATA		0x00000080
#define _RT_CLOSURE			0x00000100
#define _RT_NATIVECLOSURE	0x00000200
#define _RT_GENERATOR		0x00000400
#define _RT_USERPOINTER		0x00000800
#define _RT_THREAD			0x00001000
#define _RT_FUNCPROTO		0x00002000
#define _RT_CLASS			0x00004000
#define _RT_INSTANCE		0x00008000
#define _RT_WEAKREF			0x00010000

#define ISREFCOUNTED(t) ((t)&SQOBJECT_REF_COUNTED)

typedef enum tagSQObjectType {
	OT_NULL = (_RT_NULL | SQOBJECT_CANBEFALSE),
	OT_INTEGER = (_RT_INTEGER | SQOBJECT_NUMERIC | SQOBJECT_CANBEFALSE),
	OT_FLOAT = (_RT_FLOAT | SQOBJECT_NUMERIC | SQOBJECT_CANBEFALSE),
	OT_BOOL = (_RT_BOOL | SQOBJECT_CANBEFALSE),
	OT_STRING = (_RT_STRING | SQOBJECT_REF_COUNTED),
	OT_TABLE = (_RT_TABLE | SQOBJECT_REF_COUNTED | SQOBJECT_DELEGABLE),
	OT_ARRAY = (_RT_ARRAY | SQOBJECT_REF_COUNTED),
	OT_USERDATA = (_RT_USERDATA | SQOBJECT_REF_COUNTED | SQOBJECT_DELEGABLE),
	OT_CLOSURE = (_RT_CLOSURE | SQOBJECT_REF_COUNTED),
	OT_NATIVECLOSURE = (_RT_NATIVECLOSURE | SQOBJECT_REF_COUNTED),
	OT_GENERATOR = (_RT_GENERATOR | SQOBJECT_REF_COUNTED),
	OT_USERPOINTER = _RT_USERPOINTER,
	OT_THREAD = (_RT_THREAD | SQOBJECT_REF_COUNTED),
	OT_FUNCPROTO = (_RT_FUNCPROTO | SQOBJECT_REF_COUNTED), //internal usage only
	OT_CLASS = (_RT_CLASS | SQOBJECT_REF_COUNTED),
	OT_INSTANCE = (_RT_INSTANCE | SQOBJECT_REF_COUNTED | SQOBJECT_DELEGABLE),
	OT_WEAKREF = (_RT_WEAKREF | SQOBJECT_REF_COUNTED)
} SQObjectType;

typedef union tagSQObjectValue // dunno?
{
	struct SQTable* pTable;
	struct SQArray* pArray;
	struct SQClosure* pClosure;
	struct SQGenerator* pGenerator;
	struct SQNativeClosure* pNativeClosure;
	struct SQString* pString;
	struct SQUserData* pUserData;
	SQInteger nInteger;
	SQFloat fFloat;
	SQUserPointer pUserPointer;
	struct SQFunctionProto* pFunctionProto;
	struct SQRefCounted* pRefCounted;
	struct SQDelegable* pDelegable;
	struct SQVM* pThread;
	struct SQClass* pClass;
	struct SQInstance* pInstance;
	struct SQWeakRef* pWeakRef;
	SQRawObjectVal raw;
} SQObjectValue;
//typedef tagSQObjectValue SQObjectValue; // not sure if we need this

typedef struct tagSQObject
{
	SQObjectType _type;
	SQObjectValue _unVal;
} SQObject;

typedef tagSQObject SQObject;

// TODO: do we need this? if not, delet
struct SQRefCounted
{
	SQRefCounted() { _uiRef = 0; _weakref = NULL; }
	virtual ~SQRefCounted();
	SQWeakRef* GetWeakRef(SQObjectType type);
	SQUnsignedInteger _uiRef;
	struct SQWeakRef* _weakref;
	virtual void Release() = 0;
};

struct alignas(8) SQString : public SQRefCounted
{
	int64 pad;
	int64 pad2;
	int length; //28
	char _hash[12]; 
	char _val[1];
};

struct SQWeakRef : SQRefCounted
{
	void Release();
	SQObject _obj;
};

struct SQCollectable : public SQRefCounted
{
	SQCollectable* _next;
	SQCollectable* _prev;
	SQSharedState* _sharedstate;
};

struct SQDelegable : public SQCollectable
{
	SQTable* _delegate;
};

struct alignas(8) SQTable : public SQDelegable
{
	struct _HashNode
	{
		SQObject val;
		SQObject key;
		_HashNode* next;
	};
	int64* pad;
	_HashNode* _nodes;
	int _numOfNodes;
	int size;
	int field_48;
	int _usedNodes;
};

struct SQArray : public SQCollectable
{
	SQObject* _values;
	int _usedSlots;
	int _allocated;
};

////

// UTILITY MACRO
#define sq_isnumeric(o) ((o)._type&SQOBJECT_NUMERIC)
#define sq_istable(o) ((o)._type==OT_TABLE)
#define sq_isarray(o) ((o)._type==OT_ARRAY)
#define sq_isfunction(o) ((o)._type==OT_FUNCPROTO)
#define sq_isclosure(o) ((o)._type==OT_CLOSURE)
#define sq_isgenerator(o) ((o)._type==OT_GENERATOR)
#define sq_isnativeclosure(o) ((o)._type==OT_NATIVECLOSURE)
#define sq_isstring(o) ((o)._type==OT_STRING)
#define sq_isinteger(o) ((o)._type==OT_INTEGER)
#define sq_isfloat(o) ((o)._type==OT_FLOAT)
#define sq_isuserpointer(o) ((o)._type==OT_USERPOINTER)
#define sq_isuserdata(o) ((o)._type==OT_USERDATA)
#define sq_isthread(o) ((o)._type==OT_THREAD)
#define sq_isnull(o) ((o)._type==OT_NULL)
#define sq_isclass(o) ((o)._type==OT_CLASS)
#define sq_isinstance(o) ((o)._type==OT_INSTANCE)
#define sq_isbool(o) ((o)._type==OT_BOOL)
#define sq_isweakref(o) ((o)._type==OT_WEAKREF)
#define sq_type(o) ((o)._type)

#define SQ_OK (0)
#define SQ_ERROR (-1)

#define SQ_FAILED(res) ((res)<0)
#define SQ_SUCCEEDED(res) ((res)>=0)

///////////////////////////////////////////////////////////////////////////////////////

struct R1SquirrelVM
{
	unsigned char vtbl[8];
	HSQUIRRELVM sqvm;
};

struct SQFuncRegistrationInternal {
	const char* squirrelFuncName; //0x0000 
	const char* cppFuncName; //0x0008 
	const char* helpText; //0x0010 

	// stuff for flags&2
	// first parameter here is "this", params counter includes it
	// nparamscheck 0 = don't check params, number = check for exact number of params, -number = check for at least abs(number) params
	// . = any; o = null (?); i = integer; f = float; n = integer or float; s = string; t = table; a = array; u = userdata; c = closure/nativeclosure; g = generator; p = userpointer; v = thread; x = class instance; y = class; b = bool
	// at least those are supported for sure: nxbs. (number, instance, boolean, string, any)
	// [untested] #define SQ_MATCHTYPEMASKSTRING (-99999) = If SQ_MATCHTYPEMASKSTRING is passed instead of the number of parameters, the function will automatically extrapolate the number of parameters to check from the typemask(eg. if the typemask is ".sn" is like passing 3).
	const char* szTypeMask; //0x0018 can be: .s, .ss., .., ..  
	__int64 nparamscheck_probably; //0x0020 can be: 2, -3, 2, 0  

	const char* returnValueTypeText; //0x0028 
	const char* argNamesText; //0x0030 

	__int64 UnkSeemsToAlwaysBe32; //0x0038 
	char pad_0x0040[0x20]; //0x0040 // CUtlVector of parameter types, can't be arsed to reverse this
	void* pfnBinding; //0x0068 
	void* pFunction; //0x0070 
	__int64 flags; //0x0078 // if it's 2, then the CUtlVector mentioned above will not be used

	SQFuncRegistrationInternal()
	{
		memset(this, 0, sizeof(SQFuncRegistrationInternal));
		this->UnkSeemsToAlwaysBe32 = 32;
	}
};

inline const char* empty_str = "";
inline __int64 __fastcall SQFuncBindingFn(
	__int64(__fastcall* a1)(const char*),
	__int64 a2,
	const char** a3,
	__int64 a4,
	__int64 a5)
{
	const char* v6; // rcx
	__int64 result; // rax

	v6 = empty_str;
	if (*a3)
		v6 = *a3;
	result = a1(v6);
	*(_WORD*)(a5 + 8) = 32;
	*(_QWORD*)a5 = result;
	return result;
}
class SQFuncRegistration
{
public:
	SQFuncRegistration(
		ScriptContext context,
		const std::string& name,
		SQFUNCTION funcPtr,
		const std::string& szTypeMask,
		int nParamsCheck,
		const std::string& returnType,
		const std::string& argNames,
		const std::string& helpText
	) :
		m_context(context),
		m_funcName(name),
		m_szTypeMask(szTypeMask),
		m_retValueType(returnType),
		m_argNames(argNames),
		m_helpText(helpText)
	{
		m_internalReg.squirrelFuncName = m_internalReg.cppFuncName = m_funcName.c_str();
		m_internalReg.helpText = m_helpText.c_str();
		m_internalReg.szTypeMask = m_szTypeMask.c_str();
		m_internalReg.nparamscheck_probably = nParamsCheck;
		m_internalReg.returnValueTypeText = m_retValueType.c_str();
		m_internalReg.argNamesText = m_argNames.c_str();
		m_internalReg.pfnBinding = SQFuncBindingFn;
		m_internalReg.pFunction = funcPtr;
		m_internalReg.flags = 2;
	}

	ScriptContext GetContext() const
	{
		return m_context;
	}

	SQFuncRegistrationInternal* GetInternalReg()
	{
		return &m_internalReg;
	}

	const std::string& GetName() const
	{
		return m_funcName;
	}

private:
	ScriptContext m_context;
	std::string m_funcName;
	std::string m_helpText;
	std::string m_szTypeMask;
	std::string m_retValueType;
	std::string m_argNames;
	SQFuncRegistrationInternal m_internalReg;
};


typedef void* (*CScriptManager__CreateNewVMType)(__int64 a1, int a2, unsigned int a3);
extern CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;
void* CScriptManager__CreateNewVM(__int64 a1, int a2, unsigned int a3);
typedef __int64 (*CScriptVM__ctortype)(void* thisptr);
extern CScriptVM__ctortype CScriptVM__ctororiginal;
__int64 __fastcall CScriptVM__ctor(void* thisptr);
typedef void* (*CScriptVM__GetUnknownVMPtrType)();
extern CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;
void* CScriptVM__GetUnknownVMPtr();
typedef __int64 (*sub_180008AB0Type)(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
extern sub_180008AB0Type sub_180008AB0Original;
__int64 __fastcall sub_180008AB0(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
typedef void (*CScriptManager__DestroyVMType)(void* a1, void* vmptr);
extern CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;
void __fastcall CScriptManager__DestroyVM(void* a1, void* vmptr);
struct ScriptVariant_t;
typedef void (*CSquirrelVM__RegisterFunctionGutsType)(__int64* a1, __int64 a2, const char** a3);
extern CSquirrelVM__RegisterFunctionGutsType CSquirrelVM__RegisterFunctionGutsOriginal;
typedef __int64 (*CSquirrelVM__PushVariantType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__PushVariantType CSquirrelVM__PushVariantOriginal;
typedef char (*CSquirrelVM__ConvertToVariantType)(__int64* a1, __int64 a2, ScriptVariant_t* a3);
extern CSquirrelVM__ConvertToVariantType CSquirrelVM__ConvertToVariantOriginal;
typedef __int64 (*CSquirrelVM__ReleaseValueType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__ReleaseValueType CSquirrelVM__ReleaseValueOriginal;
typedef bool (*CSquirrelVM__SetValueType)(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueType CSquirrelVM__SetValueOriginal;
typedef bool (*CSquirrelVM__SetValueExType)(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueExType CSquirrelVM__SetValueExOriginal;
typedef __int64 (*CSquirrelVM__TranslateCallType)(__int64* a1);
extern CSquirrelVM__TranslateCallType CSquirrelVM__TranslateCallOriginal;
void __fastcall CSquirrelVM__TranslateCall(__int64* a1);
void __fastcall CSquirrelVM__RegisterFunctionGuts(__int64* a1, __int64 a2, const char** a3);
__int64 __fastcall CSquirrelVM__PushVariant(__int64* a1, ScriptVariant_t* a2);
char __fastcall CSquirrelVM__ConvertToVariant(__int64* a1, __int64 a2, ScriptVariant_t* a3);
__int64 __fastcall CSquirrelVM__ReleaseValue(__int64* a1, ScriptVariant_t* a2);
bool __fastcall CSquirrelVM__SetValue(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
bool __fastcall CSquirrelVM__SetValueEx(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
__declspec(dllexport) R1SquirrelVM* GetServerVMPtr();

void CSquirrelVM__PrintFunc1(void* m_hVM, const char* s, ...);
void CSquirrelVM__PrintFunc2(void* m_hVM, const char* s, ...);
void CSquirrelVM__PrintFunc3(void* m_hVM, const char* s, ...);
void script_cmd(const CCommand& args);
void script_client_cmd(const CCommand& args);
void script_ui_cmd(const CCommand& args);

void localilze_string(const char* str, char* localized_str, int size);

enum SVFlags_t
{
	SV_FREE = 0x01,
	SV_IHAVENOFUCKINGCLUE = 0x02,
	// Start from the most significant bit for the new flags
	SV_CONVERTED_TO_R1 = 0x1000,
	SV_CONVERTED_TO_R1O = 0x2000,
};

struct __declspec(align(8)) ScriptVariant_t
{
	union
	{
		int             m_int;
		float           m_float;
		const char* m_pszString;
		const float* m_pVector;
		char            m_char;
		bool            m_bool;
		void* m_hScript;
	};

	int16               m_type;
	int16               m_flags;
};

typedef enum {
	R1_TO_R1O,
	R1O_TO_R1
} ConversionDirection;

typedef SQRESULT(*sq_compile_t)(HSQUIRRELVM, SQLEXREADFUNC, SQUserPointer, const SQChar*, SQBool);
typedef SQRESULT(*sq_compilebuffer_t)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
typedef __int64(__fastcall* base_getroottable_t)(HSQUIRRELVM);
typedef SQRESULT(*sq_call_t)(HSQUIRRELVM, SQInteger, SQBool, SQBool);
typedef SQRESULT(*sq_newslot_t)(HSQUIRRELVM, SQInteger, SQBool);
typedef void (*SQVM_Pop_t)(HSQUIRRELVM, SQInteger);
typedef void (*sq_push_t)(HSQUIRRELVM, SQInteger);
typedef void (*SQVM_Raise_Error_t)(HSQUIRRELVM, const SQChar*, ...);
typedef SQChar* (__fastcall* IdType2Name_t)(SQObjectType);
typedef SQRESULT(*sq_getstring_t)(HSQUIRRELVM, SQInteger, const SQChar**);
typedef SQRESULT(*sq_getinteger_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQInteger*);
typedef SQRESULT(*sq_getfloat_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQFloat*);
typedef SQRESULT(*sq_getbool_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQBool*);
typedef void (*sq_pushnull_t)(HSQUIRRELVM);
typedef void (*sq_pushstring_t)(HSQUIRRELVM, const SQChar*, SQInteger);
typedef void (*sq_pushinteger_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger);
typedef void (*sq_pushfloat_t)(R1SquirrelVM*, HSQUIRRELVM, SQFloat);
typedef void (*sq_pushbool_t)(R1SquirrelVM*, HSQUIRRELVM, SQBool);
typedef void (*sq_tostring_t)(HSQUIRRELVM, SQInteger);
typedef SQInteger(*sq_getsize_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger);
typedef SQObjectType(*sq_gettype_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_getstackobj_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQObject*);
typedef SQRESULT(*sq_get_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_get_noerr_t)(HSQUIRRELVM, SQInteger);
typedef SQInteger(*sq_gettop_t)(R1SquirrelVM*, HSQUIRRELVM);
typedef void (*sq_newtable_t)(HSQUIRRELVM);
typedef SQRESULT (*sq_get_table_t)(HSQUIRRELVM, SQInteger,SQTable*);
typedef SQRESULT(*sq_next_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_getinstanceup_t)(HSQUIRRELVM, SQInteger, SQUserPointer*, SQUserPointer);
typedef void (*sq_newarray_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_arrayappend_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_throwerror_t)(HSQUIRRELVM, const char* err);
typedef bool (*RunCallback_t)(R1SquirrelVM*, const char*);
typedef __int64 (*CSquirrelVM__RegisterGlobalConstantInt_t)(R1SquirrelVM*, const char*, signed int);
typedef void* (*CSquirrelVM__GetEntityFromInstance_t)(R1SquirrelVM*, SQObject*, char**);
typedef char** (*sq_GetEntityConstant_CBaseEntity_t)();
typedef int64_t(*AddSquirrelReg_t)(R1SquirrelVM*, SQFuncRegistrationInternal*);

// Global variables for all the functions
extern sq_compile_t sq_compile;
extern sq_compilebuffer_t sq_compilebuffer;
extern base_getroottable_t base_getroottable;
extern sq_call_t sq_call;
extern sq_newslot_t sq_newslot;
extern SQVM_Pop_t sq_pop;
extern sq_push_t sq_push;
extern SQVM_Raise_Error_t SQVM_Raise_Error;
extern IdType2Name_t IdType2Name;
extern sq_getstring_t sq_getstring;
extern sq_getinteger_t sq_getinteger;
extern sq_getfloat_t sq_getfloat;
extern sq_getbool_t sq_getbool;
extern sq_pushnull_t sq_pushnull;
extern sq_pushstring_t sq_pushstring;
extern sq_pushinteger_t sq_pushinteger;
extern sq_pushfloat_t sq_pushfloat;
extern sq_pushbool_t sq_pushbool;
extern sq_tostring_t sq_tostring;
extern sq_getsize_t sq_getsize;
extern sq_gettype_t sq_gettype;
extern sq_getstackobj_t sq_getstackobj;
extern sq_get_t sq_get;
extern sq_get_noerr_t sq_get_noerr;
extern sq_gettop_t sq_gettop;
extern sq_newtable_t sq_newtable;
extern sq_next_t sq_next;
extern sq_getinstanceup_t sq_getinstanceup;
extern sq_newarray_t sq_newarray;
extern sq_arrayappend_t sq_arrayappend;
extern sq_throwerror_t sq_throwerror;
extern RunCallback_t RunCallback;
extern CSquirrelVM__RegisterGlobalConstantInt_t CSquirrelVM__RegisterGlobalConstantInt;
extern CSquirrelVM__GetEntityFromInstance_t CSquirrelVM__GetEntityFromInstance;
extern sq_GetEntityConstant_CBaseEntity_t sq_GetEntityConstant_CBaseEntity; // CLIENT
extern AddSquirrelReg_t AddSquirrelReg;
extern void* sq_getentity(HSQUIRRELVM v, SQInteger iStackPos);
extern std::vector<std::string> modLocalization_files;

#define REGISTER_SCRIPT_FUNCTION(context, name, func, typeMask, paramsCheck, returnType, argNames, helpText) \
    ScriptFunctionRegistry::getInstance().addFunction(std::make_unique<SQFuncRegistration>( \
        static_cast<ScriptContext>(context), name, func, typeMask, paramsCheck, returnType, argNames, helpText \
    ))