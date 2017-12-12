//
//  ReplicatorLoopbackTest.cc
//  LiteCore
//
//  Created by Jens Alfke on 2/20/17.
//  Copyright © 2017 Couchbase. All rights reserved.
//

#include "ReplicatorLoopbackTest.hh"
#include "Timer.hh"
#include <chrono>

using namespace litecore::actor;

constexpr duration ReplicatorLoopbackTest::kLatency;
constexpr duration ReplicatorLoopbackTest::kCheckpointSaveDelay;

TEST_CASE_METHOD(ReplicatorLoopbackTest, "Fire Timer At Same Time", "[Push][Pull]") {
    int counter = 0;
    Timer t1([&counter] { 
        counter++; 
    });

    Timer t2([&counter] {
        counter++;
    });
    auto at = chrono::steady_clock::now() + chrono::milliseconds(500);
    t1.fireAt(at);
    t2.fireAt(at);

    this_thread::sleep_for(chrono::milliseconds(600));
    CHECK(counter == 2);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push Empty DB", "[Push]") {
    runPushReplication();
    compareDatabases();
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push Small Non-Empty DB", "[Push]") {
    importJSONLines(sFixturesDir + "names_100.json");
    _expectedDocumentCount = 100;
    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":100}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push Empty Docs", "[Push]") {
    Encoder enc;
    enc.beginDict();
    enc.endDict();
    alloc_slice body = enc.finish();
    createRev("doc"_sl, kRevID, body);
    _expectedDocumentCount = 1;

    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":1}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push large docs", "[Push]") {
    importJSONLines(sFixturesDir + "wikipedia_100.json");
    _expectedDocumentCount = 100;
    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":100}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Incremental Push", "[Push]") {
    importJSONLines(sFixturesDir + "names_100.json");
    _expectedDocumentCount = 100;
    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":100}");

    Log("-------- Second Replication --------");
    createRev("new1"_sl, kRev2ID, kFleeceBody);
    createRev("new2"_sl, kRev3ID, kFleeceBody);
    _expectedDocumentCount = 2;

    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":102}", "2-cc");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push large database", "[Push]") {
    importJSONLines(sFixturesDir + "iTunesMusicLibrary.json");
    _expectedDocumentCount = 12189;
    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":12189}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push large database no-conflicts", "[Push][NoConflicts]") {
    auto serverOpts = Replicator::Options::passive();
    serverOpts.setProperty(C4STR(kC4ReplicatorOptionNoConflicts), "true"_sl);

    importJSONLines(sFixturesDir + "iTunesMusicLibrary.json");
    _expectedDocumentCount = 12189;
    runReplicators(Replicator::Options::pushing(kC4OneShot), serverOpts);
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":12189}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull large database no-conflicts", "[Pull][NoConflicts]") {
    auto serverOpts = Replicator::Options::passive();
    serverOpts.setProperty(C4STR(kC4ReplicatorOptionNoConflicts), "true"_sl);

    importJSONLines(sFixturesDir + "iTunesMusicLibrary.json");
    _expectedDocumentCount = 12189;
    runReplicators(serverOpts, Replicator::Options::pulling(kC4OneShot));
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":12189}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Empty DB", "[Pull]") {
    runPullReplication();
    compareDatabases();
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Small Non-Empty DB", "[Pull]") {
    importJSONLines(sFixturesDir + "names_100.json");
    _expectedDocumentCount = 100;
    runPullReplication();
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":100}");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Incremental Pull", "[Pull]") {
    importJSONLines(sFixturesDir + "names_100.json");
    _expectedDocumentCount = 100;
    runPullReplication();
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":100}");

    Log("-------- Second Replication --------");
    createRev("new1"_sl, kRev2ID, kFleeceBody);
    createRev("new2"_sl, kRev3ID, kFleeceBody);
    _expectedDocumentCount = 2;

    runPullReplication();
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":102}", "2-cc");
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push/Pull Active Only", "[Pull]") {
    // Add 100 docs, then delete 50 of them:
    importJSONLines(sFixturesDir + "names_100.json");
    for (unsigned i = 1; i <= 100; i += 2) {
        char docID[20];
        sprintf(docID, "%07u", i);
        createRev(slice(docID), kRev2ID, nullslice, kRevDeleted); // delete it
    }
    _expectedDocumentCount = 50;

    auto pushOpt = Replicator::Options::passive();
    auto pullOpt = Replicator::Options::passive();
    bool pull = false, skipDeleted = false;

    SECTION("Pull") {
        // Pull replication. skipDeleted is automatic because destination is empty.
        pull = true;
        pullOpt = Replicator::Options::pulling();
        skipDeleted = true;
        //pullOpt.setProperty(slice(kC4ReplicatorOptionSkipDeleted), "true"_sl);
    }
    SECTION("Push") {
        // Push replication. skipDeleted is not automatic, so test both ways:
        pushOpt = Replicator::Options::pushing();
        SECTION("Push + SkipDeleted") {
            skipDeleted = true;
            pushOpt.setProperty(slice(kC4ReplicatorOptionSkipDeleted), "true"_sl);
        }
    }

    runReplicators(pushOpt, pullOpt);
    compareDatabases();

    if (pull)
        validateCheckpoints(db2, db, "{\"remote\":100}");
    else
        validateCheckpoints(db, db2, "{\"local\":100}");

    // If skipDeleted was used, ensure only 50 revisions got created (no tombstones):
    CHECK(c4db_getLastSequence(db2) == (skipDeleted ?50 : 100));
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push With Existing Key", "[Push]") {
    // Add a doc to db2; this adds the keys "name" and "gender" to the SharedKeys:
    {
        TransactionHelper t(db2);
        C4Error c4err;
        alloc_slice body = c4db_encodeJSON(db2, "{\"name\":\"obo\", \"gender\":-7}"_sl, &c4err);
        REQUIRE(body.buf);
        createRev(db2, "another"_sl, kRevID, body);
    }

    // Import names_100.json into db:
    importJSONLines(sFixturesDir + "names_100.json");
    _expectedDocumentCount = 100;

    // Push db into db2:
    runPushReplication();
    compareDatabases(true);
    validateCheckpoints(db, db2, "{\"local\":100}");

    // Get one of the pushed docs from db2 and look up "gender":
    c4::ref<C4Document> doc = c4doc_get(db2, "0000001"_sl, true, nullptr);
    REQUIRE(doc);
    Dict root = Value::fromData(doc->selectedRev.body).asDict();
    Value gender = root.get("gender"_sl, c4db_getFLSharedKeys(db2));
    REQUIRE(gender != nullptr);
    REQUIRE(gender.asstring() == "female");
}


#pragma mark - CONTINUOUS:


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Continuous Push Of Tiny DB", "[Push][Continuous]") {
    createRev(db, "doc1"_sl, "1-11"_sl, kFleeceBody);
    createRev(db, "doc2"_sl, "1-aa"_sl, kFleeceBody);
    _expectedDocumentCount = 2;

    _stopOnIdle = true;
    auto pushOpt = Replicator::Options::pushing(kC4Continuous);
    pushOpt.setProperty(slice(kC4ReplicatorCheckpointInterval), 1);
    runReplicators(pushOpt, Replicator::Options::passive());
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Continuous Pull Of Tiny DB", "[Pull][Continuous]") {
    createRev(db, "doc1"_sl, "1-11"_sl, kFleeceBody);
    createRev(db, "doc2"_sl, "1-aa"_sl, kFleeceBody);
    _expectedDocumentCount = 2;

    _stopOnIdle = true;
    auto pullOpt = Replicator::Options::pulling(kC4Continuous);
    pullOpt.setProperty(slice(kC4ReplicatorCheckpointInterval), 1);
    runReplicators(Replicator::Options::passive(), pullOpt);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Continuous Push Starting Empty", "[Push][Continuous]") {
    addDocsInParallel(chrono::milliseconds(1500), 6);
    runPushReplication(kC4Continuous);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Continuous Pull Starting Empty", "[Pull][Continuous]") {
    addDocsInParallel(chrono::milliseconds(1500), 6);
    runPullReplication(kC4Continuous);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Continuous Fast Push", "[Push][Continuous]") {
    addDocsInParallel(chrono::milliseconds(100), 5000);
    runPushReplication(kC4Continuous);
    //FIX: Stop this when bg thread stops adding docs
}


#pragma mark - ATTACHMENTS:


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push Attachments", "[Push][blob]") {
    vector<string> attachments = {"Hey, this is an attachment!", "So is this", ""};
    vector<C4BlobKey> blobKeys;
    {
        TransactionHelper t(db);
        blobKeys = addDocWithAttachments("att1"_sl, attachments, "text/plain");
        _expectedDocumentCount = 1;
    }
    runPushReplication();
    compareDatabases();
    validateCheckpoints(db, db2, "{\"local\":1}");

    checkAttachments(db2, blobKeys, attachments);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Attachments", "[Pull][blob]") {
    vector<string> attachments = {"Hey, this is an attachment!", "So is this", ""};
    vector<C4BlobKey> blobKeys;
    {
        TransactionHelper t(db);
        blobKeys = addDocWithAttachments("att1"_sl, attachments, "text/plain");
        _expectedDocumentCount = 1;
    }
    runPullReplication();
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":1}");

    checkAttachments(db2, blobKeys, attachments);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Large Attachments", "[Pull][blob]") {
    string att1(100000, '!');
    string att2( 80000, '?');
    string att3(110000, '/');
    string att4(  3000, '.');
    vector<string> attachments = {att1, att2, att3, att4};
    vector<C4BlobKey> blobKeys;
    {
        TransactionHelper t(db);
        blobKeys = addDocWithAttachments("att1"_sl, attachments, "text/plain");
        _expectedDocumentCount = 1;
    }
    runPullReplication();
    compareDatabases();
    validateCheckpoints(db2, db, "{\"remote\":1}");

    checkAttachments(db2, blobKeys, attachments);
}


#pragma mark - FILTERS & VALIDATION:


TEST_CASE_METHOD(ReplicatorLoopbackTest, "DocID Filtered Replication", "[Push][Pull]") {
    importJSONLines(sFixturesDir + "names_100.json");

    Encoder enc;
    enc.beginDict();
    enc.writeKey(C4STR(kC4ReplicatorOptionDocIDs));
    enc.beginArray();
    enc.writeString("0000001"_sl);
    enc.writeString("0000010"_sl);
    enc.writeString("0000100"_sl);
    enc.endArray();
    enc.endDict();
    AllocedDict properties(enc.finish());

    SECTION("Push") {
        auto pushOptions = Replicator::Options::pushing();
        pushOptions.properties = properties;
        _expectedDocumentCount = 3;
        runReplicators(pushOptions,
                       Replicator::Options::passive());
    }
    SECTION("Pull") {
        auto pullOptions = Replicator::Options::pulling();
        pullOptions.properties = properties;
        _expectedDocumentCount = 3;
        runReplicators(Replicator::Options::passive(),
                       pullOptions);
    }

    CHECK(c4db_getDocumentCount(db2) == 3);
    c4::ref<C4Document> doc = c4doc_get(db2, "0000001"_sl, true, nullptr);
    CHECK(doc != nullptr);
    doc = c4doc_get(db2, "0000010"_sl, true, nullptr);
    CHECK(doc != nullptr);
    doc = c4doc_get(db2, "0000100"_sl, true, nullptr);
    CHECK(doc != nullptr);
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Channels", "[Pull]") {
    Encoder enc;
    enc.beginDict();
    enc.writeKey("filter"_sl);
    enc.writeString("Melitta"_sl);
    enc.endDict();
    alloc_slice data = enc.finish();
    auto opts = Replicator::Options::pulling();
    opts.properties = AllocedDict(data);

    // LiteCore's replicator doesn't support filters, so we expect an Unsupported error back:
    _expectedError = {LiteCoreDomain, kC4ErrorUnsupported};
    runReplicators(opts, Replicator::Options::passive());
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Push Validation Failure", "[Push]") {
    importJSONLines(sFixturesDir + "names_100.json");
    auto pullOptions = Replicator::Options::passive();
    atomic<int> validationCount {0};
    pullOptions.pullValidatorContext = &validationCount;
    pullOptions.pullValidator = [](FLString docID, FLDict body, void *context)->bool {
        ++(*(atomic<int>*)context);
        return (Dict(body)["birthday"].asstring() < "1993");
    };
    _expectedDocPushErrors = set<string>{"0000052", "0000065", "0000071", "0000072"};
    _expectedDocPullErrors = set<string>{"0000052", "0000065", "0000071", "0000072"};
    _expectedDocumentCount = 100 - 4;
    runReplicators(Replicator::Options::pushing(),
                   pullOptions);
    validateCheckpoints(db, db2, "{\"local\":100}");
    CHECK(validationCount == 100);
    CHECK(c4db_getDocumentCount(db2) == 96);
}


#pragma mark - CONFLICTS:


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Conflict", "[Push]") {
    createFleeceRev(db,  C4STR("conflict"), C4STR("1-11111111"), C4STR("{}"));
    _expectedDocumentCount = 1;
    
    // Push db to db2, so both will have the doc:
    runPushReplication();
    validateCheckpoints(db, db2, "{\"local\":1}");

    // Update the doc differently in each db:
    createFleeceRev(db,  C4STR("conflict"), C4STR("2-2a2a2a2a"), C4STR("{\"db\":1}"));
    createFleeceRev(db2, C4STR("conflict"), C4STR("2-2b2b2b2b"), C4STR("{\"db\":2}"));

    // Verify that rev 1 body is still available, for later use in conflict resolution:
    c4::ref<C4Document> doc = c4doc_get(db, C4STR("conflict"), true, nullptr);
    REQUIRE(doc);
    CHECK(doc->selectedRev.revID == C4STR("2-2a2a2a2a"));
    CHECK(doc->selectedRev.body.size > 0);
    REQUIRE(c4doc_selectParentRevision(doc));
    CHECK(doc->selectedRev.revID == C4STR("1-11111111"));
    CHECK(doc->selectedRev.body.size > 0);
    CHECK((doc->selectedRev.flags & kRevKeepBody) != 0);

    // Now pull to db from db2, creating a conflict:
    C4Log("-------- Pull db <- db2 --------");
    _expectedDocPullErrors = set<string>{"conflict"};
    runReplicators(Replicator::Options::pulling(), Replicator::Options::passive());
    validateCheckpoints(db, db2, "{\"local\":1,\"remote\":2}");

    doc = c4doc_get(db, C4STR("conflict"), true, nullptr);
    REQUIRE(doc);
    CHECK((doc->flags & kDocConflicted) != 0);
    CHECK(doc->selectedRev.revID == C4STR("2-2a2a2a2a"));
    CHECK(doc->selectedRev.body.size > 0);
    REQUIRE(c4doc_selectParentRevision(doc));
    CHECK(doc->selectedRev.revID == C4STR("1-11111111"));
    CHECK(doc->selectedRev.body.size > 0);
    CHECK((doc->selectedRev.flags & kRevKeepBody) != 0);
    REQUIRE(c4doc_selectCurrentRevision(doc));
    REQUIRE(c4doc_selectNextRevision(doc));
    CHECK(doc->selectedRev.revID == C4STR("2-2b2b2b2b"));
    CHECK((doc->selectedRev.flags & kRevIsConflict) != 0);
    CHECK(doc->selectedRev.body.size > 0);
    REQUIRE(c4doc_selectParentRevision(doc));
    CHECK(doc->selectedRev.revID == C4STR("1-11111111"));
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Pull Then Push No-Conflicts", "[Pull][Push][NoConflicts]") {
    auto serverOpts = Replicator::Options::passive();
    serverOpts.setProperty(C4STR(kC4ReplicatorOptionNoConflicts), "true"_sl);

    createRev(kDocID, kRevID, kFleeceBody);
    createRev(kDocID, kRev2ID, kFleeceBody);
    _expectedDocumentCount = 1;

    Log("-------- First Replication db->db2 --------");
    runReplicators(serverOpts,
                   Replicator::Options::pulling());
    validateCheckpoints(db2, db, "{\"remote\":2}");

    Log("-------- Update Doc --------");
    alloc_slice body;
    {
        Encoder enc(c4db_createFleeceEncoder(db2));
        enc.beginDict();
        enc.writeKey("answer"_sl);
        enc.writeInt(666);
        enc.endDict();
        body = enc.finish();
    }

    createRev(db2, kDocID, kRev3ID, body);
    createRev(db2, kDocID, "4-4444"_sl, body);
    _expectedDocumentCount = 1;

    Log("-------- Second Replication db2->db --------");
    runReplicators(serverOpts,
                   Replicator::Options::pushing());
    validateCheckpoints(db2, db, "{\"local\":3,\"remote\":2}");
    compareDatabases();

    Log("-------- Update Doc Again --------");
    createRev(db2, kDocID, "5-5555"_sl, body);
    createRev(db2, kDocID, "6-6666"_sl, body);
    _expectedDocumentCount = 1;

    Log("-------- Third Replication db2->db --------");
    runReplicators(serverOpts,
                   Replicator::Options::pushing());
    validateCheckpoints(db2, db, "{\"local\":5,\"remote\":2}");
    compareDatabases();
}


TEST_CASE_METHOD(ReplicatorLoopbackTest, "Lost Checkpoint No-Conflicts", "[Pull][NoConflicts]") {
    auto serverOpts = Replicator::Options::passive();
    serverOpts.setProperty(C4STR(kC4ReplicatorOptionNoConflicts), "true"_sl);

    createRev(kDocID, kRevID, kFleeceBody);
    createRev(kDocID, kRev2ID, kFleeceBody);

    Log("-------- First Replication: push db->db2 --------");
    _expectedDocumentCount = 1;
    runReplicators(Replicator::Options::pushing(), serverOpts);
    validateCheckpoints(db, db2, "{\"local\":2}");

    clearCheckpoint(db, true);
    Log("-------- Second Replication: push db->db2 --------");
    _expectedDocumentCount = 0;
    runReplicators(Replicator::Options::pushing(), serverOpts);
    validateCheckpoints(db, db2, "{\"local\":2}");
}

