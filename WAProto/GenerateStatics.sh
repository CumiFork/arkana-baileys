#!/bin/sh
set -e

cd "$(dirname "$0")"

npx --yes --package=protobufjs@7.5.6 --package=protobufjs-cli@1.1.3 pbjs -t static-module -w es6 -o ./index.js ./index.proto
npx --yes --package=protobufjs@7.5.6 --package=protobufjs-cli@1.1.3 pbts -o ./index.d.ts ./index.js
node ./fix-imports.js
