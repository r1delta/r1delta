#pragma once
class CBaseFileSystem;
struct FileAsyncRequest_t;
__int64 __fastcall Hooked_CBaseFileSystem__SyncRead(CBaseFileSystem* filesystem,
    FileAsyncRequest_t* request);
extern __int64 (*Original_CBaseFileSystem__SyncRead)(
    CBaseFileSystem* filesystem,
    FileAsyncRequest_t* request);
void CFileAsyncReadJob_dtor(FileAsyncRequest_t* thisptr);
extern void (*Original_CFileAsyncReadJob_dtor)(FileAsyncRequest_t* thisptr);