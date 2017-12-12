//
//  Replicator.cc
//  LiteCore
//
//  Created by Jens Alfke on 2/13/17.
//  Copyright © 2017 Couchbase. All rights reserved.
//
//  https://github.com/couchbase/couchbase-lite-core/wiki/Replication-Protocol

#include "Replicator.hh"
#include "DBWorker.hh"
#include "Pusher.hh"
#include "Puller.hh"
#include "Error.hh"
#include "StringUtil.hh"
#include "Logging.hh"
#include "SecureDigest.hh"
#include "BLIP.hh"

using namespace std;
using namespace std::placeholders;
using namespace fleece;
using namespace fleeceapi;


namespace litecore { namespace repl {


    // Subroutine of constructor that looks up HTTP cookies for the request, adds them to
    // options.properties' cookies, and returns the properties dict.
    static AllocedDict propertiesWithCookies(C4Database *db,
                                             const websocket::Address &address,
                                             Worker::Options &options)
    {
        options.setProperty(slice(kC4SocketOptionWSProtocols), Connection::kWSProtocolName);
        if (!options.properties[kC4ReplicatorOptionCookies]) {
            C4Address c4addr {
                slice(address.scheme),
                slice(address.hostname),
                address.port,
                slice(address.path)
            };
            C4Error err;
            alloc_slice cookies = c4db_getCookies(db, c4addr, &err);
            if (cookies)
                options.setProperty(slice(kC4ReplicatorOptionCookies), cookies);
            else if (err.code)
                Warn("Error getting cookies from db: %d/%d", err.domain, err.code);
        }
        return options.properties;
    }


    // The 'designated initializer', in Obj-C terms :)
    Replicator::Replicator(C4Database* db,
                           const websocket::Address &address,
                           Delegate &delegate,
                           const Options &options,
                           Connection *connection)
    :Worker(connection, nullptr, options, "Repl")
    ,_remoteAddress(address)
    ,_delegate(&delegate)
    ,_connectionState(connection->state())
    ,_pushStatus(options.push == kC4Disabled ? kC4Stopped : kC4Busy)
    ,_pullStatus(options.pull == kC4Disabled ? kC4Stopped : kC4Busy)
    ,_dbActor(new DBWorker(connection, this, db, address, options))
    {
        if (options.push != kC4Disabled)
            _pusher = new Pusher(connection, this, _dbActor, _options);
        if (options.pull != kC4Disabled)
            _puller = new Puller(connection, this, _dbActor, _options);
        _checkpoint.enableAutosave(options.checkpointSaveDelay(),
                                   bind(&Replicator::saveCheckpoint, this, _1));
    }

    Replicator::Replicator(C4Database *db,
                           websocket::Provider &provider,
                           const websocket::Address &address,
                           Delegate &delegate,
                           Options options)
    :Replicator(db, address, delegate, options,
                new Connection(address, provider,
                               propertiesWithCookies(db, address, options),
                               *this))
    { }

    Replicator::Replicator(C4Database *db,
                           websocket::WebSocket *webSocket,
                           Delegate &delegate,
                           Options options)
    :Replicator(db, webSocket->address(), delegate, options,
                new Connection(webSocket, options.properties, *this))
    { }


    void Replicator::_start() {
        Assert(_connectionState == Connection::kClosed);
        _connectionState = Connection::kConnecting;
        connection()->start();
        // Now wait for _onConnect or _onClose...
    }


    void Replicator::_stop() {
        if (connection()) {
            connection()->close();
            _connectionState = Connection::kClosing;
        }
    }


    // Called after the checkpoint is established.
    void Replicator::startReplicating() {
        auto cp = _checkpoint.sequences();
        if (_options.push > kC4Passive)
            _pusher->start(cp.local);
        if (_options.pull > kC4Passive)
            _puller->start(cp.remote);
    }


#pragma mark - STATUS:


    // The status of one of the actors has changed; update mine
    void Replicator::_childChangedStatus(Worker *task, Status taskStatus)
    {
        if (status().level == kC4Stopped)       // I've already stopped & cleared refs; ignore this
            return;

        if (task == _pusher) {
            _pushStatus = taskStatus;
        } else if (task == _puller) {
            _pullStatus = taskStatus;
        } else if (task == _dbActor) {
            _dbStatus = taskStatus;
        }

        setProgress(_pushStatus.progress + _pullStatus.progress);

        logDebug("pushStatus=%-s, pullStatus=%-s, dbStatus=%-s, progress=%llu/%llu",
                 kC4ReplicatorActivityLevelNames[_pushStatus.level],
                 kC4ReplicatorActivityLevelNames[_pullStatus.level],
                 kC4ReplicatorActivityLevelNames[_dbStatus.level],
                 status().progress.unitsCompleted, status().progress.unitsTotal);

        if (_pullStatus.error.code)
            onError(_pullStatus.error);
        else if (_pushStatus.error.code)
            onError(_pushStatus.error);

        // Save a checkpoint immediately when push or pull finishes:
        if (taskStatus.level == kC4Stopped)
            _checkpoint.save();
    }


    Worker::ActivityLevel Replicator::computeActivityLevel() const {
        switch (_connectionState) {
            case Connection::kConnecting:
                return kC4Connecting;
            case Connection::kConnected: {
                ActivityLevel level;
                if (_checkpoint.isUnsaved())
                    level = kC4Busy;
                else
                    level = Worker::computeActivityLevel();
                level = max(level, max(_pushStatus.level, _pullStatus.level));
                if (level == kC4Idle && !isContinuous() && !isOpenServer()) {
                    // Detect that a non-continuous active push or pull replication is done:
                    log("Replication complete! Closing connection");
                    const_cast<Replicator*>(this)->_stop();
                    level = kC4Busy;
                }
                assert(level > kC4Stopped);
                return level;
            }
            case Connection::kClosing:
                // Remain active while I wait for the connection to finish closing:
                return kC4Busy;
            case Connection::kDisconnected:
            case Connection::kClosed:
                // After connection closes, remain active while I wait for db to finish writes:
                return (_dbStatus.level == kC4Busy) ? kC4Busy : kC4Stopped;
        }
    }


    void Replicator::changedStatus() {
        if (status().level == kC4Stopped) {
            assert(!connection());  // must already have gotten _onClose() delegate callback
            _pusher = nullptr;
            _puller = nullptr;
            _dbActor = nullptr;
        }
        if (_delegate) {
            // Notify the delegate of the current status, but not too often:
            auto waitFor = kMinDelegateCallInterval - _sinceDelegateCall.elapsed();
            if (waitFor <= 0 || status().level != _lastDelegateCallLevel) {
                reportStatus();
            } else if (!_waitingToCallDelegate) {
                _waitingToCallDelegate = true;
                enqueueAfter(actor::delay_t(waitFor), &Replicator::reportStatus);
            }
        }
    }


    void Replicator::reportStatus() {
        _waitingToCallDelegate = false;
        _lastDelegateCallLevel = status().level;
        _sinceDelegateCall.reset();
        if (_delegate)
            _delegate->replicatorStatusChanged(this, status());
        if (status().level == kC4Stopped)
            _delegate = nullptr;        // Never call delegate after telling it I've stopped
    }


    void Replicator::gotDocumentError(slice docID, C4Error error, bool pushing, bool transient) {
        _delegate->replicatorDocumentError(this, pushing, docID, error, transient);
    }


#pragma mark - BLIP DELEGATE:


    void Replicator::_onHTTPResponse(int status, fleeceapi::AllocedDict headers) {
        auto setCookie = headers["Set-Cookie"_sl];
        if (setCookie.type() == kFLArray) {
            // Yes, there can be multiple Set-Cookie headers.
            for (Array::iterator i(setCookie.asArray()); i; ++i) {
                _dbActor->setCookie(i.value().asString());
            }
        } else if (setCookie) {
            _dbActor->setCookie(setCookie.asString());
        }
        _delegate->replicatorGotHTTPResponse(this, status, headers);
    }


    void Replicator::_onConnect() {
        log("BLIP Connected");
        _connectionState = Connection::kConnected;
        if (_options.push > kC4Passive || _options.pull > kC4Passive)
            getCheckpoints();
    }


    void Replicator::_onClose(Connection::CloseStatus status, Connection::State state) {
        log("Connection closed with %-s %d: \"%.*s\"",
            status.reasonName(), status.code, SPLAT(status.message));

        bool closedByPeer = (_connectionState != Connection::kClosing);
        _connectionState = state;

        _checkpoint.stopAutosave();

        // Clear connection() and notify the other agents to do the same:
        _connectionClosed();
        _dbActor->connectionClosed();
        if (_pusher)
            _pusher->connectionClosed();
        if (_puller)
            _puller->connectionClosed();

        if (status.isNormal() && closedByPeer) {
            log("I didn't initiate the close; treating this as code 1001 (GoingAway)");
            status.code = websocket::kCodeGoingAway;
            status.message = alloc_slice("WebSocket connection closed by peer");
        }
        _closeStatus = status;

        static const C4ErrorDomain kDomainForReason[] = {WebSocketDomain, POSIXDomain, NetworkDomain};

        // If this was an unclean close, set my error property:
        if (status.reason != websocket::kWebSocketClose || status.code != websocket::kCodeNormal) {
            int code = status.code;
            C4ErrorDomain domain;
            if (status.reason < sizeof(kDomainForReason)/sizeof(C4ErrorDomain))
                domain = kDomainForReason[status.reason];
            else {
                domain = LiteCoreDomain;
                code = kC4ErrorRemoteError;
            }
            gotError(c4error_make(domain, code, status.message));
        }

        if (_delegate)
            _delegate->replicatorConnectionClosed(this, status);
    }


    // This only gets called if none of the registered handlers were triggered.
    void Replicator::_onRequestReceived(Retained<MessageIn> msg) {
        warn("Received unrecognized BLIP request #%llu with Profile '%.*s', %zu bytes",
                msg->number(), SPLAT(msg->property("Profile"_sl)), msg->body().size);
        msg->notHandled();
    }


#pragma mark - CHECKPOINT:


    // Start off by getting the local & remote checkpoints, if this is an active replicator:
    void Replicator::getCheckpoints() {
        // Get the local checkpoint:
        _dbActor->getCheckpoint(asynchronize([this](alloc_slice checkpointID,
                                                    alloc_slice data,
                                                    bool dbIsEmpty,
                                                    C4Error err) {
            if (status().level == kC4Stopped)
                return;
            
            _checkpointDocID = checkpointID;
            bool haveLocalCheckpoint = false;

            if (data) {
                _checkpoint.decodeFrom(data);
                auto cp = _checkpoint.sequences();
                log("Local checkpoint '%.*s' is [%llu, '%.*s']; getting remote ...",
                    SPLAT(checkpointID), cp.local, SPLAT(cp.remote));
                haveLocalCheckpoint = true;
            } else if (err.code == 0) {
                log("No local checkpoint '%.*s'", SPLAT(checkpointID));
                // If pulling into an empty db with no checkpoint, it's safe to skip deleted
                // revisions as an optimization.
                if (dbIsEmpty && _options.pull > kC4Passive)
                    _puller->setSkipDeleted();
            } else {
                log("Fatal error getting checkpoint");
                gotError(err);
                stop();
                return;
            }

            // Get the remote checkpoint, using the same checkpointID:
            MessageBuilder msg("getCheckpoint"_sl);
            msg["client"_sl] = checkpointID;
            sendRequest(msg, [this,haveLocalCheckpoint](MessageProgress progress) {
                MessageIn *response = progress.reply;
                if (!response)
                    return;
                Checkpoint remoteCheckpoint;

                if (response->isError()) {
                    auto err = response->getError();
                    if (!(err.domain == "HTTP"_sl && err.code == 404))
                        return gotError(response);
                    log("No remote checkpoint");
                    _checkpointRevID.reset();
                } else {
                    log("Received remote checkpoint: '%.*s'", SPLAT(response->body()));
                    remoteCheckpoint.decodeFrom(response->body());
                    _checkpointRevID = response->property("rev"_sl);
                }

                auto gotcp = remoteCheckpoint.sequences();
                log("...got remote checkpoint: [%llu, '%.*s'] rev='%.*s'",
                    gotcp.local, SPLAT(gotcp.remote), SPLAT(_checkpointRevID));

                if (haveLocalCheckpoint) {
                    // Compare checkpoints, reset if mismatched:
                    _checkpoint.validateWith(remoteCheckpoint);

                    // Now we have the checkpoints! Time to start replicating:
                    startReplicating();
                }
            });

            if (!haveLocalCheckpoint)
                startReplicating();
        }));
    }


    void Replicator::_saveCheckpoint(alloc_slice json) {
        if (!connection())
            return;
        log("Saving remote checkpoint %.*s with rev='%.*s' ...",
            SPLAT(_checkpointDocID), SPLAT(_checkpointRevID));
        MessageBuilder msg("setCheckpoint"_sl);
        msg["client"_sl] = _checkpointDocID;
        msg["rev"_sl] = _checkpointRevID;
        msg << json;
        sendRequest(msg, [=](MessageProgress progress) {
            MessageIn *response = progress.reply;
            if (!response)
                return;
            if (response->isError()) {
                gotError(response);
                // TODO: On 409 error, reload remote checkpoint
            } else {
                // Remote checkpoint saved, so update local one:
                _checkpointRevID = response->property("rev"_sl);
                log("Successfully saved remote checkpoint %.*s as rev='%.*s'",
                    SPLAT(_checkpointDocID), SPLAT(_checkpointRevID));
                _dbActor->setCheckpoint(json, asynchronize([this]{
                    _checkpoint.saved();
                }));
            }
            // Tell the checkpoint the save is finished, so it will call me again
        });
    }

} }
