//
// EncryptedStream.cc
//
// Copyright (c) 2016 Couchbase, Inc All rights reserved.
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

#include "EncryptedStream.hh"
#include "Error.hh"
#include "Logging.hh"
#include "SecureRandomize.hh"
#include "SecureSymmetricCrypto.hh"
#include "Endian.hh"
#include <algorithm>

/*
    Implementing a random-access encrypted stream is actually kind of tricky.
 
    First, we generate a random nonce the size of an AES-128 key (16 bytes). The given encryption key
    is XORed with the nonce, giving a new key that's used for the actual encryption. (The nonce
    will be appended to the file after all the data is written, so the reader can recover the key.)

    The data is divided into blocks of size kFileBlockSize (4kbytes), which are numbered starting
    at 0.

    Each block is encrypted with AES128 using CBC; the IV is simply the block number (big-endian.)
    This allows any block to be read and decrypted without having to read the prior blocks.

    All blocks except the last are of course full of data, so their size is kFileBlockSize. They
    are encrypted without padding, so the ciphertext is the same size as the plaintext. (This
    avoids bloating the size of the file, and ensures that the encrypted blocks are aligned with
    filesystem blocks for more efficient access.)
 
    The last block is special: it's partial, so it's encrypted using PKCS7 padding to preserve its
    true length.
 
    As a special case, if the data length is an exact multiple of the block size, an extra
    zero-length block is added. This is because, if the final block were the size of a full block,
    the PKCS7 padding would increase its length, making it overflow.
 
    Finally, the nonce is appended to the end of the stream.
 */


namespace litecore {
    using namespace std;

    extern LogDomain BlobLog;


    void EncryptedStream::initEncryptor(EncryptionAlgorithm alg,
                                        slice encryptionKey,
                                        slice nonce)
    {
        bool available = false;
        if (alg == kAES128) {
#if AES128_AVAILABLE
            available = true;
#endif
        }
        if (!available)
            error::_throw(error::UnsupportedEncryption);

        memcpy(&_key, encryptionKey.buf, kAES128KeySize);
        memcpy(&_nonce, nonce.buf, kAES128KeySize);
    }


    EncryptedStream::~EncryptedStream() {
    }


#pragma mark - WRITER:


    EncryptedWriteStream::EncryptedWriteStream(std::shared_ptr<WriteStream> output,
                                               EncryptionAlgorithm alg,
                                               slice encryptionKey)
    :_output(output)
    {
        // Derive a random nonce with which to scramble the key, and write it to the file:
        uint8_t buf[kAES128KeySize];
        slice nonce(buf, sizeof(buf));
        SecureRandomize(nonce);
        initEncryptor(alg, encryptionKey, nonce);
    }


    EncryptedWriteStream::~EncryptedWriteStream() {
        // Destructors aren't allowed to throw exceptions, so it's not safe to call close().
        if (_output)
            Warn("EncryptedWriteStream was not closed");
    }


    void EncryptedWriteStream::writeBlock(slice plaintext, bool finalBlock) {
#if AES128_AVAILABLE
        DebugAssert(plaintext.size <= kFileBlockSize, "Block is too large");
        uint64_t iv[2] = {0, _endian_encode(_blockID)};
        ++_blockID;
        uint8_t cipherBuf[kFileBlockSize + kAESBlockSize];
        slice ciphertext(cipherBuf, sizeof(cipherBuf));
        ciphertext.shorten(AES128(true,
                                  slice(&_key, sizeof(_key)), slice(iv, sizeof(iv)),
                                  finalBlock,
                                  ciphertext,
                                  plaintext));
        _output->write(ciphertext);
        LogVerbose(BlobLog, "WRITE #%2llu: %llu bytes, final=%d --> %llu bytes ciphertext",
            (unsigned long long)(_blockID-1), (unsigned long long)plaintext.size, finalBlock, (unsigned long long)ciphertext.size);
#else
        error::_throw(error::Unimplemented);
#endif
    }


    void EncryptedWriteStream::write(slice plaintext) {
        // Fill the current partial block buffer:
        auto capacity = min((size_t)kFileBlockSize - _bufferPos, plaintext.size);
        memcpy(&_buffer[_bufferPos], plaintext.buf, capacity);
        _bufferPos += capacity;
        plaintext.moveStart(capacity);
        if (_bufferPos < sizeof(_buffer))
            return; // done; didn't fill buffer

        // Write the completed buffer:
        writeBlock(slice(_buffer, kFileBlockSize), false);

        // Write entire blocks:
        while (plaintext.size >= kFileBlockSize)
            writeBlock(plaintext.read(kFileBlockSize), false);

        // Save remainder (if any) in the buffer.
        memcpy(_buffer, plaintext.buf, plaintext.size);
        _bufferPos = plaintext.size;
    }


    void EncryptedWriteStream::close() {
        if (_output) {
            // Write the final (partial or empty) block with PKCS7 padding:
            writeBlock(slice(_buffer, _bufferPos), true);
            // End with the nonce:
            _output->write(slice(_nonce, kAES128KeySize));
            _output->close();
            _output = nullptr;
        }
    }


#pragma mark - READER:


    EncryptedReadStream::EncryptedReadStream(std::shared_ptr<SeekableReadStream> input,
                                             EncryptionAlgorithm alg,
                                             slice encryptionKey)
    :_input(input),
     _inputLength(_input->getLength() - kFileSizeOverhead),
     _finalBlockID((_inputLength - 1) / kFileBlockSize)
    {
        // Read the random nonce from the end of the file:
        _input->seek(_input->getLength() - kFileSizeOverhead);
        uint8_t buf[kAES128KeySize];
        if (_input->read(buf, sizeof(buf)) < sizeof(buf))
            error::_throw(error::CorruptData);
        _input->seek(0);

        initEncryptor(alg, encryptionKey, slice(buf, sizeof(buf)));
    }


    void EncryptedReadStream::close() {
        if (_input ) {
            _input->close();
            _input = nullptr;
        }
    }


    // Reads & decrypts the next block from the file into `output`
    size_t EncryptedReadStream::readBlockFromFile(slice output) {
#if AES128_AVAILABLE
        if (_blockID > _finalBlockID)
            return 0; // at EOF already
        uint8_t blockBuf[kFileBlockSize + kAESBlockSize];
        bool finalBlock = (_blockID == _finalBlockID);
        size_t readSize = kFileBlockSize;
        if (finalBlock)
            readSize = (size_t)(_inputLength - (_blockID * kFileBlockSize));  // don't read trailer
        size_t bytesRead = _input->read(blockBuf, readSize);

        uint64_t iv[2] = {0, _endian_encode(_blockID)};
        ++_blockID;
        size_t outputSize = AES128(false,
                      slice(_key, sizeof(_key)),
                      slice(iv, sizeof(iv)),
                      finalBlock,
                      output, slice(blockBuf, bytesRead));
        LogVerbose(BlobLog, "READ  #%2llu: %llu bytes, final=%d --> %llu bytes ciphertext",
            (unsigned long long)(_blockID-1), (unsigned long long)bytesRead, finalBlock, (unsigned long long)outputSize);
        return outputSize;
#else
        error::_throw(error::Unimplemented);
#endif
    }


    // Reads the next block from the file into _buffer
    void EncryptedReadStream::fillBuffer() {
        _bufferBlockID = _blockID;
        _bufferSize = readBlockFromFile(slice(_buffer, kFileBlockSize));
        _bufferPos = 0;
    }


    // Reads as many bytes as possible from _buffer into `remaining`.
    void EncryptedReadStream::readFromBuffer(slice &remaining) {
        size_t nFromBuffer = min(_bufferSize - _bufferPos, remaining.size);
        if (nFromBuffer > 0) {
            remaining.writeFrom(slice(&_buffer[_bufferPos], nFromBuffer));
            _bufferPos += nFromBuffer;
        }
    }


    size_t EncryptedReadStream::read(void *dst, size_t count) {
        slice remaining(dst, count);
        // If there's decrypted data in the buffer, copy it to the output:
        readFromBuffer(remaining);
        if (remaining.size > 0 && _blockID <= _finalBlockID) {
            // Read & decrypt as many blocks as possible from the file to the output:
            while (remaining.size >= kFileBlockSize && _blockID <= _finalBlockID) {
                remaining.moveStart(readBlockFromFile(remaining));
            }

            if (remaining.size > 0) {
                // Partial block: decrypt entire block to buffer, then copy part to the output:
                fillBuffer();
                readFromBuffer(remaining);
            }
        }
        return (uint8_t*)remaining.buf - (uint8_t*)dst;
    }


    uint64_t EncryptedReadStream::getLength() const {
        if (_cleartextLength == UINT64_MAX)
            (const_cast<EncryptedReadStream*>(this))->findLength();
        return _cleartextLength;
    }

    void EncryptedReadStream::findLength() {
        // Have to read the final block of the file to determine its actual length:
        uint64_t pos = tell();
        seek(_inputLength);
        _cleartextLength = tell();
        seek(pos);
    }

    
    void EncryptedReadStream::seek(uint64_t pos) {
        if (pos > _inputLength)
            pos = _inputLength;
        uint64_t blockID = min(pos / kFileBlockSize, _finalBlockID);
        uint64_t blockPos = blockID * kFileBlockSize;
        if (blockID != _bufferBlockID) {
            LogVerbose(BlobLog, "SEEK %llu (block %llu + %llu bytes)", (unsigned long long)pos, (unsigned long long)blockID, (unsigned long long)(pos - blockPos));
            _input->seek(blockPos);
            _blockID = blockID;
            fillBuffer();
        }
        _bufferPos = min((size_t)(pos - blockPos), _bufferSize);
    }


    uint64_t EncryptedReadStream::tell() const {
        if (_bufferBlockID == UINT64_MAX)
            return 0;
        return _bufferBlockID * kFileBlockSize + _bufferPos;
    }
    
}
