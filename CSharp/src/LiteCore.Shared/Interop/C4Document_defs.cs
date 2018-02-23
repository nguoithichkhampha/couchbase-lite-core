//
// C4Document_defs.cs
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
using System.Threading;

using LiteCore.Util;

namespace LiteCore.Interop
{
    [Flags]
#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    enum C4DocumentFlags : uint
    {
        DocDeleted         = 0x01,
        DocConflicted      = 0x02,
        DocHasAttachments  = 0x04,
        DocExists          = 0x1000
    }

    [Flags]
#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    enum C4RevisionFlags : byte
    {
        Deleted        = 0x01,
        Leaf           = 0x02,
        New            = 0x04,
        HasAttachments = 0x08,
        KeepBody       = 0x10,
        IsConflict     = 0x20,
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4Revision
    {
        public C4Slice revID;
        public C4RevisionFlags flags;
        public ulong sequence;
        public C4Slice body;
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4DocPutRequest
    {
        public C4Slice body;
        public C4Slice docID;
        public C4RevisionFlags revFlags;
        private byte _existingRevision;
        private byte _allowConflict;
        public C4Slice* history;
        private UIntPtr _historyCount;
        private byte _save;
        public uint maxRevTreeDepth;
        public uint remoteDBID;

        public bool existingRevision
        {
            get {
                return Convert.ToBoolean(_existingRevision);
            }
            set {
                _existingRevision = Convert.ToByte(value);
            }
        }

        public bool allowConflict
        {
            get {
                return Convert.ToBoolean(_allowConflict);
            }
            set {
                _allowConflict = Convert.ToByte(value);
            }
        }

        public ulong historyCount
        {
            get {
                return _historyCount.ToUInt64();
            }
            set {
                _historyCount = (UIntPtr)value;
            }
        }

        public bool save
        {
            get {
                return Convert.ToBoolean(_save);
            }
            set {
                _save = Convert.ToByte(value);
            }
        }
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4Document
    {
        public C4DocumentFlags flags;
        public C4Slice docID;
        public C4Slice revID;
        public ulong sequence;
        public C4Revision selectedRev;
    }
}