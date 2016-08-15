////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 Realm Inc.
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
////////////////////////////////////////////////////////////////////////////

/* eslint-env es6, node */
/* eslint-disable no-console */

'use strict';

const Realm = require('../..');

process.on('message', (message) => {
    process.send(handleMessage(message));
});

function handleMessage(message) {
    let error, result;
    if (message[0] == 'echo') {
        return {result: message[1]}
    }

    try {
        let realm = new Realm(message[0]);
        let run = (m) => {
            if (m[0] == 'create') {
                result = m[2].map((value) => realm.create(m[1], value));
            }
            else if (m[0] == 'delete') {
                let objects = realm.objects(m[1]);
                objects = m[2].map((index) => objects[index]);
                realm.delete(objects);
            }
            else if (m[0] == 'update') {
                result = m[2].map((value) => realm.create(m[1], value, true));
            }
            else if (m[0] == 'list_method') {
                var listObject = realm.objects(m[1])[0];
                var list = listObject[m[2]];
                result = list[m[3]].apply(list, m.slice(4));
            }
            else {
                throw new Error('Unknown realm method: ' + m[0]);
            }
        };

        realm.write(() => {
            if (message[1] instanceof Array) {
                for (let i = 1; i < message.length; ++i) {
                    run(message[i]);
                }
            }
            else {
                run(message.slice(1));
            }
        });
    } catch (e) {
        console.warn(e);
        error = e.message;
    }

    return {error, result};
}
