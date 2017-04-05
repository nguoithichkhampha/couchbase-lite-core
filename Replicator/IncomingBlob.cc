//
//  IncomingBlob.cc
//  LiteCore
//
//  Created by Jens Alfke on 4/4/17.
//  Copyright © 2017 Couchbase. All rights reserved.
//

#include "IncomingBlob.hh"
#include "StringUtil.hh"
#include "MessageBuilder.hh"

using namespace fleece;
using namespace litecore::blip;

namespace litecore { namespace repl {

    IncomingBlob::IncomingBlob(Worker *parent, C4BlobStore *blobStore)
    :Worker(parent, "blob")
    ,_blobStore(blobStore)
    { }


    void IncomingBlob::_start(BlobRequest blob) {
        _blob = blob;
        alloc_slice digest = c4blob_keyToString(_blob.key);
        logVerbose("Requesting blob %.*s (%llu bytes)", SPLAT(digest), _blob.size);

        C4Error err;
        _writer = c4blob_openWriteStream(_blobStore, &err);
        if (!_writer)
            return gotError(err);

        addProgress({0, _blob.size});

        MessageBuilder req("getAttachment"_sl);
        req["digest"_sl] = digest;
        sendRequest(req, asynchronize([=](blip::MessageProgress progress) {
            //... After request is sent:
            if (_writer && progress.reply) {
                if (progress.reply->isError()) {
                    gotError(progress.reply);
                } else {
                    writeToBlob(progress.reply->extractBody());
                    if (progress.state == MessageProgress::kComplete)
                        finishBlob();
                }
            }
        }));
    }


    void IncomingBlob::writeToBlob(alloc_slice data) {
        C4Error err;
        if (!c4stream_write(_writer, data.buf, data.size, &err))
            return gotError(err);
        addProgress({data.size, 0});
    }


    void IncomingBlob::finishBlob() {
        C4Error err;
        if (!c4stream_install(_writer, &_blob.key, &err))
            gotError(err);
        c4stream_closeWriter(_writer);
        _writer = nullptr;

        alloc_slice digest = c4blob_keyToString(_blob.key);
        logVerbose("Finished receiving blob %.*s (%llu bytes)", SPLAT(digest), _blob.size);
    }


    void IncomingBlob::onError(C4Error err) {
        c4stream_closeWriter(_writer);
        _writer = nullptr;
        Worker::onError(err);
        // Bump progress to 100% so as not to mess up overall progress tracking:
        setProgress({_blob.size, _blob.size});
    }


    Worker::ActivityLevel IncomingBlob::computeActivityLevel() const {
        if (Worker::computeActivityLevel() == kC4Busy || _writer != nullptr)
            return kC4Busy;
        else
            return kC4Stopped;
    }

} }