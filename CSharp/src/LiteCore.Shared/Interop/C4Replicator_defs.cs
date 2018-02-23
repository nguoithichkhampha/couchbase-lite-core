//
// C4Replicator_defs.cs
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
#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    enum C4ReplicatorMode : int
    {
        Disabled,
        Passive,
        OneShot,
        Continuous
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    enum C4ReplicatorActivityLevel : int
    {
        Stopped,
        Offline,
        Connecting,
        Idle,
        Busy
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe partial struct C4ReplicatorParameters
    {
        public C4ReplicatorMode push;
        public C4ReplicatorMode pull;
        public C4Slice optionsDictFleece;
        private IntPtr validationFunc;
        private IntPtr onStatusChanged;
        private IntPtr onDocumentError;
        public void* callbackContext;
    }


#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4Progress
    {
        public ulong unitsCompleted;
        public ulong unitsTotal;
        public ulong documentCount;
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4Replicator
    {
    }

#if LITECORE_PACKAGED
    internal
#else
    public
#endif
    unsafe struct C4ReplicatorStatus
    {
        public C4ReplicatorActivityLevel level;
        public C4Progress progress;
        public C4Error error;
    }
}