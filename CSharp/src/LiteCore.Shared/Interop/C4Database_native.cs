//
// C4Database_native.cs
//
// Copyright (c) 2018 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

using System;
using System.Linq;
using System.Runtime.InteropServices;

using LiteCore.Util;

namespace LiteCore.Interop
{
#if LITECORE_PACKAGED
    internal
#else
    public
#endif 
    unsafe static partial class Native
    {
        public static C4Database* c4db_open(string path, C4DatabaseConfig* config, C4Error* outError)
        {
            using(var path_ = new C4String(path)) {
                return NativeRaw.c4db_open(path_.AsC4Slice(), config, outError);
            }
        }

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4Database* c4db_openAgain(C4Database* db, C4Error* outError);

        public static bool c4db_copy(string sourcePath, string destinationPath, C4DatabaseConfig* config, C4Error* error)
        {
            using(var sourcePath_ = new C4String(sourcePath))
            using(var destinationPath_ = new C4String(destinationPath)) {
                return NativeRaw.c4db_copy(sourcePath_.AsC4Slice(), destinationPath_.AsC4Slice(), config, error);
            }
        }

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4Database* c4db_retain(C4Database* db);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_free(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_close(C4Database* database, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_delete(C4Database* database, C4Error* outError);

        public static bool c4db_deleteAtPath(string dbPath, C4Error* outError)
        {
            using(var dbPath_ = new C4String(dbPath)) {
                return NativeRaw.c4db_deleteAtPath(dbPath_.AsC4Slice(), outError);
            }
        }

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_rekey(C4Database* database, C4EncryptionKey* newKey, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4_shutdown(C4Error* outError);

        public static string c4db_getPath(C4Database* db)
        {
            using(var retVal = NativeRaw.c4db_getPath(db)) {
                return ((C4Slice)retVal).CreateString();
            }
        }

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4DatabaseConfig* c4db_getConfig(C4Database* db);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong c4db_getDocumentCount(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong c4db_getLastSequence(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong c4db_nextDocExpiration(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint c4db_getMaxRevTreeDepth(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void c4db_setMaxRevTreeDepth(C4Database* database, uint maxRevTreeDepth);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_getUUIDs(C4Database* database, C4UUID* publicUUID, C4UUID* privateUUID, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_compact(C4Database* database, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_beginTransaction(C4Database* database, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_endTransaction(C4Database* database, [MarshalAs(UnmanagedType.U1)]bool commit, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_isInTransaction(C4Database* database);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void c4raw_free(C4RawDocument* rawDoc);

        public static C4RawDocument* c4raw_get(C4Database* database, string storeName, string docID, C4Error* outError)
        {
            using(var storeName_ = new C4String(storeName))
            using(var docID_ = new C4String(docID)) {
                return NativeRaw.c4raw_get(database, storeName_.AsC4Slice(), docID_.AsC4Slice(), outError);
            }
        }

        public static bool c4raw_put(C4Database* database, string storeName, string key, string meta, string body, C4Error* outError)
        {
            using(var storeName_ = new C4String(storeName))
            using(var key_ = new C4String(key))
            using(var meta_ = new C4String(meta))
            using(var body_ = new C4String(body)) {
                return NativeRaw.c4raw_put(database, storeName_.AsC4Slice(), key_.AsC4Slice(), meta_.AsC4Slice(), body_.AsC4Slice(), outError);
            }
        }


    }
    
#if LITECORE_PACKAGED
    internal
#else
    public
#endif 
    unsafe static partial class NativeRaw
    {
        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4Database* c4db_open(C4Slice path, C4DatabaseConfig* config, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_copy(C4Slice sourcePath, C4Slice destinationPath, C4DatabaseConfig* config, C4Error* error);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4db_deleteAtPath(C4Slice dbPath, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4SliceResult c4db_getPath(C4Database* db);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern C4RawDocument* c4raw_get(C4Database* database, C4Slice storeName, C4Slice docID, C4Error* outError);

        [DllImport(Constants.DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool c4raw_put(C4Database* database, C4Slice storeName, C4Slice key, C4Slice meta, C4Slice body, C4Error* outError);


    }
}
