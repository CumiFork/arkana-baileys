import { readFileSync, writeFileSync } from 'fs';
import { exit } from 'process';

const jsPath = './index.js';
const dtsPath = './index.d.ts';

const longToStringHelper =
  'function longToString(value, unsigned) {\n' +
  '\tif (typeof value === "string") {\n' +
  '\t\treturn value;\n' +
  '\t}\n' +
  '\tif (typeof value === "number") {\n' +
  '\t\treturn String(value);\n' +
  '\t}\n' +
  '\t// Fast path: convert Long {low, high} directly via native BigInt\n' +
  '\t// BigInt.toString() is a native C++ operation, much faster than Long\'s pure JS division loops\n' +
  '\tif (value && typeof value.low === "number" && typeof value.high === "number") {\n' +
  '\t\tconst lo = BigInt(value.low >>> 0);\n' +
  '\t\tconst hi = BigInt(value.high >>> 0);\n' +
  '\t\tconst combined = (hi << 32n) | lo;\n' +
  '\t\tif (!unsigned && value.high < 0) {\n' +
  '\t\t\treturn (combined - (1n << 64n)).toString();\n' +
  '\t\t}\n' +
  '\t\treturn combined.toString();\n' +
  '\t}\n' +
  '\treturn String(value);\n' +
  '}\n\n';

const longToNumberHelper =
  'function longToNumber(value, unsigned) {\n' +
  '\tif (typeof value === "number") {\n' +
  '\t\treturn value;\n' +
  '\t}\n' +
  '\tif (typeof value === "string") {\n' +
  '\t\treturn Number(value);\n' +
  '\t}\n' +
  '\t// Fast path: convert Long {low, high} directly via native BigInt\n' +
  '\tif (value && typeof value.low === "number" && typeof value.high === "number") {\n' +
  '\t\tconst lo = BigInt(value.low >>> 0);\n' +
  '\t\tconst hi = BigInt(value.high >>> 0);\n' +
  '\t\tconst combined = (hi << 32n) | lo;\n' +
  '\t\tif (!unsigned && value.high < 0) {\n' +
  '\t\t\treturn Number(combined - (1n << 64n));\n' +
  '\t\t}\n' +
  '\t\treturn Number(combined);\n' +
  '\t}\n' +
  '\treturn Number(value);\n' +
  '}\n\n';

const patchJs = () => {
  let content = readFileSync(jsPath, 'utf8');

  content = content.replace(/\$protobuf\./g, '$protobufRuntime.');
  content = content.replace(
    /import \* as \$protobuf from "protobufjs\/minimal(?:\.js)?";/,
    'import * as $protobuf from "protobufjs/minimal.js";\nconst $protobufRuntime = $protobuf.default ?? $protobuf;'
  );

  const marker = 'const $root = $protobufRuntime.roots["default"] || ($protobufRuntime.roots["default"] = {});\n\n';
  if (!content.includes('function longToString(')) {
    const markerIndex = content.indexOf(marker);
    if (markerIndex === -1) {
      throw new Error('Unable to inject Long helpers: marker not found in WAProto index output');
    }
    content = content.replace(marker, `${marker}${longToStringHelper}${longToNumberHelper}`);
  } else {
    const longToStringRegex = /function longToString\(value, unsigned\) {\n[\s\S]*?\n}\n\n/;
    const longToNumberRegex = /function longToNumber\(value, unsigned\) {\n[\s\S]*?\n}\n\n/;
    if (!longToStringRegex.test(content) || !longToNumberRegex.test(content)) {
      throw new Error('Unable to update Long helpers: existing definitions not found');
    }
    content = content.replace(longToStringRegex, longToStringHelper);
    content = content.replace(longToNumberRegex, longToNumberHelper);
  }

  const longPattern = /([ \t]+d\.(\w+) = )o\.longs === String \? \$util\.Long\.prototype\.toString\.call\(m\.\2\) : o\.longs === Number \? new \$util\.LongBits\(m\.\2\.low >>> 0, m\.\2\.high >>> 0\)\.toNumber\((true)?\) : m\.\2;/g;
  content = content.replace(longPattern, (_match, prefix, field, unsignedFlag) => {
    const unsignedArg = unsignedFlag ? ', true' : '';
    return `${prefix}o.longs === String ? longToString(m.${field}${unsignedArg}) : o.longs === Number ? longToNumber(m.${field}${unsignedArg}) : m.${field};`;
  });

  if (/\brequire\s*\(/.test(content)) {
    throw new Error('Generated WAProto index.js contains require()');
  }

  writeFileSync(jsPath, content, 'utf8');
  console.log(`✅ Fixed imports in ${jsPath}`);
};

const patchDts = () => {
  const content = readFileSync(dtsPath, 'utf8').replace(/(["'])protobufjs\/minimal\1/g, '$1protobufjs/minimal.js$1');
  writeFileSync(dtsPath, content, 'utf8');
  console.log(`✅ Fixed imports in ${dtsPath}`);
};

try {
  patchJs();
  patchDts();
} catch (error) {
  console.error(`❌ Error fixing imports: ${error.message}`);
  exit(1);
}
