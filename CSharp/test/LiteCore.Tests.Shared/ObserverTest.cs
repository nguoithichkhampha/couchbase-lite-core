// 
//  ObserverTest.cs
// 
//  Copyright (c) 2017 Couchbase, Inc All rights reserved.
// 
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
// 
//  http://www.apache.org/licenses/LICENSE-2.0
// 
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// 
using System.Collections.Generic;
using FluentAssertions;
using LiteCore.Interop;
#if !WINDOWS_UWP
using Xunit;
using Xunit.Abstractions;
#else
using Fact = Microsoft.VisualStudio.TestTools.UnitTesting.TestMethodAttribute;
#endif

namespace LiteCore.Tests
{
#if WINDOWS_UWP
    [Microsoft.VisualStudio.TestTools.UnitTesting.TestClass]
#endif
    public unsafe class ObserverTest : Test
    {
        private DatabaseObserver _dbObserver;
        private DocumentObserver _docObserver;

        private int _dbCallbackCalls;

        private int _docCallbackCalls;

        protected override int NumberOfOptions 
        {
            get {
                return 1;
            }
        }

#if !WINDOWS_UWP
        public ObserverTest(ITestOutputHelper output) : base(output)
        {

        }
#endif

        [Fact]
        public void TestDBObserver()
        {
            RunTestVariants(() => {
                _dbObserver = Native.c4dbobs_create(Db, DBObserverCallback, this);
                CreateRev("A", C4Slice.Constant("1-aa"), Body);
                _dbCallbackCalls.Should().Be(1, "because we should have received a callback");
                CreateRev("B", C4Slice.Constant("1-bb"), Body);
                _dbCallbackCalls.Should().Be(1, "because we should have received a callback");

                CheckChanges(new[] { "A", "B" }, new[] { "1-aa", "1-bb" });

                CreateRev("B", C4Slice.Constant("2-bbbb"), Body);
                _dbCallbackCalls.Should().Be(2, "because we should have received a callback");
                CreateRev("C", C4Slice.Constant("1-cc"), Body);
                _dbCallbackCalls.Should().Be(2, "because we should have received a callback");

                 CheckChanges(new[] { "B", "C" }, new[] { "2-bbbb", "1-cc" });
                 _dbObserver.Dispose();
                 _dbObserver = null;

                 CreateRev("A", C4Slice.Constant("2-aaaa"), Body);
                 _dbCallbackCalls.Should().Be(2, "because the observer was disposed");
            });
        }

        [Fact]
        public void TestDocObserver()
        {
            RunTestVariants(() => {
                CreateRev("A", C4Slice.Constant("1-aa"), Body);
                _docObserver = Native.c4docobs_create(Db, "A", DocObserverCallback, this);
                
                CreateRev("A", C4Slice.Constant("2-bb"), Body);
                CreateRev("B", C4Slice.Constant("1-bb"), Body);
                _docCallbackCalls.Should().Be(1, "because there was only one update to the doc in question");
            });
        }

        [Fact]
        public void TestMultiDbObserver()
        {
            RunTestVariants(() => {
                _dbObserver = Native.c4dbobs_create(Db, DBObserverCallback, this);
                CreateRev("A", C4Slice.Constant("1-aa"), Body);
                _dbCallbackCalls.Should().Be(1, "because we should have received a callback");
                CreateRev("B", C4Slice.Constant("1-bb"), Body);
                _dbCallbackCalls.Should().Be(1, "because we should have received a callback");

                CheckChanges(new[] { "A", "B" }, new[] { "1-aa", "1-bb" });

                // Open another database on the same file
                var otherdb = (C4Database *)LiteCoreBridge.Check(err => Native.c4db_open(DatabasePath(), Native.c4db_getConfig(Db), err));
                LiteCoreBridge.Check(err => Native.c4db_beginTransaction(otherdb, err));
                try {
                    CreateRev(otherdb, "C", C4Slice.Constant("1-cc"), Body);
                    CreateRev(otherdb, "D", C4Slice.Constant("1-dd"), Body);
                    CreateRev(otherdb, "E", C4Slice.Constant("1-ee"), Body);
                } finally {
                    LiteCoreBridge.Check(err => Native.c4db_endTransaction(otherdb, true, err));
                }

                _dbCallbackCalls.Should().Be(2, "because the observer should cover all connections");

                CheckChanges(new[] { "C", "D", "E" }, new[] { "1-cc", "1-dd", "1-ee" }, true);
                _dbObserver.Dispose();
                _dbObserver = null;

                CreateRev("A", C4Slice.Constant("2-aaaa"), Body);
                _dbCallbackCalls.Should().Be(2, "because the observer was disposed");

                LiteCoreBridge.Check(err => Native.c4db_close(otherdb, err));
                Native.c4db_free(otherdb);
            });
        }

        private void CheckChanges(IList<string> expectedDocIDs, IList<string> expectedRevIDs, bool expectedExternal = false)
        {
            var changes = new C4DatabaseChange[100];
            bool external;
            var changeCount = Native.c4dbobs_getChanges(_dbObserver.Observer, changes, 100, &external);
            changeCount.Should().Be((uint)expectedDocIDs.Count, "because otherwise we didn't get the correct number of changes");
            for(int i = 0; i < changeCount; i++) {
                changes[i].docID.CreateString().Should().Be(expectedDocIDs[i], "because otherwise we have an invalid document ID");
                changes[i].revID.CreateString().Should().Be(expectedRevIDs[i], "because otherwise we have an invalid document revision ID");
            }

            external.Should().Be(expectedExternal, "because otherwise the external parameter was wrong");
        }

        private static void DBObserverCallback(C4DatabaseObserver* obs, object context)
        {
            ((ObserverTest)context).DbObserverCalled(obs);
        }

        private static void DocObserverCallback(C4DocumentObserver* obs, string docID, ulong sequence, object context)
        {
            ((ObserverTest)context).DocObserverCalled(obs, docID, sequence);
        }

        private void DbObserverCalled(C4DatabaseObserver *obs)
        {
            ((long)obs).Should().Be((long)_dbObserver.Observer, "because the callback should be for the proper DB");
            _dbCallbackCalls++;
        }

        private void DocObserverCalled(C4DocumentObserver *obs, string docID, ulong sequence)
        {
            ((long)obs).Should().Be((long)_docObserver.Observer, "because the callback should be for the proper DB");
            _docCallbackCalls++;
        }

        protected override void SetupVariant(int option)
        {
            base.SetupVariant(option);

            _dbCallbackCalls = 0;
            _docCallbackCalls = 0;
        }

        protected override void TeardownVariant(int option)
        {
            _dbObserver?.Dispose();
            _dbObserver = null;
            _docObserver?.Dispose();
            _docObserver = null;

            base.TeardownVariant(option);
        }
    }
}