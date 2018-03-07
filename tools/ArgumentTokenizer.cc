//
// ArgumentTokenizer.cc
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

#include "ArgumentTokenizer.hh"
using namespace std;

bool ArgumentTokenizer::tokenize(const char* input, deque<string> &args)
{
    if(input == nullptr) {
        return false;
    }
    
    const char* start = input;
    const char* current = start;
    char quoteChar = 0;
    bool inQuote = false;
    bool forceAppend = false;
    string nextArg;
    while(*current) {
        char c = *current;
        current++;
        if(c == '\r' || c == '\n') {
            continue;
        }
        
        if(!forceAppend) {
            if(c == '\\') {
                forceAppend = true;
                continue;
            } else if(c == '"' || c == '\'') {
                if(quoteChar != 0 && c == quoteChar) {
                    inQuote = false;
                    quoteChar = 0;
                    continue;
                } else if(quoteChar == 0) {
                    inQuote = true;
                    quoteChar = c;
                    continue;
                }
            } else if(c == ' ' && !inQuote) {
                args.push_back(nextArg);
                nextArg.clear();
                continue;
            }
        } else {
            forceAppend = false;
        }
        
        nextArg.append(1, c);
    }
    
    if(inQuote || forceAppend) {
        return false;
    }
    
    if(nextArg.length() > 0) {
        args.push_back(nextArg);
    }
    
    return true;
}
